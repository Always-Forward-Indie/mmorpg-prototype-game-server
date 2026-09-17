// Unit tests for game ThreadPool (void + future tasks).
#include "utils/ThreadPool.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>

TEST(ThreadPool, RunsVoidTasks)
{
    ThreadPool pool(2);
    std::atomic<int> counter{0};
    for (int i = 0; i < 20; ++i)
        pool.enqueueTask([&] { ++counter; });
    for (int i = 0; i < 200 && counter.load() < 20; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(counter.load(), 20);
}

TEST(ThreadPool, FutureTasksReturnValues)
{
    ThreadPool pool(2);
    auto f = pool.enqueueTask([](int x) { return x * 2; }, 21);
    EXPECT_EQ(f.get(), 42);
}

TEST(ThreadPool, ThrowingTaskDoesNotKillWorker)
{
    // Wave 1.8: an uncaught task exception used to propagate out of the worker
    // thread (std::terminate) or silently shrink the pool. Now the worker
    // logs to stderr and keeps serving.
    ThreadPool pool(2);
    pool.enqueueTask([] { throw std::runtime_error("boom"); });
    pool.enqueueTask([] { throw 42; });
    std::atomic<int> counter{0};
    for (int i = 0; i < 20; ++i)
        pool.enqueueTask([&] { ++counter; });
    for (int i = 0; i < 200 && counter.load() < 20; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(counter.load(), 20);
}
