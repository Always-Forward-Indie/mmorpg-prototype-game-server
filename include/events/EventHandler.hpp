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

    void handleGetNPCsListEvent(const Event &event);
    void handleGetNPCsAttributesEvent(const Event &event);
    void handleSavePositionsEvent(const Event &event);
    void handleSaveHpManaEvent(const Event &event); // ARCH-4
    void handleSaveCharacterProgressEvent(const Event &event);
    void handleSaveInventoryChangeEvent(const Event &event);
    void handleGetPlayerInventoryEvent(const Event &event);

    // Dialogue & Quest events
    void handleGetDialoguesEvent(const Event &event);
    void handleGetQuestsEvent(const Event &event);
    void handleGetPlayerQuestsEvent(const Event &event);
    void handleGetPlayerFlagsEvent(const Event &event);
    void handleGetPlayerActiveEffectsEvent(const Event &event);
    void handleGetCharacterAttributesRefreshEvent(const Event &event);
    void handleUpdatePlayerQuestProgressEvent(const Event &event);
    void handleUpdatePlayerFlagEvent(const Event &event);

    // Game config
    void handleGetGameConfigEvent(const Event &event);

    // Vendor / durability
    void handleGetVendorDataEvent(const Event &event);
    void handleSaveDurabilityChangeEvent(const Event &event);
    void handleSaveItemKillCountEvent(const Event &event);
    void handleTransferInventoryItemEvent(const Event &event);
    void handleNullifyItemOwnerEvent(const Event &event);
    void handleDeleteInventoryItemEvent(const Event &event);
    void handleSaveCurrencyTransactionEvent(const Event &event);
    void handleSaveEquipmentChangeEvent(const Event &event);

    // Respawn zones
    void handleGetRespawnZonesEvent(const Event &event);
    void handleGetStatusEffectTemplatesEvent(const Event &event);
    void handleGetGameZonesEvent(const Event &event);

    // Pity & Bestiary
    void handleGetPlayerPityEvent(const Event &event);
    void handleGetPlayerBestiaryEvent(const Event &event);
    void handleSavePityCounterEvent(const Event &event);
    void handleSaveBestiaryKillEvent(const Event &event);

    // Champion system (Stage 3)
    void handleGetTimedChampionTemplatesEvent(const Event &event);
    void handleTimedChampionKilledEvent(const Event &event);

    // Stage 4: Reputation, Mastery, Zone Events
    void handleGetPlayerReputationsEvent(const Event &event);
    void handleSaveReputationEvent(const Event &event);
    void handleGetPlayerMasteriesEvent(const Event &event);
    void handleSaveMasteryEvent(const Event &event);
    void handleGetZoneEventTemplatesEvent(const Event &event);

    // Experience debt
    void handleSaveExperienceDebtEvent(const Event &event);

    // Active status effects
    void handleSaveActiveEffectEvent(const Event &event);

    NetworkManager &networkManager_;
    GameServices &gameServices_;
    std::shared_ptr<spdlog::logger> log_;
};