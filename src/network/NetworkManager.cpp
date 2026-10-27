#include "network/NetworkManager.hpp"

#include "events/EventDispatcher.hpp"
#include "handlers/MessageHandler.hpp"
#include "utils/TimestampUtils.hpp"
#include "utils/WireFraming.hpp"
#include <spdlog/logger.h>

NetworkManager::NetworkManager(
    EventQueue &eventQueue,
    EventQueue &eventQueuePing,
    std::tuple<DatabaseConfig, GameServerConfig> &configs,
    Logger &logger)
    : acceptor_(io_context_),
      logger_(logger),
      configs_(configs),
      jsonParser_(),
      eventQueue_(eventQueue),
      eventQueuePing_(eventQueuePing)
{
    log_ = logger.getSystem("network");
    boost::system::error_code ec;

    std::uint16_t customPort = std::get<1>(configs).port;
    std::string customIP = std::get<1>(configs).host;
    int maxClients = std::get<1>(configs).max_clients;

    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(customIP), customPort);
    acceptor_.open(endpoint.protocol(), ec);
    if (!ec)
    {
        log_->info("Starting Game Server...");
        acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
        acceptor_.bind(endpoint, ec);
        acceptor_.listen(maxClients, ec);
    }
    if (ec)
    {
        log_->error("Error during server initialization: " + ec.message());
        return;
    }
    log_->info("Game Server started on IP: " + customIP + ", Port: " + std::to_string(customPort));

    // Dispatcher + handler are fully wired here (no late setGameServer step:
    // parsing/pushing needs no GameServer link).
    eventDispatcher_ = std::make_unique<EventDispatcher>(eventQueue_, eventQueuePing_, logger_, jsonParser_);
    messageHandler_ = std::make_unique<MessageHandler>(jsonParser_);
}

void
NetworkManager::startAccept()
{
    auto clientSocket = std::make_shared<boost::asio::ip::tcp::socket>(io_context_);
    acceptor_.async_accept(*clientSocket, [this, clientSocket](const boost::system::error_code &error)
        {
        if (!error) {
            boost::asio::ip::tcp::endpoint remoteEndpoint = clientSocket->remote_endpoint();
            std::string clientIP = remoteEndpoint.address().to_string();
            std::string portNumber = std::to_string(remoteEndpoint.port());

            int maxClients = std::get<1>(configs_).max_clients;
            {
                std::lock_guard<std::mutex> lock(sessionsMutex_);
                if (activeSessions_.size() >= static_cast<size_t>(maxClients))
                {
                    log_->warn("Max clients reached (" + std::to_string(maxClients) +
                               "), rejecting connection from " + clientIP + ":" + portNumber);
                    boost::system::error_code closeEc;
                    clientSocket->close(closeEc);
                    startAccept();
                    return;
                }
            }

            log_->info("New Client with IP: " + clientIP + " Port: " + portNumber + " - connected!");
            // Pass the shared pointer to the ClientSession
            auto session = std::make_shared<ClientSession>(clientSocket, logger_, eventQueue_, eventQueuePing_, *eventDispatcher_, *messageHandler_);
            session->setDisconnectCallback([this](std::shared_ptr<ClientSession> s) { removeActiveSession(s); });
            // Strand-safe error path for framing failures (see ClientSession::setSendCallback).
            session->setSendCallback(
                [this](std::shared_ptr<boost::asio::ip::tcp::socket> s, const std::string &data)
                { sendResponse(std::move(s), data); });
            // S14: record the sniffed transport per socket (mirrors setSendCallback).
            session->setFramingCallback(
                [this](std::shared_ptr<boost::asio::ip::tcp::socket> s, bool v2)
                { setSocketFramingV2(std::move(s), v2); });
            addActiveSession(session);
            session->start();
        }
        else{
            log_->warn("Accept client connection error: " + error.message());
        }
        startAccept(); });
}

void
NetworkManager::startIOEventLoop()
{
    log_->info("Starting Game Server IO Context...");
    auto numThreads = std::thread::hardware_concurrency();
    for (size_t i = 0; i < numThreads; ++i)
    {
        threadPool_.emplace_back([this]()
            { io_context_.run(); });
    }
}

