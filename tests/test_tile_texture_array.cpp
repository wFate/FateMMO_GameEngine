#include <doctest/doctest.h>
#include "engine/render/tile_texture_array.h"

using namespace fate;

TEST_CASE("TileTextureArray GID mapping") {
    TileTextureArray arr;
    // Can't test GL operations without context, but can test the mapping
    arr.setGidMapping(1, 0);
    arr.setGidMapping(5, 3);
    arr.setGidMapping(10, 7);

    CHECK(arr.gidToLayer(1) == 0);
    CHECK(arr.gidToLayer(5) == 3);
    CHECK(arr.gidToLayer(10) == 7);
    CHECK(arr.gidToLayer(999) == -1); // unmapped
}

TEST_CASE("TileTextureArray default state") {
    TileTextureArray arr;
    CHECK(arr.glId() == 0);
    CHECK(arr.layerCount() == 0);
    CHECK(arr.tileWidth() == 0);
    CHECK(arr.tileHeight() == 0);
}

TEST_CASE("TileTextureArray GID mapping overwrite") {
    TileTextureArray arr;
    arr.setGidMapping(42, 5);
    CHECK(arr.gidToLayer(42) == 5);

    // Overwrite with new layer
    arr.setGidMapping(42, 10);
    CHECK(arr.gidToLayer(42) == 10);
}

TEST_CASE("TileTextureArray multiple GIDs to same layer") {
    TileTextureArray arr;
    arr.setGidMapping(1, 0);
    arr.setGidMapping(2, 0);
    arr.setGidMapping(3, 0);

    CHECK(arr.gidToLayer(1) == 0);
    CHECK(arr.gidToLayer(2) == 0);
    CHECK(arr.gidToLayer(3) == 0);
}
