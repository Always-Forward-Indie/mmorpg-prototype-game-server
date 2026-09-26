#include "events/handlers/ItemEventHandler.hpp"

#include "data/DataStructs.hpp"
#include "events/Outbox.hpp"
#include "utils/ResponseBuilder.hpp"

ItemEventHandler::ItemEventHandler(
    NetworkManager &networkManager,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServices)
{
}

void
ItemEventHandler::handleSaveDurabilityChangeEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveDurabilityChangeEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int inventoryItemId = j.value("inventoryItemId", 0);
        int durabilityCurrent = j.value("durabilityCurrent", 0);

        if (characterId <= 0 || inventoryItemId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string durFactKey = outboxFactKeyOf(j);
        if (!durFactKey.empty())
        {
            const int durClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, durFactKey);
            if (durClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), durFactKey, "duplicate");
                return;
            }
            if (durClaimed < 0)
                return;
        }
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "update_durability_current", {durabilityCurrent, inventoryItemId, characterId}));
        txn.commit();
        if (!durFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), durFactKey, "applied");

        log_->info("[SAVE_DUR] char=" + std::to_string(characterId) +
                   " item=" + std::to_string(inventoryItemId) +
                   " dur=" + std::to_string(durabilityCurrent));

        // Read-your-write (same race as equip): recompute attributes here,
        // right after the commit — the async chunk-side refresh travels on
        // another queue and could overtake the save, caching stale
        // durability tiers until the next incidental refresh.
        try
        {
            auto _dbConn2 = gameServices_.getDatabase().getConnectionLocked();
            pqxx::work txn2(_dbConn2.get());
            auto attrs = gameServices_.getDatabase().execChecked(
                txn2, "get_character_attributes", {characterId});
            nlohmann::json attrsJson = nlohmann::json::array();
            for (const auto &row : attrs)
            {
                nlohmann::json attr;
                attr["id"] = row["id"].as<int>();
                attr["name"] = row["name"].as<std::string>();
                attr["slug"] = row["slug"].as<std::string>();
                attr["value"] = row["value"].as<int>();
                attrsJson.push_back(std::move(attr));
            }
            ResponseBuilder builder;
            nlohmann::json response = builder
                                          .setHeader("message", "Character attributes refresh")
                                          .setHeader("hash", "")
                                          .setHeader("clientId", characterId)
                                          .setHeader("eventType", "setCharacterAttributesRefresh")
                                          .setBody("characterId", characterId)
                                          .setBody("attributesData", attrsJson)
                                          .build();
            auto sendSocket =
                gameServices_.getChunkManager().resolveLiveSocket(event.getClientSocket());
            if (sendSocket)
                networkManager_.sendResponse(sendSocket,
                    networkManager_.generateResponseMessage("success", response));
        }
        catch (const std::exception &refreshEx)
        {
            log_->warn(std::string("[SAVE_DUR] post-commit refresh failed for char=") +
                       std::to_string(characterId) + ": " + refreshEx.what());
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleSaveDurabilityChangeEvent: " + std::string(ex.what()));
    }
}

void
ItemEventHandler::handleSaveCurrencyTransactionEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveCurrencyTransactionEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int npcId = j.value("npcId", 0);
        int itemId = j.value("itemId", 0);
        int quantity = j.value("quantity", 0);
        int totalPrice = j.value("totalPrice", 0);
        std::string txType = j.value("transactionType", "");

        if (characterId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string curFactKey = outboxFactKeyOf(j);
        if (!curFactKey.empty())
        {
            const int curClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, curFactKey);
            if (curClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), curFactKey, "duplicate");
                return;
            }
            if (curClaimed < 0)
                return;
        }
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "insert_currency_transaction", {characterId, npcId, totalPrice, txType}));
        txn.commit();
        if (!curFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), curFactKey, "applied");

        log_->info("[CURRENCY_TX] char=" + std::to_string(characterId) +
                   " type=" + txType + " item=" + std::to_string(itemId) +
                   " qty=" + std::to_string(quantity) + " price=" + std::to_string(totalPrice));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleSaveCurrencyTransactionEvent: " + std::string(ex.what()));
    }
}

