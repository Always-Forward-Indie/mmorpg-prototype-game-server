#pragma once

#include <cstddef>

/// Single source of truth for wire-protocol framing limits.
///
/// Mirrors `mmorpg-prototype-integration-tests/proto/mmo_proto.py`:
/// one JSON object per line, `\n`-terminated, UTF-8, max 8KB/message.
namespace protocol {

/// Max bytes per single `\n`-framed JSON message (8KB, matches test harness).
inline constexpr std::size_t kMaxMessageBytes = 8 * 1024;

/// Max bytes buffered per connection without a `\n` delimiter (DoS guard).
inline constexpr std::size_t kMaxAccumulatedBytes = 64 * 1024;

/// Read chunk size for `async_read_some`.
inline constexpr std::size_t kReadChunkBytes = 8 * 1024;

/// Wire error codes for framing failures (protocol v1, additive only).
inline constexpr const char *kErrOversize = "ERR_OVERSIZE";
inline constexpr const char *kErrMalformed = "ERR_MALFORMED";

/// Protocol v2 (S14, chunk docs/api/14-framing-v2-protocol.md): length-prefixed
/// frames `[u32 LE length][UTF-8 JSON]`, no `\n` delimiter.
inline constexpr std::size_t kMaxFrameBytes = 4 * 1024 * 1024;

/// v2: max buffered per connection (a legit frame can be up to 4MB and
/// arrives in 8KB TCP chunks — the v1 cap would kill it mid-stream).
inline constexpr std::size_t kMaxAccumulatedBytesV2 = kMaxFrameBytes + 64 * 1024;

/// v2 framing version offered/selected in joinGameClient `body.framing`.
inline constexpr int kFramingV2 = 2;

/// Error-shape convention (additive, old clients ignore unknown body keys):
/// every `status:error` response carries `body.errorCode` mirroring
/// `header.message` (injected centrally in `generateResponseMessage`;
/// framing paths set it directly).

/// Protocol version echoed in `header.version`.
inline constexpr const char *kProtocolVersion = "1.0";

} // namespace protocol
