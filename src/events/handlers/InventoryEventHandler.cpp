#include "events/handlers/InventoryEventHandler.hpp"

#include "data/DataStructs.hpp"
#include "events/Outbox.hpp"
#include "utils/ResponseBuilder.hpp"
#include "utils/TerminalColors.hpp"

InventoryEventHandler::InventoryEventHandler(
    NetworkManager &networkManager,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServices)
{
}

void
InventoryEventHandler::handleSaveInventoryChangeEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveInventoryChangeEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int itemId = j.value("itemId", 0);
        int quantity = j.value("quantity", 0);
        int inventoryItemId = static_cast<int>(j.value("inventoryItemId", 0));
        // Chunk-local pending-sync token (0 = none/legacy). Echoed back in
        // inventoryItemIdSync so the chunk can match the exact instance.
        uint64_t syncToken = j.value("syncToken", (uint64_t)0);

        if (characterId <= 0 || itemId <= 0)
            return;

        std::shared_ptr<boost::asio::ip::tcp::socket> chunkSocket = event.getClientSocket();

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string invFactKey = outboxFactKeyOf(j);
        if (!invFactKey.empty())
        {
            const int claimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, invFactKey);
            if (claimed == 0)
            {
                outboxSendFactAck(networkManager_, chunkSocket, invFactKey, "duplicate");
                return;
            }
            if (claimed < 0)
                return;
        }
        if (quantity > 0)
        {
            if (inventoryItemId > 0)
            {
                // Row already exists in DB — UPDATE quantity in-place, no new row.
                static_cast<void>(gameServices_.getDatabase().execChecked(
                    txn, "update_player_inventory_quantity", {quantity, inventoryItemId, characterId}));
                txn.commit();
                // No id sync needed — the id didn't change.
            }
            else
            {
                // New item — INSERT and send assigned id back to chunk server.
                auto result = gameServices_.getDatabase().execChecked(
                    txn, "upsert_player_inventory_item", {characterId, itemId, quantity});
                txn.commit();

                // Send back the assigned player_inventory.id so chunk server can fix in-memory id=0.
                // Without the sync every later save goes down the additive
                // upsert path (double count), so answer on the live socket.
                if (!result.empty())
                {
                    int64_t assignedId = result[0]["id"].as<int64_t>();
                    nlohmann::json syncPkt;
                    syncPkt["header"]["eventType"] = "inventoryItemIdSync";
                    syncPkt["header"]["clientId"] = 0;
                    syncPkt["body"]["characterId"] = characterId;
                    syncPkt["body"]["itemId"] = itemId;
                    syncPkt["body"]["inventoryItemId"] = assignedId;
                    syncPkt["body"]["syncToken"] = syncToken;
                    auto syncSocket = gameServices_.getChunkManager().resolveLiveSocket(chunkSocket);
                    if (!syncSocket)
                    {
                        log_->error("[GAME] inventoryItemIdSync: no live chunk socket for char={}",
                            characterId);
                    }
                    else
                    {
                        networkManager_.sendResponse(syncSocket,
                            networkManager_.generateResponseMessage("success", syncPkt));
                    }
                }
            }
        }
        else
        {
            // quantity == 0: delete the row.
            if (inventoryItemId > 0)
            {
                // Prefer deleting by exact id to avoid affecting other stacks.
                static_cast<void>(gameServices_.getDatabase().execChecked(
                    txn, "delete_player_inventory_item_by_char_id", {inventoryItemId, characterId}));
            }
            else
            {
                static_cast<void>(gameServices_.getDatabase().execChecked(
                    txn, "delete_player_inventory_item", {characterId, itemId}));
            }
            txn.commit();
        }
        // Outbox ack: the fact is durable now.
        if (!invFactKey.empty())
            outboxSendFactAck(networkManager_, chunkSocket, invFactKey, "applied");
        log_->info("[SAVE_INVENTORY] character=" + std::to_string(characterId) +
                   " item=" + std::to_string(itemId) + " qty=" + std::to_string(quantity));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleSaveInventoryChangeEvent: " + std::string(ex.what()));
    }
}

void
InventoryEventHandler::handleGetPlayerInventoryEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerInventoryEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_player_inventory", {characterId});

        nlohmann::json itemsJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json item;
            item["id"] = row["id"].as<int64_t>();
            item["itemId"] = row["item_id"].as<int>();
            item["quantity"] = row["quantity"].as<int>();
            item["slotIndex"] = row["slot_index"].as<int>();
            item["durabilityCurrent"] = row["durability_current"].as<int>();
            item["isEquipped"] = row["is_equipped"].as<bool>(false);
            item["killCount"] = row["kill_count"].as<int>(0);
            itemsJson.push_back(std::move(item));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player inventory")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerInventoryData")
                                      .setBody("characterId", characterId)
                                      .setBody("items", itemsJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        gameServices_.getLogger().log("[EH] Sent " + std::to_string(itemsJson.size()) +
                                          " inventory items for characterId=" + std::to_string(characterId),
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleGetPlayerInventoryEvent: " + std::string(ex.what()));
    }
}
