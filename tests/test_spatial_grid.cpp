#include <doctest/doctest.h>
#include "engine/spatial/spatial_grid.h"
#include "engine/spatial/spatial_index.h"

TEST_CASE("SpatialGrid insert and queryRadius") {
    fate::Arena arena(4 * 1024 * 1024);
    fate::SpatialGrid grid;

    // 100x100 tile map, 32px tiles, 64px cells
    grid.init(arena, 100, 100, 32.0f, 64.0f);
    CHECK(grid.isInitialized());
    CHECK(grid.totalCells() > 0);

    // Three entities at known positions
    fate::EntityHandle e1(1, 1); // at (100, 100)
    fate::EntityHandle e2(2, 1); // at (120, 110) -- close to e1
    fate::EntityHandle e3(3, 1); // at (500, 500) -- far away

    grid.beginRebuild(3);
    grid.addEntity(e1, 100.0f, 100.0f);
    grid.addEntity(e2, 120.0f, 110.0f);
    grid.addEntity(e3, 500.0f, 500.0f);
    grid.endRebuild();

    CHECK(grid.entityCount() == 3);

    // Query radius around (110, 105) with radius 50 -- should find e1 and e2
    auto scratch = fate::GetScratch();
    fate::ScratchScope scope(scratch);

    auto results = grid.queryRadius(110.0f, 105.0f, 50.0f, scope);
    CHECK(results.size() == 2);

    // Verify both e1 and e2 are in results
    bool foundE1 = false, foundE2 = false;
    for (auto h : results) {
        if (h == e1) foundE1 = true;
        if (h == e2) foundE2 = true;
    }
    CHECK(foundE1);
    CHECK(foundE2);
}

TEST_CASE("SpatialGrid findNearest") {
    fate::Arena arena(4 * 1024 * 1024);
    fate::SpatialGrid grid;
    grid.init(arena, 100, 100, 32.0f, 64.0f);

    fate::EntityHandle e1(1, 1); // at (100, 100)
    fate::EntityHandle e2(2, 1); // at (200, 200)
    fate::EntityHandle e3(3, 1); // at (105, 105) -- closest to query point

    grid.beginRebuild(3);
    grid.addEntity(e1, 100.0f, 100.0f);
    grid.addEntity(e2, 200.0f, 200.0f);
    grid.addEntity(e3, 105.0f, 105.0f);
    grid.endRebuild();

    // Find nearest to (110, 110) -- e3 is closest
    auto result = grid.findNearest(110.0f, 110.0f, 500.0f);
    REQUIRE(result.has_value());
    CHECK(result.value() == e3);

    // Find nearest to (200, 200) -- e2 is right there
    auto result2 = grid.findNearest(200.0f, 200.0f, 500.0f);
    REQUIRE(result2.has_value());
    CHECK(result2.value() == e2);
}

TEST_CASE("SpatialGrid empty query returns empty span") {
    fate::Arena arena(4 * 1024 * 1024);
    fate::SpatialGrid grid;
    grid.init(arena, 50, 50, 32.0f, 64.0f);

    // No entities added
    grid.beginRebuild(0);
    grid.endRebuild();

    auto scratch = fate::GetScratch();
    fate::ScratchScope scope(scratch);

    auto results = grid.queryRadius(100.0f, 100.0f, 50.0f, scope);
    CHECK(results.empty());

    // findNearest on empty grid returns NotFound
    auto nearest = grid.findNearest(100.0f, 100.0f, 50.0f);
    CHECK_FALSE(nearest.has_value());
    CHECK(nearest.error() == fate::SpatialError::NotFound);
}

TEST_CASE("SpatialGrid handles grid boundary") {
    fate::Arena arena(4 * 1024 * 1024);
    fate::SpatialGrid grid;

    // 50x50 tile map = 1600x1600 pixel map
    grid.init(arena, 50, 50, 32.0f, 64.0f);

    // Entities at edges
    fate::EntityHandle e1(1, 1); // near origin
    fate::EntityHandle e2(2, 1); // near max edge
    fate::EntityHandle e3(3, 1); // at (0, 0) corner

    grid.beginRebuild(3);
    grid.addEntity(e1, 1.0f, 1.0f);
    grid.addEntity(e2, 1590.0f, 1590.0f);
    grid.addEntity(e3, 0.0f, 0.0f);
    grid.endRebuild();

    CHECK(grid.entityCount() == 3);

    // Query near origin should find e1 and e3
    auto scratch = fate::GetScratch();
    fate::ScratchScope scope(scratch);

    auto results = grid.queryRadius(0.5f, 0.5f, 10.0f, scope);
    CHECK(results.size() == 2);

    // Query near the far edge should find e2
    // Use a separate scratch allocation range
    auto scratch2 = fate::GetScratch();
    fate::ScratchScope scope2(scratch2);

    auto farResults = grid.queryRadius(1590.0f, 1590.0f, 10.0f, scope2);
    CHECK(farResults.size() == 1);
    CHECK(farResults[0] == e2);

    // findNearest at boundary corner
    auto nearest = grid.findNearest(0.0f, 0.0f, 5.0f);
    REQUIRE(nearest.has_value());
    CHECK(nearest.value() == e3);
}

TEST_CASE("SpatialGrid findAtPoint") {
    fate::Arena arena(4 * 1024 * 1024);
    fate::SpatialGrid grid;
    grid.init(arena, 100, 100, 32.0f, 64.0f);

    fate::EntityHandle e1(1, 1); // at (100, 100)
    fate::EntityHandle e2(2, 1); // at (200, 200)

    grid.beginRebuild(2);
    grid.addEntity(e1, 100.0f, 100.0f);
    grid.addEntity(e2, 200.0f, 200.0f);
    grid.endRebuild();

    // Simple bounds check: entity is "hit" if point is within 16px
    auto boundsCheck = [&](fate::EntityHandle h, float px, float py) -> bool {
        // Find the entity position by matching handle
        float ex = 0, ey = 0;
        if (h == e1) { ex = 100.0f; ey = 100.0f; }
        else if (h == e2) { ex = 200.0f; ey = 200.0f; }
        float dx = px - ex;
        float dy = py - ey;
        return (dx * dx + dy * dy) <= 16.0f * 16.0f;
    };

    // Click near e1
    auto hit = grid.findAtPoint(105.0f, 105.0f, boundsCheck);
    CHECK(hit == e1);

    // Click near e2
    auto hit2 = grid.findAtPoint(198.0f, 202.0f, boundsCheck);
    CHECK(hit2 == e2);

    // Click nowhere near any entity
    auto miss = grid.findAtPoint(400.0f, 400.0f, boundsCheck);
    CHECK(miss.isNull());
}
