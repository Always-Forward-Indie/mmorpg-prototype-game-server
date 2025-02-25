#pragma once
#include <memory>
#include <array>
#include <string>
#include <boost/asio.hpp>
#include "utils/Logger.hpp"
#include "events/EventQueue.hpp"
#include "utils/JSONParser.hpp"
#include <nlohmann/json.hpp>

class ClientSession : public std::enable_shared_from_this<ClientSession> {
public:
    // Accept a shared_ptr to a tcp::socket instead of a raw socket.
    ClientSession(std::shared_ptr<boost::asio::ip::tcp::socket> socket, 
                  Logger &logger, 
                  EventQueue &eventQueue, 
                  EventQueue &eventQueuePing,
                  JSONParser &jsonParser)
        : socket_(socket), 
        logger_(logger), 
        eventQueue_(eventQueue), 
        eventQueuePing_(eventQueuePing), 
        jsonParser_(jsonParser) {}

    void start() { doRead(); 
    }

private:
    void doRead() {
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

    void processMessage(const std::string &message) {
        try {
            std::array<char, 1024> messageBuffer{};
            std::copy(message.begin(), message.end(), messageBuffer.begin());
            size_t messageLength = message.size();

            // Parse JSON data
            std::string eventType = jsonParser_.parseEventType(messageBuffer, messageLength);
            ClientDataStruct clientData = jsonParser_.parseClientData(messageBuffer, messageLength);
            CharacterDataStruct characterData = jsonParser_.parseCharacterData(messageBuffer, messageLength);
            PositionStruct positionData = jsonParser_.parsePositionData(messageBuffer, messageLength);
            MessageStruct messageStruct = jsonParser_.parseMessage(messageBuffer, messageLength);

            logger_.log("Event type: " + eventType, YELLOW);
            logger_.log("Client ID: " + std::to_string(clientData.clientId), YELLOW);
            logger_.log("Client hash: " + clientData.hash, YELLOW);
            logger_.log("Character ID: " + std::to_string(characterData.characterId), YELLOW);

            std::vector<Event> eventsBatch;
            std::vector<Event> pingEventBatch;
            constexpr int BATCH_SIZE = 10;
            constexpr int PING_BATCH_SIZE = 1;

            // joinGame event
            if (eventType == "joinGame" && !clientData.hash.empty() && clientData.clientId != 0) {
                characterData.characterPosition = positionData;
                clientData.characterData = characterData;
                clientData.socket = socket_;  // now socket_ is a shared_ptr
                Event joinEvent(Event::JOIN_CHARACTER_CHUNK, clientData.clientId, clientData, socket_);
                eventsBatch.push_back(joinEvent);
                if (eventsBatch.size() >= BATCH_SIZE) {
                    eventQueue_.pushBatch(eventsBatch);
                    eventsBatch.clear();
                }
            }

            // getConnectedCharacters event
            if (eventType == "getConnectedCharacters" && !clientData.hash.empty() && clientData.clientId != 0) {
                characterData.characterPosition = positionData;
                clientData.characterData = characterData;
                clientData.socket = socket_;
                Event getConnectedCharactersEvent(Event::GET_CONNECTED_CHARACTERS_CHUNK, clientData.clientId, clientData, socket_);
                eventsBatch.push_back(getConnectedCharactersEvent);
                if (eventsBatch.size() >= BATCH_SIZE) {
                    eventQueue_.pushBatch(eventsBatch);
                    eventsBatch.clear();
                }
            }

            // moveCharacter event
            if (eventType == "moveCharacter" && !clientData.hash.empty() && clientData.clientId != 0 && characterData.characterId != 0) {
                characterData.characterPosition = positionData;
                clientData.characterData = characterData;
                clientData.socket = socket_;
                Event moveCharacterEvent(Event::MOVE_CHARACTER_CHUNK, clientData.clientId, clientData, socket_);
                eventsBatch.push_back(moveCharacterEvent);
                if (eventsBatch.size() >= BATCH_SIZE) {
                    eventQueue_.pushBatch(eventsBatch);
                    eventsBatch.clear();
                }
            }

            // disconnectClient event
            if (eventType == "disconnectClient" && !clientData.hash.empty() && clientData.clientId != 0) {
                characterData.characterPosition = positionData;
                clientData.characterData = characterData;
                clientData.socket = socket_;
                // Send a disconnect event to the game server
                Event disconnectEvent(Event::DISCONNECT_CLIENT, clientData.clientId, clientData, socket_);
                // Send a disconnect event to the chunk server
                Event disconnectEventChunk(Event::DISCONNECT_CLIENT_CHUNK, clientData.clientId, clientData, socket_);

                eventsBatch.push_back(disconnectEventChunk);
                eventsBatch.push_back(disconnectEvent);

                if (eventsBatch.size() >= BATCH_SIZE) {
                    eventQueue_.pushBatch(eventsBatch);
                    eventsBatch.clear();
                }
            }

            // pingClient event
            if (eventType == "pingClient") {
                clientData.socket = socket_;
                Event pingEvent(Event::PING_CLIENT, clientData.clientId, clientData, socket_);
                pingEventBatch.push_back(pingEvent);
                if (pingEventBatch.size() >= PING_BATCH_SIZE) {
                    eventQueuePing_.pushBatch(pingEventBatch);
                    pingEventBatch.clear();
                }
            }

            // Push any remaining events.
            if (!eventsBatch.empty()) {
                eventQueue_.pushBatch(eventsBatch);
            }
        }
        catch (const nlohmann::json::parse_error &e) {
            logger_.logError("JSON parsing error: " + std::string(e.what()), RED);
        }
    }


    void handleClientDisconnect() {
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
    


    std::shared_ptr<boost::asio::ip::tcp::socket> socket_;
    std::array<char, 1024> dataBuffer_;
    std::string accumulatedData_;
    Logger &logger_;
    EventQueue &eventQueue_;
    EventQueue &eventQueuePing_;
    JSONParser &jsonParser_;
};
