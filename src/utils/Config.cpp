#include "utils/Config.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace {

const char *getEnvOrDefault(const char *name, const char *defaultVal) {
    const char *env = std::getenv(name);
    return (env && env[0] != '\0') ? env : defaultVal;
}

bool envPresent(const char *name) {
    const char *env = std::getenv(name);
    return env && env[0] != '\0';
}

std::uint16_t parsePort(const char *varName, const std::string &raw) {
    try {
        int v = std::stoi(raw);
        if (v < 1 || v > 65535)
            throw std::out_of_range("port out of range 1..65535");
        return static_cast<std::uint16_t>(v);
    } catch (const std::invalid_argument &) {
        throw std::runtime_error(std::string("Config: invalid integer in ") + varName + "='" + raw + "'");
    } catch (const std::out_of_range &) {
        throw std::runtime_error(std::string("Config: value out of range in ") + varName + "='" + raw + "'");
    }
}

int parsePositiveInt(const char *varName, const std::string &raw, int lo, int hi) {
    try {
        int v = std::stoi(raw);
        if (v < lo || v > hi)
            throw std::out_of_range("out of range");
        return v;
    } catch (const std::invalid_argument &) {
        throw std::runtime_error(std::string("Config: invalid integer in ") + varName + "='" + raw + "'");
    } catch (const std::out_of_range &) {
        throw std::runtime_error(std::string("Config: value out of range in ") + varName + "='" + raw +
                                 "' (expected " + std::to_string(lo) + ".." + std::to_string(hi) + ")");
    }
}

nlohmann::json loadJsonFile(const std::string &path) {
    std::ifstream f(path);
    if (!f.good())
        return nlohmann::json::object();
    try {
        nlohmann::json j;
        f >> j;
        if (!j.is_object())
            throw std::runtime_error("Config: '" + path + "' root must be a JSON object");
        return j;
    } catch (const nlohmann::json::exception &e) {
        throw std::runtime_error(std::string("Config: cannot parse '") + path + "': " + e.what());
    }
}

std::string jsonStr(const nlohmann::json &j, const char *key, const std::string &fallback) {
    if (j.contains(key) && j[key].is_string())
        return j[key].get<std::string>();
    return fallback;
}

} // namespace

std::tuple<DatabaseConfig, GameServerConfig> Config::parseConfig() {
    return parseConfig("config.json");
}

std::tuple<DatabaseConfig, GameServerConfig> Config::parseConfig(const std::string &jsonPath) {
    const nlohmann::json j = loadJsonFile(jsonPath);

    DatabaseConfig DBConfig;
    DBConfig.dbname = jsonStr(j, "db_name", "mmo_prototype");
    DBConfig.user = jsonStr(j, "db_user", "postgres");
    DBConfig.password = jsonStr(j, "db_password", "");
    DBConfig.host = jsonStr(j, "db_host", "127.0.0.1");
    DBConfig.port = 5432;
    if (j.contains("db_port") && j["db_port"].is_number_integer())
        DBConfig.port = parsePort("db_port(config.json)", std::to_string(j["db_port"].get<int>()));

    GameServerConfig GSConfig;
    GSConfig.host = jsonStr(j, "server_host", "0.0.0.0");
    GSConfig.port = 27016;
    GSConfig.max_clients = 3000;
    if (j.contains("server_port") && j["server_port"].is_number_integer())
        GSConfig.port = parsePort("server_port(config.json)", std::to_string(j["server_port"].get<int>()));
    if (j.contains("server_max_clients") && j["server_max_clients"].is_number_integer())
        GSConfig.max_clients =
            parsePositiveInt("server_max_clients(config.json)", std::to_string(j["server_max_clients"].get<int>()), 1, 100000);

    if (envPresent("DB_NAME"))
        DBConfig.dbname = getEnvOrDefault("DB_NAME", "mmo_prototype");
    if (envPresent("DB_USER"))
        DBConfig.user = getEnvOrDefault("DB_USER", "postgres");
    if (envPresent("DB_PASSWORD"))
        DBConfig.password = getEnvOrDefault("DB_PASSWORD", "");
    if (envPresent("DB_HOST"))
        DBConfig.host = getEnvOrDefault("DB_HOST", "127.0.0.1");
    if (envPresent("DB_PORT"))
        DBConfig.port = parsePort("DB_PORT", getEnvOrDefault("DB_PORT", "5432"));

    if (envPresent("SERVER_HOST"))
        GSConfig.host = getEnvOrDefault("SERVER_HOST", "0.0.0.0");
    if (envPresent("SERVER_PORT"))
        GSConfig.port = parsePort("SERVER_PORT", getEnvOrDefault("SERVER_PORT", "27016"));
    if (envPresent("SERVER_MAX_CLIENTS"))
        GSConfig.max_clients = parsePositiveInt("SERVER_MAX_CLIENTS", getEnvOrDefault("SERVER_MAX_CLIENTS", "3000"), 1, 100000);

    return std::make_tuple(DBConfig, GSConfig);
}
