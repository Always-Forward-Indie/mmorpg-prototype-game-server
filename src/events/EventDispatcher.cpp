#include "events/EventDispatcher.hpp"
#include <spdlog/logger.h>

EventDispatcher::EventDispatcher(
    EventQueue &eventQueue,
    EventQueue &eventQueuePing,
    GameServer *gameServer,
    Logger &logger,
    JSONParser &jsonParser)
    : eventQueue_(eventQueue),
      eventQueuePing_(eventQueuePing),
      gameServer_(gameServer),
      logger_(logger),
      jsonParser_(jsonParser)
{
    log_ = logger.getSystem("events");
}

void
EventDispatcher::dispatch(const std::string &eventType,
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // CRITICAL-3 fix: eventsBatch_ is a shared member; serialise all dispatch() invocations
    // so concurrent io_context threads cannot race on it.
    std::lock_guard<std::mutex> dispatchLock(dispatchMutex_);

    if (eventType == "joinGame")
    {
        handleJoinGame(payload, socket);
    }
    else if (eventType == "chunkServerConnection")
    {
        handleChunkServerConnection(payload, socket);
    }
    else if (eventType == "moveCharacter")
    {
        handleMoveCharacter(payload, socket);
    }
    else if (eventType == "disconnectClient")
    {
        handleDisconnect(payload, socket);
    }
    else if (eventType == "pingClient")
    {
        handlePing(payload, socket);
    }
    else if (eventType == "getSpawnZones")
    {
        handleGetSpawnZones(payload, socket);
    }
    else if (eventType == "getMobData")
    {
        handleGetMobData(payload, socket);
    }
    else if (eventType == "getCharacterExpForLevel")
    {
        handleGetCharacterExpForLevel(payload, socket);
    }
    else if (eventType == "getExpLevelTable")
    {
        handleGetExpLevelTable(payload, socket);
    }
    else if (eventType == "getNPCsData")
    {
        handleGetNPCsData(payload, socket);
    }
    else if (eventType == "savePositions")
    {
        handleSavePositions(payload, socket);
    }
    else if (eventType == "saveHpMana")
    {
        handleSaveHpMana(payload, socket);
    }
    else if (eventType == "saveCharacterProgress")
    {
        handleSaveCharacterProgress(payload, socket);
    }
    else if (eventType == "getPlayerQuests")
    {
        handleGetPlayerQuests(payload, socket);
    }
    else if (eventType == "getPlayerFlags")
    {
        handleGetPlayerFlags(payload, socket);
    }
    else if (eventType == "getPlayerActiveEffects")
    {
        handleGetPlayerActiveEffects(payload, socket);
    }
    else if (eventType == "getCharacterAttributes")
    {
        handleGetCharacterAttributesRefresh(payload, socket);
    }
    else if (eventType == "updatePlayerQuestProgress")
    {
        handleUpdatePlayerQuestProgress(payload, socket);
    }
    else if (eventType == "updatePlayerFlag")
    {
        handleUpdatePlayerFlag(payload, socket);
    }
    else if (eventType == "saveInventoryChange")
    {
        handleSaveInventoryChange(payload, socket);
    }
    else if (eventType == "getPlayerInventory")
    {
        handleGetPlayerInventory(payload, socket);
    }
    else if (eventType == "saveEquipmentChange")
    {
        handleSaveEquipmentChange(payload, socket);
    }
    else if (eventType == "saveExperienceDebt")
    {
        handleSaveExperienceDebt(payload, socket);
    }
    else if (eventType == "saveActiveEffect")
    {
        handleSaveActiveEffect(payload, socket);
    }
    else if (eventType == "saveDurabilityChange")
    {
        handleSaveDurabilityChange(payload, socket);
    }
    else if (eventType == "saveItemKillCount")
    {
        handleSaveItemKillCount(payload, socket);
    }
    else if (eventType == "transferInventoryItem")
    {
        handleTransferInventoryItem(payload, socket);
    }
    else if (eventType == "nullifyItemOwner")
    {
        handleNullifyItemOwner(payload, socket);
    }
    else if (eventType == "deleteInventoryItem")
    {
        handleDeleteInventoryItem(payload, socket);
    }
    else if (eventType == "getPlayerPityData")
    {
        handleGetPlayerPityData(payload, socket);
    }
    else if (eventType == "getPlayerBestiaryData")
    {
        handleGetPlayerBestiaryData(payload, socket);
    }
    else if (eventType == "savePityCounter")
    {
        handleSavePityCounter(payload, socket);
    }
    else if (eventType == "saveBestiaryKill")
    {
        handleSaveBestiaryKill(payload, socket);
    }
    else if (eventType == "timedChampionKilled")
    {
        handleTimedChampionKilled(payload, socket);
    }
    else if (eventType == "getPlayerReputationsData")
    {
        handleGetPlayerReputationsData(payload, socket);
    }
    else if (eventType == "saveReputation")
    {
        handleSaveReputation(payload, socket);
    }
    else if (eventType == "getPlayerMasteriesData")
    {
        handleGetPlayerMasteriesData(payload, socket);
    }
    else if (eventType == "saveMastery")
    {
        handleSaveMastery(payload, socket);
    }
    else
    {
        log_->error("Unknown event type: " + eventType);
    }

    // Push batched events at the end
    if (!eventsBatch_.empty())
    {
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
}

void
EventDispatcher::handleJoinGame(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    Event joinEvent(Event::JOIN_PLAYER_CLIENT, payload.clientData.clientId, payload.clientData, socket);
    eventsBatch_.push_back(joinEvent);
    // if (eventsBatch_.size() >= BATCH_SIZE)
    //{
    eventQueue_.pushBatch(eventsBatch_);
    eventsBatch_.clear();
    //}
}

void
EventDispatcher::handleMoveCharacter(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    Event moveEvent(Event::MOVE_CHARACTER, payload.clientData.clientId, payload.clientData, socket);
    eventsBatch_.push_back(moveEvent);
    if (eventsBatch_.size() >= BATCH_SIZE)
    {
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
}

void
EventDispatcher::handleDisconnect(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Merge last known position into CharacterDataStruct so the game server can persist it
    CharacterDataStruct charDataWithPos = payload.characterData;
    charDataWithPos.characterPosition = payload.positionData;

    Event disconnectEvent(Event::DISCONNECT_CLIENT, payload.clientData.clientId, charDataWithPos, socket);

    eventsBatch_.push_back(disconnectEvent);
    eventQueue_.pushBatch(eventsBatch_);
    eventsBatch_.clear();
}

void
EventDispatcher::handleGetSpawnZones(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (!gameServer_)
    {
        log_->error("GameServer is nullptr in EventDispatcher!");
        return;
    }

    Event getSpawnZonesEvent(Event::SPAWN_MOBS_IN_ZONE, payload.clientData.clientId, payload.clientData, socket);
    eventsBatch_.push_back(getSpawnZonesEvent);

    // if (eventsBatch_.size() >= BATCH_SIZE)
    //{
    eventQueue_.pushBatch(eventsBatch_);
    eventsBatch_.clear();
    //}
}

void
EventDispatcher::handleChunkServerConnection(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    Event chunkServerConnectionEvent(Event::JOIN_CHUNK_SERVER, payload.chunkData.id, payload.chunkData, socket);
    eventsBatch_.push_back(chunkServerConnectionEvent);

    eventQueue_.pushBatch(eventsBatch_);
    eventsBatch_.clear();
}

void
EventDispatcher::handleGetMobData(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    Event getMobDataEvent(Event::GET_MOB_DATA, payload.clientData.clientId, payload.clientData, socket);
    eventsBatch_.push_back(getMobDataEvent);

    // if (eventsBatch_.size() >= BATCH_SIZE)
    //{
    eventQueue_.pushBatch(eventsBatch_);
    eventsBatch_.clear();
    //}
}

void
EventDispatcher::handleGetCharacterExpForLevel(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    Event getCharacterExpForLevelEvent(Event::GET_CHARACTER_EXP_FOR_LEVEL, payload.clientData.clientId, payload.clientData, socket);
    eventsBatch_.push_back(getCharacterExpForLevelEvent);

    eventQueue_.pushBatch(eventsBatch_);
    eventsBatch_.clear();
}

void
EventDispatcher::handleGetExpLevelTable(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    Event getExpLevelTableEvent(Event::GET_EXP_LEVEL_TABLE, payload.clientData.clientId, payload.clientData, socket);
    eventsBatch_.push_back(getExpLevelTableEvent);

    eventQueue_.pushBatch(eventsBatch_);
    eventsBatch_.clear();
}

void
EventDispatcher::handlePing(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    Event pingEvent(Event::PING_CLIENT, payload.clientData.clientId, payload.clientData, socket);
    eventQueuePing_.push(pingEvent);

    log_->info("Ping event dispatched for client " + std::to_string(payload.clientData.clientId));
}

void
EventDispatcher::dispatchPingDirectly(const Event &pingEvent)
{
    eventQueuePing_.push(pingEvent);

    logger_.log("Ping event with timestamps dispatched for client " + std::to_string(pingEvent.getClientID()), GREEN);
}

void
EventDispatcher::handleGetNPCsData(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    Event getNPCsDataEvent(Event::GET_NPCS_LIST, payload.clientData.clientId, payload.clientData, socket);
    eventsBatch_.push_back(getNPCsDataEvent);
}

void
EventDispatcher::handleSavePositions(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Parse positions directly from the raw message — keeps EventPayload clean
    auto positionsList = jsonParser_.parseSavePositionsData(payload.rawMessage.data(), payload.rawMessage.size());
    Event saveEvent(Event::SAVE_POSITIONS, 0, positionsList, socket);
    eventsBatch_.push_back(saveEvent);
    eventQueue_.pushBatch(eventsBatch_);
    eventsBatch_.clear();
}

void
EventDispatcher::handleSaveHpMana(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // ARCH-4: Parse HP/Mana snapshot from chunk-server and create a save event
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        const auto &arr = j["body"]["characters"];
        std::vector<CharacterDataStruct> charactersList;
        charactersList.reserve(arr.size());
        for (const auto &entry : arr)
        {
            CharacterDataStruct cd;
            cd.characterId = entry.value("characterId", 0);
            cd.characterCurrentHealth = entry.value("currentHp", 0);
            cd.characterCurrentMana = entry.value("currentMana", 0);
            if (cd.characterId > 0)
                charactersList.push_back(cd);
        }
        Event saveEvent(Event::SAVE_HP_MANA, 0, charactersList, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSaveHpMana parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleSaveCharacterProgress(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    auto progressList = jsonParser_.parseSaveCharacterProgressData(payload.rawMessage.data(), payload.rawMessage.size());
    Event saveEvent(Event::SAVE_CHARACTER_PROGRESS, 0, progressList, socket);
    eventsBatch_.push_back(saveEvent);
    eventQueue_.pushBatch(eventsBatch_);
    eventsBatch_.clear();
}

void
EventDispatcher::handleGetPlayerQuests(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Expect: {"header":{"eventType":"getPlayerQuests"},"body":{"characterId":N}}
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        int characterId = j["body"].value("characterId", 0);
        Event ev(Event::GET_PLAYER_QUESTS, characterId, static_cast<int>(characterId), socket);
        eventsBatch_.push_back(ev);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleGetPlayerQuests parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleGetPlayerFlags(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        int characterId = j["body"].value("characterId", 0);
        Event ev(Event::GET_PLAYER_FLAGS, characterId, static_cast<int>(characterId), socket);
        eventsBatch_.push_back(ev);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleGetPlayerFlags parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleUpdatePlayerQuestProgress(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Pass the whole raw JSON to the EventHandler via nlohmann::json variant
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        Event ev(Event::UPDATE_PLAYER_QUEST_PROGRESS, 0, j, socket);
        eventsBatch_.push_back(ev);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleUpdatePlayerQuestProgress parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleUpdatePlayerFlag(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        Event ev(Event::UPDATE_PLAYER_FLAG, 0, j, socket);
        eventsBatch_.push_back(ev);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleUpdatePlayerFlag parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleGetPlayerActiveEffects(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        int characterId = j["body"].value("characterId", 0);
        Event ev(Event::GET_PLAYER_ACTIVE_EFFECTS, characterId, static_cast<int>(characterId), socket);
        eventsBatch_.push_back(ev);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleGetPlayerActiveEffects parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleGetCharacterAttributesRefresh(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        int characterId = j["body"].value("characterId", 0);
        Event ev(Event::GET_CHARACTER_ATTRIBUTES_REFRESH, characterId, static_cast<int>(characterId), socket);
        eventsBatch_.push_back(ev);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleGetCharacterAttributesRefresh parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleSaveInventoryChange(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::SAVE_INVENTORY_CHANGE, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSaveInventoryChange parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleSaveEquipmentChange(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::SAVE_EQUIPMENT_CHANGE, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSaveEquipmentChange parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleGetPlayerInventory(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        int characterId = j["body"].value("characterId", 0);
        Event ev(Event::GET_PLAYER_INVENTORY, characterId, static_cast<int>(characterId), socket);
        eventsBatch_.push_back(ev);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleGetPlayerInventory parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleSaveExperienceDebt(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::SAVE_EXPERIENCE_DEBT, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSaveExperienceDebt parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleSaveActiveEffect(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::SAVE_ACTIVE_EFFECT, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSaveActiveEffect parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleSaveDurabilityChange(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::SAVE_DURABILITY_CHANGE, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSaveDurabilityChange parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleSaveItemKillCount(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::SAVE_ITEM_KILL_COUNT, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSaveItemKillCount parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleTransferInventoryItem(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::TRANSFER_INVENTORY_ITEM, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleTransferInventoryItem parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleNullifyItemOwner(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::NULLIFY_ITEM_OWNER, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleNullifyItemOwner parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleDeleteInventoryItem(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::DELETE_INVENTORY_ITEM, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleDeleteInventoryItem parse error: " + std::string(ex.what()));
    }
}

// ── Pity ──────────────────────────────────────────────────────────────────

void
EventDispatcher::handleGetPlayerPityData(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        int characterId = j["body"].value("characterId", 0);
        Event ev(Event::GET_PLAYER_PITY, characterId, static_cast<int>(characterId), socket);
        eventsBatch_.push_back(ev);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleGetPlayerPityData parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleSavePityCounter(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::SAVE_PITY_COUNTER, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSavePityCounter parse error: " + std::string(ex.what()));
    }
}

// ── Bestiary ──────────────────────────────────────────────────────────────

void
EventDispatcher::handleGetPlayerBestiaryData(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        int characterId = j["body"].value("characterId", 0);
        Event ev(Event::GET_PLAYER_BESTIARY, characterId, static_cast<int>(characterId), socket);
        eventsBatch_.push_back(ev);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleGetPlayerBestiaryData parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleSaveBestiaryKill(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::SAVE_BESTIARY_KILL, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSaveBestiaryKill parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleTimedChampionKilled(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event evt(Event::TIMED_CHAMPION_KILLED, 0, body, socket);
        eventsBatch_.push_back(evt);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleTimedChampionKilled parse error: " + std::string(ex.what()));
    }
}

// ── Stage 4: Reputation ───────────────────────────────────────────────────

void
EventDispatcher::handleGetPlayerReputationsData(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        int characterId = j["body"].value("characterId", 0);
        Event ev(Event::GET_PLAYER_REPUTATIONS, characterId, static_cast<int>(characterId), socket);
        eventsBatch_.push_back(ev);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleGetPlayerReputationsData parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleSaveReputation(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::SAVE_REPUTATION, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSaveReputation parse error: " + std::string(ex.what()));
    }
}

// ── Stage 4: Mastery ──────────────────────────────────────────────────────

void
EventDispatcher::handleGetPlayerMasteriesData(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        int characterId = j["body"].value("characterId", 0);
        Event ev(Event::GET_PLAYER_MASTERIES, characterId, static_cast<int>(characterId), socket);
        eventsBatch_.push_back(ev);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleGetPlayerMasteriesData parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleSaveMastery(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        auto j = nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::SAVE_MASTERY, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSaveMastery parse error: " + std::string(ex.what()));
    }
}
