#include "events/handlers/SkillsEventHandler.hpp"

#include "data/DataStructs.hpp"
#include "events/Outbox.hpp"
#include "utils/ResponseBuilder.hpp"

SkillsEventHandler::SkillsEventHandler(
    NetworkManager &networkManager,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServices)
{
}

void
SkillsEventHandler::handleGetPlayerReputationsEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerReputationsEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_player_reputations", {characterId});

        nlohmann::json entriesJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json entry;
            entry["factionSlug"] = row["faction_slug"].as<std::string>();
            entry["value"] = row["value"].as<int>();
            entriesJson.push_back(std::move(entry));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player reputations")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerReputationsData")
                                      .setBody("characterId", characterId)
                                      .setBody("entries", entriesJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[REPUTATION] Sent {} entries for characterId={}", entriesJson.size(), characterId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetPlayerReputationsEvent error: " + std::string(ex.what()));
    }
}

void
SkillsEventHandler::handleSaveReputationEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveReputationEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        std::string faction = j.value("factionSlug", "");
        int value = j.value("value", 0);

        if (characterId <= 0 || faction.empty())
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the write below).
        const std::string repFactKey = outboxFactKeyOf(j);
        if (!repFactKey.empty())
        {
            const int claimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, repFactKey);
            if (claimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), repFactKey, "duplicate");
                return;
            }
            if (claimed < 0)
                return;
        }
        // Prefer the delta when the sender provides one: concurrent changes
        // apply atomically instead of racing last-writer-wins on absolutes.
        // Legacy absolute-only senders keep the old upsert path.
        if (j.contains("delta") && j["delta"].is_number_integer() && j["delta"].get<int>() != 0)
        {
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "add_reputation", {characterId, faction, j["delta"].get<int>()}));
        }
        else
        {
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "upsert_reputation", {characterId, faction, value}));
        }
        txn.commit();

        if (!repFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), repFactKey, "applied");

        log_->info("[REPUTATION] Saved char={} faction={} value={}", characterId, faction, value);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveReputationEvent error: " + std::string(ex.what()));
    }
}

void
SkillsEventHandler::handleGetPlayerMasteriesEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerMasteriesEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_player_masteries", {characterId});

        nlohmann::json entriesJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json entry;
            entry["masterySlug"] = row["mastery_slug"].as<std::string>();
            entry["value"] = row["value"].as<float>();
            entriesJson.push_back(std::move(entry));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player masteries")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerMasteriesData")
                                      .setBody("characterId", characterId)
                                      .setBody("entries", entriesJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[MASTERY] Sent {} entries for characterId={}", entriesJson.size(), characterId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetPlayerMasteriesEvent error: " + std::string(ex.what()));
    }
}

void
SkillsEventHandler::handleSaveMasteryEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveMasteryEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        std::string masterySlug = j.value("masterySlug", "");
        float value = j.value("value", 0.0f);

        if (characterId <= 0 || masterySlug.empty())
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string mastFactKey = outboxFactKeyOf(j);
        if (!mastFactKey.empty())
        {
            const int mastClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, mastFactKey);
            if (mastClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), mastFactKey, "duplicate");
                return;
            }
            if (mastClaimed < 0)
                return;
        }
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "upsert_mastery", {characterId, masterySlug, value}));
        txn.commit();
        if (!mastFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), mastFactKey, "applied");

        log_->info("[MASTERY] Saved char={} slug={} value={}", characterId, masterySlug, value);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveMasteryEvent error: " + std::string(ex.what()));
    }
}

