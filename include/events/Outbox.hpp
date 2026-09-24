#pragma once

#include <boost/asio.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "events/Event.hpp"
#include "network/NetworkManager.hpp"
#include "services/GameServices.hpp"

// ── Outbox fact helpers (see Tools/Tests/OUTBOX_PLAN.md) ─────────────────
// Shared by EventHandler and the extracted domain handlers (previously an
// anonymous-namespace copy inside EventHandler.cpp).
// factKeyOf: "" when the sender carries no key (legacy path: proceed,
// no ack — backward compatible).
std::string outboxFactKeyOf(const nlohmann::json &body);

// Claim a fact key atomically inside the caller's txn. Returns 1 = fresh
// (proceed), 0 = duplicate (skip business writes, caller sends
// ack-duplicate), -1 = transport error (caller must abort WITHOUT ack so
// the chunk retries). On fresh claims the high-water mark
// (outbox_seq_watermark) is upserted in the same txn — never ahead of
// effects, since a throw aborts everything together.
int outboxClaimFactKey(Database &db, pqxx::work &txn, const std::string &key);

// Parsed fact key ("{characterId}:{factType}:{bootId}:{seq}").
// nullopt for 3-part pre-bootId legacy keys and "test-*" keys (both skip
// the watermark but still claim normally); analytics characterId 0 accepted.
// Inline + dependency-free so unit tests link without NetworkManager/Database.
struct OutboxFactKeyParts
{
    int characterId;
    int64_t seq;
};

inline std::optional<OutboxFactKeyParts> outboxParseFactKey(const std::string &key)
{
    // Expected: "{characterId}:{factType}:{bootId}:{seq}". factType may not
    // contain ':' (chunk allowlist), bootId is opaque (digits today).
    if (key.rfind("test-", 0) == 0)
        return std::nullopt;
    size_t p1 = key.find(':');
    size_t p2 = (p1 == std::string::npos) ? std::string::npos : key.find(':', p1 + 1);
    size_t p3 = (p2 == std::string::npos) ? std::string::npos : key.find(':', p2 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos ||
        key.find(':', p3 + 1) != std::string::npos)
        return std::nullopt;
    try
    {
        int characterId = std::stoi(key.substr(0, p1));
        if (characterId < 0)
            return std::nullopt;
        int64_t seq = std::stoll(key.substr(p3 + 1));
        if (seq < 0)
            return std::nullopt;
        return OutboxFactKeyParts{characterId, seq};
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

void outboxSendFactAck(NetworkManager &net,
    const std::shared_ptr<boost::asio::ip::tcp::socket> &socket,
    const std::string &key,
    const std::string &status);

// Reconcile answer: split chunk-reported unacked keys into applied (drop,
// game is durable) vs unknown (resend now). Sent on the live chunk socket.
void outboxSendDiff(NetworkManager &net,
    const std::shared_ptr<boost::asio::ip::tcp::socket> &socket,
    const std::vector<std::string> &applied,
    const std::vector<std::string> &unknown);
