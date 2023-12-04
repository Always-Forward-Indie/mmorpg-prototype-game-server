#pragma once

#include <chrono>
#include <functional>

struct Task {
    std::function<void()> func;
    int interval; // in seconds
    std::chrono::time_point<std::chrono::system_clock> nextRunTime;
};