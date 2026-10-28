#include "events/handlers/DialogueQuestEventHandler.hpp"

#include "data/DataStructs.hpp"
#include "events/Outbox.hpp"
#include "utils/ResponseBuilder.hpp"
#include "utils/TerminalColors.hpp"

DialogueQuestEventHandler::DialogueQuestEventHandler(
    NetworkManager &networkManager,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServices)
{
}

void
DialogueQuestEventHandler::handleGetDialoguesEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto dialoguesJson = gameServices_.getDialogueQuestManager().getAllDialoguesJson();
        auto mappingsJson = gameServices_.getDialogueQuestManager().getAllNPCDialogueMappingsJson();

        ResponseBuilder builder;
        nlohmann::json resp1 = builder
                                   .setHeader("message", "Dialogues data")
                                   .setHeader("hash", "")
                                   .setHeader("clientId", clientID)
                                   .setHeader("eventType", "setDialoguesData")
                                   .setBody("dialogues", dialoguesJson)
                                   .build();
        networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", resp1));

        nlohmann::json resp2 = builder
                                   .setHeader("message", "NPC dialogue mappings")
                                   .setHeader("hash", "")
                                   .setHeader("clientId", clientID)
                                   .setHeader("eventType", "setNPCDialogueMappings")
                                   .setBody("mappings", mappingsJson)
                                   .build();
        networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", resp2));

        gameServices_.getLogger().log("[EH] Sent " + std::to_string(dialoguesJson.size()) +
                                          " dialogues + " + std::to_string(mappingsJson.size()) + " mappings to chunk",
            GREEN);
    }
    catch (const DatabaseError &)
    {
        // DB transport failure: no partial/empty success here.
        // JOIN entries are counted by dispatchCatalog; runtime
        // reads fall through to the error response below.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleGetDialoguesEvent: " + std::string(ex.what()));
    }
}

void
DialogueQuestEventHandler::handleGetQuestsEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto questsJson = gameServices_.getDialogueQuestManager().getAllQuestsJson();

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Quests data")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setQuestsData")
                                      .setBody("quests", questsJson)
                                      .build();
        networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", response));

        gameServices_.getLogger().log("[EH] Sent " + std::to_string(questsJson.size()) +
                                          " quests to chunk",
            GREEN);
    }
    catch (const DatabaseError &)
    {
        // DB transport failure: no partial/empty success here.
        // JOIN entries are counted by dispatchCatalog; runtime
        // reads fall through to the error response below.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleGetQuestsEvent: " + std::string(ex.what()));
    }
}

void
DialogueQuestEventHandler::handleGetPlayerQuestsEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerQuestsEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto questsJson = gameServices_.getDialogueQuestManager().getPlayerQuestsJson(characterId);

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player quests")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerQuestsData")
                                      .setBody("characterId", characterId)
                                      .setBody("quests", questsJson)
                                      .build();
        networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", response));

        gameServices_.getLogger().log("[EH] Sent " + std::to_string(questsJson.size()) +
                                          " quests for characterId=" + std::to_string(characterId),
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleGetPlayerQuestsEvent: " + std::string(ex.what()));
    }
}

void
DialogueQuestEventHandler::handleGetPlayerFlagsEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerFlagsEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto flagsJson = gameServices_.getDialogueQuestManager().getPlayerFlagsJson(characterId);

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player flags")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerFlagsData")
                                      .setBody("characterId", characterId)
                                      .setBody("flags", flagsJson)
                                      .build();
        networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", response));

        gameServices_.getLogger().log("[EH] Sent " + std::to_string(flagsJson.size()) +
                                          " flags for characterId=" + std::to_string(characterId),
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleGetPlayerFlagsEvent: " + std::string(ex.what()));
    }
}

void
DialogueQuestEventHandler::handleUpdatePlayerQuestProgressEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleUpdatePlayerQuestProgressEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        auto &body = j.at("body");

        int characterId = body.value("characterId", 0);
        int questId = body.value("questId", 0);
        std::string state = body.value("state", "active");
        int currentStep = body.value("currentStep", 0);
        // progress may be a JSON object or a pre-serialized string
        std::string progressJson = "{}";
        if (body.contains("progress"))
        {
            const auto &prog = body.at("progress");
            progressJson = prog.is_string() ? prog.get<std::string>() : prog.dump();
        }

        if (characterId <= 0 || questId <= 0)
        {
            log_->error("handleUpdatePlayerQuestProgressEvent: invalid characterId/questId");
            return;
        }

        gameServices_.getDialogueQuestManager().savePlayerQuestProgress(
            characterId, questId, state, currentStep, progressJson);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleUpdatePlayerQuestProgressEvent: " + std::string(ex.what()));
    }
}