void
SkillsEventHandler::handleSaveLearnedSkillEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveLearnedSkillEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int clientId = j.value("clientId", 0);
        std::string skillSlug = j.value("skillSlug", "");

        if (characterId <= 0 || skillSlug.empty())
        {
            log_->error("handleSaveLearnedSkillEvent: invalid params char={} slug={}", characterId, skillSlug);
            return;
        }

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());

        // Outbox idempotency: claim the fact key atomically with the writes
        // below (duplicate deliveries skip + ack-duplicate; transport errors
        // abort without ack so the chunk retries).
        const std::string learnFactKey = outboxFactKeyOf(j);
        if (!learnFactKey.empty())
        {
            const int claimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, learnFactKey);
            if (claimed == 0)
            {
                outboxSendFactAck(networkManager_, clientSocket, learnFactKey, "duplicate");
                return;
            }
            if (claimed < 0)
                return;
        }

        // Persist the skill
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "save_learned_skill", {characterId, skillSlug}));

        // Persist SP as the authoritative value decided by chunk (single
        // owner: chunk validates+deducts in-session, game stores the fact).
        // Repeating the same fact is a no-op (idempotent). Legacy senders
        // without the fact fall back to the old cost-table decrement.
        if (j.contains("freeSkillPoints") && j["freeSkillPoints"].is_number_integer())
        {
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "set_free_skill_points",
                {characterId, j["freeSkillPoints"].get<int>()}));
        }
        else
        {
            auto costRows = gameServices_.getDatabase().execChecked(
                txn, "get_skill_sp_cost", {skillSlug});
            if (!costRows.empty() && !costRows[0]["skill_point_cost"].is_null())
            {
                int spCost = costRows[0]["skill_point_cost"].as<int>(0);
                if (spCost > 0)
                    static_cast<void>(gameServices_.getDatabase().execChecked(
                        txn, "decrement_skill_points", {characterId, spCost}));
            }
        }

        // Query full skill data for the newly learned skill
        auto rows = gameServices_.getDatabase().execChecked(
            txn, "get_character_skills", {characterId});
        txn.commit();

        // Outbox ack: the fact is durable now.
        if (!learnFactKey.empty())
            outboxSendFactAck(networkManager_, clientSocket, learnFactKey, "applied");

        // Find the newly learned skill in the results
        nlohmann::json skillJson = nlohmann::json::object();
        for (const auto &row : rows)
        {
            if (row["skill_slug"].as<std::string>() == skillSlug)
            {
                skillJson["skillName"] = row["skill_name"].as<std::string>();
                skillJson["skillSlug"] = row["skill_slug"].as<std::string>();
                skillJson["scaleStat"] = row["scale_stat"].as<std::string>();
                skillJson["school"] = row["school"].as<std::string>();
                skillJson["skillEffectType"] = row["skill_effect_type"].as<std::string>();
                skillJson["skillLevel"] = row["skill_level"].is_null() ? 1 : row["skill_level"].as<int>();
                skillJson["coeff"] = row["coeff"].as<double>(0.0);
                skillJson["flatAdd"] = row["flat_add"].as<double>(0.0);
                skillJson["cooldownMs"] = row["cooldown_ms"].as<int>(0);
                skillJson["gcdMs"] = row["gcd_ms"].as<int>(0);
                skillJson["castMs"] = row["cast_ms"].as<int>(0);
                skillJson["costMp"] = row["cost_mp"].as<int>(0);
                skillJson["maxRange"] = row["max_range"].as<int>(0);
                skillJson["areaRadius"] = row["area_radius"].as<int>(0);
                skillJson["swingMs"] = row["swing_ms"].as<int>(300);
                skillJson["animationName"] = row["animation_name"].as<std::string>("");
                skillJson["isPassive"] = row["is_passive"].as<bool>();

                // Include active effects (from skill_active_effects for non-passive skills,
                // or from passive_skill_modifiers for passive skills)
                if (!row["active_effects"].is_null())
                {
                    try
                    {
                        skillJson["effects"] = nlohmann::json::parse(row["active_effects"].as<std::string>());
                    }
                    catch (const std::exception &)
                    {
                        skillJson["effects"] = nlohmann::json::array();
                    }
                }
                else
                {
                    skillJson["effects"] = nlohmann::json::array();
                }

                break;
            }
        }

        if (skillJson.empty())
        {
            log_->error("handleSaveLearnedSkillEvent: skill {} not found after save for char={}", skillSlug, characterId);
            return;
        }

        // For passive skills also include passive_skill_modifiers so the chunk server can
        // apply ActiveEffects immediately (permanent stat modifiers).
        if (skillJson.value("isPassive", false))
        {
            nlohmann::json effectsArr = nlohmann::json::array();
            try
            {
                // Fresh txn: the write txn above is already committed.
                // (Querying on it threw and always yielded empty effects.)
                auto _dbConnPm = gameServices_.getDatabase().getConnectionLocked();
                pqxx::work txnPm(_dbConnPm.get());
                auto effRows = gameServices_.getDatabase().execChecked(
                    txnPm, "get_passive_skill_modifiers_by_slug", {skillSlug});
                txnPm.commit();
                for (const auto &row : effRows)
                {
                    nlohmann::json eff;
                    eff["effectSlug"] = row["effect_slug"].as<std::string>();
                    eff["effectTypeSlug"] = std::string("passive");
                    eff["attributeSlug"] = row["attribute_slug"].as<std::string>();
                    eff["value"] = row["value"].as<float>();
                    eff["durationSeconds"] = 0;
                    eff["tickMs"] = 0;
                    effectsArr.push_back(std::move(eff));
                }
            }
            catch (const std::exception &effEx)
            {
                log_->warn("[SKILL] Could not load passive modifiers for skill {}: {}", skillSlug, effEx.what());
            }
            skillJson["effects"] = std::move(effectsArr);
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Skill learned")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientId)
                                      .setHeader("eventType", "setLearnedSkill")
                                      .setBody("characterId", characterId)
                                      .setBody("skillData", skillJson)
                                      .build();

        // Send on the live chunk socket, not the captured one: the event
        // queued async and the chunk may have reconnected since (writes
        // into a half-open stale socket fail silently). resolveLiveSocket
        // prefers the current registration; sendResponse still guards
        // closed sockets with an error log.
        auto sendSocket =
            gameServices_.getChunkManager().resolveLiveSocket(clientSocket);
        if (!sendSocket)
        {
            log_->error("[GAME] setLearnedSkill: no live chunk socket for char={}", characterId);
            return;
        }
        networkManager_.sendResponse(sendSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[SKILL] Saved learned skill slug={} for char={}", skillSlug, characterId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveLearnedSkillEvent error: " + std::string(ex.what()));
    }
}

void
SkillsEventHandler::handleSaveSkillBarSlotEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveSkillBarSlotEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int slotIndex = j.value("slotIndex", -1);
        std::string skillSlug = j.value("skillSlug", "");

        if (characterId <= 0 || slotIndex < 0 || slotIndex >= 12)
        {
            log_->error("handleSaveSkillBarSlotEvent: invalid params char={} slot={}", characterId, slotIndex);
            return;
        }

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string barFactKey = outboxFactKeyOf(j);
        if (!barFactKey.empty())
        {
            const int barClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, barFactKey);
            if (barClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), barFactKey, "duplicate");
                return;
            }
            if (barClaimed < 0)
                return;
        }

        if (skillSlug.empty())
        {
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "clear_skill_bar_slot", {characterId, slotIndex}));
        }
        else
        {
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "save_skill_bar_slot", {characterId, slotIndex, skillSlug}));
        }
        txn.commit();
        if (!barFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), barFactKey, "applied");

        log_->info("[SKILL_BAR] slot={} slug='{}' saved for char={}", slotIndex, skillSlug, characterId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveSkillBarSlotEvent error: " + std::string(ex.what()));
    }
}
