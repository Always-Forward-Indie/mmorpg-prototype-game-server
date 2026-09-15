#include "events/EventQueue.hpp"

void EventQueue::push(const Event &event)
{
    std::unique_lock<std::mutex> lock(mtx);
    queue.push(event);
    cv.notify_one();
}

bool EventQueue::pop(Event &event)
{
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this] { return !queue.empty() || stopped_.load(std::memory_order_relaxed); });
    if (queue.empty())
        return false; // stopped with no pending events

    event = std::move(queue.front());
    queue.pop();
    return true;
}

void EventQueue::pushBatch(const std::vector<Event>& events) 
{
    std::unique_lock<std::mutex> lock(mtx);
    for (const auto& event : events) {
        queue.push(std::move(event));
    }
    cv.notify_all();
}

// bool EventQueue::popBatch(std::vector<Event>& events, int batchSize) 
// {
//     std::unique_lock<std::mutex> lock(mtx);
//     cv.wait(lock, [this] { return !queue.empty(); }); // Ждем, пока в очереди появятся данные

//     while (!queue.empty() && batchSize > 0) {
//         events.push_back(std::move(queue.front())); 
//         queue.pop();
//         batchSize--;
//     }

//     return !events.empty();
// }

bool EventQueue::popBatch(std::vector<Event>& events, int batchSize)
{
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this] { return !queue.empty() || stopped_.load(std::memory_order_relaxed); });
    if (queue.empty())
        return false; // stopped

    int actualSize = std::min(batchSize, static_cast<int>(queue.size()));
    events.reserve(events.size() + actualSize);

    while (!queue.empty() && batchSize > 0) {
        events.push_back(std::move(queue.front())); 
        queue.pop();
        batchSize--;
    }

    return !events.empty();
}



bool EventQueue::empty()
{
    std::unique_lock<std::mutex> lock(mtx);
    return queue.empty();
}

void EventQueue::stop()
{
    stopped_.store(true, std::memory_order_release);
    cv.notify_all(); // wake all blocked pop/popBatch callers
}