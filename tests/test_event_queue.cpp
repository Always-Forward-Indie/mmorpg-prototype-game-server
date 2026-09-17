// Unit tests for game EventQueue: FIFO order, batching, stop() wake-up.
#include "events/Event.hpp"
#include "events/EventQueue.hpp"

#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace
{

Event makeEvent(Event::EventType type, int clientId, int payload)
{
    return Event(type, clientId, EventData{payload}, nullptr);
}

int payloadOf(const Event &e)
{
    return std::get<int>(e.getData());
}

} // namespace

TEST(EventQueue, PushPopSinglePreservesContent)
{
    EventQueue q;
    EXPECT_TRUE(q.empty());
    q.push(makeEvent(Event::PING_CLIENT, 7, 42));
    EXPECT_FALSE(q.empty());

    Event out(Event::PING_CLIENT, 0, EventData{0}, nullptr);
    EXPECT_TRUE(q.pop(out));
    EXPECT_EQ(out.getType(), Event::PING_CLIENT);
    EXPECT_EQ(out.getClientID(), 7);
    EXPECT_EQ(payloadOf(out), 42);
    EXPECT_TRUE(q.empty());
}

TEST(EventQueue, FifoOrder)
{
    EventQueue q;
    for (int i = 0; i < 5; ++i)
        q.push(makeEvent(Event::MOVE_CHARACTER, i, 100 + i));

    std::vector<Event> batch;
    EXPECT_TRUE(q.popBatch(batch, 10));
    ASSERT_EQ(batch.size(), 5u);
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_EQ(batch[i].getClientID(), i);
        EXPECT_EQ(payloadOf(batch[i]), 100 + i);
    }
    EXPECT_TRUE(q.empty());
}

TEST(EventQueue, PopBatchRespectsBatchSize)
{
    EventQueue q;
    for (int i = 0; i < 5; ++i)
        q.push(makeEvent(Event::GET_MOBS_LIST, i, i));

    std::vector<Event> first;
    EXPECT_TRUE(q.popBatch(first, 3));
    EXPECT_EQ(first.size(), 3u);

    std::vector<Event> rest;
    EXPECT_TRUE(q.popBatch(rest, 10));
    EXPECT_EQ(rest.size(), 2u);
    EXPECT_TRUE(q.empty());
}

TEST(EventQueue, PopOnStoppedEmptyQueueReturnsFalse)
{
    EventQueue q;
    EXPECT_FALSE(q.isStopped());
    q.stop();
    EXPECT_TRUE(q.isStopped());
    Event out(Event::PING_CLIENT, 0, EventData{0}, nullptr);
    EXPECT_FALSE(q.pop(out));
}

TEST(EventQueue, BlockedPopWakesUpOnStop)
{
    EventQueue q;
    bool result = true;
    std::thread consumer([&]
        {
            Event out(Event::PING_CLIENT, 0, EventData{0}, nullptr);
            result = q.pop(out);
        });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    q.stop();
    consumer.join();
    EXPECT_FALSE(result);
}
