// Unit tests for outboxParseFactKey (pure, no DB).
#include "events/Outbox.hpp"

#include <gtest/gtest.h>

TEST(OutboxParseFactKey, ValidFourPart)
{
    auto p = outboxParseFactKey("10:saveReputation:12345678901234567890:9101");
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->characterId, 10);
    EXPECT_EQ(p->seq, 9101);
}

TEST(OutboxParseFactKey, ZeroBucketAccepted)
{
    // analytics/default facts use characterId 0.
    auto p = outboxParseFactKey("0:analyticsEvent:999:42");
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->characterId, 0);
    EXPECT_EQ(p->seq, 42);
}

TEST(OutboxParseFactKey, ThreePartLegacyRejected)
{
    EXPECT_FALSE(outboxParseFactKey("10:saveReputation:9101").has_value());
}

TEST(OutboxParseFactKey, TestKeysRejected)
{
    // test_factkey_idempotency keys must claim normally but skip watermark.
    EXPECT_FALSE(outboxParseFactKey("test-double-c086e6045877").has_value());
    EXPECT_FALSE(outboxParseFactKey("test-distinct-41c9483b-0").has_value());
}

TEST(OutboxParseFactKey, GarbageRejected)
{
    EXPECT_FALSE(outboxParseFactKey("").has_value());
    EXPECT_FALSE(outboxParseFactKey(":::").has_value());
    EXPECT_FALSE(outboxParseFactKey("abc:saveReputation:1:2").has_value());
    EXPECT_FALSE(outboxParseFactKey("10:saveReputation:1:-5").has_value());
    EXPECT_FALSE(outboxParseFactKey("10:saveReputation:1:2:3").has_value());
}
