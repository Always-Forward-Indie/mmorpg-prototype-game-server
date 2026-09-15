#pragma once

#include <pqxx/pqxx>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>
#include "utils/Config.hpp"
#include "utils/Logger.hpp"

/// Connection pool for game-server (v2).
/// Replaces the single serialized pqxx::connection: concurrent transactions
/// run on separate connections instead of queueing on one mutex.
///
/// Design notes (why not a copy of login's pool):
/// - Guards hold a slot INDEX, not a raw pointer, so a dead connection can
///   be transparently replaced (reconnect) without invalidating holders.
/// - Health-checked on acquire: a dropped connection is reopened (with
///   prepared statements re-registered) instead of handed out broken.
/// - Prepare logic is injected as a callback: the pool never includes
///   Database.hpp (no header cycle).
/// - Contended-acquire and timeout counters for monitoring.
class DatabasePool
{
  public:
    /// RAII guard — holds one pooled connection slot; returns it on destruction.
    /// Non-copyable, movable.
    class Guard
    {
      public:
        Guard() : pool_(nullptr), slot_(static_cast<size_t>(-1)) {}
        Guard(DatabasePool &pool, size_t slot) : pool_(&pool), slot_(slot) {}
        ~Guard()
        {
            if (pool_)
                pool_->release(slot_);
        }
        Guard(Guard &&o) noexcept : pool_(o.pool_), slot_(o.slot_)
        {
            o.pool_ = nullptr;
        }
        Guard(const Guard &) = delete;
        Guard &operator=(const Guard &) = delete;

        pqxx::connection &get() { return pool_->connectionAt(slot_); }

      private:
        DatabasePool *pool_;
        size_t slot_;
    };

    using PrepareFn = std::function<void(pqxx::connection &)>;

    /// @param cfg      database config (host, port, dbname, user, password)
    /// @param logger   logger reference
    /// @param poolSize number of connections (clamped to [1, 20])
    /// @param prepare  runs prepared-statement registration on every
    ///                 (re)opened connection
    DatabasePool(const DatabaseConfig &cfg, Logger &logger, int poolSize,
        PrepareFn prepare);
    ~DatabasePool() = default;

    DatabasePool(const DatabasePool &) = delete;
    DatabasePool &operator=(const DatabasePool &) = delete;

    /// Acquire a slot. Blocks until one is available or timeout expires.
    /// Throws std::runtime_error on timeout or when no healthy connection
    /// could be (re)opened.
    Guard acquire(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    size_t size() const { return connections_.size(); }
    size_t inUse() const;
    uint64_t acquireTimeouts() const { return timeouts_.load(std::memory_order_relaxed); }
    uint64_t reconnects() const { return reconnects_.load(std::memory_order_relaxed); }

  private:
    void release(size_t slot);
    pqxx::connection &connectionAt(size_t slot) { return *connections_.at(slot); }
    /// (Re)open slot: fresh connection + prepared statements. Throws on failure.
    void openSlot(size_t slot);

    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
    std::string connStr_;
    PrepareFn prepare_;
    std::vector<std::unique_ptr<pqxx::connection>> connections_;
    std::queue<size_t> available_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<uint64_t> timeouts_{0};
    std::atomic<uint64_t> reconnects_{0};
};
