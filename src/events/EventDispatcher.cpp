#include "events/EventDispatcher.hpp"

EventDispatcher::EventDispatcher(
    EventQueue &eventQueue,
    EventQueue &eventQueuePing,
    GameServer *gameServer,
    Logger &logger)
    : eventQueue_(eventQueue),
      eventQueuePing_(eventQueuePing),
      gameServer_(gameServer),
      logger_(logger)
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
    else if (eventType == "getSpawnZones")
    {
        handleGetSpawnZones(payload, socket);
    }
    else if (eventType == "getMobData")
    {
        handleGetMobData(payload, socket);
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
    Event disconnectEvent(Event::DISCONNECT_CLIENT, payload.clientData.clientId, payload.clientData, socket);

    eventsBatch_.push_back(disconnectEvent);

    // if (eventsBatch_.size() >= BATCH_SIZE)
    //{
    eventQueue_.pushBatch(eventsBatch_);
    eventsBatch_.clear();
    //}
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