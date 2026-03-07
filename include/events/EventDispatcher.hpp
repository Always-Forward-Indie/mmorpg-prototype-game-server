#pragma once

#include "events/Event.hpp"
#include "events/EventQueue.hpp"
#include "game_server/GameServer.hpp"
#include "utils/JSONParser.hpp"
#include <mutex>

class EventDispatcher
{
  public:
    EventDispatcher(EventQueue &eventQueue, EventQueue &eventQueuePing, GameServer *gameServer, Logger &logger, JSONParser &jsonParser);

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
    void handleGetCharacterExpForLevel(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetExpLevelTable(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    void handleGetNPCsData(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleSavePositions(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleSaveHpMana(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket); // ARCH-4
    void handleSaveCharacterProgress(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    // Dialogue & Quest
    void handleGetPlayerQuests(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetPlayerFlags(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetPlayerActiveEffects(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetCharacterAttributesRefresh(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleUpdatePlayerQuestProgress(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleUpdatePlayerFlag(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    EventQueue &eventQueue_;
    EventQueue &eventQueuePing_;
    GameServer *gameServer_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
    JSONParser &jsonParser_;

    std::vector<Event> eventsBatch_;
    constexpr static int BATCH_SIZE = 10;
    mutable std::mutex dispatchMutex_; ///< CRITICAL-3: serialises concurrent dispatch() calls from io_context threads
};