void
DialogueQuestEventHandler::handleDeletePlayerQuestEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleDeletePlayerQuestEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        auto &body = j.at("body");

        int characterId = body.value("characterId", 0);
        int questId = body.value("questId", 0);

        if (characterId <= 0 || questId <= 0)
        {
            log_->error("handleDeletePlayerQuestEvent: invalid characterId/questId");
            return;
        }

        gameServices_.getDialogueQuestManager().deletePlayerQuestProgress(characterId, questId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleDeletePlayerQuestEvent: " + std::string(ex.what()));
    }
}

void
DialogueQuestEventHandler::handleUpdatePlayerFlagEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleUpdatePlayerFlagEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        auto &body = j.at("body");

        int characterId = body.value("characterId", 0);
        std::string flagKey = body.value("flagKey", "");
        int intValue = body.value("intValue", 0);
        bool boolValue = body.value("boolValue", false);

        if (characterId <= 0 || flagKey.empty())
        {
            log_->error("handleUpdatePlayerFlagEvent: invalid characterId or flagKey");
            return;
        }

        gameServices_.getDialogueQuestManager().savePlayerFlag(characterId, flagKey, intValue, boolValue);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleUpdatePlayerFlagEvent: " + std::string(ex.what()));
    }
}

void
DialogueQuestEventHandler::handleClaimDialogueGiftEvent(const Event &event)
{
    const auto &data = event.getData();
    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleClaimDialogueGiftEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        const auto &body = j.at("body");
        const int characterId = body.value("characterId", 0);
        const std::string flagKey = body.value("flagKey", "");
        const std::string factKey = body.value("factKey", j.value("factKey", ""));
        if (characterId <= 0 || flagKey.empty())
        {
            log_->error("handleClaimDialogueGiftEvent: invalid characterId or flagKey");
            return;
        }
        std::vector<DialogueQuestManager::ClaimGiftItem> items;
        if (body.contains("items") && body["items"].is_array())
        {
            for (const auto &it : body["items"])
            {
                DialogueQuestManager::ClaimGiftItem g;
                g.itemId = it.value("itemId", 0);
                g.quantity = it.value("quantity", 0);
                g.syncToken = it.value("syncToken", (uint64_t)0);
                if (g.itemId > 0 && g.quantity > 0)
                    items.push_back(g);
            }
        }
        if (items.empty())
        {
            log_->error("handleClaimDialogueGiftEvent: empty items");
            return;
        }
        auto res = gameServices_.getDialogueQuestManager().claimDialogueGift(characterId, flagKey, items, factKey);
        // Fire-and-forget atomic: game is the arbiter, chunk reconciles on next
        // load. Ack the fact so the chunk outbox stops retrying.
        if (!factKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), factKey, "applied");
        log_->info("[CLAIM_GIFT] char=" + std::to_string(characterId) + " key=" + flagKey +
                   " ok=" + (res.ok ? "1" : "0") + " reason=" + res.reason);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleClaimDialogueGiftEvent: " + std::string(ex.what()));
    }
}

void
DialogueQuestEventHandler::handleClaimQuestTurnInEvent(const Event &event)
{
    const auto &data = event.getData();
    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleClaimQuestTurnInEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        const auto &body = j.at("body");
        const int characterId = body.value("characterId", 0);
        int questId = body.value("questId", 0);
        const std::string questSlug = body.value("questSlug", "");
        const std::string factKey = body.value("factKey", j.value("factKey", ""));
        if (characterId <= 0 || (questId <= 0 && questSlug.empty()))
        {
            log_->error("handleClaimQuestTurnInEvent: invalid characterId/quest");
            return;
        }
        if (questId <= 0 && !questSlug.empty())
        {
            // Resolve slug -> id (single query, no txn needed for routing).
            try
            {
                auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
                pqxx::work txn(_dbConn.get());
                auto r = gameServices_.getDatabase().execChecked(txn, "get_quest_id_by_slug", {questSlug});
                txn.commit();
                if (!r.empty())
                    questId = r[0]["id"].as<int>(0);
            }
            catch (...)
            {
            }
            if (questId <= 0)
            {
                log_->error("handleClaimQuestTurnInEvent: unknown questSlug");
                return;
            }
        }
        std::vector<DialogueQuestManager::ClaimQuestConsume> consume;
        if (body.contains("consume") && body["consume"].is_array())
        {
            for (const auto &c : body["consume"])
            {
                DialogueQuestManager::ClaimQuestConsume cc;
                cc.itemId = c.value("itemId", 0);
                cc.count = c.value("count", c.value("quantity", 0));
                if (cc.itemId > 0 && cc.count > 0)
                    consume.push_back(cc);
            }
        }
        std::vector<DialogueQuestManager::ClaimQuestReward> rewards;
        if (body.contains("rewards") && body["rewards"].is_array())
        {
            for (const auto &r : body["rewards"])
            {
                DialogueQuestManager::ClaimQuestReward rw;
                rw.rewardType = r.value("rewardType", r.value("type", std::string("")));
                rw.itemId = r.value("itemId", 0);
                rw.quantity = r.value("quantity", 0);
                rw.amount = r.value("amount", (int64_t)0);
                rw.syncToken = r.value("syncToken", (uint64_t)0);
                if (!rw.rewardType.empty())
                    rewards.push_back(rw);
            }
        }
        auto res = gameServices_.getDialogueQuestManager().claimQuestTurnIn(
            characterId, questId, consume, rewards, factKey);
        if (!factKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), factKey, "applied");
        log_->info("[CLAIM_TURNIN] char=" + std::to_string(characterId) +
                   " quest=" + std::to_string(questId) + " ok=" + (res.ok ? "1" : "0") +
                   " reason=" + res.reason);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleClaimQuestTurnInEvent: " + std::string(ex.what()));
    }
}

