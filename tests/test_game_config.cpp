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

TEST(GameConfig, GetIntFallback)
{
    Logger logger{"test"};
    GameConfigService cfg(logger);
    EXPECT_EQ(cfg.getInt("scheduler.chunk_sweep_sec", 60), 60); // missing -> fallback
    cfg.setConfig({{"scheduler.chunk_sweep_sec", "30"}, {"bad", "not_a_number"}});
    EXPECT_EQ(cfg.getInt("scheduler.chunk_sweep_sec", 60), 30);
    EXPECT_EQ(cfg.getInt("bad", 60), 60);          // bad value -> fallback
    EXPECT_EQ(cfg.getInt("absent", 60), 60);       // missing -> fallback
}
