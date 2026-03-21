#include <doctest/doctest.h>
#include "engine/vfs/virtual_fs.h"
#include <string>

// FATE_SOURCE_DIR is defined by CMake as the project root
static std::string assetsPath() {
    return std::string(FATE_SOURCE_DIR) + "/assets";
}

TEST_CASE("VFS initializes and shuts down cleanly") {
    VirtualFS vfs;
    CHECK(vfs.init("fate_tests"));
    vfs.shutdown();
}

TEST_CASE("VFS double init is safe") {
    VirtualFS vfs;
    CHECK(vfs.init("fate_tests"));
    CHECK(vfs.init("fate_tests")); // should warn but succeed
    vfs.shutdown();
}

TEST_CASE("VFS mounts a directory") {
    VirtualFS vfs;
    REQUIRE(vfs.init("fate_tests"));
    CHECK(vfs.mount(assetsPath(), "/"));
    vfs.shutdown();
}

TEST_CASE("VFS reads existing file") {
    VirtualFS vfs;
    REQUIRE(vfs.init("fate_tests"));
    REQUIRE(vfs.mount(assetsPath(), "/"));

    auto text = vfs.readText("data/pvp_balance.json");
    REQUIRE(text.has_value());
    CHECK(text->size() > 0);
    // Verify it looks like JSON
    CHECK(text->find('{') != std::string::npos);

    auto binary = vfs.readFile("data/pvp_balance.json");
    REQUIRE(binary.has_value());
    CHECK(binary->size() == text->size());

    vfs.shutdown();
}

TEST_CASE("VFS returns nullopt for missing file") {
    VirtualFS vfs;
    REQUIRE(vfs.init("fate_tests"));
    REQUIRE(vfs.mount(assetsPath(), "/"));

    auto text = vfs.readText("nonexistent/file.txt");
    CHECK_FALSE(text.has_value());

    auto binary = vfs.readFile("nonexistent/file.txt");
    CHECK_FALSE(binary.has_value());

    vfs.shutdown();
}

TEST_CASE("VFS exists check") {
    VirtualFS vfs;
    REQUIRE(vfs.init("fate_tests"));
    REQUIRE(vfs.mount(assetsPath(), "/"));

    CHECK(vfs.exists("data/pvp_balance.json"));
    CHECK_FALSE(vfs.exists("no_such_file.bin"));

    vfs.shutdown();
}

TEST_CASE("VFS lists directory contents") {
    VirtualFS vfs;
    REQUIRE(vfs.init("fate_tests"));
    REQUIRE(vfs.mount(assetsPath(), "/"));

    auto entries = vfs.listDir("data");
    CHECK(entries.size() > 0);

    // pvp_balance.json should be in the listing
    bool foundPvp = false;
    for (const auto& e : entries) {
        if (e == "pvp_balance.json") {
            foundPvp = true;
            break;
        }
    }
    CHECK(foundPvp);

    vfs.shutdown();
}

TEST_CASE("VFS operations fail gracefully before init") {
    VirtualFS vfs;
    // No init() called
    CHECK_FALSE(vfs.exists("anything"));
    CHECK_FALSE(vfs.readFile("anything").has_value());
    CHECK_FALSE(vfs.readText("anything").has_value());
    CHECK(vfs.listDir("/").empty());
}

TEST_CASE("VFS destructor cleans up automatically") {
    {
        VirtualFS vfs;
        REQUIRE(vfs.init("fate_tests"));
        REQUIRE(vfs.mount(assetsPath(), "/"));
        CHECK(vfs.exists("data/pvp_balance.json"));
        // destructor should call shutdown
    }
    // If we get here without crash, the destructor worked
    CHECK(true);
}
