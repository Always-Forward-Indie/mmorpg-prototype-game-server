#pragma once

#include "data/DataStructs.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>

struct Task
{
    std::function<void()> func;
    int interval; // Interval in seconds
    std::chrono::time_point<std::chrono::system_clock> nextRunTime;
    bool stopFlag = false; // Remove tasks flag
    int id;

    Task(std::function<void()> func, int interval, std::chrono::time_point<std::chrono::system_clock> startTime, int id)
        : func(std::move(func)), interval(interval), nextRunTime(startTime), id(id) {}
};

struct EventPayload
{
    ClientDataStruct clientData;
    ChunkInfoStruct chunkData;
    CharacterDataStruct characterData;
    PositionStruct positionData;
    MessageStruct messageStruct;
    std::string rawMessage; // raw message string, used by event-specific handlers (e.g. savePositions)
    // Single-parse: the wire json parsed once in MessageHandler, shared with
    // all handlers (read-only). Empty only on defensive paths — handlers fall
    // back to parsing rawMessage (see payloadJson in EventDispatcher.cpp).
    std::shared_ptr<const nlohmann::json> parsed;
};
