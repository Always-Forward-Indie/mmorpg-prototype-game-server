#include "services/ClassSpawnZoneManager.hpp"
#include "utils/RandomUtils.hpp"
#include <cmath>
#include <spdlog/logger.h>

ClassSpawnZoneManager::ClassSpawnZoneManager(Logger &logger)
    : logger_(logger)
{
    log_ = logger.getSystem("classspawn");
}

void
ClassSpawnZoneManager::loadClassSpawnZones(Database &database)
{
    try
    {
        auto _dbConn = database.getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        pqxx::result rows = database.executeQueryWithTransaction(txn, "get_class_spawn_zones", {});
        txn.commit();

        std::map<int, ClassSpawnZoneStruct> newZones;
        for (const auto &row : rows)
        {
            ClassSpawnZoneStruct zone;
            zone.id = row["id"].as<int>();
            zone.classId = row["class_id"].as<int>();
            zone.className = row["class_name"].as<std::string>();
            if (!row["zone_id"].is_null())
                zone.zoneId = row["zone_id"].as<int>();
            zone.minX = row["min_x"].as<float>();
            zone.maxX = row["max_x"].as<float>();
            zone.minY = row["min_y"].as<float>();
            zone.maxY = row["max_y"].as<float>();
            zone.minZ = row["min_z"].as<float>();
            zone.maxZ = row["max_z"].as<float>();

            std::string shapeStr = row["shape_type"].as<std::string>("RECT");
            if (shapeStr == "CIRCLE")
                zone.shape = ZoneShape::CIRCLE;
            else if (shapeStr == "ANNULUS")
                zone.shape = ZoneShape::ANNULUS;
            else
                zone.shape = ZoneShape::RECT;

            zone.centerX = row["center_x"].as<float>(0.0f);
            zone.centerY = row["center_y"].as<float>(0.0f);
            zone.innerRadius = row["inner_radius"].as<float>(0.0f);
            zone.outerRadius = row["outer_radius"].as<float>(0.0f);

            newZones[zone.classId] = zone;
        }

        {
            std::unique_lock lock(mutex_);
            zones_ = std::move(newZones);
        }

        log_->info("Loaded {} class spawn zones", zones_.size());
    }
    catch (const std::exception &e)
    {
        logger_.logError("ClassSpawnZoneManager::loadClassSpawnZones: " + std::string(e.what()));
    }
}

const ClassSpawnZoneStruct *
ClassSpawnZoneManager::getSpawnZoneForClass(int classId) const
{
    std::shared_lock lock(mutex_);
    auto it = zones_.find(classId);
    if (it != zones_.end())
        return &it->second;
    return nullptr;
}

void
ClassSpawnZoneManager::setClassSpawnZones(const std::vector<ClassSpawnZoneStruct> &zones)
{
    std::unique_lock lock(mutex_);
    zones_.clear();
    for (const auto &zone : zones)
        zones_[zone.classId] = zone;
}

const std::map<int, ClassSpawnZoneStruct> &
ClassSpawnZoneManager::getAllClassSpawnZones() const
{
    return zones_;
}

PositionStruct
ClassSpawnZoneManager::getRandomPointInZone(const ClassSpawnZoneStruct &zone)
{
    // RNG: RandomUtils (one thread_local engine per thread; the old statics
    // shared one mt19937 + two distributions across ThreadPool workers — race).
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    switch (zone.shape)
    {
    case ZoneShape::CIRCLE:
    {
        float angle = RandomUtils::angle();
        float r = zone.outerRadius * std::sqrt(RandomUtils::uniform01());
        x = zone.centerX + r * std::cos(angle);
        y = zone.centerY + r * std::sin(angle);
        break;
    }
    case ZoneShape::ANNULUS:
    {
        float angle = RandomUtils::angle();
        float r2in = zone.innerRadius * zone.innerRadius;
        float r2out = zone.outerRadius * zone.outerRadius;
        float r = std::sqrt(r2in + RandomUtils::uniform01() * (r2out - r2in));
        x = zone.centerX + r * std::cos(angle);
        y = zone.centerY + r * std::sin(angle);
        break;
    }
    case ZoneShape::RECT:
    default:
    {
        x = zone.minX + RandomUtils::uniform01() * (zone.maxX - zone.minX);
        y = zone.minY + RandomUtils::uniform01() * (zone.maxY - zone.minY);
        break;
    }
    }

    z = zone.minZ + RandomUtils::uniform01() * (zone.maxZ - zone.minZ);

    PositionStruct pos;
    pos.positionX = x;
    pos.positionY = y;
    pos.positionZ = z;
    pos.rotationZ = 0.0f;
    return pos;
}
