#include <doctest/doctest.h>
#include "engine/asset/asset_registry.h"

// Mock loader: stores an int on the heap
static void* mockLoad(const std::string& path) {
    return new int(42);
}
static bool mockReload(void* existing, const std::string& path) {
    *static_cast<int*>(existing) = 99;
    return true;
}
static bool mockValidate(const std::string& path) { return true; }
static void mockDestroy(void* data) { delete static_cast<int*>(data); }

static fate::AssetLoader makeMockLoader() {
    return {
        .kind = fate::AssetKind::Texture,
        .load = mockLoad,
        .reload = mockReload,
        .validate = mockValidate,
        .destroy = mockDestroy,
        .extensions = {".mock"}
    };
}

TEST_CASE("AssetRegistry slot 0 is reserved") {
    auto& reg = fate::AssetRegistry::instance();
    reg.clear();
    reg.registerLoader(makeMockLoader());

    auto h = reg.load("test.mock");
    CHECK(h.index() != 0); // slot 0 is null sentinel
}

TEST_CASE("AssetRegistry load and get") {
    auto& reg = fate::AssetRegistry::instance();
    reg.clear();
    reg.registerLoader(makeMockLoader());

    auto h = reg.load("test.mock");
    REQUIRE(h.valid());
    int* val = reg.get<int>(h);
    REQUIRE(val != nullptr);
    CHECK(*val == 42);
}

TEST_CASE("AssetRegistry duplicate path returns same handle") {
    auto& reg = fate::AssetRegistry::instance();
    reg.clear();
    reg.registerLoader(makeMockLoader());

    auto h1 = reg.load("test.mock");
    auto h2 = reg.load("test.mock");
    CHECK(h1 == h2);
}

TEST_CASE("AssetRegistry stale handle returns nullptr") {
    auto& reg = fate::AssetRegistry::instance();
    reg.clear();
    reg.registerLoader(makeMockLoader());

    auto h = reg.load("test.mock");
    // Manually bump generation to simulate reload
    auto stale = fate::AssetHandle::make(h.index(), h.generation() + 1);
    CHECK(reg.get<int>(stale) == nullptr);
}

TEST_CASE("AssetRegistry find by path") {
    auto& reg = fate::AssetRegistry::instance();
    reg.clear();
    reg.registerLoader(makeMockLoader());

    auto h = reg.load("test.mock");
    auto found = reg.find("test.mock");
    CHECK(h == found);

    auto notFound = reg.find("nonexistent.mock");
    CHECK_FALSE(notFound.valid());
}

TEST_CASE("AssetRegistry clear destroys all assets") {
    auto& reg = fate::AssetRegistry::instance();
    reg.clear();
    reg.registerLoader(makeMockLoader());

    auto h = reg.load("test.mock");
    REQUIRE(reg.get<int>(h) != nullptr);
    reg.clear();
    // After clear, handle is stale
    CHECK(reg.get<int>(h) == nullptr);
}

TEST_CASE("AssetRegistry reload bumps generation") {
    auto& reg = fate::AssetRegistry::instance();
    reg.clear();
    reg.registerLoader(makeMockLoader());

    auto h = reg.load("test.mock");
    REQUIRE(h.valid());
    int* val = reg.get<int>(h);
    REQUIRE(val != nullptr);
    CHECK(*val == 42);

    // Queue and process reload
    reg.queueReload("test.mock");
    reg.processReloads(1.0f); // stamp time
    reg.processReloads(2.0f); // past 300ms debounce

    // Old handle is now stale
    CHECK(reg.get<int>(h) == nullptr);

    // Get fresh handle
    auto h2 = reg.find("test.mock");
    CHECK(h2.generation() == h.generation() + 1);
    int* val2 = reg.get<int>(h2);
    REQUIRE(val2 != nullptr);
    CHECK(*val2 == 99); // reload sets to 99
}

TEST_CASE("AssetRegistry debounce prevents premature reload") {
    auto& reg = fate::AssetRegistry::instance();
    reg.clear();
    reg.registerLoader(makeMockLoader());

    auto h = reg.load("test.mock");
    reg.queueReload("test.mock");

    // Process immediately (within debounce window)
    reg.processReloads(0.1f); // stamp
    reg.processReloads(0.2f); // only 0.1s elapsed, < 0.3s debounce

    // Handle should still be valid (not reloaded yet)
    CHECK(reg.get<int>(h) != nullptr);

    // Now past debounce
    reg.processReloads(0.5f); // 0.4s since stamp
    CHECK(reg.get<int>(h) == nullptr); // reloaded, old handle stale
}
