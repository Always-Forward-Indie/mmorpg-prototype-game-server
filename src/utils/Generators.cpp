#include "utils/Generators.hpp"
#include "utils/RandomUtils.hpp"

// NOTE: rand() is neither thread-safe nor seeded here (no srand call
// anywhere); both helpers run on ThreadPool workers. RandomUtils gives each
// thread its own seeded mt19937. Key formula itself is unchanged (1-1).

long long Generators::generateUniqueTimeBasedKey(int keyId) {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    long long key = now_ms * 100 + keyId + RandomUtils::rangeInt(0, 99999);

    return key;
}

int Generators::generateSimpleRandomNumber(int min, int max) {
    return RandomUtils::rangeInt(min, max);
}