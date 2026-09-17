// Unit tests for game ItemManager in-memory catalog (Logger only, no DB).
// DB loading stays in loadItems(Database&)/loadMobLoot(Database&), exercised
// live at server boot; the pure set*/get* paths are pinned here.
#include "services/ItemManager.hpp"

#include <gtest/gtest.h>

namespace
{

ItemDataStruct makeItem(int id, const std::string &slug)
{
    ItemDataStruct it;
    it.id = id;
    it.slug = slug;
    it.vendorPriceBuy = 10;
    it.vendorPriceSell = 4;
    return it;
}

MobLootInfoStruct makeLoot(int mob, int item, float chance)
{
    MobLootInfoStruct e;
    e.mobId = mob;
    e.itemId = item;
    e.dropChance = chance;
    return e;
}

struct GameItemFixture : ::testing::Test
{
    Logger logger{"test"};
    ItemManager items{logger};
};

} // namespace

TEST_F(GameItemFixture, SetAndLookup)
{
    EXPECT_EQ(items.getItemById(1).id, 0); // miss sentinel
    EXPECT_TRUE(items.getItems().empty());
    items.setItemsList({makeItem(1, "sword"), makeItem(2, "potion")});
    EXPECT_EQ(items.getItemById(1).slug, "sword");
    EXPECT_EQ(items.getItemById(2).slug, "potion");
    EXPECT_EQ(items.getItems().size(), 2u);
    EXPECT_EQ(items.getItemsAsVector().size(), 2u);
    // Reload replaces.
    items.setItemsList({makeItem(3, "shield")});
    EXPECT_EQ(items.getItemById(1).id, 0);
    EXPECT_EQ(items.getItemById(3).slug, "shield");
}

TEST_F(GameItemFixture, LootTableSetAndLookup)
{
    EXPECT_TRUE(items.getLootForMob(7).empty());
    EXPECT_TRUE(items.getMobLootInfo().empty());
    items.setMobLootInfo({makeLoot(7, 100, 0.5f), makeLoot(7, 101, 1.0f)});
    auto table = items.getLootForMob(7);
    ASSERT_EQ(table.size(), 2u);
    EXPECT_EQ(table[0].itemId, 100);
    EXPECT_EQ(items.getMobLootInfo().size(), 1u);
    EXPECT_TRUE(items.getLootForMob(424242).empty());
    // Reload replaces.
    items.setMobLootInfo({makeLoot(8, 200, 0.25f)});
    EXPECT_TRUE(items.getLootForMob(7).empty());
    EXPECT_EQ(items.getLootForMob(8).size(), 1u);
}
