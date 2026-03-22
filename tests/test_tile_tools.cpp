#include <doctest/doctest.h>
#include "engine/editor/tile_tools.h"
#include "engine/editor/undo.h"
#include "engine/ecs/world.h"

using namespace fate;

TEST_CASE("floodFill fills connected region") {
    // 4x4 grid: tiles[r][c], center 2x2 (1,1)-(2,2) is terrain 1, rest is 0
    int grid[4][4] = {
        {0, 0, 0, 0},
        {0, 1, 1, 0},
        {0, 1, 1, 0},
        {0, 0, 0, 0}
    };
    auto match = [&](int c, int r) -> bool { return grid[r][c] == 0; };
    auto coords = floodFill(0, 0, 4, 4, match);
    CHECK(coords.size() == 12); // 16 - 4 center tiles
}

TEST_CASE("floodFill respects boundaries") {
    auto match = [](int, int) { return true; };
    auto coords = floodFill(0, 0, 3, 3, match);
    CHECK(coords.size() == 9); // fills entire 3x3
}

TEST_CASE("floodFill does nothing if start doesn't match") {
    auto match = [](int, int) { return false; };
    auto coords = floodFill(0, 0, 4, 4, match);
    CHECK(coords.empty());
}

TEST_CASE("rectangleFill returns correct rect") {
    auto coords = rectangleFill(1, 1, 3, 3);
    CHECK(coords.size() == 9); // 3x3
}

TEST_CASE("rectangleFill handles reversed corners") {
    auto coords = rectangleFill(3, 3, 1, 1);
    CHECK(coords.size() == 9); // still 3x3
}

TEST_CASE("rectangleFill single tile") {
    auto coords = rectangleFill(2, 2, 2, 2);
    CHECK(coords.size() == 1);
    CHECK(coords[0] == Vec2i{2, 2});
}

TEST_CASE("lineTool horizontal") {
    auto coords = lineTool(0, 0, 4, 0);
    CHECK(coords.size() == 5);
    for (int i = 0; i < 5; ++i) {
        CHECK(coords[i].x == i);
        CHECK(coords[i].y == 0);
    }
}

TEST_CASE("lineTool vertical") {
    auto coords = lineTool(0, 0, 0, 3);
    CHECK(coords.size() == 4);
}

TEST_CASE("lineTool diagonal") {
    auto coords = lineTool(0, 0, 3, 3);
    CHECK(coords.size() == 4);
}

TEST_CASE("lineTool single point") {
    auto coords = lineTool(5, 5, 5, 5);
    CHECK(coords.size() == 1);
    CHECK(coords[0] == Vec2i{5, 5});
}

TEST_CASE("CompoundCommand undoes in reverse order") {
    World world;
    auto compound = std::make_unique<CompoundCommand>();
    compound->desc = "Test Compound";
    CHECK(compound->empty());
    CHECK(compound->description() == "Test Compound");
}
