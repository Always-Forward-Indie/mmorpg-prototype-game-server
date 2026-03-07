#ifndef DATABASE_HPP
#define DATABASE_HPP

#include "utils/Config.hpp"
#include "utils/Logger.hpp"
#include <memory>
#include <mutex>
#include <pqxx/pqxx>
#include <variant>

class Database
{
  public:
    // Constructor
    Database(std::tuple<DatabaseConfig, GameServerConfig> &configs, Logger &logger);

    // Establish a database connection
    void connect(std::tuple<DatabaseConfig, GameServerConfig> &configs);

    // Prepare default queries
    void prepareDefaultQueries();

    /// CRITICAL-6 fix: RAII wrapper that holds the DB mutex for the lifetime of a transaction.
    /// Usage:
    ///   auto sc = db.getConnectionLocked();
    ///   pqxx::work txn(sc.get());
    ///   ...
    ///   txn.commit();  // sc goes out of scope — mutex released
    struct ScopedConnection
    {
        std::unique_lock<std::mutex> lock;
        pqxx::connection &conn;
        /// Construct by locking mutex from scratch (original path)
        ScopedConnection(std::mutex &m, pqxx::connection &c) : lock(m), conn(c) {}
        /// Construct with an already-owned lock (HIGH-10 reconnect path)
        ScopedConnection(std::unique_lock<std::mutex> l, pqxx::connection &c) : lock(std::move(l)), conn(c) {}
        ScopedConnection(ScopedConnection &&) = default;
        pqxx::connection &get()
        {
            return conn;
        }
    };
    ScopedConnection getConnectionLocked();

    /// Legacy accessor — NOT thread-safe when used directly for transactions.
    /// Kept for prepareDefaultQueries() which runs single-threaded at startup.
    pqxx::connection &getConnection();

    // Handle database connection or query errors
    void handleDatabaseError(const std::exception &e);
    // Execute a query with a transaction
    pqxx::result executeQueryWithTransaction(
        pqxx::work &transaction,
        const std::string &preparedQueryName,
        const std::vector<std::variant<int, float, double, std::string>> &parameters);

  private:
    // Database connection
    std::unique_ptr<pqxx::connection> connection_;
    /// HIGH-10: connection string stored so getConnectionLocked() can reconnect
    std::string connectionString_;
    /// CRITICAL-6: serialises concurrent pqxx::work transactions on the single connection
    mutable std::mutex dbMutex_;
    // Logger
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
};

#endif // DATABASE_HPP
