#include "services/GameConfigService.hpp"
#include <stdexcept>
#include <spdlog/logger.h>

GameConfigService::GameConfigService(Database &db, Logger &logger)
    : db_(db), logger_(logger)
{
    log_ = logger.getSystem("config");
}

void
GameConfigService::loadConfig()
{
    try
    {
        auto _dbConn = db_.getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        pqxx::result rows = db_.executeQueryWithTransaction(txn, "get_game_config", {});
        txn.commit();

        std::unordered_map<std::string, std::string> newConfig;
        newConfig.reserve(rows.size());

        for (const auto &row : rows)
        {
            newConfig[row["key"].as<std::string>()] = row["value"].as<std::string>();
        }

        {
            std::unique_lock lock(mutex_);
            config_ = std::move(newConfig);
        }

        logger_.log("GameConfigService: loaded " + std::to_string(config_.size()) + " config entries.");
    }
    catch (const std::exception &e)
    {
        logger_.logError("GameConfigService::loadConfig error: " + std::string(e.what()));
    }
}

void
GameConfigService::reload()
{
    log_->info("GameConfigService: reloading config from database...");
    loadConfig();
}

std::unordered_map<std::string, std::string>
GameConfigService::getAll() const
{
    std::shared_lock lock(mutex_);
    return config_; // copy — лок держится только на время копирования
}
