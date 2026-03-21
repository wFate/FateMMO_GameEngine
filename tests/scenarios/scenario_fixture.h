#pragma once
#include "test_bot.h"
#include <doctest/doctest.h>
#include <string>
#include <cstdlib>

namespace fate {

inline std::string getEnvOr(const char* name, const char* defaultVal) {
    const char* val = std::getenv(name);
    return val ? std::string(val) : std::string(defaultVal);
}

inline std::string getEnvRequired(const char* name) {
    const char* val = std::getenv(name);
    REQUIRE_MESSAGE(val != nullptr,
        "Required environment variable not set: ", name,
        "\nSet TEST_USERNAME and TEST_PASSWORD before running scenario tests.");
    return std::string(val);
}

struct ScenarioFixture {
    TestBot bot;
    std::string host;
    uint16_t gamePort;
    uint16_t authPort;
    std::string username;
    std::string password;

    ScenarioFixture() {
        host     = getEnvOr("TEST_HOST", "127.0.0.1");
        gamePort = static_cast<uint16_t>(std::stoi(getEnvOr("TEST_GAME_PORT", "7777")));
        authPort = static_cast<uint16_t>(std::stoi(getEnvOr("TEST_AUTH_PORT", "7778")));
        username = getEnvRequired("TEST_USERNAME");
        password = getEnvRequired("TEST_PASSWORD");

        auto auth = bot.login(host, authPort, username, password);
        REQUIRE_MESSAGE(auth.success,
            "Login failed: ", auth.errorReason);
        bot.connectUDP(host, gamePort);
    }

    ~ScenarioFixture() {
        bot.disconnect();
    }
};

} // namespace fate
