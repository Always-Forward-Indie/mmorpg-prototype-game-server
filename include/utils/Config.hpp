#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <tuple>

struct DatabaseConfig {
    std::string dbname;
    std::string user;
    std::string password;
    std::string host;
    std::uint16_t port = 5432;
};

struct GameServerConfig {
    std::string host;
    std::uint16_t port = 27016;
    int max_clients = 3000;
};

class Config {
public:
    /// Layered config: compiled defaults < config.json (if present) < ENV.
    /// Throws std::runtime_error with the variable name on invalid values.
    std::tuple<DatabaseConfig, GameServerConfig> parseConfig();
    /// Override for tests / explicit paths.
    std::tuple<DatabaseConfig, GameServerConfig> parseConfig(const std::string &jsonPath);
};
