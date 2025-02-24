#include "utils/Scheduler.hpp"

Scheduler::Scheduler()
    : stopFlag(false) {}

void Scheduler::start()
{
    thread = std::thread(&Scheduler::run, this);
}

void Scheduler::stop()
{
    stopFlag.store(true);
    if (thread.joinable())
    {
        thread.join();
    }
}

Scheduler::~Scheduler()
{
    stop();
}

void Scheduler::scheduleTask(const Task &task)
{
    std::lock_guard<std::mutex> lock(mutex);
    
    // Check if the task is already in the list
    if (std::none_of(tasks.begin(), tasks.end(), [&](const Task &t) { return t.id == task.id; }))
    {
        tasks.push_back(task);
    }
}

void Scheduler::removeTask(int id)
{
    std::lock_guard<std::mutex> lock(mutex);
    for (auto &task : tasks)
    {
        if (task.id == id)
        {
            task.stopFlag = true; // mark the task for deletion
        }
    }
}

void Scheduler::run()
{
    while (!stopFlag.load())
    {
        auto now = std::chrono::system_clock::now();
        std::vector<Task> tasksToRun;

        {
            std::lock_guard<std::mutex> lock(mutex);
            
            // remove tasks that need to be stopped
            tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
                                       [](const Task &task) { return task.stopFlag; }),
                        tasks.end());

            // copy tasks that need to be run
            for (auto &task : tasks)
            {
                if (now >= task.nextRunTime)
                {
                    tasksToRun.push_back(task); // Копируем задачу для выполнения
                    task.nextRunTime = now + std::chrono::seconds(task.interval);
                }
            }
        }

        // run the tasks
        for (auto &task : tasksToRun)
        {
            task.func();
        }

        // sleep until the next task
        std::chrono::milliseconds sleep_time(100);
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!tasks.empty())
            {
                auto nextRunTime = std::min_element(tasks.begin(), tasks.end(),
                                                    [](const Task &a, const Task &b) {
                                                        return a.nextRunTime < b.nextRunTime;
                                                    })->nextRunTime;
                sleep_time = std::chrono::duration_cast<std::chrono::milliseconds>(nextRunTime - now);
            }
        }

        std::this_thread::sleep_for(std::max(sleep_time, std::chrono::milliseconds(1)));
    }
}

