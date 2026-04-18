#include "services/SpawnZoneManager.hpp"
#include "utils/TimeUtils.hpp"
#include <algorithm>
#include <spdlog/logger.h>

SpawnZoneManager::SpawnZoneManager(MobManager &mobManager, Database &database, Logger &logger)
    : mobManager_(mobManager), database_(database), logger_(logger)
{
    log_ = logger.getSystem("spawn");
    loadMobSpawnZones();
}

void
SpawnZoneManager::loadMobSpawnZones()
{
    try
    {
        auto _dbConn = database_.getConnectionLocked();
        pqxx::work transaction(_dbConn.get()); // Start a transaction
        pqxx::result selectSpawnZones = database_.executeQueryWithTransaction(
            transaction,
            "get_mob_spawn_zone_data",
            {});

        if (selectSpawnZones.empty())
        {
            // log that the data is empty
            log_->error("No spawn zones found in the database");
            // Rollback the transaction
            transaction.abort(); // Rollback the transaction
        }

        for (const auto &row : selectSpawnZones)
        {
            SpawnZoneStruct spawnZone;
            // szm_id is the unique id from spawn_zone_mobs — use as map key so
            // multiple mobs in the same zone don't overwrite each other.
            spawnZone.id = row["szm_id"].as<int>();
            spawnZone.zoneId = row["zone_id"].as<int>();
            spawnZone.zoneName = row["zone_name"].as<std::string>();
            spawnZone.minX = row["min_spawn_x"].as<float>();
            spawnZone.maxX = row["max_spawn_x"].as<float>();
            spawnZone.minY = row["min_spawn_y"].as<float>();
            spawnZone.maxY = row["max_spawn_y"].as<float>();
            spawnZone.minZ = row["min_spawn_z"].as<float>();
            spawnZone.maxZ = row["max_spawn_z"].as<float>();
            // New geometry fields (migration 061)
            std::string shapeStr = row["shape_type"].as<std::string>("RECT");
            if (shapeStr == "CIRCLE")
                spawnZone.shape = ZoneShape::CIRCLE;
            else if (shapeStr == "ANNULUS")
                spawnZone.shape = ZoneShape::ANNULUS;
            else
                spawnZone.shape = ZoneShape::RECT;
            spawnZone.centerX = row["center_x"].as<float>(0.0f);
            spawnZone.centerY = row["center_y"].as<float>(0.0f);
            spawnZone.innerRadius = row["inner_radius"].as<float>(0.0f);
            spawnZone.outerRadius = row["outer_radius"].as<float>(0.0f);
            if (!row["exclusion_game_zone_id"].is_null())
                spawnZone.exclusionGameZoneId = row["exclusion_game_zone_id"].as<int>();
            // mob data from spawn_zone_mobs join
            spawnZone.spawnMobId = row["mob_id"].as<int>();
            spawnZone.spawnCount = row["spawn_count"].as<int>();
            spawnZone.respawnTime = std::chrono::seconds(TimeConverter::getSeconds(row["respawn_time"].as<std::string>()));

            mobSpawnZones_[spawnZone.id] = spawnZone;
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

// get mobs in the zone — collect all spawn entries whose zoneId matches
std::vector<MobDataStruct>
SpawnZoneManager::getMobsInZone(int zoneId)
{
    for (const auto &entry : mobSpawnZones_)
    {
        if (entry.second.zoneId == zoneId)
        {
            return entry.second.spawnedMobsList;
        }
    }
    return std::vector<MobDataStruct>();
}
