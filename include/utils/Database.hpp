#ifndef DATABASE_HPP
#define DATABASE_HPP

#include "utils/Config.hpp"
#include "utils/DatabasePool.hpp"
#include "utils/Logger.hpp"
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <pqxx/pqxx>
#include <variant>

class Database
{
  public:
    // Constructor
    Database(std::tuple<DatabaseConfig, GameServerConfig> &configs, Logger &logger);

    // Establish the connection pool (called once from the constructor)
    void connect(std::tuple<DatabaseConfig, GameServerConfig> &configs);

    // Register prepared statements on one connection. Static so the pool can
    // run it on every (re)opened connection (prepared statements are
    // per-connection in pqxx).
    static void prepareQueriesOn(pqxx::connection &conn);

    /// RAII wrapper that holds one pooled connection for the lifetime of a
    /// transaction. Same call pattern as before (getConnectionLocked):
    ///   auto sc = db.getConnectionLocked();
    ///   pqxx::work txn(sc.get());
    ///   ...
    ///   txn.commit();  // sc goes out of scope — slot released to the pool
    struct ScopedConnection
    {
        DatabasePool::Guard guard;
        explicit ScopedConnection(DatabasePool::Guard &&g) : guard(std::move(g)) {}
        ScopedConnection(ScopedConnection &&) = default;
        pqxx::connection &get()
        {
            return guard.get();
        }
    };
    ScopedConnection getConnectionLocked();

    // Pool stats for monitoring (contention visibility).
    size_t dbPoolSize() const;
    size_t dbPoolInUse() const;
    uint64_t dbPoolTimeouts() const;
    uint64_t dbPoolReconnects() const;

    // Handle database connection or query errors
    void handleDatabaseError(const std::exception &e);
    // Execute a query with a transaction
    pqxx::result executeQueryWithTransaction(
        pqxx::work &transaction,
        const std::string &preparedQueryName,
        const std::vector<std::variant<int, int64_t, float, double, std::string>> &parameters);

  private:
    static int poolSizeFromEnv();
    // Connection pool (replaces the single serialized connection).
    std::unique_ptr<DatabasePool> pool_;
    // Logger
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
};

#endif // DATABASE_HPP
