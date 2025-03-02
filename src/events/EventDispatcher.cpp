#include "events/EventDispatcher.hpp"

EventDispatcher::EventDispatcher(EventQueue &eventQueue, EventQueue &eventQueuePing, GameServer *gameServer, Logger &logger)
    : eventQueue_(eventQueue), eventQueuePing_(eventQueuePing), gameServer_(gameServer), logger_(logger) {}

void EventDispatcher::dispatch(const std::string &eventType, ClientDataStruct &clientData, std::shared_ptr<boost::asio::ip::tcp::socket> socket) {

    if (eventType == "joinGame") {
        handleJoinGame(clientData, socket);
    } else if (eventType == "moveCharacter") {
        handleMoveCharacter(clientData, socket);
    } else if (eventType == "disconnectClient") {
        handleDisconnect(clientData, socket);
    } else if (eventType == "pingClient") {
        handlePing(clientData, socket);
    } else if (eventType == "getSpawnZones") {
        handleGetSpawnZones(clientData, socket);
    } else if (eventType == "getConnectedCharacters") {
        handleGetConnectedClients(clientData, socket);
    } else {
        logger_.logError("Unknown event type: " + eventType, RED);
    }

    // ✅ Push batched events at the end
    if (!eventsBatch_.empty()) {
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
}

void EventDispatcher::handleJoinGame(ClientDataStruct &clientData, std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    Event joinEvent(Event::JOIN_CHARACTER_CHUNK, clientData.clientId, clientData, socket);
    eventsBatch_.push_back(joinEvent);
    if (eventsBatch_.size() >= BATCH_SIZE) {
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
}

void EventDispatcher::handleMoveCharacter(ClientDataStruct &clientData, std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    Event moveEvent(Event::MOVE_CHARACTER_CHUNK, clientData.clientId, clientData, socket);
    eventsBatch_.push_back(moveEvent);
    if (eventsBatch_.size() >= BATCH_SIZE) {
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
}

void EventDispatcher::handleDisconnect(ClientDataStruct &clientData, std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    Event disconnectEvent(Event::DISCONNECT_CLIENT, clientData.clientId, clientData, socket);
    Event disconnectEventChunk(Event::DISCONNECT_CLIENT_CHUNK, clientData.clientId, clientData, socket);
    
    eventsBatch_.push_back(disconnectEvent);
    eventsBatch_.push_back(disconnectEventChunk);

    if (eventsBatch_.size() >= BATCH_SIZE) {
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
}

void EventDispatcher::handlePing(ClientDataStruct &clientData, std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    Event pingEvent(Event::PING_CLIENT, clientData.clientId, clientData, socket);
    eventQueuePing_.push(pingEvent);
}

void EventDispatcher::handleGetSpawnZones(ClientDataStruct &clientData, std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    if (!gameServer_) {
        logger_.logError("GameServer is nullptr in EventDispatcher!", RED);
        return;
    }
    
    gameServer_->spawnZoneManager_.spawnMobsInZone(1);
    SpawnZoneStruct spawnZone = gameServer_->spawnZoneManager_.getMobSpawnZoneByID(1);

    Event getSpawnZonesEvent(Event::SPAWN_MOBS_IN_ZONE, clientData.clientId, spawnZone, socket);
    eventsBatch_.push_back(getSpawnZonesEvent);

    if (eventsBatch_.size() >= BATCH_SIZE) {
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
}

void EventDispatcher::handleGetConnectedClients(ClientDataStruct &clientData, std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    Event getConnectedClientsEvent(Event::GET_CONNECTED_CHARACTERS_CHUNK, clientData.clientId, clientData, socket);
    eventsBatch_.push_back(getConnectedClientsEvent);

    if (eventsBatch_.size() >= BATCH_SIZE) {
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
}
