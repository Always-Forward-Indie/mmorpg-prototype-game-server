#pragma once

#include "data/DataStructs.hpp"
#include "services/MobManager.hpp"
#include "utils/Database.hpp"
#include "utils/Logger.hpp"
#include "utils/TimeConverter.hpp"
#include <random>
#include <shared_mutex>

class SpawnZoneManager
{
  public:
    SpawnZoneManager(MobManager &mobManager, Logger &logger);
    void loadMobSpawnZones(Database &database);

    /**
     * @brief Replace the in-memory spawn zones (pure, no DB).
     */
    void setSpawnZones(const std::vector<SpawnZoneStruct> &zones);

    std::map<int, SpawnZoneStruct> getMobSpawnZones();
    SpawnZoneStruct getMobSpawnZoneByID(int zoneId);
    std::vector<MobDataStruct> getMobsInZone(int zoneId);

    MobDataStruct getMobByUID(std::string mobUID);
    void removeMobByUID(std::string mobUID);

  private:
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
    MobManager &mobManager_;
    // Store the mob spawn zones in memory with zoneId as key
    std::map<int, SpawnZoneStruct> mobSpawnZones_;
};