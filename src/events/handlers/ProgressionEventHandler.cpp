#include "events/handlers/ProgressionEventHandler.hpp"

#include "data/DataStructs.hpp"
#include "events/Outbox.hpp"
#include "utils/ResponseBuilder.hpp"

ProgressionEventHandler::ProgressionEventHandler(
    NetworkManager &networkManager,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServices)
{
}

void
ProgressionEventHandler::handleGetPlayerPityEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerPityEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_player_pity", {characterId});

        nlohmann::json entriesJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json entry;
            entry["itemId"] = row["item_id"].as<int>();
            entry["killCount"] = row["kill_count"].as<int>();
            entriesJson.push_back(std::move(entry));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player pity data")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerPityData")
                                      .setBody("characterId", characterId)
                                      .setBody("entries", entriesJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[EH] Sent " + std::to_string(entriesJson.size()) +
                   " pity counters for characterId=" + std::to_string(characterId));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetPlayerPityEvent error: " + std::string(ex.what()));
    }
}

void
ProgressionEventHandler::handleGetPlayerBestiaryEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerBestiaryEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_player_bestiary", {characterId});

        nlohmann::json entriesJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json entry;
            entry["mobTemplateId"] = row["mob_template_id"].as<int>();
            entry["killCount"] = row["kill_count"].as<int>();
            entriesJson.push_back(std::move(entry));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player bestiary data")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerBestiaryData")
                                      .setBody("characterId", characterId)
                                      .setBody("entries", entriesJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[EH] Sent " + std::to_string(entriesJson.size()) +
                   " bestiary entries for characterId=" + std::to_string(characterId));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetPlayerBestiaryEvent error: " + std::string(ex.what()));
    }
}

void
ProgressionEventHandler::handleSavePityCounterEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSavePityCounterEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int itemId = j.value("itemId", 0);
        int killCount = j.value("killCount", 0);

        if (characterId <= 0 || itemId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string pityFactKey = outboxFactKeyOf(j);
        if (!pityFactKey.empty())
        {
            const int pityClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, pityFactKey);
            if (pityClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), pityFactKey, "duplicate");
                return;
            }
            if (pityClaimed < 0)
                return;
        }
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "upsert_pity_counter", {characterId, itemId, killCount}));
        txn.commit();
        if (!pityFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), pityFactKey, "applied");

        log_->info("[SAVE_PITY] char=" + std::to_string(characterId) +
                   " item=" + std::to_string(itemId) +
                   " kills=" + std::to_string(killCount));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSavePityCounterEvent error: " + std::string(ex.what()));
    }
}

void
ProgressionEventHandler::handleSaveBestiaryKillEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveBestiaryKillEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int mobTemplateId = j.value("mobTemplateId", 0);
        int killCount = j.value("killCount", 0);

        if (characterId <= 0 || mobTemplateId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string bestFactKey = outboxFactKeyOf(j);
        if (!bestFactKey.empty())
        {
            const int bestClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, bestFactKey);
            if (bestClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), bestFactKey, "duplicate");
                return;
            }
            if (bestClaimed < 0)
                return;
        }
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "upsert_bestiary_kill", {characterId, mobTemplateId, killCount}));
        txn.commit();
        if (!bestFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), bestFactKey, "applied");

        log_->info("[SAVE_BESTIARY] char=" + std::to_string(characterId) +
                   " mob=" + std::to_string(mobTemplateId) +
                   " kills=" + std::to_string(killCount));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveBestiaryKillEvent error: " + std::string(ex.what()));
    }
}

void
ProgressionEventHandler::handleGetTimedChampionTemplatesEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto rows = gameServices_.getDatabase().execChecked(txn, "get_timed_champion_templates", {});
        txn.commit();

        nlohmann::json arr = nlohmann::json::array();
        for (const auto &row : rows)
        {
            nlohmann::json t;
            t["id"] = row["id"].as<int>();
            t["slug"] = row["slug"].as<std::string>();
            t["gameZoneId"] = row["game_zone_id"].as<int>();
            t["mobTemplateId"] = row["mob_template_id"].as<int>();
            t["intervalHours"] = row["interval_hours"].as<int>();
            t["windowMinutes"] = row["window_minutes"].as<int>();
            t["nextSpawnAt"] = row["next_spawn_at"].as<int64_t>();
            t["announceKey"] = row["announce_key"].as<std::string>();
            arr.push_back(std::move(t));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Timed champion templates loaded")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setTimedChampionTemplatesList")
                                      .setBody("timedChampionTemplates", arr)
                                      .build();

        std::string responseData = networkManager_.generateResponseMessage("success", response);
        networkManager_.sendResponse(clientSocket, responseData);

        log_->info("[TIMED_CHAMP] Sent {} timed champion templates to chunk server", arr.size());
    }
    catch (const DatabaseError &)
    {
        // Catalog entry: DB failure must reach dispatchCatalog
        // (catalogFailed -> incomplete JOIN), never a partial success.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetTimedChampionTemplatesEvent error: " + std::string(ex.what()));
    }
}

void
ProgressionEventHandler::handleTimedChampionKilledEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleTimedChampionKilledEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        const std::string slug = j.value("slug", "");
        const int64_t killedAt = j.value("killedAt", int64_t{0});

        if (slug.empty())
            return;

        // Compute next_spawn_at = killedAt + interval_hours * 3600
        // We need the interval_hours for this slug. Query and then update.
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string champFactKey = outboxFactKeyOf(j);
        if (!champFactKey.empty())
        {
            const int champClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, champFactKey);
            if (champClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), champFactKey, "duplicate");
                return;
            }
            if (champClaimed < 0)
                return;
        }

        // Get interval_hours for this slug
        auto rows = txn.exec_params(
            "SELECT interval_hours FROM timed_champion_templates WHERE slug = $1", slug);
        if (!rows.empty())
        {
            int intervalHours = rows[0]["interval_hours"].as<int>();
            int64_t nextSpawnAt = killedAt + static_cast<int64_t>(intervalHours) * 3600;

            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "update_timed_champion_next_spawn", {slug, nextSpawnAt}));
        }
        txn.commit();
        if (!champFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), champFactKey, "applied");

        log_->info("[TIMED_CHAMP] Updated next_spawn for slug='{}' after kill", slug);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleTimedChampionKilledEvent error: " + std::string(ex.what()));
    }
}
