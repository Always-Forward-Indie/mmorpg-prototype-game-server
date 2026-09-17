// Unit tests for game MobManager in-memory catalog (Logger only, no DB).
#include "services/MobManager.hpp"

#include <gtest/gtest.h>

namespace
{

MobDataStruct makeMob(int id, const std::string &slug, int level = 5)
{
    MobDataStruct m;
    m.id = id;
    m.slug = slug;
    m.name = slug;
    m.level = level;
    m.maxHealth = 100;
    return m;
}

struct GameMobFixture : ::testing::Test
{
    Logger logger{"test"};
    MobManager mobs{logger};
};

} // namespace

TEST_F(GameMobFixture, SetAndLookup)
{
    EXPECT_EQ(mobs.getMobById(1).id, 0); // miss sentinel
    EXPECT_TRUE(mobs.getMobs().empty());
    mobs.setMobsList({makeMob(1, "wolf"), makeMob(2, "boar")});
    EXPECT_EQ(mobs.getMobById(1).slug, "wolf");
    EXPECT_EQ(mobs.getMobById(2).level, 5);
    EXPECT_EQ(mobs.getMobs().size(), 2u);
    EXPECT_EQ(mobs.getMobsAsVector().size(), 2u);
    // Reload replaces.
    mobs.setMobsList({makeMob(3, "bear")});
    EXPECT_EQ(mobs.getMobById(1).id, 0);
    EXPECT_EQ(mobs.getMobs().size(), 1u);
}

TEST_F(GameMobFixture, AttributesAggregation)
{
    MobDataStruct m = makeMob(1, "wolf");
    MobAttributeStruct a;
    a.mob_id = 1;
    a.slug = "strength";
    a.value = 12;
    m.attributes = {a};
    mobs.setMobsList({m});
    auto agg = mobs.getMobsAttributes();
    ASSERT_EQ(agg.size(), 1u);
    EXPECT_EQ(agg.begin()->second.slug, "strength");
    EXPECT_EQ(agg.begin()->second.value, 12);
}
