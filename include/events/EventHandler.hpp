#pragma once

#include <boost/asio.hpp>
#include <string>

#include "Event.hpp"
#include "network/NetworkManager.hpp"
#include "services/GameServices.hpp"
#include "utils/ResponseBuilder.hpp"

class EventHandler
{
  public:
    EventHandler(NetworkManager &networkManager,
        GameServices &gameServices);
    void dispatchEvent(const Event &event);

  private:
    void handlePingClientEvent(const Event &event);
    void handleJoinClientEvent(const Event &event);
    void handleJoinChunkEvent(const Event &event);
    void handleLeaveClientEvent(const Event &event);
    void handleLeaveChunkEvent(const Event &event);
    void handleGetConnectedCharactersClientEvent(const Event &event);
    void handleGetConnectedCharactersChunkEvent(const Event &event);
    void handleMoveCharacterChunkEvent(const Event &event);
    void handleMoveCharacterClientEvent(const Event &event);
    void handleInteractChunkEvent(const Event &event);
    void handleInteractClientEvent(const Event &event);
    void handleSpawnMobsInZoneEvent(const Event &event);
    void handleGetSpawnZonesEvent(const Event &event);
    void handleGetMobDataEvent(const Event &event);
    void handleGetMobsListEvent(const Event &event);
    void handleZoneMoveMobsEvent(const Event &event);
    void handleDisconnectChunkEvent(const Event &event);
    void handleDisconnectClientEvent(const Event &event);
    void handleJoinChunkServerEvent(const Event &event);
    void handleDisconnectChunkServerEvent(const Event &event);

    NetworkManager &networkManager_;
    GameServices &gameServices_;
};