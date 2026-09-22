#pragma once

#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief World static-data domain (seventh extracted domain).
 *
 * Manager-backed reads: spawn zones, mobs (list/attributes/single),
 * items, mob loot, NPCs (list/attributes). JOIN catalog entries rethrow
 * DatabaseError to dispatchCatalog.
 */
class WorldEventHandler : public BaseEventHandler
{
  public:
    WorldEventHandler(
        NetworkManager &networkManager,
        GameServices &gameServices);

    void handleGetSpawnZonesEvent(const Event &event);
    void handleGetMobsListEvent(const Event &event);
    void handleGetMobsAttributesEvent(const Event &event);
    void handleGetMobDataEvent(const Event &event);
    void handleGetItemsListEvent(const Event &event);
    void handleGetMobLootInfoEvent(const Event &event);
    void handleGetNPCsListEvent(const Event &event);
    void handleGetNPCsAttributesEvent(const Event &event);
};
