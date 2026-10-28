#include "services/DialogueQuestManager.hpp"
#include "events/Outbox.hpp"
#include <pqxx/pqxx>
#include <spdlog/logger.h>

DialogueQuestManager::DialogueQuestManager(Database &database, Logger &logger)
    : database_(database), logger_(logger)
{
    log_ = logger.getSystem("quest");
}

// ---------------------------------------------------------------------------
// Static startup data
// ---------------------------------------------------------------------------

nlohmann::json
DialogueQuestManager::getAllDialoguesJson()
{
    log_->debug("[DQM] Loading dialogues from database");
    auto _dbConn = database_.getConnectionLocked();
    pqxx::work txn(_dbConn.get());

    auto dialogues = database_.execChecked(txn, "get_dialogues", {});
    auto nodes = database_.execChecked(txn, "get_dialogue_nodes", {});
    auto edges = database_.execChecked(txn, "get_dialogue_edges", {});
    txn.commit();

    nlohmann::json result = nlohmann::json::array();
    for (const auto &row : dialogues)
    {
        nlohmann::json d;
        long long dialogueId = row["id"].as<long long>();
        d["id"] = dialogueId;
        d["slug"] = row["slug"].as<std::string>();
        d["version"] = row["version"].as<int>();
        d["startNodeId"] = row["start_node_id"].is_null() ? 0LL : row["start_node_id"].as<long long>();
        d["nodes"] = nlohmann::json::array();
        d["edges"] = nlohmann::json::array();

        for (const auto &n : nodes)
        {
            if (n["dialogue_id"].as<long long>() != dialogueId)
                continue;
            nlohmann::json node;
            node["id"] = n["id"].as<long long>();
            node["type"] = n["type"].as<std::string>();
            node["speakerNpcId"] = n["speaker_npc_id"].is_null() ? 0LL : n["speaker_npc_id"].as<long long>();
            node["clientNodeKey"] = n["client_node_key"].is_null() ? "" : n["client_node_key"].as<std::string>();
            node["jumpTargetNodeId"] = n["jump_target_node_id"].as<long long>();
            // Parse jsonb fields from text to actual JSON objects
            {
                std::string cg = n["condition_group"].as<std::string>();
                node["conditionGroup"] = cg.empty() ? nlohmann::json(nullptr) : nlohmann::json::parse(cg);
            }
            {
                std::string ag = n["action_group"].as<std::string>();
                node["actionGroup"] = ag.empty() ? nlohmann::json(nullptr) : nlohmann::json::parse(ag);
            }
            d["nodes"].push_back(node);
        }

        for (const auto &e : edges)
        {
            bool found = false;
            for (const auto &n : nodes)
            {
                if (n["dialogue_id"].as<long long>() == dialogueId &&
                    n["id"].as<long long>() == e["from_node_id"].as<long long>())
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                continue;

            nlohmann::json edge;
            edge["id"] = e["id"].as<long long>();
            edge["fromNodeId"] = e["from_node_id"].as<long long>();
            edge["toNodeId"] = e["to_node_id"].as<long long>();
            edge["orderIndex"] = e["order_index"].as<int>();
            edge["clientChoiceKey"] = e["client_choice_key"].as<std::string>();
            edge["hideIfLocked"] = e["hide_if_locked"].as<bool>();
            // Parse jsonb fields from text to actual JSON objects
            {
                std::string cg = e["condition_group"].as<std::string>();
                edge["conditionGroup"] = cg.empty() ? nlohmann::json(nullptr) : nlohmann::json::parse(cg);
            }
            {
                std::string ag = e["action_group"].as<std::string>();
                edge["actionGroup"] = ag.empty() ? nlohmann::json(nullptr) : nlohmann::json::parse(ag);
            }
            d["edges"].push_back(edge);
        }

        result.push_back(d);
    }

    logger_.log("[DQM] Loaded " + std::to_string(result.size()) + " dialogues", GREEN);
    return result;
}

nlohmann::json
DialogueQuestManager::getAllNPCDialogueMappingsJson()
{
    auto _dbConn = database_.getConnectionLocked();
    pqxx::work txn(_dbConn.get());
    auto mappings = database_.execChecked(txn, "get_npc_dialogue_mappings", {});
    txn.commit();

    nlohmann::json result = nlohmann::json::array();
    for (const auto &row : mappings)
    {
        nlohmann::json m;
        m["npcId"] = row["npc_id"].as<long long>();
        m["dialogueId"] = row["dialogue_id"].as<long long>();
        m["priority"] = row["priority"].as<int>();
        {
            std::string cg = row["condition_group"].as<std::string>();
            m["conditionGroup"] = cg.empty() ? nlohmann::json(nullptr) : nlohmann::json::parse(cg);
        }
        result.push_back(m);
    }
    return result;
}

nlohmann::json
DialogueQuestManager::getAllQuestsJson()
{
    log_->debug("[DQM] Loading quests from database");
    auto _dbConn = database_.getConnectionLocked();
    pqxx::work txn(_dbConn.get());

    auto quests = database_.execChecked(txn, "get_quests", {});
    auto steps = database_.execChecked(txn, "get_quest_steps", {});
    auto rewards = database_.execChecked(txn, "get_quest_rewards", {});
    txn.commit();

    nlohmann::json result = nlohmann::json::array();
    for (const auto &row : quests)
    {
        nlohmann::json q;
        long long questId = row["id"].as<long long>();
        q["id"] = questId;
        q["slug"] = row["slug"].as<std::string>();
        q["minLevel"] = row["min_level"].as<int>();
        q["repeatable"] = row["repeatable"].as<bool>();
        q["cooldownSec"] = row["cooldown_sec"].as<int>();
        q["giverNpcId"] = row["giver_npc_id"].as<long long>();
        q["turninNpcId"] = row["turnin_npc_id"].as<long long>();
        q["clientQuestKey"] = row["client_quest_key"].as<std::string>();
        q["reputationFactionSlug"] = row["reputation_faction_slug"].as<std::string>();
        q["reputationOnComplete"] = row["reputation_on_complete"].as<int>();
        q["reputationOnFail"] = row["reputation_on_fail"].as<int>();
        q["steps"] = nlohmann::json::array();
        q["rewards"] = nlohmann::json::array();

        for (const auto &s : steps)
        {
            if (s["quest_id"].as<long long>() != questId)
                continue;
            nlohmann::json step;
            step["id"] = s["id"].as<long long>();
            step["stepIndex"] = s["step_index"].as<int>();
            step["stepType"] = s["step_type"].as<std::string>();
            step["completionMode"] = s["completion_mode"].as<std::string>();
            step["clientStepKey"] = s["client_step_key"].as<std::string>();
            // Parse params jsonb text to actual JSON object
            {
                std::string paramsStr = s["params"].is_null() ? "{}" : s["params"].as<std::string>();
                step["params"] = paramsStr.empty() ? nlohmann::json::object() : nlohmann::json::parse(paramsStr);
            }
            q["steps"].push_back(step);
        }

        for (const auto &r : rewards)
        {
            if (r["quest_id"].as<long long>() != questId)
                continue;
            nlohmann::json reward;
            reward["id"] = r["id"].as<long long>();
            reward["rewardType"] = r["reward_type"].as<std::string>();
            reward["itemId"] = r["item_id"].as<long long>();
            reward["quantity"] = r["quantity"].as<int>();
            reward["amount"] = r["amount"].as<long long>();
            q["rewards"].push_back(reward);
        }

        result.push_back(q);
    }

    logger_.log("[DQM] Loaded " + std::to_string(result.size()) + " quests", GREEN);
    return result;
}

// ---------------------------------------------------------------------------
// Per-player dynamic data
// ---------------------------------------------------------------------------

nlohmann::json
DialogueQuestManager::getPlayerQuestsJson(int characterId)
{
    auto _dbConn = database_.getConnectionLocked();
    pqxx::work txn(_dbConn.get());
    auto result = database_.execChecked(txn, "get_player_quests", {(int)characterId});
    txn.commit();

    nlohmann::json questsJson = nlohmann::json::array();
    for (const auto &row : result)
    {
        nlohmann::json q;
        q["questId"] = row["quest_id"].as<long long>();
        q["questSlug"] = row["slug"].as<std::string>();
        q["state"] = row["state"].as<std::string>();
        q["currentStep"] = row["current_step"].as<int>();
        // Parse progress jsonb text to actual JSON object
        {
            std::string progStr = row["progress"].is_null() ? "{}" : row["progress"].as<std::string>();
            q["progress"] = progStr.empty() ? nlohmann::json::object() : nlohmann::json::parse(progStr);
        }
        questsJson.push_back(q);
    }
    return questsJson;
}

nlohmann::json
DialogueQuestManager::getPlayerFlagsJson(int characterId)
{
    auto _dbConn = database_.getConnectionLocked();
    pqxx::work txn(_dbConn.get());
    auto result = database_.execChecked(txn, "get_player_flags", {(int)characterId});
    txn.commit();

    nlohmann::json flagsJson = nlohmann::json::array();
    for (const auto &row : result)
    {
        nlohmann::json f;
        f["flagKey"] = row["flag_key"].as<std::string>();
        f["intValue"] = row["int_value"].as<int>();
        f["boolValue"] = row["bool_value"].as<bool>();
        flagsJson.push_back(f);
    }
    return flagsJson;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void
DialogueQuestManager::savePlayerQuestProgress(int characterId, int questId, const std::string &state, int currentStep, const std::string &progressJson)
{
    try
    {
        auto _dbConn = database_.getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        static_cast<void>(database_.execChecked(txn, "upsert_player_quest", {(int)characterId, (int)questId, state, (int)currentStep, progressJson}));
        txn.commit();
        logger_.log("[DQM] Saved quest characterId=" + std::to_string(characterId) +
                        " questId=" + std::to_string(questId) + " state=" + state,
            GREEN);
    }
    catch (const std::exception &ex)
    {
        logger_.logError("[DQM] savePlayerQuestProgress error: " + std::string(ex.what()));
    }
}

void
DialogueQuestManager::deletePlayerQuestProgress(int characterId, int questId)
{
    try
    {
        auto _dbConn = database_.getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        static_cast<void>(database_.execChecked(txn, "delete_player_quest", {(int)characterId, (int)questId}));
        txn.commit();
        logger_.log("[DQM] Deleted quest characterId=" + std::to_string(characterId) +
                        " questId=" + std::to_string(questId),
            GREEN);
    }
    catch (const std::exception &ex)
    {
        logger_.logError("[DQM] deletePlayerQuestProgress error: " + std::string(ex.what()));
    }
}

void
DialogueQuestManager::savePlayerFlag(int characterId, const std::string &flagKey, int intValue, bool boolValue)
{
    try
    {
        auto _dbConn = database_.getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        static_cast<void>(database_.execChecked(txn, "upsert_player_flag", {(int)characterId, flagKey, (int)intValue, std::string(boolValue ? "true" : "false")}));
        txn.commit();
        log_->info("[DQM] Saved flag characterId=" + std::to_string(characterId) +
                   " key=" + flagKey);
    }
    catch (const std::exception &ex)
    {
        logger_.logError("[DQM] savePlayerFlag error: " + std::string(ex.what()));
    }
}

// ---------------------------------------------------------------------------
// Atomic one-time claims
// ---------------------------------------------------------------------------

DialogueQuestManager::ClaimResult
DialogueQuestManager::claimDialogueGift(int characterId, const std::string &flagKey,
    const std::vector<ClaimGiftItem> &items, const std::string &factKey)
{
    ClaimResult out{false, "error"};
    if (characterId <= 0 || flagKey.empty() || items.empty())
    {
        out.reason = "invalid";
        return out;
    }
    try
    {
        auto _dbConn = database_.getConnectionLocked();
        pqxx::work txn(_dbConn.get());

        // Outbox idempotency first (atomic with business writes below).
        if (!factKey.empty())
        {
            const int claimed = outboxClaimFactKey(database_, txn, factKey);
            if (claimed == 0)
            {
                txn.commit();
                out.ok = true;
                out.reason = "duplicate";
                return out;
            }
            if (claimed < 0)
            {
                out.reason = "error";
                return out;
            }
        }

        // Idempotency guard: flag already true -> drop (no item writes).
        auto flagRows = database_.execChecked(txn, "get_player_flag_for_update", {(int)characterId, flagKey});
        if (!flagRows.empty() && flagRows[0]["bool_value"].as<bool>(false))
        {
            txn.commit();
            out.ok = false;
            out.reason = "already_claimed";
            log_->info("[DQM] claimDialogueGift already_claimed char=" + std::to_string(characterId) + " key=" + flagKey);
            return out;
        }

        // Grant items (token path so chunk retries converge to same rows).
        for (const auto &it : items)
        {
            if (it.itemId <= 0 || it.quantity <= 0)
                continue;
            const int64_t tok = static_cast<int64_t>(it.syncToken);
            if (tok != 0)
            {
                auto found = database_.execChecked(txn, "get_inventory_id_by_token", {(int)characterId, tok});
                if (!found.empty())
                {
                    const int rowId = found[0]["id"].as<int>(0);
                    // Absolute SET: resends converge, never double.
                    auto q = database_.execChecked(txn, "get_inventory_qty", {(int)characterId, (int)it.itemId});
                    (void)q;
                    static_cast<void>(database_.execChecked(txn, "update_player_inventory_quantity",
                        {(int)it.quantity, rowId, (int)characterId}));
                }
                else
                {
                    // Adopt keeps stackables on one row, but each stackMax=1
                    // instance must stay a separate row — adopting would merge
                    // the gift into a virgin starter row while chunk keeps two
                    // rows in-memory (farm-shaped divergence on next load).
                    bool stackable = true;
                    try
                    {
                        auto sr = database_.execChecked(txn, "get_item_stack_max", {(int)it.itemId});
                        if (!sr.empty())
                            stackable = (sr[0]["stack_max"].as<int>(1) > 1);
                    }
                    catch (...)
                    {
                    }
                    bool inserted = false;
                    if (stackable)
                    {
                        auto adopted = database_.execChecked(txn, "adopt_legacy_inventory_row",
                            {(int)it.quantity, tok, (int)characterId, (int)it.itemId});
                        inserted = !adopted.empty();
                    }
                    if (!inserted)
                    {
                        auto ins = database_.execChecked(txn, "insert_player_inventory_with_token",
                            {(int)characterId, (int)it.itemId, (int)it.quantity, tok});
                        if (ins.empty())
                        {
                            // Lost insert race: winner row is ours.
                            auto retry = database_.execChecked(txn, "get_inventory_id_by_token", {(int)characterId, tok});
                            if (!retry.empty())
                            {
                                const int rowId = retry[0]["id"].as<int>(0);
                                static_cast<void>(database_.execChecked(txn, "update_player_inventory_quantity",
                                    {(int)it.quantity, rowId, (int)characterId}));
                            }
                        }
                    }
                }
            }
            else
            {
                // No token (should not happen for claim path): legacy SELECT-then-write.
                auto legacy = database_.execChecked(txn, "get_inventory_id_by_char_item", {(int)characterId, (int)it.itemId});
                if (!legacy.empty())
                {
                    const int rowId = legacy[0]["id"].as<int>(0);
                    // Read current qty and add (claim path has no base qty; chunk sent absolute).
                    // Use absolute quantity from the claim to stay convergent.
                    static_cast<void>(database_.execChecked(txn, "update_player_inventory_quantity",
                        {(int)it.quantity, rowId, (int)characterId}));
                }
                else
                {
                    static_cast<void>(database_.execChecked(txn, "insert_player_inventory_item",
                        {(int)characterId, (int)it.itemId, (int)it.quantity}));
                }
            }
        }

        static_cast<void>(database_.execChecked(txn, "upsert_player_flag",
            {(int)characterId, flagKey, (int)0, std::string("true")}));
        txn.commit();
        out.ok = true;
        out.reason = "applied";
        log_->info("[DQM] claimDialogueGift applied char=" + std::to_string(characterId) + " key=" + flagKey);
        return out;
    }
    catch (const std::exception &ex)
    {
        logger_.logError("[DQM] claimDialogueGift error: " + std::string(ex.what()));
        out.ok = false;
        out.reason = "error";
        return out;
    }
}

DialogueQuestManager::ClaimResult
DialogueQuestManager::claimQuestTurnIn(int characterId, int questId,
    const std::vector<ClaimQuestConsume> &consume,
    const std::vector<ClaimQuestReward> &rewards, const std::string &factKey)
{
    ClaimResult out{false, "error"};
    if (characterId <= 0 || questId <= 0)
    {
        out.reason = "invalid";
        return out;
    }
    try
    {
        auto _dbConn = database_.getConnectionLocked();
        pqxx::work txn(_dbConn.get());

        if (!factKey.empty())
        {
            const int claimed = outboxClaimFactKey(database_, txn, factKey);
            if (claimed == 0)
            {
                txn.commit();
                out.ok = true;
                out.reason = "duplicate";
                return out;
            }
            if (claimed < 0)
            {
                out.reason = "error";
                return out;
            }
        }

        auto qrows = database_.execChecked(txn, "get_player_quest_for_update", {(int)characterId, (int)questId});
        if (qrows.empty())
        {
            txn.commit();
            out.ok = false;
            out.reason = "not_completable";
            return out;
        }
        const std::string state = qrows[0]["state"].as<std::string>();
        if (state != "completed")
        {
            txn.commit();
            out.ok = false;
            out.reason = (state == "turned_in") ? "already_claimed" : "not_completable";
            log_->info("[DQM] claimQuestTurnIn reject char=" + std::to_string(characterId) +
                       " quest=" + std::to_string(questId) + " state=" + state);
            return out;
        }
        const int currentStep = qrows[0]["current_step"].as<int>(0);
        std::string progressJson = "{}";
        try
        {
            progressJson = qrows[0]["progress"].as<std::string>();
        }
        catch (...)
        {
        }

        // Consume checks first (no partial writes on missing items).
        for (const auto &c : consume)
        {
            if (c.itemId <= 0 || c.count <= 0)
                continue;
            auto qr = database_.execChecked(txn, "get_inventory_qty", {(int)characterId, (int)c.itemId});
            const int have = qr.empty() ? 0 : qr[0]["qty"].as<int>(0);
            if (have < c.count)
            {
                txn.commit();
                out.ok = false;
                out.reason = "missing_items";
                return out;
            }
        }

        // Consume: decrement stacks (delete row when emptied). Keep simple and
        // convergent: operate per item_id (collect stacks are fungible).
        for (const auto &c : consume)
        {
            if (c.itemId <= 0 || c.count <= 0)
                continue;
            auto rows = database_.execChecked(txn, "get_inventory_id_by_char_item", {(int)characterId, (int)c.itemId});
            int remaining = c.count;
            for (const auto &r : rows)
            {
                if (remaining <= 0)
                    break;
                const int rowId = r["id"].as<int>(0);
                auto qq = database_.execChecked(txn, "get_inventory_qty", {(int)characterId, (int)c.itemId});
                (void)qq;
                // Re-read row qty via SUM is coarse; use direct row read instead:
                // SELECT quantity for this row is not a prepared stmt; use qty math
                // via update/delete by known remaining: simplest is delete-by-id when
                // the whole stack is consumed, else absolute update. We fetch the
                // exact row quantity with a one-off query to stay correct.
                pqxx::result rq = txn.exec("SELECT quantity FROM player_inventory WHERE id = " +
                    txn.quote(rowId) + " AND character_id = " + txn.quote(characterId));
                const int rowQty = rq.empty() ? 0 : rq[0]["quantity"].as<int>(0);
                if (rowQty <= 0)
                    continue;
                if (rowQty > remaining)
                {
                    static_cast<void>(database_.execChecked(txn, "update_player_inventory_quantity",
                        {(int)(rowQty - remaining), rowId, (int)characterId}));
                    remaining = 0;
                }
                else
                {
                    static_cast<void>(database_.execChecked(txn, "delete_player_inventory_item_by_char_id",
                        {rowId, (int)characterId}));
                    remaining -= rowQty;
                }
            }
        }

        // Grant rewards.
        int goldItemId = 0;
        for (const auto &rw : rewards)
        {
            if (rw.rewardType == "item" && rw.itemId > 0 && rw.quantity > 0)
            {
                const int64_t tok = static_cast<int64_t>(rw.syncToken);
                if (tok != 0)
                {
                    auto found = database_.execChecked(txn, "get_inventory_id_by_token", {(int)characterId, tok});
                    if (!found.empty())
                    {
                        const int rowId = found[0]["id"].as<int>(0);
                        static_cast<void>(database_.execChecked(txn, "update_player_inventory_quantity",
                            {(int)rw.quantity, rowId, (int)characterId}));
                    }
                    else
                    {
                        bool stackable = true;
                        try
                        {
                            auto sr = database_.execChecked(txn, "get_item_stack_max", {(int)rw.itemId});
                            if (!sr.empty())
                                stackable = (sr[0]["stack_max"].as<int>(1) > 1);
                        }
                        catch (...)
                        {
                        }
                        bool done = false;
                        if (stackable)
                        {
                            auto adopted = database_.execChecked(txn, "adopt_legacy_inventory_row",
                                {(int)rw.quantity, tok, (int)characterId, (int)rw.itemId});
                            done = !adopted.empty();
                        }
                        if (!done)
                            static_cast<void>(database_.execChecked(txn, "insert_player_inventory_with_token",
                                {(int)characterId, (int)rw.itemId, (int)rw.quantity, tok}));
                    }
                }
                else
                {
                    auto legacy = database_.execChecked(txn, "get_inventory_id_by_char_item", {(int)characterId, (int)rw.itemId});
                    if (!legacy.empty())
                    {
                        const int rowId = legacy[0]["id"].as<int>(0);
                        auto qr = database_.execChecked(txn, "get_inventory_qty", {(int)characterId, (int)rw.itemId});
                        const int have = qr.empty() ? 0 : qr[0]["qty"].as<int>(0);
                        static_cast<void>(database_.execChecked(txn, "update_player_inventory_quantity",
                            {(int)(have + rw.quantity), rowId, (int)characterId}));
                    }
                    else
                    {
                        static_cast<void>(database_.execChecked(txn, "insert_player_inventory_item",
                            {(int)characterId, (int)rw.itemId, (int)rw.quantity}));
                    }
                }
            }
            else if (rw.rewardType == "gold" && rw.amount > 0)
            {
                if (goldItemId == 0)
                {
                    pqxx::result gr = txn.exec("SELECT id FROM items WHERE slug = 'gold_coin' LIMIT 1");
                    if (!gr.empty())
                        goldItemId = gr[0]["id"].as<int>(0);
                }
                if (goldItemId > 0)
                {
                    const int64_t tok = static_cast<int64_t>(rw.syncToken);
                    const int qty = static_cast<int>(rw.amount);
                    if (tok != 0)
                    {
                        auto found = database_.execChecked(txn, "get_inventory_id_by_token", {(int)characterId, tok});
                        if (!found.empty())
                        {
                            const int rowId = found[0]["id"].as<int>(0);
                            static_cast<void>(database_.execChecked(txn, "update_player_inventory_quantity",
                                {qty, rowId, (int)characterId}));
                        }
                        else
                        {
                            auto adopted = database_.execChecked(txn, "adopt_legacy_inventory_row",
                                {qty, tok, (int)characterId, goldItemId});
                            if (adopted.empty())
                                static_cast<void>(database_.execChecked(txn, "insert_player_inventory_with_token",
                                    {(int)characterId, goldItemId, qty, tok}));
                        }
                    }
                    else
                    {
                        auto qr = database_.execChecked(txn, "get_inventory_qty", {(int)characterId, goldItemId});
                        const int have = qr.empty() ? 0 : qr[0]["qty"].as<int>(0);
                        auto legacy = database_.execChecked(txn, "get_inventory_id_by_char_item", {(int)characterId, goldItemId});
                        if (!legacy.empty())
                        {
                            const int rowId = legacy[0]["id"].as<int>(0);
                            static_cast<void>(database_.execChecked(txn, "update_player_inventory_quantity",
                                {(int)(have + qty), rowId, (int)characterId}));
                        }
                        else
                        {
                            static_cast<void>(database_.execChecked(txn, "insert_player_inventory_item",
                                {(int)characterId, goldItemId, qty}));
                        }
                    }
                }
            }
            else if (rw.rewardType == "exp" && rw.amount > 0)
            {
                // Exp is additive: apply here so a duplicate turn-in cannot double it
                // (state guard above already rejected duplicates).
                txn.exec("UPDATE characters SET experience_points = experience_points + " +
                    txn.quote(static_cast<long long>(rw.amount)) + " WHERE id = " + txn.quote(characterId));
            }
        }

        static_cast<void>(database_.execChecked(txn, "upsert_player_quest",
            {(int)characterId, (int)questId, std::string("turned_in"), (int)currentStep, progressJson}));
        txn.commit();
        out.ok = true;
        out.reason = "applied";
        log_->info("[DQM] claimQuestTurnIn applied char=" + std::to_string(characterId) +
                   " quest=" + std::to_string(questId));
        return out;
    }
    catch (const std::exception &ex)
    {
        logger_.logError("[DQM] claimQuestTurnIn error: " + std::string(ex.what()));
        out.ok = false;
        out.reason = "error";
        return out;
    }
}