NetworkManager::~NetworkManager()
{
    log_->warn("Network Manager destructor is called...");
    stopIO();
    // ASan-found UAF fix: SocketWriteState holds a strand bound to
    // io_context_'s executor service, but socketStates_ is declared BEFORE
    // io_context_ — without this, member destruction tears down io_context
    // first and ~strand() touches the freed service. Clearing here (threads
    // already joined, io alive) destroys strands safely.
    {
        std::lock_guard<std::mutex> lock(socketStatesMutex_);
        socketStates_.clear();
    }
}

void
NetworkManager::stopIO()
{
    boost::system::error_code ec;
    acceptor_.close(ec);
    io_context_.stop();
    for (auto &thread : threadPool_)
    {
        if (thread.joinable())
            thread.join();
    }
}

void
NetworkManager::sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string &responseString)
{
    if (!clientSocket || !clientSocket->is_open())
    {
        log_->error("Attempted write on closed or invalid socket.");
        return;
    }

    // S14: v2 sockets get length-prefixed frames and skip the v1 line path.
    // framingError() payloads carry a trailing '\n' for v1; strip one so
    // the v2 frame holds pure JSON.
    if (isSocketFramingV2(clientSocket))
    {
        std::string logical = responseString;
        if (!logical.empty() && logical.back() == '\n')
            logical.pop_back();
        auto framed = wirev2::packFramedMessage(logical);
        if (framed.empty())
        {
            log_->error("v2 frame over {} bytes, dropping instead of sending whole",
                protocol::kMaxFrameBytes);
            return;
        }
        auto dataPtr = std::make_shared<const std::string>(std::move(framed));
        auto state = getOrCreateSocketState(clientSocket);
        if (!state)
            return;
        boost::asio::post(state->strand, [this, clientSocket, dataPtr, state]() mutable
            {
                state->writeQueue.push(dataPtr);
                if (!state->writePending)
                    doNextWrite(std::move(clientSocket), std::move(state));
            });
        return;
    }

    // Wire-contract guard (seam with the UE client): the client parses one
    // '\n'-terminated JSON object per line with an 8KB cap
    // (protocol::kMaxMessageBytes, mirrored by FNetFraming::MaxMessageBytes).
    // Anything bigger is dropped client-side, so flag it here at warn level
    // with the event type instead of silently producing snapshot holes.
    // Server-to-server links are exempt (chunk GameServerWorker accumulates
    // unbounded '\n' lines by design). Threshold 6KB leaves headroom for
    // the trailing delimiter and header growth.
    constexpr size_t kClientLineCapWarnBytes = 6 * 1024;
    if (responseString.size() > kClientLineCapWarnBytes && !isServerLink(clientSocket))
    {
        std::string eventType = "?";
        const std::string key = "\"eventType\":\"";
        const size_t pos = responseString.find(key);
        if (pos != std::string::npos)
        {
            const size_t begin = pos + key.size();
            const size_t end = responseString.find('"', begin);
            if (end != std::string::npos)
                eventType = responseString.substr(begin, end - begin);
        }
        log_->warn("Oversized packet for client wire ({} bytes, eventType={}): exceeds UE 8KB line cap — chunk the array payload",
            responseString.size(), eventType);
    }

    // MEDIUM-8 fix: Serialise concurrent writes per socket via a per-socket strand +
    // write queue. Multiple EventHandler threads can call sendResponse concurrently
    // for the same chunk-server connection; without serialisation that causes UB.
    auto dataPtr = std::make_shared<const std::string>(responseString);
    auto state = getOrCreateSocketState(clientSocket);
    if (!state)
        return;

    boost::asio::post(state->strand, [this, clientSocket, dataPtr, state]() mutable
        {
            state->writeQueue.push(dataPtr);
            if (!state->writePending)
                doNextWrite(std::move(clientSocket), std::move(state)); });
}

