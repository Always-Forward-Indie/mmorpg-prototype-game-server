#include "network/ClientSession.hpp"

#include "events/EventDispatcher.hpp"
#include "game_server/GameServer.hpp"
#include "handlers/MessageHandler.hpp"
#include "utils/ProtocolLimits.hpp"
#include "utils/TimestampUtils.hpp"
#include <spdlog/logger.h>

namespace {

// Best-effort eventType so the client can correlate a framing error.
std::string probeEventType(const std::string &message) {
    try {
        auto probe = nlohmann::json::parse(message.substr(0, 512), nullptr, false);
        if (!probe.is_discarded() && probe.is_object() && probe.contains("header") &&
            probe["header"].is_object() && probe["header"].contains("eventType") &&
            probe["header"]["eventType"].is_string())
            return probe["header"]["eventType"].get<std::string>();
    } catch (...) {}
    return "unknown";
}

std::string framingError(const std::string &eventType, const char *code) {
    nlohmann::json err;
    err["header"]["eventType"] = eventType;
    err["header"]["message"] = code;
    err["header"]["status"] = "error";
    err["header"]["timestamp"] = TimestampUtils::getCurrentTimestamp();
    err["header"]["version"] = protocol::kProtocolVersion;
    err["body"] = nlohmann::json::object();
    err["body"]["errorCode"] = code;
    return err.dump() + "\n";
}

} // namespace

ClientSession::ClientSession(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
    Logger &logger,
    EventQueue &eventQueue,
    EventQueue &eventQueuePing,
    EventDispatcher &eventDispatcher,
    MessageHandler &messageHandler)
    : socket_(socket),
      logger_(logger),
      eventQueue_(eventQueue),
      eventQueuePing_(eventQueuePing),
      eventDispatcher_(eventDispatcher),
      messageHandler_(messageHandler)
{
    log_ = logger.getSystem("network");
}

void
ClientSession::start()
{
    doRead();
}

void
ClientSession::setDisconnectCallback(std::function<void(std::shared_ptr<ClientSession>)> callback)
{
    disconnectCallback_ = std::move(callback);
}

void
ClientSession::setSendCallback(
    std::function<void(std::shared_ptr<boost::asio::ip::tcp::socket>, const std::string &)> callback)
{
    sendCallback_ = std::move(callback);
}

void
ClientSession::doRead()
{
    auto self(shared_from_this());
    socket_->async_read_some(boost::asio::buffer(dataBuffer_),
        [this, self](boost::system::error_code ec, std::size_t bytes_transferred)
        {
            if (!ec)
            {
                // Framing DoS guard (see utils/ProtocolLimits.hpp, mirrors chunk + login).
                if (accumulatedData_.size() + bytes_transferred > protocol::kMaxAccumulatedBytes)
                {
                    log_->error("Buffer overflow detected, disconnecting client");
                    handleClientDisconnect();
                    return;
                }
                // Append new data to our session-specific buffer.
                accumulatedData_.append(dataBuffer_.data(), bytes_transferred);

                // debug-level: per-read payload dump (very noisy with bots/swarm).
                log_->debug("DEBUG Received data from client: " + accumulatedData_);

                std::string delimiter = "\n";
                size_t pos;
                size_t processed = 0;
                // Bound per read cycle (parity with chunk): an unbounded loop
                // lets one flooding connection starve the io_context worker.
                constexpr size_t MAX_MESSAGES_PER_READ = 10;
                // Process all complete messages found.
                while ((pos = accumulatedData_.find(delimiter)) != std::string::npos &&
                       processed < MAX_MESSAGES_PER_READ)
                {
                    std::string message = accumulatedData_.substr(0, pos);
                    // 8KB/message matches proto/mmo_proto.py. Oversize gets an
                    // explicit wire ERR_OVERSIZE (previously silent drop).
                    if (message.size() > protocol::kMaxMessageBytes)
                    {
                        logger_.logError(std::string(protocol::kErrOversize) + ": message too large, skipping: " +
                                             std::to_string(message.size()) + " bytes",
                            RED);
                        if (sendCallback_)
                        {
                            try {
                                sendCallback_(socket_,
                                    framingError(probeEventType(message), protocol::kErrOversize));
                            } catch (...) {}
                        }
                        accumulatedData_.erase(0, pos + delimiter.size());
                        ++processed;
                        continue;
                    }
                    log_->debug("Received data from client: " + message);
                    processMessage(message);
                    accumulatedData_.erase(0, pos + delimiter.size());
                    ++processed;
                }
                // Clear a large incomplete buffer (client never sends '\n').
                if (accumulatedData_.size() > protocol::kMaxAccumulatedBytes / 2 &&
                    accumulatedData_.find(delimiter) == std::string::npos)
                {
                    log_->error("Clearing large incomplete buffer to prevent memory leak");
                    accumulatedData_.clear();
                }
                doRead();
            }
            else if (ec == boost::asio::error::eof)
            {
                log_->error("Client disconnected gracefully.");
                handleClientDisconnect();
            }
            else
            {
                log_->error("Error during async_read_some: " + ec.message());
                handleClientDisconnect();
            }
        });
}

