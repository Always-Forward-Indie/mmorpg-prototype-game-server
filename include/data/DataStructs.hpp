#pragma once
#include "SkillStructs.hpp"
#include <boost/asio.hpp>
#include <chrono>
#include <string>
#include <vector>

/**
 * @brief Timestamp structure for lag compensation
 * Contains timing information for client-server communication and request synchronization
 */
struct TimestampStruct
{
    long long serverRecvMs = 0;     // When server received the packet (milliseconds since epoch)
    long long serverSendMs = 0;     // When server sends the response (milliseconds since epoch)
    long long clientSendMsEcho = 0; // Echo of client timestamp from original request (milliseconds since epoch)
    std::string requestId = "";     // Echo of client requestId for packet synchronization (format: sync_timestamp_session_sequence_hash)
};

struct PositionStruct
{
    float positionX = 0;
    float positionY = 0;
    float positionZ = 0;
    float rotationZ = 0;
};

struct MessageStruct
{
    std::string status = "";
    std::string message = "";
};

struct ChunkInfoStruct
{
    int id = 0;
    std::string ip = "";
    std::shared_ptr<boost::asio::ip::tcp::socket> socket;
    int port = 0;
    float posX = 0;
    float posY = 0;
    float posZ = 0;
    float sizeX = 0;
    float sizeY = 0;
    float sizeZ = 0;
};

struct CharacterAttributeStruct
{
    int id = 0;
    int character_id = 0;
    std::string name = "";
    std::string slug = "";
    int value = 0;
};

struct MobAttributeStruct
{
    int id = 0;
    int mob_id = 0;
    std::string name = "";
    std::string slug = "";
    int value = 0;
};

struct ItemAttributeStruct
{
    int id = 0;
    int item_id = 0;
    std::string name = "";
    std::string slug = "";
    int value = 0;
};

struct ItemDataStruct
{
    int id = 0;
    std::string name = "";
    std::string slug = "";
    std::string description = "";
    bool isQuestItem = false;
    int itemType = 0;
    std::string itemTypeName = "";
    std::string itemTypeSlug = "";
    bool isContainer = false;
    bool isDurable = false;
    bool isTradable = true;
    bool isEquippable = false;
    bool isHarvest = false; // New field for harvestable items
    float weight = 0.0f;
    int rarityId = 1;
    std::string rarityName = "";
    std::string raritySlug = "";
    int stackMax = 64;
    int durabilityMax = 100;
    int vendorPriceBuy = 1;
    int vendorPriceSell = 1;
    int equipSlot = 0;
    std::string equipSlotName = "";
    std::string equipSlotSlug = "";
    int levelRequirement = 0;
    std::vector<ItemAttributeStruct> attributes;
};

struct MobLootInfoStruct
{
    int id = 0;
    int mobId = 0;
    int itemId = 0;
    float dropChance = 0.0f;
};

struct DroppedItemStruct
{
    int uid = 0; // Unique instance ID for the dropped item
    int itemId = 0;
    int quantity = 1;
    PositionStruct position;
    std::chrono::time_point<std::chrono::system_clock> dropTime;
    int droppedByMobUID = 0; // UID of the mob that dropped it
    bool canBePickedUp = true;
};

struct PlayerInventoryItemStruct
{
    int id = 0;
    int characterId = 0;
    int itemId = 0;
    int quantity = 1;
};

struct CharacterDataStruct
{
    int clientId = 0; // ID of the client that owns this character
    int characterId = 0;
    int characterLevel = 0;
    int characterExperiencePoints = 0;
    int characterCurrentHealth = 0;
    int characterCurrentMana = 0;
    int characterMaxHealth = 0;
    int characterMaxMana = 0;
    int expForNextLevel = 0;
    std::string characterName = "";
    std::string characterClass = "";
    std::string characterRace = "";
    PositionStruct characterPosition;
    std::vector<CharacterAttributeStruct> attributes;
    std::vector<SkillStruct> skills;
};

struct ClientDataStruct
{
    int clientId = 0;
    int characterId = 0;
    std::string hash = "";
    std::shared_ptr<boost::asio::ip::tcp::socket> socket;
};

struct MobDataStruct
{
    int id = 0;
    int uid = 0;
    int zoneId = 0;
    std::string name = "";
    std::string slug = "";
    std::string raceName = "";
    int level = 0;
    int currentHealth = 0;
    int currentMana = 0;
    int maxHealth = 0;
    int maxMana = 0;
    std::vector<MobAttributeStruct> attributes;
    std::vector<SkillStruct> skills;
    PositionStruct position;
    int baseExperience = 0;
    int radius = 0;
    bool isAggressive = false;
    bool isDead = false;

    float speedMultiplier = 1.0f;
    float nextMoveTime = 0.0f;

    // New movement attributes
    float movementDirectionX = 0.0f;
    float movementDirectionY = 0.0f;

    float stepMultiplier = 0.0f;

    // Define the equality operator
    bool operator==(const MobDataStruct &other) const
    {
        return uid == other.uid;
    }
};

struct SpawnZoneStruct
{
    int zoneId = 0;
    std::string zoneName = "";
    float posX = 0;
    float sizeX = 0;
    float posY = 0;
    float sizeY = 0;
    float posZ = 0;
    float sizeZ = 0;
    int spawnMobId = 0;
    int spawnCount = 0;
    int spawnedMobsCount = 0;
    std::vector<int> spawnedMobsUIDList;
    std::vector<MobDataStruct> spawnedMobsList;
    std::chrono::seconds respawnTime; // Represents respawn time in seconds
};

/**
 * @brief Experience level entry structure
 * Contains experience points required for a specific level
 */
struct ExperienceLevelEntry
{
    int level = 0;
    int experiencePoints = 0;
};
