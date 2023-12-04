#pragma once
#include <iostream>
#include <thread>
#include <chrono>
#include <functional>
#include <vector>
#include <mutex>
#include "data/SpecialStructs.hpp"

class Scheduler
{
public:
    Scheduler();
    ~Scheduler();
    
    void start();
    void scheduleTask(const Task &task);
    void stop();

private:
    std::vector<Task> tasks;
    std::thread thread;
    std::mutex mutex;
    bool stopFlag;

    void run();
};