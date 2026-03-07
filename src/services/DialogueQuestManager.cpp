#include "services/DialogueQuestManager.hpp"
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

    auto dialogues = database_.executeQueryWithTransaction(txn, "get_dialogues", {});
    auto nodes = database_.executeQueryWithTransaction(txn, "get_dialogue_nodes", {});
    auto edges = database_.executeQueryWithTransaction(txn, "get_dialogue_edges", {});
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
    auto mappings = database_.executeQueryWithTransaction(txn, "get_npc_dialogue_mappings", {});
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

    auto quests = database_.executeQueryWithTransaction(txn, "get_quests", {});
    auto steps = database_.executeQueryWithTransaction(txn, "get_quest_steps", {});
    auto rewards = database_.executeQueryWithTransaction(txn, "get_quest_rewards", {});
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
    auto result = database_.executeQueryWithTransaction(txn, "get_player_quests", {(int)characterId});
    txn.commit();

    nlohmann::json questsJson = nlohmann::json::array();
    for (const auto &row : result)
    {
        nlohmann::json q;
        q["questId"] = row["quest_id"].as<long long>();
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
    auto result = database_.executeQueryWithTransaction(txn, "get_player_flags", {(int)characterId});
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
        database_.executeQueryWithTransaction(txn, "upsert_player_quest", {(int)characterId, (int)questId, state, (int)currentStep, progressJson});
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
DialogueQuestManager::savePlayerFlag(int characterId, const std::string &flagKey, int intValue, bool boolValue)
{
    try
    {
        auto _dbConn = database_.getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        database_.executeQueryWithTransaction(txn, "upsert_player_flag", {(int)characterId, flagKey, (int)intValue, std::string(boolValue ? "true" : "false")});
        txn.commit();
        log_->info("[DQM] Saved flag characterId=" + std::to_string(characterId) +
                        " key=" + flagKey);
    }
    catch (const std::exception &ex)
    {
        logger_.logError("[DQM] savePlayerFlag error: " + std::string(ex.what()));
    }
}
