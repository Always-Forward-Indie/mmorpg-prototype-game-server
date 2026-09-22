#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "Event.hpp"
#include "events/handlers/ItemEventHandler.hpp"
#include "events/handlers/WorldEventHandler.hpp"
#include "events/handlers/CatalogEventHandler.hpp"
#include "events/handlers/InventoryEventHandler.hpp"
#include "events/handlers/CharacterEventHandler.hpp"
#include "events/handlers/DialogueQuestEventHandler.hpp"
#include "events/handlers/VendorEventHandler.hpp"
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

    void handleMoveCharacterChunkEvent(const Event &event);
    void handleDisconnectChunkEvent(const Event &event);
    void handleJoinChunkServerEvent(const Event &event);
    void handleDisconnectChunkServerEvent(const Event &event);

    void handleSaveHpManaEvent(const Event &event); // ARCH-4

    // Dialogue & Quest events

    // Game config

    // Vendor / trainer catalog (extracted domain, see events/handlers/).
    // Item lifecycle / economy saves (extracted domain).
    // (Transfer/equipment/currency bodies moved to ItemEventHandler.)

    // Respawn zones

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
    // Mastery definition system
    // Title system
    void handleGetTitleDefinitionsEvent(const Event &event);
    void handleGetPlayerTitlesEvent(const Event &event);
    void handleSavePlayerTitleEvent(const Event &event);
    // Emote system
    void handleGetEmoteDefinitionsEvent(const Event &event);
    void handleGetPlayerEmotesEvent(const Event &event);
    // NPC Ambient Speech system

    // Experience debt

    // Active status effects
    void handleSaveActiveEffectEvent(const Event &event);

    // World Interactive Objects (migration 043)

    // Skill cooldown persistence (migration 067)
    void handleSaveSkillCooldownEvent(const Event &event);
    void handleGetPlayerSkillCooldownsEvent(const Event &event);

    // Analytics system (migration 058)
    void handleSaveAnalyticsEventEvent(const Event &event);

    // Online status recovery

    NetworkManager &networkManager_;
    GameServices &gameServices_;
    std::shared_ptr<spdlog::logger> log_;

    // Extracted domain handlers (god-object split, one domain per commit).
    VendorEventHandler vendorHandler_;
    ItemEventHandler itemHandler_;
    WorldEventHandler worldHandler_;
    CatalogEventHandler catalogHandler_;
    InventoryEventHandler inventoryHandler_;
    CharacterEventHandler characterHandler_;
    DialogueQuestEventHandler dialogueQuestHandler_;

    /// Per-chunk-id JOIN serialization. The full catalog push is ~24 sequential
    /// DB round-trips in this worker; two concurrent JOINs for one chunk id
    /// (flap + heartbeat race) would interleave duplicate pushes. Heartbeat
    /// re-asserts skip the push (sameSocket fast-path) so this lock is only
    /// contended on real (re)connects. One entry per chunk id, created on demand.
    std::mutex joinMapMutex_;
    std::unordered_map<int, std::shared_ptr<std::mutex>> joinLocks_;
    std::shared_ptr<std::mutex> joinLockFor(int chunkId);
};