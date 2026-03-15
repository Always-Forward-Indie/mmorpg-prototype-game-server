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

// Runtime buff/debuff applied to a character
struct ActiveEffectStruct
{
    int64_t id = 0;
    int effectId = 0;
    std::string effectSlug = "";
    int attributeId = 0;            // 0 = non-stat effect
    std::string attributeSlug = ""; // slug matched against entity_attributes
    float value = 0.0f;
    std::string sourceType = ""; // quest|skill|item|dialogue
    int64_t expiresAt = 0;       // Unix timestamp sec; 0 = permanent
};

struct MobAttributeStruct
{
    int id = 0;
    int mob_id = 0;
    std::string name = "";
    std::string slug = "";
    int value = 0;
};

struct NPCAttributeStruct
{
    int id = 0;
    int npc_id = 0;
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
    std::string apply_on = "equip"; // 'equip' | 'use'
};

// -----------------------------------------------------------------------
// Item use effect — one row from item_use_effects table.
// -----------------------------------------------------------------------
struct ItemUseEffectStruct
{
    std::string effectSlug;
    std::string attributeSlug;
    float value = 0.0f;
    bool isInstant = true;
    int durationSeconds = 0;
    int tickMs = 0;
    int cooldownSeconds = 30;
};

struct ItemDataStruct
{
    int id = 0;
    std::string slug = ""; // used as localisation key on the client
    bool isQuestItem = false;
    int itemType = 0;
    std::string itemTypeName = "";
    std::string itemTypeSlug = "";
    bool isContainer = false;
    bool isDurable = false;
    bool isTradable = true;
    bool isEquippable = false;
    bool isHarvest = false; // New field for harvestable items
    bool isUsable = false;  // TRUE = can be used from inventory (potion, scroll, food)
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
    bool isTwoHanded = false;
    std::vector<int> allowedClassIds;
    int setId = 0;            // 0 = not part of any set
    std::string setSlug = ""; // slug of the item set
    std::vector<ItemAttributeStruct> attributes;
    std::vector<ItemUseEffectStruct> useEffects; // populated for isUsable items
    std::string masterySlug;                     ///< mastery progression slug (Stage 4, migration 039)
};

struct MobLootInfoStruct
{
    int id = 0;
    int mobId = 0;
    int itemId = 0;
    float dropChance = 0.0f;
    bool isHarvestOnly = false; // only drops from harvesting, not regular kill
    int minQuantity = 1;
    int maxQuantity = 1;
    std::string lootTier = "common"; ///< 'common' | 'uncommon' | 'rare' | 'very_rare' (migration 040)
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
    int experienceDebt = 0;
    std::string characterName = "";
    std::string characterClass = "";
    std::string characterRace = "";
    int classId = 0;
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

    // Rank / difficulty tier (from mob_ranks)
    int rankId = 1; // 1=normal, 2=pack, 3=strong, 4=elite, 5=miniboss, 6=boss
    std::string rankCode = "normal";
    float rankMult = 1.0f; // XP / stat multiplier for this rank

    // Per-mob AI configuration (migration 011, from mob table columns)
    float aggroRange = 400.0f;    // Aggro detection radius (world units)
    float attackRange = 150.0f;   // Melee/ranged attack range (world units)
    float attackCooldown = 2.0f;  // Seconds between attacks
    float chaseMultiplier = 2.0f; // aggroRange * chaseMultiplier = max chase distance
    float patrolSpeed = 1.0f;     // Patrol movement speed multiplier

    // Social and chase behaviour (migration 012)
    bool isSocial = false;       // Group aggro / passive-social enabled
    float chaseDuration = 30.0f; // Max seconds of pursuit before leashing

    // AI depth (migration 016)
    float fleeHpThreshold = 0.0f;      // 0.0 = never flees; 0.25 = flee at 25% HP
    std::string aiArchetype = "melee"; // melee | caster | ranged | support

    // Survival / Rare mob groundwork (Stage 3, migration 038)
    bool canEvolve = false;         ///< Mob can become Survival Champion if alive too long
    bool isRare = false;            ///< Rare mob variant flag
    float rareSpawnChance = 0.0f;   ///< Spawn probability [0..1]
    std::string rareSpawnCondition; ///< 'night' | 'day' | '' = any time

    // Social systems (Stage 4, migration 039)
    std::string factionSlug; ///< faction this mob belongs to
    int repDeltaPerKill = 0; ///< reputation change on kill (negative = lose rep)

    // Bestiary static metadata (migration 040)
    std::string biomeSlug;   ///< e.g. 'forest', 'dungeon', 'swamp'
    std::string mobTypeSlug; ///< e.g. 'beast', 'undead', 'humanoid', 'elemental'
    int hpMin = 0;           ///< Min HP observable in the wild (for bestiary Tier-1)
    int hpMax = 0;           ///< Max HP observable in the wild

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
    int id = 0; // surrogate PK from spawn_zones.id
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

struct NPCDataStruct
{
    int id = 0;
    std::string name = "";
    std::string slug = "";
    std::string raceName = "";
    int level = 0;
    int currentHealth = 0;
    int currentMana = 0;
    int maxHealth = 0;
    int maxMana = 0;
    int zoneId = 0; // game zone this NPC belongs to (from npc_placements or npc_position)
    std::vector<NPCAttributeStruct> attributes;
    std::vector<SkillStruct> skills;
    PositionStruct position;

    // NPC specific properties
    std::string npcType = ""; // "vendor", "quest_giver", "general", etc.
    bool isInteractable = true;
    int dialogueId = 0;
    int questId = 0;
    std::string factionSlug; ///< faction this NPC belongs to (Stage 4, migration 039)

    // Define the equality operator
    bool operator==(const NPCDataStruct &other) const
    {
        return id == other.id;
    }
};
