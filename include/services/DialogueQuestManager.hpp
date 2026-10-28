#pragma once
#include "utils/Database.hpp"
#include "utils/Logger.hpp"
#include <nlohmann/json.hpp>
#include <string>

/**
 * @brief Handles DB queries for dialogue/quest systems.
 *        Returns JSON for EventHandler to serialize and send.
 */
class DialogueQuestManager
{
  public:
    DialogueQuestManager(Database &database, Logger &logger);

    // --- Static data (for chunk-server startup) ---
    nlohmann::json getAllDialoguesJson();
    nlohmann::json getAllNPCDialogueMappingsJson();
    nlohmann::json getAllQuestsJson();

    // --- Per-player data ---
    nlohmann::json getPlayerQuestsJson(int characterId);
    nlohmann::json getPlayerFlagsJson(int characterId);

    // --- Persistence ---
    void savePlayerQuestProgress(int characterId, int questId, const std::string &state, int currentStep, const std::string &progressJson);
    void deletePlayerQuestProgress(int characterId, int questId);
    void savePlayerFlag(int characterId, const std::string &flagKey, int intValue, bool boolValue);

    // --- Atomic one-time claims (AAA, single txn: guard + effects) ---
    struct ClaimGiftItem
    {
        int itemId = 0;
        int quantity = 0;
        uint64_t syncToken = 0;
    };
    struct ClaimResult
    {
        bool ok = false;
        std::string reason; // applied | already_claimed | not_completable | missing_items | invalid | error
    };
    // Dialogue gift: SELECT flag FOR UPDATE; if true -> already_claimed (no writes);
    // else INSERT inventory rows (token path) + upsert flag true, commit.
    ClaimResult claimDialogueGift(int characterId, const std::string &flagKey,
        const std::vector<ClaimGiftItem> &items, const std::string &factKey);
    struct ClaimQuestConsume
    {
        int itemId = 0;
        int count = 0;
    };
    struct ClaimQuestReward
    {
        std::string rewardType; // item | gold | exp
        int itemId = 0;
        int quantity = 0;
        int64_t amount = 0;
        uint64_t syncToken = 0; // for item/gold inserts
    };
    // Quest turn-in: SELECT quest FOR UPDATE; require state==completed;
    // check/consume collect items, grant rewards, set turned_in, commit.
    ClaimResult claimQuestTurnIn(int characterId, int questId,
        const std::vector<ClaimQuestConsume> &consume,
        const std::vector<ClaimQuestReward> &rewards, const std::string &factKey);

  private:
    Database &database_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
};
