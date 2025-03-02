#include "network/ClientSession.hpp"
#include "game_server/GameServer.hpp"
#include "handlers/MessageHandler.hpp"
#include "events/EventDispatcher.hpp"

ClientSession::ClientSession(std::shared_ptr<boost::asio::ip::tcp::socket> socket, 
                    GameServer *gameServer,
                    Logger &logger, 
                    EventQueue &eventQueue, 
                    EventQueue &eventQueuePing,
                    JSONParser &jsonParser,
                    EventDispatcher &eventDispatcher,
                    MessageHandler &messageHandler)
    : socket_(socket), 
    logger_(logger), 
    eventQueue_(eventQueue), 
    eventQueuePing_(eventQueuePing),
    gameServer_(gameServer),
    jsonParser_(jsonParser),
    eventDispatcher_(eventDispatcher),
    messageHandler_(messageHandler) {}

    void ClientSession::start() { 
        doRead(); 
    }


    void ClientSession::doRead() {
        auto self(shared_from_this());
        socket_->async_read_some(boost::asio::buffer(dataBuffer_),
            [this, self](boost::system::error_code ec, std::size_t bytes_transferred) {
                if (!ec) {
                    // Append new data to our session-specific buffer.
                    accumulatedData_.append(dataBuffer_.data(), bytes_transferred);
                    std::string delimiter = "\r\n\r\n";
                    size_t pos;
                    // Process all complete messages found.
                    while ((pos = accumulatedData_.find(delimiter)) != std::string::npos) {
                        std::string message = accumulatedData_.substr(0, pos);
                        logger_.log("Received data from client: " + message, YELLOW);
                        processMessage(message);
                        accumulatedData_.erase(0, pos + delimiter.size());
                    }
                    doRead();
                } else if (ec == boost::asio::error::eof) {
                    logger_.logError("Client disconnected gracefully.", RED);
                    handleClientDisconnect();
                    //socket_->close();
                } else {
                    logger_.logError("Error during async_read_some: " + ec.message(), RED);
                    handleClientDisconnect();
                    //socket_->close();
                }
            });
    }


    void ClientSession::processMessage(const std::string &message) {
        try {
            // Parse message using MessageHandler
            auto [eventType, clientData, characterData, positionData, messageStruct] = messageHandler_.parseMessage(message);
    
            // Set additional client data
            clientData.characterData = characterData;
            clientData.characterData.characterPosition = positionData;
            clientData.socket = socket_;
    
            // Dispatch event
            eventDispatcher_.dispatch(eventType, clientData, socket_);
    
        } catch (const nlohmann::json::parse_error &e) {
            logger_.logError("JSON parsing error: " + std::string(e.what()), RED);
        }
    }


    void ClientSession::handleClientDisconnect() {
        if (socket_->is_open()) {
            boost::system::error_code ec;
            socket_->close(ec);
        }
        
    
        // Construct minimal disconnect event
        ClientDataStruct clientData;
        clientData.socket = socket_;
    
        std::vector<Event> eventsBatch;
    
        // Create disconnect events
        Event disconnectEvent(Event::DISCONNECT_CLIENT, 0, clientData, socket_);
        Event disconnectEventChunk(Event::DISCONNECT_CLIENT_CHUNK, 0, clientData, socket_);
    
        eventsBatch.push_back(disconnectEvent);
        eventsBatch.push_back(disconnectEventChunk);
    
        eventQueue_.pushBatch(eventsBatch);
    }

