// Unit tests for game CharacterManager in-memory map
// + ClassSpawnZoneManager static geometry (no DB).
#include "services/CharacterManager.hpp"
#include "services/ClassSpawnZoneManager.hpp"

#include <gtest/gtest.h>

namespace
{

CharacterDataStruct makeChar(int id, int level = 5)
{
    CharacterDataStruct c;
    c.clientId = 1;
    c.characterId = id;
    c.characterLevel = level;
    c.characterName = "c" + std::to_string(id);
    return c;
}

} // namespace

TEST(GameCharacters, AddGetHasRemove)
{
    Logger logger{"test"};
    CharacterManager mgr(logger);
    EXPECT_FALSE(mgr.hasCharacter(1));
    mgr.addOrUpdateCharacter(makeChar(1));
    EXPECT_TRUE(mgr.hasCharacter(1));
    EXPECT_EQ(mgr.getCharacterById(1).characterName, "c1");
    EXPECT_EQ(mgr.getCharacterById(424242).characterId, 0);
    EXPECT_EQ(mgr.getAllCharacters().size(), 1u);

    // addOrUpdate overwrites
    CharacterDataStruct upd = makeChar(1, 9);
    mgr.addOrUpdateCharacter(upd);
    EXPECT_EQ(mgr.getCharacterById(1).characterLevel, 9);
    EXPECT_EQ(mgr.getAllCharacters().size(), 1u);

    mgr.removeCharacter(1);
    EXPECT_FALSE(mgr.hasCharacter(1));
    mgr.removeCharacter(1); // safe no-op
}

TEST(GameCharacters, UpdatePositionInMemory)
{
    Logger logger{"test"};
    CharacterManager mgr(logger);
    mgr.addOrUpdateCharacter(makeChar(1));
    PositionStruct p;
    p.positionX = 11;
    p.positionY = 22;
    mgr.updateCharacterPositionInMemory(1, 1, p);
    EXPECT_FLOAT_EQ(mgr.getCharacterById(1).characterPosition.positionX, 11.0f);
    EXPECT_FLOAT_EQ(mgr.getCharacterById(1).characterPosition.positionY, 22.0f);
}

TEST(ClassSpawnGeometry, RandomPointStaysInRect)
{
    ClassSpawnZoneStruct z;
    z.shape = ZoneShape::RECT;
    z.minX = 0;
    z.maxX = 100;
    z.minY = 0;
    z.maxY = 50;
    z.minZ = 0;
    z.maxZ = 10;
    for (int i = 0; i < 50; ++i)
    {
        PositionStruct p = ClassSpawnZoneManager::getRandomPointInZone(z);
        EXPECT_GE(p.positionX, 0.0f);
        EXPECT_LE(p.positionX, 100.0f);
        EXPECT_GE(p.positionY, 0.0f);
        EXPECT_LE(p.positionY, 50.0f);
        EXPECT_GE(p.positionZ, 0.0f);
        EXPECT_LE(p.positionZ, 10.0f);
    }
}
