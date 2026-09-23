#include "services/GameConfigService.hpp"
#include <stdexcept>
#include <spdlog/logger.h>

GameConfigService::GameConfigService(Logger &logger)
    : logger_(logger)
{
    log_ = logger.getSystem("config");
}

void
GameConfigService::loadConfig(Database &db)
{
    try
    {
        auto _dbConn = db.getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        pqxx::result rows = db.execChecked(txn, "get_game_config", {});
        if (rows.empty())
        {
            // Empty table must not wipe the last good config (fail-safe like NPC/Item loaders).
            logger_.logError("GameConfigService::loadConfig: game_config table is empty, keeping previous config");
            txn.abort();
            return;
        }
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
GameConfigService::reload(Database &db)
{
    log_->info("GameConfigService: reloading config from database...");
    loadConfig(db);
}

std::unordered_map<std::string, std::string>
GameConfigService::getAll() const
{
    std::shared_lock lock(mutex_);
    return config_; // copy — лок держится только на время копирования
}

int
GameConfigService::getInt(const std::string &key, int fallback) const
{
    std::shared_lock lock(mutex_);
    auto it = config_.find(key);
    if (it == config_.end())
        return fallback;
    try
    {
        return std::stoi(it->second);
    }
    catch (const std::exception &)
    {
        return fallback;
    }
}

void
GameConfigService::setConfig(const std::unordered_map<std::string, std::string> &config)
{
    std::unique_lock lock(mutex_);
    config_ = config;
}