void
DialogueQuestEventHandler::handleGetPlayerActiveEffectsEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerActiveEffectsEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Prune expired rows first to prevent unbounded table growth.
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "cleanup_expired_active_effects", {}));
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_player_active_effects", {characterId});

        nlohmann::json effectsJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json eff;
            eff["id"] = row["id"].as<int64_t>();
            eff["effectId"] = row["effect_id"].as<int>();
            eff["effectSlug"] = row["effect_slug"].as<std::string>();
            eff["effectTypeSlug"] = row["effect_type_slug"].as<std::string>();
            eff["attributeId"] = row["attribute_id"].as<int>();
            eff["attributeSlug"] = row["attribute_slug"].as<std::string>();
            eff["value"] = row["value"].as<float>();
            eff["sourceType"] = row["source_type"].as<std::string>();
            eff["tickMs"] = row["tick_ms"].as<int>();
            eff["expiresAt"] = row["expires_at_unix"].as<int64_t>();
            effectsJson.push_back(std::move(eff));
        }

        // Also append permanent modifiers from passive skills the character has learned.
        // These are computed from passive_skill_modifiers × character_skills and arrive as
        // permanent (expiresAt=0, tickMs=0) flat/percent stat modifiers.
        try
        {
            auto passiveResult = gameServices_.getDatabase().execChecked(
                txn, "get_player_passive_skill_effects", {characterId});

            for (const auto &row : passiveResult)
            {
                nlohmann::json eff;
                eff["id"] = row["id"].as<int64_t>();
                eff["effectId"] = 0;
                eff["effectSlug"] = row["effect_slug"].as<std::string>();
                eff["effectTypeSlug"] = std::string("passive");
                eff["attributeId"] = 0;
                eff["attributeSlug"] = row["attribute_slug"].as<std::string>();
                eff["value"] = row["value"].as<float>();
                eff["modifierType"] = row["modifier_type"].as<std::string>();
                eff["sourceType"] = std::string("skill_passive");
                eff["tickMs"] = 0;
                eff["expiresAt"] = int64_t(0); // permanent
                effectsJson.push_back(std::move(eff));
            }
        }
        catch (const std::exception &passiveEx)
        {
            // passive_skill_modifiers table may not exist in older DBs — degrade gracefully
            log_->warn("[EH] Could not load passive skill effects for character " +
                       std::to_string(characterId) + ": " + passiveEx.what());
        }

        txn.commit();

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player active effects")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerActiveEffects")
                                      .setBody("characterId", characterId)
                                      .setBody("effects", effectsJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        gameServices_.getLogger().log("[EH] Sent " + std::to_string(effectsJson.size()) +
                                          " active effects for characterId=" + std::to_string(characterId),
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleGetPlayerActiveEffectsEvent: " + std::string(ex.what()));
    }
}

void
DialogueQuestEventHandler::handleGetCharacterAttributesRefreshEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetCharacterAttributesRefreshEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_character_attributes", {characterId});

        nlohmann::json attrsJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json attr;
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
        // Answer on the live chunk socket (see setLearnedSkill: captured
        // sockets go stale across chunk reconnects).
        auto sendSocket =
            gameServices_.getChunkManager().resolveLiveSocket(clientSocket);
        if (!sendSocket)
        {
            log_->error("[GAME] setCharacterAttributesRefresh: no live chunk socket for char={}",
                characterId);
            return;
        }
        networkManager_.sendResponse(sendSocket,
            networkManager_.generateResponseMessage("success", response));

        gameServices_.getLogger().log("[EH] Sent " + std::to_string(attrsJson.size()) +
                                          " attribute entries (refresh) for characterId=" + std::to_string(characterId),
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleGetCharacterAttributesRefreshEvent: " + std::string(ex.what()));
    }
}
