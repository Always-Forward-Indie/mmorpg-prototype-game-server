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

    void handleJoinPlayerClientEvent(const Event &event);
    void handleGetCharacterDataEvent(const Event &event);

    void handleMoveCharacterChunkEvent(const Event &event);
    void handleGetSpawnZonesEvent(const Event &event);
    void handleGetMobDataEvent(const Event &event);
    void handleGetCharacterExpForLevelEvent(const Event &event);
    void handleGetExpLevelTableEvent(const Event &event);
    void handleGetMobsListEvent(const Event &event);
    void handleDisconnectChunkEvent(const Event &event);
    void handleJoinChunkServerEvent(const Event &event);
    void handleDisconnectChunkServerEvent(const Event &event);

    void handleGetMobsAttributesEvent(const Event &event);
    void handleGetItemsListEvent(const Event &event);
    void handleGetMobLootInfoEvent(const Event &event);

    NetworkManager &networkManager_;
    GameServices &gameServices_;
};