#include "utils/Scheduler.hpp"

Scheduler::Scheduler()
    : stopFlag(false),
      tasks(),
      thread(),
      mutex()
{
}

void Scheduler::start()
{
    thread = std::thread(&Scheduler::run, this);
}

void Scheduler::scheduleTask(const Task &task)
{
    std::lock_guard<std::mutex> lock(mutex);
    tasks.push_back(task);
}

void Scheduler::stop()
{
    stopFlag = true;
    if (thread.joinable())
    {
        thread.join();
    }
}

Scheduler::~Scheduler()
{
    stop();
}

void Scheduler::run()
{
    while (!stopFlag)
    {
        auto now = std::chrono::system_clock::now();
        std::lock_guard<std::mutex> lock(mutex);
        for (auto &task : tasks)
        {
            if (now >= task.nextRunTime)
            {
                task.func();
                task.nextRunTime = now + std::chrono::seconds(task.interval);
            }
        }
        // Optionally include a small delay or yield to prevent the loop from consuming too much CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
