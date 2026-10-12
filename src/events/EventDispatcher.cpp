#include "events/EventDispatcher.hpp"
#include <spdlog/logger.h>

EventDispatcher::EventDispatcher(
    EventQueue &eventQueue,
    EventQueue &eventQueuePing,
    Logger &logger,
    JSONParser &jsonParser)
    : eventQueue_(eventQueue),
      eventQueuePing_(eventQueuePing),
      logger_(logger),
      jsonParser_(jsonParser)
{
    log_ = logger.getSystem("events");
    buildRouteTable();
}

void
EventDispatcher::dispatch(const std::string &eventType,
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // CRITICAL-3 fix: eventsBatch_ is a shared member; serialise all dispatch() invocations
    // so concurrent io_context threads cannot race on it.
    std::lock_guard<std::mutex> dispatchLock(dispatchMutex_);

    const auto routeIt = routeTable_.find(eventType);
    if (routeIt != routeTable_.end())
    {
        (this->*(routeIt->second))(payload, socket);
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
EventDispatcher::buildRouteTable()
{
    // Single source of truth for wire routing (was a 52-branch
    // if/else chain). All handlers share the parse/push
    // (payload, socket) signature, so the table is uniform.
    routeTable_ = {
        {"joinGame", &EventDispatcher::handleJoinGame},
        {"joinGameClient", &EventDispatcher::handleJoinGame},
        {"chunkServerConnection", &EventDispatcher::handleChunkServerConnection},
        {"chunkServerData", &EventDispatcher::handleChunkInitAck},
        {"moveCharacter", &EventDispatcher::handleMoveCharacter},
        {"disconnectClient", &EventDispatcher::handleDisconnect},
        {"pingClient", &EventDispatcher::handlePing},
        {"getSpawnZones", &EventDispatcher::handleGetSpawnZones},
        {"getMobData", &EventDispatcher::handleGetMobData},
        {"getCharacterExpForLevel", &EventDispatcher::handleGetCharacterExpForLevel},
        {"getExpLevelTable", &EventDispatcher::handleGetExpLevelTable},
        {"getNPCsData", &EventDispatcher::handleGetNPCsData},
        {"savePositions", &EventDispatcher::handleSavePositions},
        {"saveHpMana", &EventDispatcher::handleSaveHpMana},
        {"saveCharacterProgress", &EventDispatcher::handleSaveCharacterProgress},
        {"getPlayerQuests", &EventDispatcher::handleGetPlayerQuests},
        {"getPlayerFlags", &EventDispatcher::handleGetPlayerFlags},
        {"getPlayerActiveEffects", &EventDispatcher::handleGetPlayerActiveEffects},
        {"getCharacterAttributes", &EventDispatcher::handleGetCharacterAttributesRefresh},
        {"updatePlayerQuestProgress", &EventDispatcher::handleUpdatePlayerQuestProgress},
        {"updatePlayerFlag", &EventDispatcher::handleUpdatePlayerFlag},
        {"saveInventoryChange", &EventDispatcher::handleSaveInventoryChange},
        {"getPlayerInventory", &EventDispatcher::handleGetPlayerInventory},
        {"saveEquipmentChange", &EventDispatcher::handleSaveEquipmentChange},
        {"saveExperienceDebt", &EventDispatcher::handleSaveExperienceDebt},
        {"saveActiveEffect", &EventDispatcher::handleSaveActiveEffect},
        {"saveDurabilityChange", &EventDispatcher::handleSaveDurabilityChange},
        {"saveItemKillCount", &EventDispatcher::handleSaveItemKillCount},
        {"transferInventoryItem", &EventDispatcher::handleTransferInventoryItem},
        {"nullifyItemOwner", &EventDispatcher::handleNullifyItemOwner},
        {"deleteInventoryItem", &EventDispatcher::handleDeleteInventoryItem},
        {"getPlayerPityData", &EventDispatcher::handleGetPlayerPityData},
        {"getPlayerBestiaryData", &EventDispatcher::handleGetPlayerBestiaryData},
        {"savePityCounter", &EventDispatcher::handleSavePityCounter},
        {"saveBestiaryKill", &EventDispatcher::handleSaveBestiaryKill},
        {"timedChampionKilled", &EventDispatcher::handleTimedChampionKilled},
        {"getPlayerReputationsData", &EventDispatcher::handleGetPlayerReputationsData},
        {"saveReputation", &EventDispatcher::handleSaveReputation},
        {"getPlayerMasteriesData", &EventDispatcher::handleGetPlayerMasteriesData},
        {"saveMastery", &EventDispatcher::handleSaveMastery},
        {"getMasteryDefinitionsData", &EventDispatcher::handleGetMasteryDefinitionsData},
        {"saveLearnedSkill", &EventDispatcher::handleSaveLearnedSkill},
        {"saveSkillBarSlot", &EventDispatcher::handleSaveSkillBarSlot},
        {"saveCurrencyTransaction", &EventDispatcher::handleSaveCurrencyTransaction},
        {"getTitleDefinitionsData", &EventDispatcher::handleGetTitleDefinitionsData},
        {"getPlayerTitlesData", &EventDispatcher::handleGetPlayerTitlesData},
        {"getEmoteDefinitionsData", &EventDispatcher::handleGetEmoteDefinitionsData},
        {"getPlayerEmotesData", &EventDispatcher::handleGetPlayerEmotesData},
        {"savePlayerTitle", &EventDispatcher::handleSavePlayerTitle},
        {"saveSkillCooldown", &EventDispatcher::handleSaveSkillCooldown},
        {"getPlayerSkillCooldowns", &EventDispatcher::handleGetPlayerSkillCooldowns},
        {"analyticsEvent", &EventDispatcher::handleSaveAnalyticsEvent},
        {"savePlayTime", &EventDispatcher::handleSavePlayTime},
        {"markCharactersOnline", &EventDispatcher::handleMarkCharactersOnline},
        {"outboxUnacked", &EventDispatcher::handleOutboxUnacked},
        {"requestCatalogReload", &EventDispatcher::handleRequestCatalogReload},
        {"saveGmAction", &EventDispatcher::handleSaveGmAction},
        {"saveUserBan", &EventDispatcher::handleSaveUserBan},
    };
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
EventDispatcher::handleChunkInitAck(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Chunk init ack (chunkServerData): one-way informational packet sent by
    // the chunk after boot ("Init success/failed for chunk!", header.chunkId).
    // Previously fell into "Unknown event type" error spam on every chunk
    // connect/reconnect. No state change: chunk online/socket mapping already
    // comes from the chunkServerConnection handshake. Just acknowledge loudly
    // enough to see, quietly enough to not spam.
    (void)socket;
    int chunkId = 0;
    std::string message;
    try
    {
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
        if (j.contains("header") && j["header"].is_object())
        {
            chunkId = j["header"].value("chunkId", 0);
            message = j["header"].value("message", "");
        }
    }
    catch (const std::exception &e)
    {
        log_->warn("chunkServerData ack: unparsable payload: " + std::string(e.what()));
        return;
    }
    if (message.find("failed") != std::string::npos || message.find("Failed") != std::string::npos)
        log_->warn("chunk " + std::to_string(chunkId) + " init ack FAILED: " + message);
    else
        log_->info("chunk " + std::to_string(chunkId) + " init ack: " + message);
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
    // Single-parse: positions read from the shared wire json (parsed once in
    // MessageHandler); buffer parse kept as defensive fallback.
    auto positionsList = payload.parsed ? jsonParser_.parseSavePositionsData(*payload.parsed)
                                        : jsonParser_.parseSavePositionsData(payload.rawMessage.data(), payload.rawMessage.size());
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
    auto progressList = payload.parsed ? jsonParser_.parseSaveCharacterProgressData(*payload.parsed)
                                       : jsonParser_.parseSaveCharacterProgressData(payload.rawMessage.data(), payload.rawMessage.size());
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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

void
EventDispatcher::handleSaveGmAction(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::SAVE_GM_ACTION, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSaveGmAction parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleSaveUserBan(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::SAVE_USER_BAN, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSaveUserBan parse error: " + std::string(ex.what()));
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
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

void
EventDispatcher::handleGetMasteryDefinitionsData(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    Event ev(Event::GET_MASTERY_DEFINITIONS, 0, 0, socket);
    eventsBatch_.push_back(ev);
    eventQueue_.pushBatch(eventsBatch_);
    eventsBatch_.clear();
}

void
EventDispatcher::handleSaveLearnedSkill(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::SAVE_LEARNED_SKILL, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSaveLearnedSkill parse error: " + std::string(ex.what()));
    }
}

// ── Skill Bar ─────────────────────────────────────────────────────────────

void
EventDispatcher::handleSaveSkillBarSlot(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::SAVE_SKILL_BAR_SLOT, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSaveSkillBarSlot parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleSaveCurrencyTransaction(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::SAVE_CURRENCY_TRANSACTION, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSaveCurrencyTransaction parse error: " + std::string(ex.what()));
    }
}

// ── Title system ──────────────────────────────────────────────────────────

void
EventDispatcher::handleGetTitleDefinitionsData(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // No characterId needed — loads the global catalog
    Event ev(Event::GET_TITLE_DEFINITIONS, 0, 0, socket);
    eventsBatch_.push_back(ev);
    eventQueue_.pushBatch(eventsBatch_);
    eventsBatch_.clear();
}

void
EventDispatcher::handleGetPlayerTitlesData(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
        int characterId = j["body"].value("characterId", 0);
        Event ev(Event::GET_PLAYER_TITLES, characterId, static_cast<int>(characterId), socket);
        eventsBatch_.push_back(ev);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleGetPlayerTitlesData parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleGetEmoteDefinitionsData(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // No characterId needed — loads the global catalog
    Event ev(Event::GET_EMOTE_DEFINITIONS, 0, 0, socket);
    eventsBatch_.push_back(ev);
    eventQueue_.pushBatch(eventsBatch_);
    eventsBatch_.clear();
}

void
EventDispatcher::handleGetPlayerEmotesData(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
        int characterId = j["body"].value("characterId", 0);
        Event ev(Event::GET_PLAYER_EMOTES, characterId, static_cast<int>(characterId), socket);
        eventsBatch_.push_back(ev);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleGetPlayerEmotesData parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleSavePlayerTitle(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::SAVE_PLAYER_TITLE, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSavePlayerTitle parse error: " + std::string(ex.what()));
    }
}

// Skill cooldown persistence (migration 067)
void
EventDispatcher::handleSaveSkillCooldown(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
        logger_.log("[EventDispatcher] handleSaveSkillCooldown received: " + payload.rawMessage);
        const auto &body = j["body"];
        Event ev(Event::SAVE_SKILL_COOLDOWN, 0, body, socket);
        eventsBatch_.push_back(ev);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSaveSkillCooldown parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleGetPlayerSkillCooldowns(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
        int characterId = j["body"].value("characterId", 0);
        Event ev(Event::GET_PLAYER_SKILL_COOLDOWNS, characterId, static_cast<int>(characterId), socket);
        eventsBatch_.push_back(ev);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleGetPlayerSkillCooldowns parse error: " + std::string(ex.what()));
    }
}

// Analytics system (migration 058)
// Fire-and-forget: parse the raw JSON body and push a SAVE_ANALYTICS_EVENT.
// No response is sent back to the chunk server.
void
EventDispatcher::handleSaveAnalyticsEvent(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
        const auto &body = j["body"];
        Event saveEvent(Event::SAVE_ANALYTICS_EVENT, 0, body, socket);
        eventsBatch_.push_back(saveEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSaveAnalyticsEvent parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleSavePlayTime(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
        PlayTimeDataStruct pt;
        pt.characterId = j["body"].value("characterId", 0);
        pt.sessionPlayTimeSec = j["body"].value("sessionPlayTimeSec", int64_t{0});
        pt.lastSessionPlayTimeSec = j["body"].value("lastSessionPlayTimeSec", int64_t{0});
        pt.isDisconnect = j["body"].value("isDisconnect", false);
        if (pt.characterId > 0)
        {
            Event saveEvent(Event::SAVE_PLAY_TIME, 0, pt, socket);
            eventsBatch_.push_back(saveEvent);
            eventQueue_.pushBatch(eventsBatch_);
            eventsBatch_.clear();
        }
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleSavePlayTime parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleMarkCharactersOnline(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
        if (j.contains("body") && j["body"].is_object() && j["body"].contains("characterIds") && j["body"]["characterIds"].is_array())
        {
            nlohmann::json body = j["body"];
            Event event(Event::MARK_CHARACTERS_ONLINE, 0, body, socket);
            eventsBatch_.push_back(event);
            eventQueue_.pushBatch(eventsBatch_);
            eventsBatch_.clear();
        }
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleMarkCharactersOnline parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleOutboxUnacked(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        // Single-parse: payload.parsed already holds these bytes (parsed once
        // in MessageHandler); handlers keep an owned json, so copy locally
        // and parse only on the defensive empty path.
        nlohmann::json j = payload.parsed ? *payload.parsed : nlohmann::json::parse(payload.rawMessage);
        if (j.contains("body") && j["body"].is_object() && j["body"].contains("keys") && j["body"]["keys"].is_array())
        {
            nlohmann::json body = j["body"];
            Event event(Event::OUTBOX_UNACKED, 0, body, socket);
            eventsBatch_.push_back(event);
            eventQueue_.pushBatch(eventsBatch_);
            eventsBatch_.clear();
        }
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleOutboxUnacked parse error: " + std::string(ex.what()));
    }
}

void
EventDispatcher::handleRequestCatalogReload(
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // DEV-only test precondition (chunk admin op reloadWorldContent): replay
    // the live-editable catalog slices on the current chunk link, no
    // restart. Same internal events (and answering sockets) as the JOIN
    // boot push; chunk handlers merge by id/slug. Completion is observed
    // by polling content (pushes are async) — no ack by design.
    (void)payload;
    if (!socket)
    {
        logger_.logError("handleRequestCatalogReload: null socket, ignoring");
        return;
    }
    try
    {
        Event npcDataEvent(Event::GET_NPCS_LIST, 0, NPCDataStruct(), socket);
        eventsBatch_.push_back(npcDataEvent);
        Event npcAttributesEvent(Event::GET_NPCS_ATTRIBUTES, 0, NPCAttributeStruct(), socket);
        eventsBatch_.push_back(npcAttributesEvent);
        Event vendorDataEvent(Event::GET_VENDOR_DATA, 0, ClientDataStruct(), socket);
        eventsBatch_.push_back(vendorDataEvent);
        Event worldObjectsEvent(Event::GET_WORLD_OBJECTS, 0, 0, socket);
        eventsBatch_.push_back(worldObjectsEvent);
        Event dialoguesEvent(Event::GET_DIALOGUES, 0, ClientDataStruct(), socket);
        eventsBatch_.push_back(dialoguesEvent);
        Event questsEvent(Event::GET_QUESTS, 0, ClientDataStruct(), socket);
        eventsBatch_.push_back(questsEvent);
        Event timedEvent(Event::GET_TIMED_CHAMPION_TEMPLATES, 0, ClientDataStruct(), socket);
        eventsBatch_.push_back(timedEvent);
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
        logger_.log("[join] catalog reload pushed (npcs/vendors/world-objects/dialogues/quests/timed)");
    }
    catch (const std::exception &ex)
    {
        logger_.logError("handleRequestCatalogReload error: " + std::string(ex.what()));
    }
}
