#include "network/NetworkManager.hpp"

#include "events/EventDispatcher.hpp"
#include "handlers/MessageHandler.hpp"
#include "utils/TimestampUtils.hpp"
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

    short customPort = std::get<1>(configs).port;
    std::string customIP = std::get<1>(configs).host;
    short maxClients = std::get<1>(configs).max_clients;

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
            log_->info("New Client with IP: " + clientIP + " Port: " + portNumber + " - connected!");
            // Pass the shared pointer to the ClientSession
            auto session = std::make_shared<ClientSession>(clientSocket, gameServer_, logger_, eventQueue_, eventQueuePing_, *eventDispatcher_, *messageHandler_);
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
    acceptor_.close();
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

    // MEDIUM-8 fix: Serialise concurrent writes per socket via a per-socket strand +
    // write queue. Multiple EventHandler threads can call sendResponse concurrently
    // for the same chunk-server connection; without serialisation that causes UB.
    auto dataPtr = std::make_shared<const std::string>(responseString);
    auto state = getOrCreateSocketState(clientSocket.get());

    boost::asio::post(state->strand, [this, clientSocket, dataPtr, state]() mutable
        {
            state->writeQueue.push(dataPtr);
            if (!state->writePending)
                doNextWrite(std::move(clientSocket), std::move(state)); });
}

std::shared_ptr<NetworkManager::SocketWriteState>
NetworkManager::getOrCreateSocketState(boost::asio::ip::tcp::socket *sock)
{
    std::lock_guard<std::mutex> lock(socketStatesMutex_);
    auto &entry = socketStates_[sock];
    if (!entry)
        entry = std::make_shared<SocketWriteState>(io_context_);
    return entry;
}

void
NetworkManager::removeSocketState(boost::asio::ip::tcp::socket *sock)
{
    std::lock_guard<std::mutex> lock(socketStatesMutex_);
    socketStates_.erase(sock);
}

void
NetworkManager::doNextWrite(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
    std::shared_ptr<SocketWriteState> state)
{
    if (state->writeQueue.empty() || !socket->is_open())
    {
        state->writePending = false;
        if (!socket->is_open())
            removeSocketState(socket.get());
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
                    removeSocketState(socket.get());
                    return;
                }
                log_->debug("Bytes sent: " + std::to_string(bytes_transferred));
                boost::system::error_code ec;
                auto ep = socket->remote_endpoint(ec);
                if (!ec)
                    logger_.log("Data sent to: " + ep.address().to_string() + ":" + std::to_string(ep.port()), BLUE);
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
    response["header"]["version"] = "1.0";
    response["body"] = message["body"];

    std::string responseString = response.dump();
    log_->info("Response generated: " + responseString);
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
    response["header"]["version"] = "1.0";

    // Add lag compensation timestamps to header
    TimestampStruct finalTimestamps = timestamps;
    TimestampUtils::setServerSendTimestamp(finalTimestamps); // Set serverSendMs to current time
    TimestampUtils::addTimestampsToHeader(response, finalTimestamps);

    response["body"] = message["body"];

    std::string responseString = response.dump();
    log_->info("Response with timestamps generated: " + responseString);
    return responseString + "\n";
}

void
NetworkManager::setGameServer(GameServer *gameServer)
{
    if (!gameServer)
    {
        throw std::runtime_error("Invalid GameServer pointer in NetworkManager!");
    }
    gameServer_ = gameServer;

    eventDispatcher_ = std::make_unique<EventDispatcher>(eventQueue_, eventQueuePing_, gameServer_, logger_, jsonParser_);
    messageHandler_ = std::make_unique<MessageHandler>(jsonParser_);
}