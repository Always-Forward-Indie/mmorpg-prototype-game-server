#pragma once

#include "events/Event.hpp"
#include "events/EventQueue.hpp"
#include "game_server/GameServer.hpp"

class EventDispatcher
{
  public:
    EventDispatcher(EventQueue &eventQueue, EventQueue &eventQueuePing, GameServer *gameServer, Logger &logger);

    void dispatch(const std::string &eventType, const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handlePing(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void dispatchPingDirectly(const Event &pingEvent);

  private:
    void handleJoinGame(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleMoveCharacter(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleDisconnect(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetSpawnZones(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetConnectedClients(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleChunkServerConnection(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    void handleGetMobData(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    EventQueue &eventQueue_;
    EventQueue &eventQueuePing_;
    GameServer *gameServer_;
    Logger &logger_;

    std::vector<Event> eventsBatch_;
    constexpr static int BATCH_SIZE = 10;
};