void
ItemEventHandler::handleSaveEquipmentChangeEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveEquipmentChangeEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int inventoryItemId = j.value("inventoryItemId", 0);
        std::string action = j.value("action", "");
        std::string equipSlotSlug = j.value("equipSlotSlug", "");
        // A1 fallback identity: the chunk defers id==0 saves, but a save
        // can still arrive with a token and no id (flush race). Resolve by
        // (character, token) instead of dropping silently.
        uint64_t syncToken = j.value("syncToken", (uint64_t)0);

        if (characterId <= 0 || action.empty())
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string equipFactKey = outboxFactKeyOf(j);
        if (!equipFactKey.empty())
        {
            const int equipClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, equipFactKey);
            if (equipClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), equipFactKey, "duplicate");
                return;
            }
            if (equipClaimed < 0)
                return;
        }

        if (inventoryItemId <= 0)
        {
            if (syncToken == 0)
            {
                log_->warn("[SAVE_EQUIP] no id and no token: char={} action={}",
                    characterId, action);
                return;
            }
            auto byToken = gameServices_.getDatabase().execChecked(
                txn, "get_inventory_id_by_token",
                {characterId, static_cast<int64_t>(syncToken)});
            if (byToken.empty())
            {
                log_->warn("[SAVE_EQUIP] token resolves to no row (sync lost?): char={}",
                    characterId);
                return;
            }
            inventoryItemId = static_cast<int>(byToken[0]["id"].as<int64_t>());
        }

        if (action == "equip")
        {
            if (equipSlotSlug.empty())
            {
                log_->error("[SAVE_EQUIP] equipSlotSlug is empty for char=" + std::to_string(characterId));
                return;
            }
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "insert_character_equipment", {characterId, equipSlotSlug, inventoryItemId}));
        }
        else if (action == "unequip")
        {
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "delete_character_equipment", {characterId, inventoryItemId}));
        }
        else
        {
            log_->error("[SAVE_EQUIP] Unknown action: " + action);
            return;
        }
        txn.commit();
        if (!equipFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), equipFactKey, "applied");

        log_->info("[SAVE_EQUIP] char=" + std::to_string(characterId) +
                   " action=" + action +
                   " slot=" + equipSlotSlug +
                   " invItemId=" + std::to_string(inventoryItemId));

        // Read-your-write: recompute attributes in THIS handler, right after
        // the equip commit. The chunk also asks for a refresh separately
        // (triggerAttributesRefresh), but that message travels on another
        // queue and can overtake the save -> stale recompute cached
        // chunk-side (set bonus / durability tiers missing until the next
        // refresh). Pushing here is ordered by construction.
        try
        {
            auto _dbConn2 = gameServices_.getDatabase().getConnectionLocked();
            pqxx::work txn2(_dbConn2.get());
            auto attrs = gameServices_.getDatabase().execChecked(
                txn2, "get_character_attributes", {characterId});
            nlohmann::json attrsJson = nlohmann::json::array();
            for (const auto &row : attrs)
            {
                nlohmann::json attr;
                attr["id"] = row["id"].as<int>();
                attr["name"] = row["name"].as<std::string>();
                attr["slug"] = row["slug"].as<std::string>();
                attr["value"] = row["value"].as<int>();
                attrsJson.push_back(std::move(attr));
            }
            ResponseBuilder builder;
            nlohmann::json response = builder
                                          .setHeader("message", "Character attributes refresh")
                                          .setHeader("hash", "")
                                          .setHeader("clientId", characterId)
                                          .setHeader("eventType", "setCharacterAttributesRefresh")
                                          .setBody("characterId", characterId)
                                          .setBody("attributesData", attrsJson)
                                          .build();
            auto sendSocket =
                gameServices_.getChunkManager().resolveLiveSocket(event.getClientSocket());
            if (sendSocket)
                networkManager_.sendResponse(sendSocket,
                    networkManager_.generateResponseMessage("success", response));
        }
        catch (const std::exception &refreshEx)
        {
            // Best effort: the periodic/asymmetric refresh still converges.
            log_->warn(std::string("[SAVE_EQUIP] post-commit refresh failed for char=") +
                       std::to_string(characterId) + ": " + refreshEx.what());
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleSaveEquipmentChangeEvent: " + std::string(ex.what()));
    }
}

