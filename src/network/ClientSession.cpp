#include "network/ClientSession.hpp"

#include "events/EventDispatcher.hpp"
#include "game_server/GameServer.hpp"
#include "handlers/MessageHandler.hpp"
#include "utils/ProtocolLimits.hpp"
#include <spdlog/logger.h>

ClientSession::ClientSession(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
    GameServer *gameServer,
    Logger &logger,
    EventQueue &eventQueue,
    EventQueue &eventQueuePing,
    EventDispatcher &eventDispatcher,
    MessageHandler &messageHandler)
    : socket_(socket),
      logger_(logger),
      eventQueue_(eventQueue),
      eventQueuePing_(eventQueuePing),
      gameServer_(gameServer),
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

                // log the received data
                log_->info("DEBUG Received data from client: " + accumulatedData_);

                std::string delimiter = "\n";
                size_t pos;
                // Process all complete messages found.
                while ((pos = accumulatedData_.find(delimiter)) != std::string::npos)
                {
                    std::string message = accumulatedData_.substr(0, pos);
                    // 8KB/message matches proto/mmo_proto.py. Oversize is dropped
                    // with an explicit ERR_OVERSIZE log (wire reply needs strand — Phase 1).
                    if (message.size() > protocol::kMaxMessageBytes)
                    {
                        logger_.logError(std::string(protocol::kErrOversize) + ": message too large, skipping: " +
                                             std::to_string(message.size()) + " bytes",
                            RED);
                        accumulatedData_.erase(0, pos + delimiter.size());
                        continue;
                    }
                    log_->info("Received data from client: " + message);
                    processMessage(message);
                    accumulatedData_.erase(0, pos + delimiter.size());
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
        // Explicit ERR_MALFORMED log (was silent drop => client timeout).
        // Wire ERR_* reply needs NetworkManager strand path — Phase 1.
        logger_.logError(std::string(protocol::kErrMalformed) + ": JSON parsing error: " + std::string(e.what()), RED);
    }
    catch (const std::exception &e)
    {
        logger_.logError(std::string(protocol::kErrMalformed) + ": message handling error: " + std::string(e.what()), RED);
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
