#include "events/handlers/SocialEventHandler.hpp"

#include "data/DataStructs.hpp"
#include "events/Outbox.hpp"
#include "utils/ResponseBuilder.hpp"

SocialEventHandler::SocialEventHandler(
    NetworkManager &networkManager,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServices)
{
}

void
SocialEventHandler::handleGetTitleDefinitionsEvent(const Event &event)
{
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();
    int clientID = event.getClientID();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_title_definitions", {});

        nlohmann::json titlesJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json t;
            t["id"] = row["id"].as<int>();
            t["slug"] = row["slug"].as<std::string>();
            t["displayName"] = row["display_name"].as<std::string>();
            t["description"] = row["description"].as<std::string>();
            t["earnCondition"] = row["earn_condition"].as<std::string>();
            // bonuses column is JSONB — parse it back to a JSON array.
            // Broken row content must not fail the whole static push (chunk
            // would boot without ANY titles); warn with the row id instead.
            try
            {
                t["bonuses"] = nlohmann::json::parse(row["bonuses"].as<std::string>());
            }
            catch (const std::exception &e)
            {
                log_->warn("[TITLE] title id={} has broken bonuses JSONB ({}), sending []",
                    row["id"].as<int>(), e.what());
                t["bonuses"] = nlohmann::json::array();
            }
            catch (...)
            {
                t["bonuses"] = nlohmann::json::array();
            }
            // conditionParams is JSONB — parse it back to a JSON object
            // (same broken-row policy as bonuses above).
            try
            {
                t["conditionParams"] = nlohmann::json::parse(row["condition_params"].as<std::string>());
            }
            catch (const std::exception &e)
            {
                log_->warn("[TITLE] title id={} has broken condition_params JSONB ({}), sending {{}}",
                    row["id"].as<int>(), e.what());
                t["conditionParams"] = nlohmann::json::object();
            }
            catch (...)
            {
                t["conditionParams"] = nlohmann::json::object();
            }
            titlesJson.push_back(std::move(t));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Title definitions")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setTitleDefinitionsData")
                                      .setBody("titles", titlesJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[TITLE] Sent {} title definitions to chunk server", titlesJson.size());
    }
    catch (const DatabaseError &)
    {
        // Catalog entry: DB failure must reach dispatchCatalog
        // (catalogFailed -> incomplete JOIN), never a partial success.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetTitleDefinitionsEvent error: " + std::string(ex.what()));
    }
}

void
SocialEventHandler::handleGetPlayerTitlesEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerTitlesEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_player_titles", {characterId});

        std::string equippedSlug;
        nlohmann::json earnedSlugs = nlohmann::json::array();
        for (const auto &row : result)
        {
            std::string slug = row["title_slug"].as<std::string>();
            earnedSlugs.push_back(slug);
            if (row["equipped"].as<bool>())
                equippedSlug = slug;
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player titles")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerTitlesData")
                                      .setBody("characterId", characterId)
                                      .setBody("earnedSlugs", earnedSlugs)
                                      .setBody("equippedSlug", equippedSlug)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[TITLE] Sent {} earned titles for characterId={}", earnedSlugs.size(), characterId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetPlayerTitlesEvent error: " + std::string(ex.what()));
    }
}

void
SocialEventHandler::handleSavePlayerTitleEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSavePlayerTitleEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        std::string equippedSlug = j.value("equippedSlug", "");
        nlohmann::json earnedSlugsArr = j.value("earnedSlugs", nlohmann::json::array());

        if (characterId <= 0)
        {
            log_->error("[TITLE] handleSavePlayerTitleEvent: invalid payload char={}", characterId);
            return;
        }

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string titleFactKey = outboxFactKeyOf(j);
        if (!titleFactKey.empty())
        {
            const int titleClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, titleFactKey);
            if (titleClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), titleFactKey, "duplicate");
                return;
            }
            if (titleClaimed < 0)
                return;
        }

        // Upsert all earned titles; equipped flag is true only for the currently selected one
        for (const auto &slugVal : earnedSlugsArr)
        {
            if (!slugVal.is_string())
                continue;
            std::string slug = slugVal.get<std::string>();
            bool equipped = (!equippedSlug.empty() && slug == equippedSlug);
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "upsert_player_title", {characterId, slug, equipped}));
        }

        // Ensure at most one title has equipped=true (clear all, then re-flag the chosen one)
        if (!equippedSlug.empty())
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "set_character_equipped_title", {characterId, equippedSlug}));

        txn.commit();
        if (!titleFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), titleFactKey, "applied");
        log_->info("[TITLE] Saved char={} titles={} equipped='{}'",
            characterId,
            earnedSlugsArr.size(),
            equippedSlug);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSavePlayerTitleEvent error: " + std::string(ex.what()));
    }
}

