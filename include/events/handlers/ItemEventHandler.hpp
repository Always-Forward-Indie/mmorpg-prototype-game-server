#pragma once

#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Item-lifecycle / economy save handlers (second extracted domain).
 *
 * Durability (repair), item kill-counters, item transfers (P2P trade /
 * ground pickup), ownership nullify, item deletion, currency transactions,
 * equipment changes. All outbox-guarded (factKey claim/ack via events/Outbox).
 */
class ItemEventHandler : public BaseEventHandler
{
  public:
    ItemEventHandler(
        NetworkManager &networkManager,
        GameServices &gameServices);

    void handleSaveDurabilityChangeEvent(const Event &event);
    void handleSaveCurrencyTransactionEvent(const Event &event);
    void handleSaveEquipmentChangeEvent(const Event &event);
    void handleSaveItemKillCountEvent(const Event &event);
    void handleTransferInventoryItemEvent(const Event &event);
    void handleNullifyItemOwnerEvent(const Event &event);
    void handleDeleteInventoryItemEvent(const Event &event);
};
