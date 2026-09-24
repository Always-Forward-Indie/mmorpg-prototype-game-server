#include "events/Outbox.hpp"

#include "utils/ResponseBuilder.hpp"

std::string outboxFactKeyOf(const nlohmann::json &body)
{
    // Only object bodies carry keys (array bodies = batched snapshots that
    // self-heal on the next period; they stay on the legacy path).
    if (!body.is_object())
        return "";
    const auto it = body.find("factKey");
    if (it == body.end() || !it->is_string() || it->get<std::string>().empty())
        return "";
    return it->get<std::string>();
}

int outboxClaimFactKey(Database &db, pqxx::work &txn, const std::string &key)
{
    pqxx::result rows;
    try
    {
        rows = db.execChecked(txn, "claim_fact_key", {key});
    }
    catch (const DatabaseError &)
    {
        return -1;
    }
    if (rows.empty())
        return -1;
    if (rows[0][0].as<int>(0) != 1)
        return 0;
    // Fresh claim: record the high-water mark in the same txn (atomic with
    // the business writes the caller performs next). A throw below aborts
    // the claim with it, so the watermark can never run ahead of effects.
    // execChecked throws DatabaseError on transport failure -> -1 path.
    if (const auto parts = outboxParseFactKey(key))
    {
        static_cast<void>(db.execChecked(txn, "upsert_outbox_watermark",
            {1, parts->characterId, parts->seq}));
    }
    return 1;
}

void outboxSendFactAck(NetworkManager &net,
    const std::shared_ptr<boost::asio::ip::tcp::socket> &socket,
    const std::string &key,
    const std::string &status)
{
    if (!socket || key.empty())
        return;
    nlohmann::json response = ResponseBuilder()
                                  .setHeader("message", "Fact ack")
                                  .setHeader("hash", "")
                                  .setHeader("clientId", 0)
                                  .setHeader("eventType", "factAck")
                                  .setBody("key", key)
                                  .setBody("status", status)
                                  .build();
    net.sendResponse(socket, net.generateResponseMessage("success", response));
}
