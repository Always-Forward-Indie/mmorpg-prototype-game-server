#include "services/SpawnZoneManager.hpp"
#include <algorithm>
#include "utils/TimeUtils.hpp"

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
    std::uniform_real_distribution<float> dist(0.0f, std::nextafter(1.0f, 0.0f));
    std::uniform_real_distribution<float> rotDist(0.0f, 360.0f);

    auto zone = mobSpawnZones_.find(zoneId);

    if (zone != mobSpawnZones_.end() && zone->second.spawnedMobsCount < zone->second.spawnCount)
    {
        for (int i = zone->second.spawnedMobsCount; i < zone->second.spawnCount; i++)
        {
            MobDataStruct mob = mobManager_.getMobById(zone->second.spawnMobId);
            mob.zoneId = zoneId;
    
            // Центр зоны и размеры (правильные)
            float centerX = zone->second.posX;
            float centerY = zone->second.posY;
            float centerZ = zone->second.posZ;
            float sizeX = zone->second.sizeX;  // Ширина бокса
            float sizeY = zone->second.sizeY;  // Длина бокса
            float sizeZ = zone->second.sizeZ;  // Высота бокса

            // Правильные границы зоны
            float minX = centerX - (sizeX / 2.0f);
            float maxX = centerX + (sizeX / 2.0f);
            float minY = centerY - (sizeY / 2.0f);
            float maxY = centerY + (sizeY / 2.0f);
            float minZ = centerZ - (sizeZ / 2.0f);
            float maxZ = centerZ + (sizeZ / 2.0f);

            // Генерация случайных координат внутри зоны
            mob.position.positionX = minX + (dist(gen) * (maxX - minX));
            mob.position.positionY = minY + (dist(gen) * (maxY - minY));
            mob.position.positionZ = 200;

            // Поворот случайный от 0 до 360 градусов
            mob.position.rotationZ = rotDist(gen);
    
            // Генерация уникального ID
            mob.uid = std::to_string(mob.id) + "_" + std::to_string(Generators::generateUniqueTimeBasedKey(zoneId));
    
            // Добавляем моба в список
            mobSpawnZones_[zoneId].spawnedMobsUIDList.push_back(mob.uid);
            mobSpawnZones_[zoneId].spawnedMobsList.push_back(mob);
    
            mobs.push_back(mob);
            zone->second.spawnedMobsCount++;
        }
    }
    
    return mobs;
}

