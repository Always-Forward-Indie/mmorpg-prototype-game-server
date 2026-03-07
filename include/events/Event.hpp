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
        GET_DIALOGUES,                    // Load all dialogues and send to chunk
        GET_QUESTS,                       // Load all quests and send to chunk
        GET_PLAYER_QUESTS,                // Load player quests and send to chunk
        GET_PLAYER_FLAGS,                 // Load player flags and send to chunk
        GET_PLAYER_ACTIVE_EFFECTS,        // Load non-expired active effects and send to chunk
        GET_CHARACTER_ATTRIBUTES_REFRESH, // Re-query attributes (base+equip+perm mods) and send to chunk
        UPDATE_PLAYER_QUEST_PROGRESS,     // Persist quest progress to DB
        UPDATE_PLAYER_FLAG,               // Persist player flag to DB
        GET_GAME_CONFIG                   // Load game_config and send to chunk-server
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