std::shared_ptr<NetworkManager::SocketWriteState>
NetworkManager::getOrCreateSocketState(
    const std::shared_ptr<boost::asio::ip::tcp::socket> &socket)
{
    if (!socket)
        return nullptr;
    boost::asio::ip::tcp::socket *key = socket.get();
    std::lock_guard<std::mutex> lock(socketStatesMutex_);
    auto it = socketStates_.find(key);
    if (it != socketStates_.end())
    {
        // Same live object => same queue (one strand per socket, always:
        // concurrent async_write on one socket is UB in Asio).
        auto owner = it->second->owner.lock();
        if (owner && owner.get() == key)
            return it->second;
        // Otherwise REPLACE, never mutate in place: strand callbacks may
        // still reference the old queue object, and touching writePending /
        // writeQueue off-strand breaks the single-writer invariant
        // (SEGV under churn — see login CRITICAL-11, chunk v0.2.32).
        auto fresh = std::make_shared<SocketWriteState>(io_context_);
        fresh->owner = socket;
        it->second = fresh;
        return fresh;
    }
    auto fresh = std::make_shared<SocketWriteState>(io_context_);
    fresh->owner = socket;
    socketStates_.emplace(key, fresh);
    return fresh;
}

void
NetworkManager::removeSocketState(boost::asio::ip::tcp::socket *sock,
    const std::shared_ptr<boost::asio::ip::tcp::socket> &expectedOwner)
{
    // Erase-then-recreate for one live socket yields two strands writing it
    // (UB). So this only drops owner-expired entries; live mappings are
    // reclaimed by gcWriteQueues().
    if (!sock)
        return;
    std::lock_guard<std::mutex> lock(socketStatesMutex_);
    auto it = socketStates_.find(sock);
    if (it == socketStates_.end())
        return;
    if (expectedOwner)
    {
        auto owner = it->second->owner.lock();
        if (owner)
            return;
    }
    socketFramingV2_.erase(sock);
    serverLinkSockets_.erase(sock);
    socketStates_.erase(it);
}

// S14: sniffed framing per live socket (v2 or legacy v1). Same lock as
// socketStates_; erased together with the write state on reclaim.
void
NetworkManager::setSocketFramingV2(std::shared_ptr<boost::asio::ip::tcp::socket> socket, bool v2)
{
    if (!socket)
        return;
    std::lock_guard<std::mutex> lock(socketStatesMutex_);
    socketFramingV2_[socket.get()] = v2;
}

bool
NetworkManager::isSocketFramingV2(const std::shared_ptr<boost::asio::ip::tcp::socket> &socket) const
{
    if (!socket)
        return false;
    std::lock_guard<std::mutex> lock(socketStatesMutex_);
    auto it = socketFramingV2_.find(socket.get());
    return it != socketFramingV2_.end() && it->second;
}

void
NetworkManager::markServerLink(std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (!socket)
        return;
    std::lock_guard<std::mutex> lock(socketStatesMutex_);
    serverLinkSockets_.insert(socket.get());
}

bool
NetworkManager::isServerLink(const std::shared_ptr<boost::asio::ip::tcp::socket> &socket) const
{
    if (!socket)
        return false;
    std::lock_guard<std::mutex> lock(socketStatesMutex_);
    return serverLinkSockets_.find(socket.get()) != serverLinkSockets_.end();
}

void
NetworkManager::gcWriteQueues()
{
    std::lock_guard<std::mutex> lock(socketStatesMutex_);
    for (auto it = socketStates_.begin(); it != socketStates_.end();)
    {
        // Owner expired => no posted strand lambda can reference the socket;
        // erasing the map entry is safe (no queue fields touched off-strand).
        if (it->second->owner.expired())
        {
            socketFramingV2_.erase(it->first);
            it = socketStates_.erase(it);
        }
        else
            ++it;
    }
}

size_t
NetworkManager::writeQueueCount() const
{
    std::lock_guard<std::mutex> lock(socketStatesMutex_);
    return socketStates_.size();
}

