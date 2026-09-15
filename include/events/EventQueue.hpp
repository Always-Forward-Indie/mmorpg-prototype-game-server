#pragma once
#include <atomic>
#include <queue>
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
