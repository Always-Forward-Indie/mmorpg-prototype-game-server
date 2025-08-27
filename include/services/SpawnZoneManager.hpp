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
    SpawnZoneManager(MobManager &mobManager, Database &database, Logger &logger);
    void loadMobSpawnZones();

    std::map<int, SpawnZoneStruct> getMobSpawnZones();
    SpawnZoneStruct getMobSpawnZoneByID(int zoneId);
    std::vector<MobDataStruct> getMobsInZone(int zoneId);

    MobDataStruct getMobByUID(std::string mobUID);
    void removeMobByUID(std::string mobUID);

  private:
    Database &database_;
    Logger &logger_;
    MobManager &mobManager_;
    // Store the mob spawn zones in memory with zoneId as key
    std::map<int, SpawnZoneStruct> mobSpawnZones_;
};