void
NetworkManager::doNextWrite(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
    std::shared_ptr<SocketWriteState> state)
{
    if (state->writeQueue.empty() || !socket->is_open())
    {
        state->writePending = false;
        if (!socket->is_open())
            removeSocketState(socket.get(), socket);
        return;
    }

    state->writePending = true;
    auto dataPtr = state->writeQueue.front();
    state->writeQueue.pop();

    boost::asio::async_write(
        *socket,
        boost::asio::buffer(*dataPtr),
        boost::asio::bind_executor(
            state->strand,
            [this, socket, dataPtr, state](const boost::system::error_code &error, size_t bytes_transferred) mutable
            {
                if (error)
                {
                    log_->error("Error during async_write: " + error.message());
                    boost::system::error_code close_ec;
                    if (socket->is_open())
                        socket->close(close_ec);
                    removeSocketState(socket.get(), socket);
                    return;
                }
                log_->debug("Bytes sent: " + std::to_string(bytes_transferred));
                boost::system::error_code ec;
                auto ep = socket->remote_endpoint(ec);
                if (!ec)
                    // debug-level: fires per packet (heartbeats, catalog, ticks).
                    logger_.debug("Data sent to: " + ep.address().to_string() + ":" + std::to_string(ep.port()));
                doNextWrite(std::move(socket), std::move(state));
            }));
}

std::string
NetworkManager::generateResponseMessage(const std::string &status, const nlohmann::json &message)
{
    nlohmann::json response;
    std::string currentTimestamp = TimestampUtils::getCurrentTimestamp();
    response["header"] = message["header"];
    response["header"]["status"] = status;
    response["header"]["timestamp"] = currentTimestamp;
    response["header"]["version"] = protocol::kProtocolVersion;
    response["body"] = message["body"];
    // Protocol: every error response carries a machine-readable
    // `body.errorCode` mirroring `header.message` (additive: old clients
    // ignore unknown body keys). Explicit errorCode set by a handler wins.
    if (status == "error" && response["header"].contains("message") &&
        response["header"]["message"].is_string() &&
        (!response["body"].is_object() || !response["body"].contains("errorCode")))
    {
        if (!response["body"].is_object())
            response["body"] = nlohmann::json::object();
        response["body"]["errorCode"] = response["header"]["message"];
    }

    std::string responseString = response.dump();
    log_->debug("Response generated: " + responseString);
    return responseString + "\n";
}

std::string
NetworkManager::generateResponseMessage(const std::string &status, const nlohmann::json &message, const TimestampStruct &timestamps)
{
    nlohmann::json response;
    std::string currentTimestamp = TimestampUtils::getCurrentTimestamp();
    response["header"] = message["header"];
    response["header"]["status"] = status;
    response["header"]["timestamp"] = currentTimestamp;
    response["header"]["version"] = protocol::kProtocolVersion;

    // Add lag compensation timestamps to header
    TimestampStruct finalTimestamps = timestamps;
    TimestampUtils::setServerSendTimestamp(finalTimestamps); // Set serverSendMs to current time
    TimestampUtils::addTimestampsToHeader(response, finalTimestamps);

    response["body"] = message["body"];
    // Protocol: mirror header.message into body.errorCode on errors (see above).
    if (status == "error" && response["header"].contains("message") &&
        response["header"]["message"].is_string() &&
        (!response["body"].is_object() || !response["body"].contains("errorCode")))
    {
        if (!response["body"].is_object())
            response["body"] = nlohmann::json::object();
        response["body"]["errorCode"] = response["header"]["message"];
    }

    std::string responseString = response.dump();
    log_->debug("Response with timestamps generated: " + responseString);
    return responseString + "\n";
}

void
NetworkManager::addActiveSession(std::shared_ptr<ClientSession> session)
{
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    activeSessions_.insert(session);
}

void
NetworkManager::removeActiveSession(std::shared_ptr<ClientSession> session)
{
    auto sock = session ? session->socket() : nullptr;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        activeSessions_.erase(session);
    }
    // Reclaim this socket's write state (owner-checked: erases only if the
    // mapped owner already expired) and sweep other expired entries, so the
    // map cannot grow with dead sockets under connect/disconnect churn.
    if (sock)
        removeSocketState(sock.get(), sock);
    gcWriteQueues();
}