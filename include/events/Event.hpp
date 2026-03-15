#pragma once
#include "data/DataStructs.hpp"
#include <boost/asio.hpp>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <variant>

// Define the types of data that can be sent in an event
using EventData = std::variant<
    int,
    float,
    std::string,
    nlohmann::json,
    PositionStruct,
    CharacterDataStruct,
    ClientDataStruct,
    ChunkInfoStruct,
    SpawnZoneStruct,
    std::vector<MobDataStruct>,
    MobDataStruct,
    std::vector<MobAttributeStruct>,
    MobAttributeStruct,
    std::vector<ItemDataStruct>,
    ItemDataStruct,
    std::vector<MobLootInfoStruct>,
    MobLootInfoStruct,
    ExperienceLevelEntry,
    NPCDataStruct,
    std::vector<NPCDataStruct>,
    NPCAttributeStruct,
    std::vector<NPCAttributeStruct>,
    std::vector<CharacterDataStruct>
    /* other types */>;

class Event
{
  public:
    enum EventType
    {
        PING_CLIENT,
        JOIN_CHUNK_SERVER,
        JOIN_PLAYER_CLIENT,
        GET_CHARACTER_DATA,
        DISCONNECT_CLIENT,
        DISCONNECT_CHUNK_SERVER,
        GET_CONNECTED_CHARACTERS,
        MOVE_CHARACTER,
        GET_SPAWN_ZONES,
        GET_MOBS_LIST,
        GET_MOBS_ATTRIBUTES,
        GET_MOB_DATA,
        GET_CHARACTER_EXP_FOR_LEVEL,
        GET_EXP_LEVEL_TABLE,
        GET_ITEMS_LIST,
        GET_MOB_LOOT_INFO,
        SPAWN_MOBS_IN_ZONE,
        ZONE_MOVE_MOBS,
        MOVE_MOB,
        GET_NPCS_LIST,
        GET_NPCS_ATTRIBUTES,
        SAVE_POSITIONS,                   // Save player positions to database
        SAVE_CHARACTER_PROGRESS,          // Save player exp/level to database
        SAVE_HP_MANA,                     // ARCH-4: Save player HP/Mana to database
        SAVE_INVENTORY_CHANGE,            // Save a single inventory item change to database
        GET_PLAYER_INVENTORY,             // Load player inventory from DB and send to chunk
        GET_DIALOGUES,                    // Load all dialogues and send to chunk
        GET_QUESTS,                       // Load all quests and send to chunk
        GET_PLAYER_QUESTS,                // Load player quests and send to chunk
        GET_PLAYER_FLAGS,                 // Load player flags and send to chunk
        GET_PLAYER_ACTIVE_EFFECTS,        // Load non-expired active effects and send to chunk
        GET_CHARACTER_ATTRIBUTES_REFRESH, // Re-query attributes (base+equip+perm mods) and send to chunk
        UPDATE_PLAYER_QUEST_PROGRESS,     // Persist quest progress to DB
        UPDATE_PLAYER_FLAG,               // Persist player flag to DB
        GET_GAME_CONFIG,                  // Load game_config and send to chunk-server
        GET_VENDOR_DATA,                  // Load vendor NPC inventory and send to chunk
        SAVE_DURABILITY_CHANGE,           // Persist durability_current to player_inventory
        SAVE_CURRENCY_TRANSACTION,        // Persist vendor/repair transaction log
        SAVE_EQUIPMENT_CHANGE,            // Persist equip/unequip to character_equipment
        SAVE_EXPERIENCE_DEBT,             // Persist experience_debt to characters table
        SAVE_ACTIVE_EFFECT,               // Persist a named status effect to player_active_effect
        SAVE_ITEM_KILL_COUNT,             // Persist Item Soul kill_count to player_inventory
        TRANSFER_INVENTORY_ITEM,          // Move item instance to another character (preserves durability/kill_count)
        NULLIFY_ITEM_OWNER,               // Item dropped to ground: SET character_id = NULL
        DELETE_INVENTORY_ITEM,            // Ground item expired: DELETE the row
        GET_RESPAWN_ZONES,                // Load respawn zones from DB and send to chunk-server
        GET_STATUS_EFFECT_TEMPLATES,      // Load status effect templates from DB and send to chunk-server
        GET_GAME_ZONES,                   // Load game zones (with AABB bounds) from DB and send to chunk-server
        GET_PLAYER_PITY,                  // Load pity counters from DB and send to chunk-server
        GET_PLAYER_BESTIARY,              // Load bestiary kill counts from DB and send to chunk-server
        SAVE_PITY_COUNTER,                // Persist pity kill counter for (character, item)
        SAVE_BESTIARY_KILL,               // Persist bestiary kill count for (character, mob_template)
        GET_TIMED_CHAMPION_TEMPLATES,     // Load timed champion templates from DB and send to chunk-server
        TIMED_CHAMPION_KILLED,            // Chunk-server notifies game-server that a timed champion was killed
        // Stage 4 events
        GET_PLAYER_REPUTATIONS,  // Load character reputation rows from DB and send to chunk-server
        SAVE_REPUTATION,         // Upsert one faction reputation value for a character
        GET_PLAYER_MASTERIES,    // Load character mastery rows from DB and send to chunk-server
        SAVE_MASTERY,            // Upsert one mastery value for a character
        GET_ZONE_EVENT_TEMPLATES // Load zone_event_templates from DB and send to chunk-server
    }; // Define more event types as needed
    Event() = default; // Default constructor
    Event(EventType type, int clientID, const EventData data, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket);
    Event(EventType type, int clientID, const EventData data, const TimestampStruct &timestamps);
    Event(EventType type, int clientID, const EventData data, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const TimestampStruct &timestamps);
    // Event(EventType type, int clientID, const EventData data);

    // Get Event Data
    const EventData getData() const;
    // Get Client ID
    int getClientID() const;
    // Get Client Socket
    std::shared_ptr<boost::asio::ip::tcp::socket> getClientSocket() const;
    // Get Event Type
    EventType getType() const;
    // Get Timestamps
    const TimestampStruct &getTimestamps() const;
    // Check if event has timestamps
    bool hasTimestamps() const;

  private:
    int clientID;
    EventType type;
    EventData eventData;
    std::shared_ptr<boost::asio::ip::tcp::socket> currentClientSocket;
    TimestampStruct timestamps_;
    bool hasTimestamps_ = false;
};