#include "utils/ThreadPool.hpp"
#include <cstdio>
#include <stdexcept>

ThreadPool::ThreadPool(size_t numThreads)
{
    for (size_t i = 0; i < numThreads; ++i)
    {
        workers.emplace_back([this]() {
            while (true)
            {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->queueMutex);
                    condition.wait(lock, [this]() { return stop.load() || !tasks.empty(); });
                    if (stop.load() && tasks.empty())
                        return;
                    task = std::move(tasks.front());
                    tasks.pop();
                }
                // A throwing task must never kill the worker thread (that
                // would silently shrink the pool). Callers already wrap their
                // batches; this is defense in depth (same rule as Scheduler).
                try
                {
                    task();
                }
                catch (const std::exception &e)
                {
                    std::fprintf(stderr, "[ThreadPool] task threw: %s\n", e.what());
                }
                catch (...)
                {
                    std::fprintf(stderr, "[ThreadPool] task threw unknown exception\n");
                }
            }
        });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stop.store(true);
    }
    condition.notify_all();
    for (std::thread &worker : workers)
        worker.join();
}

void ThreadPool::enqueueTask(std::function<void()> task)
{
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        if (stop.load())
            throw std::runtime_error("enqueue on stopped ThreadPool");
        tasks.push(std::move(task));
    }
    condition.notify_one();
}
