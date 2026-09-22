#pragma once

#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Player inventory domain (fifth extracted domain).
 *
 * Outbox-guarded inventory change saves and inventory reads.
 */
class InventoryEventHandler : public BaseEventHandler
{
  public:
    InventoryEventHandler(
        NetworkManager &networkManager,
        GameServices &gameServices);

    void handleSaveInventoryChangeEvent(const Event &event);
    void handleGetPlayerInventoryEvent(const Event &event);
};
