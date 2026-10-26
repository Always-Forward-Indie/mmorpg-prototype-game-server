#pragma once
#include <atomic>
#include <queue>
#include <vector>
#include <mutex>
#include <condition_variable>
#include "Event.hpp"

class EventQueue {
public:
    void push(const Event& event);
    // Returns false when stopped and the queue is empty (lets consumer
    // threads exit cleanly on shutdown instead of hanging in cv.wait).
    bool pop(Event& event);

    void pushBatch(const std::vector<Event> &events);
    bool popBatch(std::vector<Event> &events, int batchSize);
    bool empty();

    /// Signal all blocked pop/popBatch callers to return false so that
    /// consumer threads can exit cleanly on shutdown (mirrors chunk).
    void stop();
    /// Drop everything still queued (S16a shutdown). Queued events may own
    /// sockets; the queue outlives NetworkManager's io_context at scope
    /// exit, so leftovers would destroy sockets after their service
    /// (ASan heap-use-after-free in asio internals). Call after all
    /// producers are joined and before returning from main.
    void drain()
    {
        std::vector<Event> tmp;
        while (popBatch(tmp, 1024))
            tmp.clear();
    }
    bool isStopped() const
    {
        return stopped_.load(std::memory_order_acquire);
    }

private:
    std::queue<Event> queue;
    std::mutex mtx;
    std::condition_variable cv;
    /// Set by stop(); wakes waiting consumers so they return false.
    std::atomic<bool> stopped_{false};
};
