#pragma once

#include <iostream>
#include <thread>
#include <chrono>
#include <functional>
#include <vector>
#include <mutex>
#include <atomic>
#include <algorithm>
#include "data/SpecialStructs.hpp"

class Scheduler
{
public:
    Scheduler();
    ~Scheduler();

    void start();
    void stop();
    void scheduleTask(const Task &task);
    void removeTask(int id); // Добавлен метод для удаления задачи

private:
    void run();

    std::vector<Task> tasks;
    std::thread thread;
    std::mutex mutex;
    std::atomic<bool> stopFlag;
};
