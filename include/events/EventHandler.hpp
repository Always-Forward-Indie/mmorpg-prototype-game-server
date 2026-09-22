#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

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
    void handleGetTrainerDataEvent(const Event &event);
    void handleSaveDurabilityChangeEvent(const Event &event);
    void handleSaveItemKillCountEvent(const Event &event);
    void handleTransferInventoryItemEvent(const Event &event);
    void handleNullifyItemOwnerEvent(const Event &event);
    void handleDeleteInventoryItemEvent(const Event &event);
    void handleSaveCurrencyTransactionEvent(const Event &event);
    void handleSaveEquipmentChangeEvent(const Event &event);

    // Respawn zones
    void handleGetRespawnZonesEvent(const Event &event);
    void handleGetClassSpawnZonesEvent(const Event &event);
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
    void handleSaveLearnedSkillEvent(const Event &event);
    void handleSaveSkillBarSlotEvent(const Event &event);
    void handleGetZoneEventTemplatesEvent(const Event &event);
    // Mastery definition system
    void handleGetMasteryDefinitionsEvent(const Event &event);
    // Title system
    void handleGetTitleDefinitionsEvent(const Event &event);
    void handleGetPlayerTitlesEvent(const Event &event);
    void handleSavePlayerTitleEvent(const Event &event);
    // Emote system
    void handleGetEmoteDefinitionsEvent(const Event &event);
    void handleGetPlayerEmotesEvent(const Event &event);
    // NPC Ambient Speech system
    void handleGetNPCAmbientSpeechEvent(const Event &event);

    // Experience debt
    void handleSaveExperienceDebtEvent(const Event &event);

    // Active status effects
    void handleSaveActiveEffectEvent(const Event &event);

    // World Interactive Objects (migration 043)
    void handleGetWorldObjectsEvent(const Event &event);

    // Skill cooldown persistence (migration 067)
    void handleSaveSkillCooldownEvent(const Event &event);
    void handleGetPlayerSkillCooldownsEvent(const Event &event);

    // Analytics system (migration 058)
    void handleSaveAnalyticsEventEvent(const Event &event);

    void handleSavePlayTimeEvent(const Event &event);

    // Online status recovery
    void handleMarkCharactersOnlineEvent(const Event &event);

    NetworkManager &networkManager_;
    GameServices &gameServices_;
    std::shared_ptr<spdlog::logger> log_;

    /// Per-chunk-id JOIN serialization. The full catalog push is ~24 sequential
    /// DB round-trips in this worker; two concurrent JOINs for one chunk id
    /// (flap + heartbeat race) would interleave duplicate pushes. Heartbeat
    /// re-asserts skip the push (sameSocket fast-path) so this lock is only
    /// contended on real (re)connects. One entry per chunk id, created on demand.
    std::mutex joinMapMutex_;
    std::unordered_map<int, std::shared_ptr<std::mutex>> joinLocks_;
    std::shared_ptr<std::mutex> joinLockFor(int chunkId);
};