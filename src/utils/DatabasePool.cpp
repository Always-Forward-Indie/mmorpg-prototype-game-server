#include "utils/DatabasePool.hpp"
#include <spdlog/logger.h>

DatabasePool::DatabasePool(const DatabaseConfig &cfg, Logger &logger, int poolSize,
    PrepareFn prepare)
    : logger_(logger), prepare_(std::move(prepare))
{
    log_ = logger.getSystem("db");
    if (poolSize < 1)
        poolSize = 1;
    if (poolSize > 20)
        poolSize = 20;
    connStr_ = "dbname=" + cfg.dbname +
               " user=" + cfg.user +
               " password=" + cfg.password +
               " host=" + cfg.host +
               " port=" + std::to_string(cfg.port);

    logger_.log("[DatabasePool] Opening " + std::to_string(poolSize) + " connections to " +
                cfg.host + ":" + std::to_string(cfg.port) + "/" + cfg.dbname);
    connections_.reserve(static_cast<size_t>(poolSize));
    for (int i = 0; i < poolSize; ++i)
    {
        connections_.push_back(nullptr);
        openSlot(static_cast<size_t>(i));
        available_.push(static_cast<size_t>(i));
    }
    log_->info("[DatabasePool] All " + std::to_string(poolSize) + " connections ready.");
}

void
DatabasePool::openSlot(size_t slot)
{
    auto conn = std::make_unique<pqxx::connection>(connStr_);
    if (!conn->is_open())
        throw std::runtime_error("Connection failed to open: " + connStr_);
    // Prepared statements live per-connection: (re)register every time.
    prepare_(*conn);
    connections_[slot] = std::move(conn);
}

DatabasePool::Guard
DatabasePool::acquire(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [this]
                      { return !available_.empty(); }))
    {
        timeouts_.fetch_add(1, std::memory_order_relaxed);
        throw std::runtime_error("[DatabasePool] acquire() timed out: pool exhausted after " +
                                 std::to_string(timeout.count()) + "ms. Consider increasing pool size.");
    }
    const size_t slot = available_.front();
    available_.pop();
    // Health check + transparent reconnect (HIGH-10 equivalent for pooled use).
    bool healthy = false;
    try
    {
        healthy = connections_[slot] && connections_[slot]->is_open();
    }
    catch (...)
    {
        healthy = false;
    }
    if (!healthy)
    {
        try
        {
            log_->warn("[DatabasePool] Reopening dropped connection (slot " +
                       std::to_string(slot) + ")");
            openSlot(slot);
            reconnects_.fetch_add(1, std::memory_order_relaxed);
        }
        catch (const std::exception &e)
        {
            // Put the slot back so another waiter can retry; surface the error.
            available_.push(slot);
            cv_.notify_one();
            throw std::runtime_error(std::string("[DatabasePool] reconnect failed: ") + e.what());
        }
    }
    return Guard(*this, slot);
}

void
DatabasePool::release(size_t slot)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        available_.push(slot);
    }
    cv_.notify_one();
}

size_t
DatabasePool::inUse() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size() - available_.size();
}
