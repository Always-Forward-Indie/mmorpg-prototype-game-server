// Unit tests for game NPCManager in-memory catalog (Logger only, no DB).
#include "services/NPCManager.hpp"

#include <gtest/gtest.h>

namespace
{

NPCAttributeStruct attr(const std::string &slug, int value)
{
    NPCAttributeStruct a;
    a.slug = slug;
    a.value = value;
    return a;
}

NPCDataStruct makeNpc(int id, const std::string &name)
{
    NPCDataStruct n;
    n.id = id;
    n.name = name;
    n.npcType = "vendor";
    return n;
}

struct GameNpcFixture : ::testing::Test
{
    Logger logger{"test"};
    NPCManager npcs{logger};
};

} // namespace

TEST_F(GameNpcFixture, SetAndLookup)
{
    EXPECT_FALSE(npcs.isLoaded());
    EXPECT_EQ(npcs.getNPCCount(), 0u);
    npcs.setNPCsList({makeNpc(1, "Varan"), makeNpc(2, "Bob")});
    EXPECT_TRUE(npcs.isLoaded());
    EXPECT_EQ(npcs.getNPCCount(), 2u);
    EXPECT_EQ(npcs.getNPCById(1).name, "Varan");
    EXPECT_EQ(npcs.getNPCById(424242).id, -1); // miss sentinel
    EXPECT_EQ(npcs.getNPCs().size(), 2u);
    EXPECT_EQ(npcs.getNPCsAsVector().size(), 2u);
    // Reload replaces.
    npcs.setNPCsList({makeNpc(3, "Zed")});
    EXPECT_EQ(npcs.getNPCCount(), 1u);
    EXPECT_EQ(npcs.getNPCById(1).id, -1);
}

TEST_F(GameNpcFixture, MaxHealthManaFormulas)
{
    EXPECT_EQ(NPCManager::calculateMaxHealth({attr("max_health", 250)}), 250);
    EXPECT_EQ(NPCManager::calculateMaxHealth({}), 100); // default
    EXPECT_EQ(NPCManager::calculateMaxMana({attr("max_mana", 80)}), 80);
    EXPECT_EQ(NPCManager::calculateMaxMana({}), 50); // default
}
