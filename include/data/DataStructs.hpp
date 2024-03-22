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

struct CharacterDataStruct
{
    int characterId = 0;
    int characterLevel = 0;
    int characterExperiencePoints = 0;
    int characterCurrentHealth = 0;
    int characterCurrentMana = 0;
    std::string characterName = "";
    std::string characterClass = "";
    std::string characterRace = "";
    PositionStruct characterPosition;
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

struct MessageStruct
{
    std::string status = "";
    std::string message = "";
};

struct MobAttributeStruct
{
    int id = 0;
    std::string name = "";
    std::string slug = "";
    int value = 0;
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

    // Define the equality operator
    bool operator==(const MobDataStruct& other) const {
        return uid == other.uid;
    }
};

struct SpawnZoneStruct
{
    int zoneId = 0;
    std::string zoneName = "";
    float minX = 0;
    float maxX = 0;
    float minY = 0;
    float maxY = 0;
    float minZ = 0;
    float maxZ = 0;
    int spawnMobId = 0;
    int spawnCount = 0;
    int spawnedMobsCount = 0;
    std::vector<std::string> spawnedMobsUIDList;
    std::vector<MobDataStruct> spawnedMobsList;
    std::chrono::seconds respawnTime; // Represents respawn time in seconds
};
