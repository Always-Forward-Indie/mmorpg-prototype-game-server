#include "services/SpawnZoneManager.hpp"

SpawnZoneManager::SpawnZoneManager(MobManager& mobManager, Database& database, Logger& logger)
    : mobManager_(mobManager) , database_(database), logger_(logger)
{
    loadMobSpawnZones();
}

void SpawnZoneManager::loadMobSpawnZones()
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

        for (const auto& row : selectSpawnZones)
        {
            SpawnZoneStruct spawnZone;
            spawnZone.zoneId = row["zone_id"].as<int>();
            spawnZone.zoneName = row["zone_name"].as<std::string>();
            spawnZone.minX = row["min_spawn_x"].as<float>();
            spawnZone.maxX = row["max_spawn_x"].as<float>();
            spawnZone.minY = row["min_spawn_y"].as<float>();
            spawnZone.maxY = row["max_spawn_y"].as<float>();
            spawnZone.minZ = row["min_spawn_z"].as<float>();
            spawnZone.maxZ = row["max_spawn_z"].as<float>();
            spawnZone.spawnMobId = row["mob_id"].as<int>();
            spawnZone.spawnCount = row["spawn_count"].as<int>();
            spawnZone.respawnTime = std::chrono::seconds(TimeConverter::getSeconds(row["respawn_time"].as<std::string>()));

            mobSpawnZones_[spawnZone.zoneId] = spawnZone;
        }


    }
    catch (const std::exception& e)
    {
        logger_.logError("Error loading spawn zones: " + std::string(e.what()));
    }
}

// get all spawn zones
std::map<int, SpawnZoneStruct> SpawnZoneManager::getMobSpawnZones()
{
    return mobSpawnZones_;
}

// get spawn zone by id
SpawnZoneStruct SpawnZoneManager::getMobSpawnZoneByID(int zoneId)
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
std::vector<MobDataStruct> SpawnZoneManager::getMobsInZone(int zoneId)
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

// spawn count of mobs in spawn zone wiht random position in zone
std::vector<MobDataStruct> SpawnZoneManager::spawnMobsInZone(int zoneId)
{
    std::vector<MobDataStruct> mobs;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> xDist;
    std::uniform_real_distribution<float> yDist;
    std::uniform_real_distribution<float> zDist;

    auto zone = mobSpawnZones_.find(zoneId);

    if (zone != mobSpawnZones_.end() && zone->second.spawnedMobsCount < zone->second.spawnCount)
    {
        for (int i = zone->second.spawnedMobsCount; i < zone->second.spawnCount; i++)
        {
            MobDataStruct mob = mobManager_.getMobById(zone->second.spawnMobId);
            mob.zoneId = zoneId;
            mob.position.positionX = xDist(gen) * (zone->second.maxX - zone->second.minX) + zone->second.minX;
            mob.position.positionY = yDist(gen) * (zone->second.maxY - zone->second.minY) + zone->second.minY;
            mob.position.positionZ = 90.0f;
            mob.position.rotationZ = zDist(gen) * (zone->second.maxZ - zone->second.minZ) + zone->second.minZ;
            //generate unique id for the mob
            mob.uid = std::to_string(mob.id) + "_" + std::to_string(Generators::generateUniqueTimeBasedKey(zoneId));

            // add mob unique ID to the list of ids of spawned mobs
            mobSpawnZones_[zoneId].spawnedMobsUIDList.push_back(mob.uid);
            // add mob to the list of spawned mobs
            mobSpawnZones_[zoneId].spawnedMobsList.push_back(mob);

            mobs.push_back(mob);
            zone->second.spawnedMobsCount++;
        }
    }

    return mobs;
}

//move mobs in the zone randomly to simulate movement
void SpawnZoneManager::moveMobsInZone(int zoneId)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> xDist;
    std::uniform_real_distribution<float> yDist;
    std::uniform_real_distribution<float> zDist;

    auto zone = mobSpawnZones_.find(zoneId);

    if (zone != mobSpawnZones_.end())
    {
        for (auto& mob : zone->second.spawnedMobsList)
        {
            // skip or move the mob from list randomly
            if (Generators::generateSimpleRandomNumber(1, 10) > 8)
            {
                mob.position.positionX = xDist(gen) * (zone->second.maxX - zone->second.minX) + zone->second.minX;
                mob.position.positionY = yDist(gen) * (zone->second.maxY - zone->second.minY) + zone->second.minY;
                mob.position.rotationZ = zDist(gen) * (zone->second.maxZ - zone->second.minZ) + zone->second.minZ;
            }
        }
    }
}

// detect that the mob is dead and decrease spawnedMobs
void SpawnZoneManager::mobDied(int zoneId, std::string mobUID)
{
    auto zone = mobSpawnZones_.find(zoneId);
    if (zone != mobSpawnZones_.end())
    {
        // remove mob from the list of spawned mobs
        removeMobByUID(mobUID);

        // decrease spawnedMobsCount
        zone->second.spawnedMobsCount--;
    }
}

MobDataStruct SpawnZoneManager::getMobByUID(std::string mobUID)
{
    for (const auto& zone : mobSpawnZones_)
    {
        for (const auto& mob : zone.second.spawnedMobsList)
        {
            if (mob.uid == mobUID)
            {
                return mob;
            }
        }
    }
    return MobDataStruct();
}

void SpawnZoneManager::removeMobByUID(std::string mobUID)
{
    for (auto& zone : mobSpawnZones_)
    {
        auto it = std::find_if(zone.second.spawnedMobsList.begin(), zone.second.spawnedMobsList.end(), [&mobUID](const MobDataStruct& mob) {
            return mob.uid == mobUID;
        });
        if (it != zone.second.spawnedMobsList.end()) {
            zone.second.spawnedMobsList.erase(it);
            // Assuming mobUID is unique, no need to search for more instances
        }
    }
}
