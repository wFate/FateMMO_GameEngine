#include <doctest/doctest.h>
#include "engine/asset/asset_handle.h"

TEST_CASE("AssetHandle default is invalid") {
    fate::AssetHandle h;
    CHECK(h.bits == 0);
    CHECK_FALSE(h.valid());
}

TEST_CASE("AssetHandle make and unpack") {
    auto h = fate::AssetHandle::make(42, 7);
    CHECK(h.index() == 42);
    CHECK(h.generation() == 7);
    CHECK(h.valid());
}

TEST_CASE("AssetHandle max index") {
    auto h = fate::AssetHandle::make(0xFFFFF, 1);
    CHECK(h.index() == 0xFFFFF);
    CHECK(h.generation() == 1);
}

TEST_CASE("AssetHandle max generation") {
    auto h = fate::AssetHandle::make(1, 0xFFF);
    CHECK(h.index() == 1);
    CHECK(h.generation() == 0xFFF);
}

TEST_CASE("AssetHandle equality") {
    auto a = fate::AssetHandle::make(5, 3);
    auto b = fate::AssetHandle::make(5, 3);
    auto c = fate::AssetHandle::make(5, 4);
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("AssetHandle std::hash works") {
    auto a = fate::AssetHandle::make(5, 3);
    auto b = fate::AssetHandle::make(5, 3);
    std::hash<fate::AssetHandle> hasher;
    CHECK(hasher(a) == hasher(b));
}
