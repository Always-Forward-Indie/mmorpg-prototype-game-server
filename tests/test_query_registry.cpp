// Unit tests for QueryRegistry (Step 3): prepared-statement SQL as pure
// data, verifiable without a database. No pqxx, no sockets.
#include "utils/QueryRegistry.hpp"

#include <gtest/gtest.h>
#include <regex>
#include <set>
#include <string>

namespace {

std::vector<int> placeholders(const std::string &sql) {
    static const std::regex re(R"(\$(\d+))");
    std::set<int> out;
    for (std::sregex_iterator it(sql.begin(), sql.end(), re), end; it != end; ++it)
        out.insert(std::stoi((*it)[1].str()));
    return {out.begin(), out.end()};
}

const QueryDef *findByName(const std::string &name) {
    for (const QueryDef *q = queryRegistryBegin(); q != queryRegistryEnd(); ++q)
        if (q->name == name)
            return q;
    return nullptr;
}

} // namespace

TEST(QueryRegistry, NonEmptyAndNamesUnique) {
    EXPECT_GT(queryRegistrySize(), 100u); // game catalog pin; bump on purpose
    std::set<std::string> names;
    for (const QueryDef *q = queryRegistryBegin(); q != queryRegistryEnd(); ++q) {
        EXPECT_FALSE(q->name.empty());
        EXPECT_FALSE(q->sql.empty());
        EXPECT_TRUE(names.insert(std::string(q->name)).second) << "duplicate: " << q->name;
    }
}

TEST(QueryRegistry, PlaceholdersContiguousFromOne) {
    // A $3 with only 2 bound params is a runtime error; catch it statically.
    for (const QueryDef *q = queryRegistryBegin(); q != queryRegistryEnd(); ++q) {
        auto ph = placeholders(std::string(q->sql));
        for (size_t i = 0; i < ph.size(); ++i)
            EXPECT_EQ(ph[i], static_cast<int>(i + 1)) << "query: " << q->name;
    }
}

TEST(QueryRegistry, CriticalQueriesPresent) {
    const QueryDef *join = findByName("search_user");
    ASSERT_NE(join, nullptr);
    EXPECT_NE(std::string(join->sql).find("user_sessions"), std::string::npos);

    EXPECT_NE(findByName("claim_fact_key"), nullptr); // outbox idempotency
    EXPECT_NE(findByName("get_inventory_id_by_token"), nullptr); // per-instance idempotency (096)
    EXPECT_NE(findByName("adopt_legacy_inventory_row"), nullptr);
    EXPECT_NE(findByName("insert_player_inventory_with_token"), nullptr);
    EXPECT_NE(findByName("get_inventory_id_by_char_item"), nullptr); // legacy path
    EXPECT_NE(findByName("insert_player_inventory_item"), nullptr);
    EXPECT_NE(findByName("upsert_outbox_watermark"), nullptr); // outbox high-water
    EXPECT_NE(findByName("get_outbox_global_watermark"), nullptr);
    EXPECT_NE(findByName("get_outbox_diff_keys"), nullptr);
    EXPECT_NE(findByName("delete_old_fact_keys"), nullptr); // retention sweep
    EXPECT_NE(findByName("get_world_objects"), nullptr);
    EXPECT_NE(findByName("update_timed_champion_next_spawn"), nullptr);
    EXPECT_NE(findByName("insert_game_analytics"), nullptr); // was inline exec_params
    EXPECT_NE(findByName("get_timed_champion_interval"), nullptr); // was inline SELECT
}
