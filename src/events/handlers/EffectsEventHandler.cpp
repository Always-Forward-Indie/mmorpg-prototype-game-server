#include "events/handlers/EffectsEventHandler.hpp"

#include "data/DataStructs.hpp"
#include "events/Outbox.hpp"
#include "utils/ResponseBuilder.hpp"

EffectsEventHandler::EffectsEventHandler(
    NetworkManager &networkManager,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServices)
{
}

void
EffectsEventHandler::handleSaveActiveEffectEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
            return;
        const auto &j = std::get<nlohmann::json>(data);

        int characterId = j.value("characterId", 0);
        std::string effectSlug = j.value("effectSlug", std::string(""));
        std::string attributeSlug = j.value("attributeSlug", std::string(""));
        std::string sourceType = j.value("sourceType", std::string("death"));
        double value = j.value("value", 0.0);
        int64_t expiresAt = j.value("expiresAt", int64_t(0));
        int tickMs = j.value("tickMs", 0);

        if (characterId <= 0 || effectSlug.empty())
            return;

        // $1=player_id $2=effect_slug $3=attribute_slug $4=source_type $5=value $6=expires_at $7=tick_ms
        // An empty attributeSlug results in NULL attribute_id (no match in entity_attributes).
        using DbParam = std::variant<int, int64_t, float, double, std::string>;
        std::vector<DbParam> params{
            characterId,
            effectSlug,
            attributeSlug, // '' → NULL attribute_id via SQL subquery
            sourceType,
            value,                          // double
            static_cast<double>(expiresAt), // int64 → double (unix sec, safe precision)
            tickMs};

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string effFactKey = outboxFactKeyOf(j);
        if (!effFactKey.empty())
        {
            const int effClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, effFactKey);
            if (effClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), effFactKey, "duplicate");
                return;
            }
            if (effClaimed < 0)
                return;
        }
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "insert_player_active_effect", params));
        txn.commit();
        if (!effFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), effFactKey, "applied");

        log_->info("[SAVE_ACTIVE_EFFECT] char=" + std::to_string(characterId) + " effect=" + effectSlug);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveActiveEffectEvent error: " + std::string(ex.what()));
    }
}

void
EffectsEventHandler::handleSaveSkillCooldownEvent(const Event &event)
{
    const auto &data = event.getData();
    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveSkillCooldownEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        std::string skillSlug = j.value("skillSlug", std::string(""));
        int64_t cooldownEndsAtMs = j.value("cooldownEndsAtMs", int64_t(0));

        if (characterId <= 0 || skillSlug.empty() || cooldownEndsAtMs <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string cdFactKey = outboxFactKeyOf(j);
        if (!cdFactKey.empty())
        {
            const int cdClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, cdFactKey);
            if (cdClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), cdFactKey, "duplicate");
                return;
            }
            if (cdClaimed < 0)
                return;
        }
        using DbParam = std::variant<int, int64_t, float, double, std::string>;
        // Pass cooldownEndsAtMs as string to avoid std::to_string(double) producing
        // "1776627283138.000000" which PostgreSQL cannot cast with ::bigint.
        std::vector<DbParam> params{characterId, skillSlug, std::to_string(cooldownEndsAtMs)};
        static_cast<void>(gameServices_.getDatabase().execChecked(txn, "upsert_skill_cooldown", params));
        txn.commit();
        if (!cdFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), cdFactKey, "applied");

        log_->debug("[SAVE_SKILL_COOLDOWN] char={} skill={} endsAt={}", characterId, skillSlug, cooldownEndsAtMs);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveSkillCooldownEvent error: " + std::string(ex.what()));
    }
}

void
EffectsEventHandler::handleGetPlayerSkillCooldownsEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();
    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerSkillCooldownsEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_active_skill_cooldowns", {characterId});
        txn.commit();

        nlohmann::json cooldownsJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json cd;
            cd["skillSlug"] = row["skill_slug"].as<std::string>();
            cd["remainingMs"] = row["remaining_ms"].as<int64_t>();
            cooldownsJson.push_back(std::move(cd));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player skill cooldowns")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerSkillCooldowns")
                                      .setBody("characterId", characterId)
                                      .setBody("cooldowns", cooldownsJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->debug("[GET_SKILL_COOLDOWNS] char={} active={}", characterId, cooldownsJson.size());
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetPlayerSkillCooldownsEvent error: " + std::string(ex.what()));
    }
}

void
EffectsEventHandler::handleSaveAnalyticsEventEvent(const Event &event)
{
    const auto &data = event.getData();
    if (!std::holds_alternative<nlohmann::json>(data))
    {
        log_->error("handleSaveAnalyticsEventEvent: unexpected data type");
        return;
    }

    try
    {
        const auto &j = std::get<nlohmann::json>(data);
        std::string eventType = j.value("analyticsType", "");
        int charId = j.value("characterId", 0);
        std::string sessionId = j.value("sessionId", "");
        int level = j.value("level", 0);
        int zoneId = j.value("zoneId", 0);
        std::string payload = j.contains("payload") ? j["payload"].dump() : "{}";

        if (eventType.empty())
        {
            log_->warn("handleSaveAnalyticsEventEvent: empty analyticsType, skipping");
            return;
        }

        auto sc = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(sc.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string anaFactKey = outboxFactKeyOf(j);
        if (!anaFactKey.empty())
        {
            const int anaClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, anaFactKey);
            if (anaClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), anaFactKey, "duplicate");
                return;
            }
            if (anaClaimed < 0)
                return;
        }

        // Analytics INSERT via registry prepared statement (was inline exec_params).
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "insert_game_analytics", {eventType, charId, sessionId, level, zoneId, payload}));

        txn.commit();
        if (!anaFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), anaFactKey, "applied");

        log_->debug("[ANALYTICS] {} char={} session={} lvl={} zone={}", eventType, charId, sessionId, level, zoneId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveAnalyticsEventEvent error: " + std::string(ex.what()));
    }
}