void
SocialEventHandler::handleGetEmoteDefinitionsEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_emote_definitions", {});

        nlohmann::json emotesJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json e;
            e["id"] = row["id"].as<int>();
            e["slug"] = row["slug"].as<std::string>();
            e["displayName"] = row["display_name"].as<std::string>();
            e["animationName"] = row["animation_name"].as<std::string>();
            e["category"] = row["category"].as<std::string>();
            e["isDefault"] = row["is_default"].as<bool>();
            e["sortOrder"] = row["sort_order"].as<int>();
            emotesJson.push_back(std::move(e));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Emote definitions")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setEmoteDefinitionsData")
                                      .setBody("emotes", emotesJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[EMOTE] Sent {} emote definitions to chunk server", emotesJson.size());
    }
    catch (const DatabaseError &)
    {
        // Catalog entry: DB failure must reach dispatchCatalog
        // (catalogFailed -> incomplete JOIN), never a partial success.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetEmoteDefinitionsEvent error: " + std::string(ex.what()));
    }
}

void
SocialEventHandler::handleGetPlayerEmotesEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerEmotesEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());

        // Auto-grant default emotes for new characters (idempotent)
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "grant_default_emotes", {characterId}));

        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_player_emotes", {characterId});

        nlohmann::json slugsJson = nlohmann::json::array();
        for (const auto &row : result)
            slugsJson.push_back(row["emote_slug"].as<std::string>());

        txn.commit();

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player emotes")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerEmotesData")
                                      .setBody("characterId", characterId)
                                      .setBody("emotes", slugsJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[EMOTE] Sent {} emotes for charId={}", slugsJson.size(), characterId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetPlayerEmotesEvent error: " + std::string(ex.what()));
    }
}

void
SocialEventHandler::handleSaveGmActionEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveGmActionEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        const int gmUserId = j.value("gmUserId", 0);
        const std::string action = j.value("actionType", "");
        const std::string targetType = j.value("targetType", "");
        const int64_t targetId = j.value("targetId", 0);
        const std::string oldVal = j.value("oldValue", "");
        const std::string newVal = j.value("newValue", "");

        if (gmUserId <= 0 || action.empty())
        {
            log_->error("[GMLOG] handleSaveGmActionEvent: invalid payload");
            return;
        }

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the write below).
        const std::string factKey = outboxFactKeyOf(j);
        if (!factKey.empty())
        {
            const int claimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, factKey);
            if (claimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), factKey, "duplicate");
                return;
            }
            if (claimed < 0)
                return;
        }

        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "save_gm_action", {gmUserId, action, targetType, targetId, oldVal, newVal}));
        txn.commit();

        if (!factKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), factKey, "applied");
        log_->info("[GMLOG] gm={} action={} target={}:{}", gmUserId, action, targetType, targetId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveGmActionEvent error: " + std::string(ex.what()));
    }
}

void
SocialEventHandler::handleSaveUserBanEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveUserBanEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        const int userId = j.value("userId", 0);
        const int bannedBy = j.value("bannedByUserId", 0);
        const std::string reason = j.value("reason", "");
        const std::string expiresAt = j.value("expiresAt", "");
        const bool lift = j.value("lift", false);

        if (userId <= 0 || (!lift && reason.empty()))
        {
            log_->error("[BAN] handleSaveUserBanEvent: invalid payload user={}", userId);
            return;
        }

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the write below).
        const std::string factKey = outboxFactKeyOf(j);
        if (!factKey.empty())
        {
            const int claimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, factKey);
            if (claimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), factKey, "duplicate");
                return;
            }
            if (claimed < 0)
                return;
        }

        if (lift)
        {
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "lift_user_ban", {userId}));
        }
        else
        {
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "save_user_ban", {userId, bannedBy, reason, expiresAt}));
        }
        txn.commit();

        if (!factKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), factKey, "applied");
        log_->info("[BAN] user={} lift={} reason='{}'", userId, lift, reason);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveUserBanEvent error: " + std::string(ex.what()));
    }
}
