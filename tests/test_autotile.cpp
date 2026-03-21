#include <doctest/doctest.h>
#include "engine/tilemap/autotile.h"

using namespace fate;

TEST_CASE("Autotile bitmask with no neighbors") {
    // Isolated tile with no neighbors -> maps to tile index 0 (island tile)
    CHECK(autotileLookup(0b00000000) == 0);
}

TEST_CASE("Autotile diagonal gating") {
    // NE diagonal (bit 2) only counts if N (bit 1) AND E (bit 4) are set
    uint8_t raw = 0b00010110; // N=1, NE=1, E=1 -> NE counts
    uint8_t gated = applyDiagonalGating(raw);
    CHECK((gated & 0b00000100) != 0); // NE still set

    uint8_t raw2 = 0b00000110; // NE=1, E=1 but N=0 -> NE gated out
    uint8_t gated2 = applyDiagonalGating(raw2);
    CHECK((gated2 & 0b00000100) == 0); // NE removed
}

TEST_CASE("All 256 raw masks map to valid tile indices") {
    for (int raw = 0; raw < 256; ++raw) {
        uint8_t gated = applyDiagonalGating(static_cast<uint8_t>(raw));
        uint8_t tileIdx = autotileLookup(gated);
        CHECK(tileIdx < 47);
    }
}

TEST_CASE("computeAutotileMask queries neighbors correctly") {
    // 3x3 grid where center and all neighbors are same terrain
    auto allSame = [](int, int) { return true; };
    uint8_t mask = computeAutotileMask(1, 1, allSame);
    // All 8 neighbors same -> after gating, all bits set
    CHECK(mask == 0xFF);

    // Only cardinal neighbors
    auto cardinalsOnly = [](int x, int y) {
        // Same terrain at (0,1), (2,1), (1,0), (1,2) but not diagonals
        return (x == 1 || y == 1) && !(x == 1 && y == 1);
    };
    uint8_t mask2 = computeAutotileMask(1, 1, cardinalsOnly);
    // N, W, E, S set but no diagonals (since diagonal neighbors are different terrain)
    CHECK((mask2 & (N | W | E | S)) == (N | W | E | S));
    CHECK((mask2 & (NW | NE | SW | SE)) == 0);
}
