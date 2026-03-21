#include <doctest/doctest.h>
#include "engine/render/texture.h"

using namespace fate;

TEST_CASE("TextureCache tracks VRAM usage") {
    auto& cache = TextureCache::instance();
    cache.clear();
    CHECK(cache.estimatedVRAM() == 0);
}

TEST_CASE("TextureCache eviction respects VRAM budget") {
    auto& cache = TextureCache::instance();
    cache.clear();
    cache.setVRAMBudget(1024 * 1024);
    CHECK(cache.vramBudget() == 1024 * 1024);
}

TEST_CASE("TextureCache clear resets state") {
    auto& cache = TextureCache::instance();
    cache.setVRAMBudget(256 * 1024 * 1024);
    cache.clear();
    CHECK(cache.estimatedVRAM() == 0);
    CHECK(cache.entryCount() == 0);
}

TEST_CASE("TextureCache frame counter advances") {
    auto& cache = TextureCache::instance();
    cache.clear();
    cache.advanceFrame();
    cache.advanceFrame();
    // No crash, counter increments internally
    CHECK(cache.entryCount() == 0);
}
