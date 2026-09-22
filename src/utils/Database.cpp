#include "utils/Database.hpp"
#include "utils/Config.hpp"
#include "utils/QueryRegistry.hpp"
#include <iostream>
#include <spdlog/logger.h>

int
Database::poolSizeFromEnv()
{
    // DB_POOL_SIZE, default 5 (matches login), clamped to [1, 20].
    // Well under PostgreSQL max_connections=100 (login 5 + game 5 + margin).
    int n = 5;
    if (const char *env = std::getenv("DB_POOL_SIZE"))
    {
        try
        {
            n = std::stoi(env);
        }
        catch (...)
        {
            n = 5;
        }
    }
    if (n < 1)
        n = 1;
    if (n > 20)
        n = 20;
    return n;
}

Database::Database(std::tuple<DatabaseConfig, GameServerConfig> &configs, Logger &logger)
    : logger_(logger)
{
    log_ = logger.getSystem("db");
    connect(configs);
}

void
Database::connect(std::tuple<DatabaseConfig, GameServerConfig> &configs)
{
    // Wait-for-db: Postgres may still be starting (host reboot, cold volume).
    // createWithRetry throws past DB_CONNECT_TIMEOUT_SEC and we let it
    // propagate — fail-FAST by design (main exits 1, orchestrator restarts
    // us). The old swallow-and-continue left pool_ null and every query
    // failing with "not initialized": silent degradation with no restart.
    const DatabaseConfig &dbcfg = std::get<0>(configs);
    log_->info("Opening database pool...");
    pool_ = DatabasePool::createWithRetry(dbcfg, logger_, poolSizeFromEnv(),
        [](pqxx::connection &conn)
        { Database::prepareQueriesOn(conn); },
        DatabasePool::connectTimeoutFromEnv());
    log_->info("Database pool established (" +
               std::to_string(pool_->size()) + " connections).");
}

void
Database::prepareQueriesOn(pqxx::connection &conn)
{
    if (!conn.is_open())
    {
        throw std::runtime_error("prepareQueriesOn: connection is not open.");
    }
    {
        // Registration order is preserved (see QueryRegistry).
        for (const QueryDef *q = queryRegistryBegin(); q != queryRegistryEnd(); ++q)
            conn.prepare(std::string(q->name), std::string(q->sql));
    }
}
// Returns a pooled connection for the lifetime of the caller's transaction.
// Replaces the old single-connection + mutex design (CRITICAL-6) and the
// inline reconnect (HIGH-10): the pool health-checks on acquire and reopens
// dropped connections (with prepared statements re-registered).
Database::ScopedConnection
Database::getConnectionLocked()
{
    if (!pool_)
        throw std::runtime_error("Database pool is not initialized.");
    return ScopedConnection(pool_->acquire());
}

size_t
Database::dbPoolSize() const
{
    return pool_ ? pool_->size() : 0;
}

size_t
Database::dbPoolInUse() const
{
    return pool_ ? pool_->inUse() : 0;
}

uint64_t
Database::dbPoolTimeouts() const
{
    return pool_ ? pool_->acquireTimeouts() : 0;
}

uint64_t
Database::dbPoolReconnects() const
{
    return pool_ ? pool_->reconnects() : 0;
}

// Function to handle database errors
void
Database::handleDatabaseError(const std::exception &e)
{
    // Handle database connection or query errors
    logger_.logError("Database error: " + std::string(e.what()));
}

// Fail-loud variant: throws DatabaseError (query-name context) instead of
// returning an empty result. Aborts the passed transaction before throwing.
pqxx::result
Database::execChecked(
    pqxx::work &transaction,
    const std::string &preparedQueryName,
    const std::vector<std::variant<int, int64_t, float, double, std::string>> &parameters)
{
    try
    {
        // Convert all parameters to strings
        std::vector<std::string> paramStrings;
        for (const auto &param : parameters)
        {
            paramStrings.push_back(std::visit([](const auto &value) -> std::string
                {
                if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::string>)
                    return value;
                else
                    return std::to_string(value); },
                param));
        }

        // Convert to a vector of raw C-style strings (needed for exec_prepared)
        std::vector<const char *> cstrParams;
        for (auto &param : paramStrings)
        {
            cstrParams.push_back(param.c_str());
        }

        // Use the parameter pack expansion to pass all arguments dynamically
        return transaction.exec_prepared(preparedQueryName, pqxx::prepare::make_dynamic_params(cstrParams.begin(), cstrParams.end()));
    }
    catch (const std::exception &e)
    {
        transaction.abort(); // Rollback transaction
        handleDatabaseError(e);
        // Fail loud: an empty pqxx::result is a valid "no rows" answer and
        // must never double as an error signal. Callers must not commit after.
        throw DatabaseError("execChecked[" + preparedQueryName + "]: " + e.what());
    }
}