void
ClientSession::processMessage(const std::string &message)
{
    try
    {
        // Parse message using MessageHandler with timestamps for all request-response packets
        auto [eventType, clientData, chunkData, characterData, positionData, messageStruct, timestamps] = messageHandler_.parseMessageWithTimestamps(message);

        // Set additional client data
        clientData.socket = socket_;
        clientData.characterId = characterData.characterId;

        EventPayload payload{
            .clientData = clientData,
            .chunkData = chunkData,
            .characterData = characterData,
            .positionData = positionData,
            .messageStruct = messageStruct,
            .rawMessage = message,
        };

        // For ping events, use special handling with timestamps
        if (eventType == "pingClient")
        {
            // Create event with timestamps and socket for ping and push directly to ping queue
            Event pingEvent(Event::PING_CLIENT, clientData.clientId, clientData, socket_, timestamps);
            eventDispatcher_.dispatchPingDirectly(pingEvent);
        }
        else
        {
            // Dispatch other events normally
            eventDispatcher_.dispatch(eventType, payload, socket_);
        }
    }
    catch (const nlohmann::json::parse_error &e)
    {
        // Explicit wire ERR_MALFORMED (was silent drop => client timeout).
        logger_.logError(std::string(protocol::kErrMalformed) + ": JSON parsing error: " + std::string(e.what()), RED);
        if (sendCallback_)
        {
            try {
                sendCallback_(socket_, framingError("unknown", protocol::kErrMalformed));
            } catch (...) {}
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError(std::string(protocol::kErrMalformed) + ": message handling error: " + std::string(e.what()), RED);
        if (sendCallback_)
        {
            try {
                sendCallback_(socket_, framingError("unknown", protocol::kErrMalformed));
            } catch (...) {}
        }
    }
}

void
ClientSession::handleClientDisconnect()
{
    if (disconnectCallback_)
        disconnectCallback_(shared_from_this());

    if (socket_->is_open())
    {
        boost::system::error_code ec;
        socket_->close(ec);
    }

    // Construct minimal disconnect event
    ClientDataStruct clientData;
    clientData.socket = socket_;

    std::vector<Event> eventsBatch;

    // Create disconnect events
    Event disconnectEvent(Event::DISCONNECT_CHUNK_SERVER, 0, clientData, socket_);
    // Event disconnectEventChunk(Event::DISCONNECT_CLIENT_CHUNK, 0, clientData, socket_);

    eventsBatch.push_back(disconnectEvent);
    // eventsBatch.push_back(disconnectEventChunk);

    eventQueue_.pushBatch(eventsBatch);
}