// // random movement of mobs in the zone
void SpawnZoneManager::moveMobsInZone(int zoneId)
{
    auto zone = mobSpawnZones_.find(zoneId);
    if (zone == mobSpawnZones_.end()) return;

    static std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> randFactor(0.85f, 1.2f);
    std::uniform_real_distribution<float> randCooldown(5.0f, 15.0f);
    std::uniform_real_distribution<float> randAngle(0.0f, 360.0f);
    std::uniform_real_distribution<float> randMoveTime(10.0f, 40.0f);
    std::uniform_real_distribution<float> randSpeedTime(12.0f, 28.0f);
    std::uniform_real_distribution<float> randBorderAngle(30.0f, 100.0f);
    std::uniform_real_distribution<float> randStepMultiplier(1.2f, 3.0f);
    std::uniform_real_distribution<float> randInitialDelay(0.0f, 5.0f);
    std::uniform_real_distribution<float> randRotationJitter(-5.0f, 5.0f);
    std::uniform_real_distribution<float> randBaseSpeed(80.0f, 140.0f);
    std::uniform_real_distribution<float> randDirectionAdjust(0.2f, 0.6f); 
    std::uniform_int_distribution<int> randResetStep(5, 10);

    float currentTime = getCurrentGameTime();
    float minMoveDistance = 120.0f;
    float minSeparationDistance = 140.0f;
    float borderThreshold = std::max(zone->second.sizeX, zone->second.sizeY) * 0.25f;

    float minX = zone->second.posX - (zone->second.sizeX / 2.0f);
    float maxX = zone->second.posX + (zone->second.sizeX / 2.0f);
    float minY = zone->second.posY - (zone->second.sizeY / 2.0f);
    float maxY = zone->second.posY + (zone->second.sizeY / 2.0f);

    for (auto& mob : zone->second.spawnedMobsList)
    {
        if (mob.nextMoveTime == 0.0f)
        {
            mob.nextMoveTime = currentTime + randInitialDelay(rng) + randMoveTime(rng);
        }

        if (currentTime < mob.nextMoveTime)
        {
            continue;
        }

        mob.nextMoveTime = currentTime + std::max(randSpeedTime(rng) / mob.speedMultiplier, 7.0f);

        if (randFactor(rng) > 1.15f) 
        {
            mob.nextMoveTime += randCooldown(rng) * 0.5f;
        }

        bool atBorder = (mob.position.positionX <= minX + borderThreshold ||
                         mob.position.positionX >= maxX - borderThreshold ||
                         mob.position.positionY <= minY + borderThreshold ||
                         mob.position.positionY >= maxY - borderThreshold);

        if (mob.stepMultiplier == 0.0f)
        {
            mob.stepMultiplier = randStepMultiplier(rng);
        }

        float baseSpeed = randBaseSpeed(rng);
        float maxStepSize = std::min((zone->second.sizeX + zone->second.sizeY) * 0.08f, 450.0f);
        float stepSize = std::clamp(baseSpeed * mob.stepMultiplier * randFactor(rng), minMoveDistance * 0.75f, maxStepSize);

        if (currentTime - mob.nextMoveTime > 30.0f)
        {
            stepSize = std::min(stepSize * 1.25f, maxStepSize);
        }

        if (stepSize < minMoveDistance)
        {
            continue;
        }

        float newDirectionX = mob.movementDirectionX;
        float newDirectionY = mob.movementDirectionY;
        float newAngle = randAngle(rng) * (M_PI / 180.0f);
        bool foundValidDirection = false;
        int maxRetries = 4;

        logger_.log("[DEBUG] Mob id: " + mob.uid + 
                    " Current Pos: (" + std::to_string(mob.position.positionX) + ", " + 
                    std::to_string(mob.position.positionY) + ")");

        for (int i = 0; i < maxRetries; ++i)
        {
            if (atBorder)
            {
                float angleToCenter = atan2(zone->second.posY - mob.position.positionY, 
                                            zone->second.posX - mob.position.positionX);
                newAngle = angleToCenter + (randBorderAngle(rng) * (M_PI / 180.0f));
            }

            float tempDirectionX = cos(newAngle);
            float tempDirectionY = sin(newAngle);
            float testNewX = mob.position.positionX + (tempDirectionX * stepSize);
            float testNewY = mob.position.positionY + (tempDirectionY * stepSize);

            bool isTooClose = false;
            for (const auto& otherMob : zone->second.spawnedMobsList)
            {
                if (&otherMob == &mob) continue;

                float distance = sqrt(pow(testNewX - otherMob.position.positionX, 2) + 
                                      pow(testNewY - otherMob.position.positionY, 2));
                if (distance < minSeparationDistance)
                {
                    isTooClose = true;
                    break;
                }
            }

            if (!isTooClose && testNewX >= minX && testNewX <= maxX && testNewY >= minY && testNewY <= maxY)
            {
                newDirectionX = tempDirectionX;
                newDirectionY = tempDirectionY;
                foundValidDirection = true;
                break;
            }
        }

        if (!foundValidDirection)
        {
            float adjustFactor = randDirectionAdjust(rng); 
            newDirectionX = (newDirectionX * adjustFactor) + (mob.movementDirectionX * (1.0f - adjustFactor));
            newDirectionY = (newDirectionY * adjustFactor) + (mob.movementDirectionY * (1.0f - adjustFactor));
        }

        // **Финальная проверка перед изменением координат**
        float newX = std::clamp(mob.position.positionX + (newDirectionX * stepSize), minX, maxX);
        float newY = std::clamp(mob.position.positionY + (newDirectionY * stepSize), minY, maxY);

        bool collisionDetected = false;
        for (const auto& otherMob : zone->second.spawnedMobsList)
        {
            if (&otherMob == &mob) continue;

            float distance = sqrt(pow(newX - otherMob.position.positionX, 2) + 
                                  pow(newY - otherMob.position.positionY, 2));
            if (distance < minSeparationDistance)
            {
                collisionDetected = true;
                break;
            }
        }

        if (!collisionDetected) // Только если нет коллизии, обновляем координаты
        {
            mob.movementDirectionX = newDirectionX;
            mob.movementDirectionY = newDirectionY;
            mob.position.positionX = newX;
            mob.position.positionY = newY;
            mob.position.rotationZ = atan2(newDirectionY, newDirectionX) * (180.0f / M_PI) + randRotationJitter(rng);
        }
        else
        {
            logger_.log("[WARNING] Mob id: " + mob.uid + " skipped move due to collision.");
        }

        logger_.log("[INFO] Mob id: " + mob.uid + 
                    " moved to (" + std::to_string(mob.position.positionX) + ", " + std::to_string(mob.position.positionY) + 
                    ") with rotation " + std::to_string(mob.position.rotationZ) + 
                    " | Next move in " + std::to_string(mob.nextMoveTime - currentTime) + " sec.");
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
