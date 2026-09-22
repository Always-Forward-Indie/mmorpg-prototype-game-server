// Unit tests for layered Config (defaults < config.json < ENV) + validation.
#include "utils/Config.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <unistd.h>

namespace {

void clearEnv() {
    unsetenv("DB_NAME");
    unsetenv("DB_USER");
    unsetenv("DB_PASSWORD");
    unsetenv("DB_HOST");
    unsetenv("DB_PORT");
    unsetenv("SERVER_HOST");
    unsetenv("SERVER_PORT");
    unsetenv("SERVER_MAX_CLIENTS");
}

struct EnvGuard {
    EnvGuard() { clearEnv(); }
    ~EnvGuard() { clearEnv(); }
};

std::string writeTempJson(const std::string &body) {
    char tmpl[] = "/tmp/mmo_game_cfg_XXXXXX";
    int fd = mkstemp(tmpl);
    EXPECT_NE(fd, -1);
    {
        std::ofstream f(tmpl);
        f << body;
    }
    close(fd);
    return std::string(tmpl);
}

} // namespace

TEST(Config, DefaultsWhenNoFileNoEnv) {
    EnvGuard g;
    Config c;
    auto [db, gs] = c.parseConfig("/tmp/mmo_cfg_definitely_missing_12345.json");
    EXPECT_EQ(db.port, 5432);
    EXPECT_EQ(gs.port, 27016);
    EXPECT_EQ(gs.max_clients, 3000);
}

TEST(Config, InvalidPortThrowsWithVarName) {
    EnvGuard g;
    setenv("SERVER_PORT", "bogus", 1);
    Config c;
    try {
        c.parseConfig("/tmp/mmo_cfg_definitely_missing_12345.json");
        FAIL() << "expected runtime_error";
    } catch (const std::runtime_error &e) {
        EXPECT_NE(std::string(e.what()).find("SERVER_PORT"), std::string::npos);
    }
}

TEST(Config, PortRangeEnforced) {
    EnvGuard g;
    Config c;
    setenv("SERVER_PORT", "70000", 1);
    EXPECT_THROW(c.parseConfig("/tmp/mmo_cfg_definitely_missing_12345.json"), std::runtime_error);
    setenv("SERVER_PORT", "65535", 1);
    auto [db, gs] = c.parseConfig("/tmp/mmo_cfg_definitely_missing_12345.json");
    EXPECT_EQ(gs.port, 65535);
}

TEST(Config, JsonThenEnvPrecedence) {
    EnvGuard g;
    std::string path = writeTempJson(R"({"server_port":27100})");
    Config c;
    {
        auto [db, gs] = c.parseConfig(path);
        EXPECT_EQ(gs.port, 27100);
    }
    setenv("SERVER_PORT", "27200", 1);
    {
        auto [db, gs] = c.parseConfig(path);
        EXPECT_EQ(gs.port, 27200);
    }
    std::remove(path.c_str());
}
