#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "utils/ProtocolLimits.hpp"

/// Protocol v2 wire framing, game-server twin (S14).
/// Mirrors chunk `wire::` framing helpers 1-в-1 (separate repo, no shared
/// include): `[u32 LE length][UTF-8 JSON]`, 4MB cap. See chunk-server
/// docs/api/14-framing-v2-protocol.md for the full contract.
namespace wirev2
{

/// Sniffed transport mode of one connection. Decided once from the first
/// bytes; never changes mid-session.
enum class FramingMode
{
    NeedMore, ///< fewer than 5 bytes buffered — wait for more
    V1,       ///< legacy '\n'-delimited JSON line
    V2        ///< length-prefixed frame
};

/// Classify a fresh connection from its first buffered bytes.
/// Needs >= 5 bytes (u32 length + first body byte '{'). A v1 line always
/// starts with '{', whose 4-byte LE value exceeds any sane cap, so the two
/// cases cannot collide.
inline FramingMode sniffFraming(const uint8_t *data, std::size_t len)
{
    if (data == nullptr || len < 5)
    {
        return FramingMode::NeedMore;
    }
    const std::uint32_t claimed = static_cast<std::uint32_t>(data[0]) |
                                  (static_cast<std::uint32_t>(data[1]) << 8) |
                                  (static_cast<std::uint32_t>(data[2]) << 16) |
                                  (static_cast<std::uint32_t>(data[3]) << 24);
    if (claimed <= protocol::kMaxFrameBytes && data[4] == '{')
    {
        return FramingMode::V2;
    }
    return FramingMode::V1;
}

/// Pack one logical message for a v2 socket: [u32 LE length][bytes].
/// No trailing delimiter. Empty on over-cap input (caller errors).
inline std::string packFramedMessage(const std::string &message)
{
    if (message.size() > protocol::kMaxFrameBytes)
    {
        return {};
    }
    std::string out;
    out.reserve(4 + message.size());
    const std::uint32_t n = static_cast<std::uint32_t>(message.size());
    out.push_back(static_cast<char>(n & 0xFF));
    out.push_back(static_cast<char>((n >> 8) & 0xFF));
    out.push_back(static_cast<char>((n >> 16) & 0xFF));
    out.push_back(static_cast<char>((n >> 24) & 0xFF));
    out += message;
    return out;
}

} // namespace wirev2
