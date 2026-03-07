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
    void savePlayerFlag(int characterId, const std::string &flagKey, int intValue, bool boolValue);

  private:
    Database &database_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
};
