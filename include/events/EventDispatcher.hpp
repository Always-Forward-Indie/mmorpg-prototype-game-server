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
    void handleSaveInventoryChange(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetPlayerInventory(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleSaveEquipmentChange(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleSaveExperienceDebt(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleSaveActiveEffect(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleSaveDurabilityChange(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleSaveItemKillCount(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleTransferInventoryItem(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleNullifyItemOwner(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleDeleteInventoryItem(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    // Pity & Bestiary
    void handleGetPlayerPityData(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetPlayerBestiaryData(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleSavePityCounter(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleSaveBestiaryKill(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    // Champion system (Stage 3)
    void handleTimedChampionKilled(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    // Stage 4: Reputation, Mastery
    void handleGetPlayerReputationsData(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleSaveReputation(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetPlayerMasteriesData(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleSaveMastery(const EventPayload &payload, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

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