void
ItemEventHandler::handleSaveItemKillCountEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveItemKillCountEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int inventoryItemId = j.value("inventoryItemId", 0);
        int killCount = j.value("killCount", 0);

        if (characterId <= 0 || inventoryItemId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string killFactKey = outboxFactKeyOf(j);
        if (!killFactKey.empty())
        {
            const int killClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, killFactKey);
            if (killClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), killFactKey, "duplicate");
                return;
            }
            if (killClaimed < 0)
                return;
        }
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "update_item_kill_count", {killCount, inventoryItemId, characterId}));
        txn.commit();
        if (!killFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), killFactKey, "applied");

        log_->info("[SAVE_KILL_COUNT] char=" + std::to_string(characterId) +
                   " item=" + std::to_string(inventoryItemId) +
                   " kills=" + std::to_string(killCount));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleSaveItemKillCountEvent: " + std::string(ex.what()));
    }
}

void
ItemEventHandler::handleTransferInventoryItemEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleTransferInventoryItemEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int toCharId = j.value("toCharId", 0);
        int inventoryItemId = j.value("inventoryItemId", 0);
        int fromCharId = j.value("fromCharId", 0);

        if (toCharId <= 0 || inventoryItemId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        if (fromCharId > 0)
        {
            // P2P trade: item still owned by the sender in DB
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "transfer_item_between_chars", {toCharId, inventoryItemId, fromCharId}));
        }
        else
        {
            // Ground pickup: character_id IS NULL in DB
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "transfer_item_ownership", {toCharId, inventoryItemId}));
        }
        txn.commit();

        log_->info("[TRANSFER_ITEM] to=" + std::to_string(toCharId) +
                   " invItem=" + std::to_string(inventoryItemId) +
                   (fromCharId > 0 ? " from=" + std::to_string(fromCharId) : " (ground)"));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleTransferInventoryItemEvent: " + std::string(ex.what()));
    }
}

void
ItemEventHandler::handleNullifyItemOwnerEvent(const Event &event)
{
    const auto &data = event.getData();
    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
            return;
        const auto &j = std::get<nlohmann::json>(data);
        int inventoryItemId = j.value("inventoryItemId", 0);
        int fromCharId = j.value("fromCharId", 0);
        if (inventoryItemId <= 0 || fromCharId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "nullify_item_owner", {inventoryItemId, fromCharId}));
        txn.commit();

        log_->info("[NULLIFY_OWNER] char=" + std::to_string(fromCharId) +
                   " invItem=" + std::to_string(inventoryItemId));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleNullifyItemOwnerEvent: " + std::string(ex.what()));
    }
}

void
ItemEventHandler::handleDeleteInventoryItemEvent(const Event &event)
{
    const auto &data = event.getData();
    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
            return;
        const auto &j = std::get<nlohmann::json>(data);
        int inventoryItemId = j.value("inventoryItemId", 0);
        if (inventoryItemId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "delete_inventory_item_by_id", {inventoryItemId}));
        txn.commit();

        log_->info("[DELETE_ITEM] invItem=" + std::to_string(inventoryItemId));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleDeleteInventoryItemEvent: " + std::string(ex.what()));
    }
}
