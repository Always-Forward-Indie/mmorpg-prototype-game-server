#pragma once

#include "events/Event.hpp"
#include "events/EventQueue.hpp"
#include "game_server/GameServer.hpp"

class EventDispatcher {
    public:
        EventDispatcher(EventQueue &eventQueue, EventQueue &eventQueuePing, GameServer *gameServer, Logger &logger);
        
        void dispatch(const std::string &eventType, ClientDataStruct &clientData, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
        
    private:
        void handleJoinGame(ClientDataStruct &clientData, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
        void handleMoveCharacter(ClientDataStruct &clientData, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
        void handleDisconnect(ClientDataStruct &clientData, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
        void handlePing(ClientDataStruct &clientData, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
        void handleGetSpawnZones(ClientDataStruct &clientData, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
        void handleGetConnectedClients(ClientDataStruct &clientData, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    
        EventQueue &eventQueue_;
        EventQueue &eventQueuePing_;
        GameServer *gameServer_;
        Logger &logger_;
    
        std::vector<Event> eventsBatch_;
        constexpr static int BATCH_SIZE = 10;
    };