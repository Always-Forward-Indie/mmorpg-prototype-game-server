#pragma once

#include <iostream>
#include <mutex>


class Logger {
public:
    void log(const std::string& message); 
};