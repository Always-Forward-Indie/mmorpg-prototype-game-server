// Unit tests for protocol v2 framing (S14, utils/WireFraming.hpp).
// Game-server twin of the chunk wire:: framing helpers: sniffing the
// transport from the first bytes + length-prefixed packing. Byte-level,
// no sockets/world.
#include "utils/WireFraming.hpp"

#include <cstdint>
#include <gtest/gtest.h>

TEST(GameFramingV2, SniffNeedsFiveBytes)
{
    EXPECT_EQ(wirev2::sniffFraming(nullptr, 0), wirev2::FramingMode::NeedMore);
    const uint8_t shortBuf[] = {'{', '"', 'a', '"'};
    EXPECT_EQ(wirev2::sniffFraming(shortBuf, sizeof(shortBuf)), wirev2::FramingMode::NeedMore);
}

TEST(GameFramingV2, SniffV1Line)
{
    const std::string line = "{\"header\":{\"eventType\":\"joinGameClient\"}}\n";
    EXPECT_EQ(wirev2::sniffFraming(reinterpret_cast<const uint8_t *>(line.data()), 5),
        wirev2::FramingMode::V1);
}

TEST(GameFramingV2, SniffV2Frame)
{
    const std::string packed = wirev2::packFramedMessage("{\"a\":1}");
    ASSERT_FALSE(packed.empty());
    EXPECT_EQ(wirev2::sniffFraming(reinterpret_cast<const uint8_t *>(packed.data()), 5),
        wirev2::FramingMode::V2);
    const uint8_t bigLen[] = {0x01, 0x00, 0x40, 0x00, '{'};
    EXPECT_EQ(wirev2::sniffFraming(bigLen, sizeof(bigLen)), wirev2::FramingMode::V1);
}

TEST(GameFramingV2, PackRoundTrip)
{
    const std::string body = "{\"header\":{\"eventType\":\"x\"},\"body\":{}}";
    const std::string packed = wirev2::packFramedMessage(body);
    ASSERT_EQ(packed.size(), 4 + body.size());
    const uint8_t *p = reinterpret_cast<const uint8_t *>(packed.data());
    const std::uint32_t n = static_cast<std::uint32_t>(p[0]) |
                            (static_cast<std::uint32_t>(p[1]) << 8) |
                            (static_cast<std::uint32_t>(p[2]) << 16) |
                            (static_cast<std::uint32_t>(p[3]) << 24);
    EXPECT_EQ(n, body.size());
    EXPECT_EQ(packed.substr(4), body);
    EXPECT_TRUE(wirev2::packFramedMessage(std::string(protocol::kMaxFrameBytes + 1, 'x')).empty());
}
