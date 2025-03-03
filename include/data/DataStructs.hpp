#pragma once
#include <string>
#include <boost/asio.hpp>

struct PositionStruct
{
    float positionX = 0;
    float positionY = 0;
    float positionZ = 0;
    float rotationZ = 0;
    bool needDBUpdate = false;
};

struct MessageStruct
{
    std::string status = "";
    std::string message = "";
};

struct CharacterAttributeStruct
{
    int id = 0;
    std::string name = "";
    std::string slug = "";
    int value = 0;
};

struct MobAttributeStruct
{
    int id = 0;
    std::string name = "";
    std::string slug = "";
    int value = 0;
};

struct CharacterDataStruct
{
    int characterId = 0;
    int characterLevel = 0;
    int characterExperiencePoints = 0;
    int characterCurrentHealth = 0;
    int characterCurrentMana = 0;
    int expForNextLevel = 0;
    std::string characterName = "";
    std::string characterClass = "";
    std::string characterRace = "";
    PositionStruct characterPosition;
    std::vector<CharacterAttributeStruct> attributes;
    bool needDBUpdate = false;
};

struct ClientDataStruct
{
    int clientId = 0;
    std::string hash = "";
    std::shared_ptr<boost::asio::ip::tcp::socket> socket;
    CharacterDataStruct characterData;
    bool needDBUpdate = false;
};

struct MobDataStruct
{
    int id = 0;
    std::string uid = "";
    int zoneId = 0;
    std::string name = "";
    std::string raceName = "";
    int level = 0;
    int currentHealth = 0;
    int currentMana = 0;
    std::vector<MobAttributeStruct> attributes;
    PositionStruct position;
    bool isAggressive = false;
    bool isDead = false;

    float speedMultiplier = 1.0f; 
    float nextMoveTime = 0.0f;

    // New movement attributes
    float movementDirectionX = 0.0f;
    float movementDirectionY = 0.0f;

    float stepMultiplier = 0.0f; 

    // Define the equality operator
    bool operator==(const MobDataStruct& other) const {
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
    std::vector<std::string> spawnedMobsUIDList;
    std::vector<MobDataStruct> spawnedMobsList;
    std::chrono::seconds respawnTime; // Represents respawn time in seconds
};
