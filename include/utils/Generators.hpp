#pragma once
#include <chrono>

class Generators
{
public:
    // Thread-safe: delegates to RandomUtils (per-thread seeded mt19937).
    // generate unique time based key
    static long long generateUniqueTimeBasedKey(int zoneId);
    // generate ranmdom number between min and max
    static int generateSimpleRandomNumber(int min, int max);
};