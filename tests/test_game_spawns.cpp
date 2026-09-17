// Unit tests for game SpawnZoneManager + ClassSpawnZoneManager lookups
// (Logger only; MobManager dep is Logger-only too).
#include "services/ClassSpawnZoneManager.hpp"
#include "services/MobManager.hpp"
#include "services/SpawnZoneManager.hpp"

#include <gtest/gtest.h>

namespace
{

SpawnZoneStruct makeZone(int id, int zoneId)
{
    SpawnZoneStruct z;
    z.id = id;
    z.zoneId = zoneId;
    z.zoneName = "z" + std::to_string(zoneId);
    return z;
}

ClassSpawnZoneStruct makeClassZone(int classId)
{
    ClassSpawnZoneStruct z;
    z.id = classId;
    z.classId = classId;
    z.minX = 0;
    z.maxX = 100;
    z.minY = 0;
    z.maxY = 50;
    return z;
}

} // namespace

TEST(GameSpawnZones, SetAndLookup)
{
    Logger logger{"test"};
    MobManager mobs(logger);
    SpawnZoneManager zones(mobs, logger);
    EXPECT_TRUE(zones.getMobSpawnZones().empty());
    zones.setSpawnZones({makeZone(1, 7), makeZone(2, 8)});
    EXPECT_EQ(zones.getMobSpawnZones().size(), 2u);
    EXPECT_EQ(zones.getMobSpawnZoneByID(1).zoneId, 7);
    EXPECT_EQ(zones.getMobSpawnZoneByID(424242).zoneId, 0); // miss -> empty
    // No spawn logic on game side: spawned lists stay empty.
    EXPECT_TRUE(zones.getMobsInZone(7).empty());
    zones.setSpawnZones({makeZone(3, 9)});
    EXPECT_EQ(zones.getMobSpawnZones().size(), 1u);
}

TEST(GameClassSpawnZones, SetLookupGeometry)
{
    Logger logger{"test"};
    ClassSpawnZoneManager mgr(logger);
    EXPECT_EQ(mgr.getSpawnZoneForClass(1), nullptr);
    EXPECT_TRUE(mgr.getAllClassSpawnZones().empty());
    mgr.setClassSpawnZones({makeClassZone(1), makeClassZone(2)});
    ASSERT_NE(mgr.getSpawnZoneForClass(1), nullptr);
    EXPECT_EQ(mgr.getSpawnZoneForClass(1)->classId, 1);
    EXPECT_EQ(mgr.getSpawnZoneForClass(424242), nullptr);
    EXPECT_EQ(mgr.getAllClassSpawnZones().size(), 2u);
    mgr.setClassSpawnZones({makeClassZone(3)});
    EXPECT_EQ(mgr.getSpawnZoneForClass(1), nullptr);
}
