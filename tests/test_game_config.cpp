// Unit tests for game GameConfigService in-memory map (Logger only, no DB).
#include "services/GameConfigService.hpp"

#include <gtest/gtest.h>

TEST(GameConfig, SetAndGetAll)
{
    Logger logger{"test"};
    GameConfigService cfg(logger);
    EXPECT_TRUE(cfg.getAll().empty());
    cfg.setConfig({{"combat.defense_formula_k", "7.5"}, {"a", "b"}});
    auto all = cfg.getAll();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all.at("combat.defense_formula_k"), "7.5");
    // Reload replaces.
    cfg.setConfig({{"x", "1"}});
    EXPECT_EQ(cfg.getAll().size(), 1u);
    EXPECT_EQ(cfg.getAll().at("x"), "1");
}
