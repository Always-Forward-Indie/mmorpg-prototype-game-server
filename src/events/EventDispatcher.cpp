#include "events/EventDispatcher.hpp"

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
}

void
EventDispatcher::dispatch(const std::string &eventType,
    const EventPayload &payload,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
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
    else if (eventType == "updatePlayerQuestProgress")
    {
        handleUpdatePlayerQuestProgress(payload, socket);
    }
    else if (eventType == "updatePlayerFlag")
    {
        handleUpdatePlayerFlag(payload, socket);
    }
    else
    {
        logger_.logError("Unknown event type: " + eventType, RED);
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
        logger_.logError("GameServer is nullptr in EventDispatcher!", RED);
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

    logger_.log("Ping event dispatched for client " + std::to_string(payload.clientData.clientId), GREEN);
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