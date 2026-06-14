#include "utils/Config.hpp"

namespace {

const char* getEnvOrDefault(const char* name, const char* defaultVal) {
    const char* env = std::getenv(name);
    return (env && env[0] != '\0') ? env : defaultVal;
}

}

std::tuple<DatabaseConfig, GameServerConfig> Config::parseConfig() {
    DatabaseConfig DBConfig;
    DBConfig.dbname   = getEnvOrDefault("DB_NAME", "mmo_prototype");
    DBConfig.user     = getEnvOrDefault("DB_USER", "postgres");
    DBConfig.password = getEnvOrDefault("DB_PASSWORD", "");
    DBConfig.host     = getEnvOrDefault("DB_HOST", "127.0.0.1");
    DBConfig.port     = static_cast<short>(std::stoi(getEnvOrDefault("DB_PORT", "5432")));

    GameServerConfig GSConfig;
    GSConfig.host        = getEnvOrDefault("SERVER_HOST", "0.0.0.0");
    GSConfig.port        = static_cast<short>(std::stoi(getEnvOrDefault("SERVER_PORT", "27016")));
    GSConfig.max_clients = static_cast<short>(std::stoi(getEnvOrDefault("SERVER_MAX_CLIENTS", "3000")));

    return std::make_tuple(DBConfig, GSConfig);
}
