#pragma once
#include "data/DataStructs.hpp"
#include "utils/Database.hpp"
#include "utils/Logger.hpp"
#include <map>
#include <random>
#include <shared_mutex>

class ClassSpawnZoneManager
{
  public:
    ClassSpawnZoneManager(Database &database, Logger &logger);

    void loadClassSpawnZones();

    const ClassSpawnZoneStruct *getSpawnZoneForClass(int classId) const;
    const std::map<int, ClassSpawnZoneStruct> &getAllClassSpawnZones() const;

    static PositionStruct getRandomPointInZone(const ClassSpawnZoneStruct &zone);

  private:
    Database &database_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    mutable std::shared_mutex mutex_;
    std::map<int, ClassSpawnZoneStruct> zones_;

    static std::mt19937 &getRng();
    static std::uniform_real_distribution<float> &getUnitDist();
    static std::uniform_real_distribution<float> &getAngleDist();
};
