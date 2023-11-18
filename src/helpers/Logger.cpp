#include "helpers/Logger.hpp"

std::mutex logger_mutex_;

void Logger::log(const std::string& message)
{
        std::lock_guard<std::mutex> lock(logger_mutex_);
        std::cout << message << std::endl;
}