#pragma once
#include "data/DataStructs.hpp"
#include "utils/Database.hpp"
#include "utils/Logger.hpp"
#include <map>
#include <shared_mutex>

class ClassSpawnZoneManager
{
  public:
    ClassSpawnZoneManager(Logger &logger);

    void loadClassSpawnZones(Database &database);

    /**
     * @brief Replace the in-memory class spawn zones (pure, no DB).
     */
    void setClassSpawnZones(const std::vector<ClassSpawnZoneStruct> &zones);

    const ClassSpawnZoneStruct *getSpawnZoneForClass(int classId) const;
    const std::map<int, ClassSpawnZoneStruct> &getAllClassSpawnZones() const;

    static PositionStruct getRandomPointInZone(const ClassSpawnZoneStruct &zone);

  private:
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    mutable std::shared_mutex mutex_;
    std::map<int, ClassSpawnZoneStruct> zones_;
};
