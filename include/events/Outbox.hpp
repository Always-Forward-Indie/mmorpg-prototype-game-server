#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <string>

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
// the chunk retries).
int outboxClaimFactKey(Database &db, pqxx::work &txn, const std::string &key);

void outboxSendFactAck(NetworkManager &net,
    const std::shared_ptr<boost::asio::ip::tcp::socket> &socket,
    const std::string &key,
    const std::string &status);
