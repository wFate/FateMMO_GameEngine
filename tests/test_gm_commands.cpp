#include <doctest/doctest.h>
#include "server/gm_commands.h"

using namespace fate;

TEST_SUITE("GMCommands") {

TEST_CASE("Parse simple command") {
    auto result = GMCommandParser::parse("/kick TestPlayer");
    CHECK(result.isCommand);
    CHECK(result.commandName == "kick");
    CHECK(result.args.size() == 1);
    CHECK(result.args[0] == "TestPlayer");
}

TEST_CASE("Parse command with multiple args") {
    auto result = GMCommandParser::parse("/ban TestPlayer 60 cheating");
    CHECK(result.commandName == "ban");
    CHECK(result.args.size() == 3);
    CHECK(result.args[0] == "TestPlayer");
    CHECK(result.args[1] == "60");
    CHECK(result.args[2] == "cheating");
}

TEST_CASE("Parse command with no args") {
    auto result = GMCommandParser::parse("/help");
    CHECK(result.isCommand);
    CHECK(result.commandName == "help");
    CHECK(result.args.empty());
}

TEST_CASE("Non-command returns isCommand=false") {
    auto result = GMCommandParser::parse("hello world");
    CHECK_FALSE(result.isCommand);
}

TEST_CASE("Slash only returns isCommand=false") {
    auto result = GMCommandParser::parse("/");
    CHECK_FALSE(result.isCommand);
}

TEST_CASE("Empty string returns isCommand=false") {
    auto result = GMCommandParser::parse("");
    CHECK_FALSE(result.isCommand);
}

TEST_CASE("Registry: register and find command") {
    GMCommandRegistry registry;
    registry.registerCommand({"kick", 1, nullptr});
    auto* cmd = registry.findCommand("kick");
    REQUIRE(cmd != nullptr);
    CHECK(cmd->name == "kick");
    CHECK(cmd->minRole == 1);
}

TEST_CASE("Registry: unknown command returns nullptr") {
    GMCommandRegistry registry;
    CHECK(registry.findCommand("unknown") == nullptr);
}

TEST_CASE("Permission: player (0) cannot use GM command (1)") {
    CHECK_FALSE(GMCommandRegistry::hasPermission(0, 1));
}

TEST_CASE("Permission: GM (1) can use GM command (1)") {
    CHECK(GMCommandRegistry::hasPermission(1, 1));
}

TEST_CASE("Permission: Admin (2) can use GM command (1)") {
    CHECK(GMCommandRegistry::hasPermission(2, 1));
}

TEST_CASE("Permission: GM (1) cannot use Admin command (2)") {
    CHECK_FALSE(GMCommandRegistry::hasPermission(1, 2));
}

TEST_CASE("Permission: Admin (2) can use Admin command (2)") {
    CHECK(GMCommandRegistry::hasPermission(2, 2));
}

TEST_CASE("Registry size tracks commands") {
    GMCommandRegistry registry;
    CHECK(registry.size() == 0);
    registry.registerCommand({"kick", 1, nullptr});
    registry.registerCommand({"ban", 1, nullptr});
    CHECK(registry.size() == 2);
}

} // TEST_SUITE
