#include "services/SpawnZoneManager.hpp"
#include "utils/TimeUtils.hpp"
#include <algorithm>

SpawnZoneManager::SpawnZoneManager(MobManager &mobManager, Database &database, Logger &logger)
    : mobManager_(mobManager), database_(database), logger_(logger)
{
    loadMobSpawnZones();
}

void
SpawnZoneManager::loadMobSpawnZones()
{
    try
    {
        pqxx::work transaction(database_.getConnection()); // Start a transaction
        pqxx::result selectSpawnZones = database_.executeQueryWithTransaction(
            transaction,
            "get_mob_spawn_zone_data",
            {});

        if (selectSpawnZones.empty())
        {
            // log that the data is empty
            logger_.logError("No spawn zones found in the database");
            // Rollback the transaction
            transaction.abort(); // Rollback the transaction
        }

        for (const auto &row : selectSpawnZones)
        {
            SpawnZoneStruct spawnZone;
            spawnZone.zoneId = row["zone_id"].as<int>();
            spawnZone.zoneName = row["zone_name"].as<std::string>();
            spawnZone.posX = row["min_spawn_x"].as<float>();
            spawnZone.sizeX = row["max_spawn_x"].as<float>();
            spawnZone.posY = row["min_spawn_y"].as<float>();
            spawnZone.sizeY = row["max_spawn_y"].as<float>();
            spawnZone.posZ = row["min_spawn_z"].as<float>();
            spawnZone.sizeZ = row["max_spawn_z"].as<float>();
            spawnZone.spawnMobId = row["mob_id"].as<int>();
            spawnZone.spawnCount = row["spawn_count"].as<int>();
            spawnZone.respawnTime = std::chrono::seconds(TimeConverter::getSeconds(row["respawn_time"].as<std::string>()));

            mobSpawnZones_[spawnZone.zoneId] = spawnZone;
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading spawn zones: " + std::string(e.what()));
    }
}

// get all spawn zones
std::map<int, SpawnZoneStruct>
SpawnZoneManager::getMobSpawnZones()
{
    return mobSpawnZones_;
}

// get spawn zone by id
SpawnZoneStruct
SpawnZoneManager::getMobSpawnZoneByID(int zoneId)
{
    auto zone = mobSpawnZones_.find(zoneId);
    if (zone != mobSpawnZones_.end())
    {
        return zone->second;
    }
    else
    {
        return SpawnZoneStruct();
    }
}

// get mobs in the zone
std::vector<MobDataStruct>
SpawnZoneManager::getMobsInZone(int zoneId)
{
    auto zone = mobSpawnZones_.find(zoneId);
    if (zone != mobSpawnZones_.end())
    {
        return zone->second.spawnedMobsList;
    }
    else
    {
        return std::vector<MobDataStruct>();
    }
}

MobDataStruct
SpawnZoneManager::getMobByUID(std::string mobUID)
{
    for (const auto &zone : mobSpawnZones_)
    {
        for (const auto &mob : zone.second.spawnedMobsList)
        {
            if (mob.uid == mobUID)
            {
                return mob;
            }
        }
    }
    return MobDataStruct();
}

void
SpawnZoneManager::removeMobByUID(std::string mobUID)
{
    for (auto &zone : mobSpawnZones_)
    {
        auto it = std::find_if(zone.second.spawnedMobsList.begin(), zone.second.spawnedMobsList.end(), [&mobUID](const MobDataStruct &mob)
            { return mob.uid == mobUID; });
        if (it != zone.second.spawnedMobsList.end())
        {
            zone.second.spawnedMobsList.erase(it);
            // Assuming mobUID is unique, no need to search for more instances
        }
    }
}
