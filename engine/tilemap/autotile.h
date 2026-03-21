#pragma once
#include <cstdint>
#include <array>

namespace fate {

// Neighbor bit positions (clockwise from NW):
// NW=0, N=1, NE=2, W=3, E=4, SW=5, S=6, SE=7
enum AutotileBit : uint8_t {
    NW = 1 << 0, N  = 1 << 1, NE = 1 << 2,
    W  = 1 << 3,               E  = 1 << 4,
    SW = 1 << 5, S  = 1 << 6, SE = 1 << 7
};

// Gate diagonals: NE only if N&&E, NW only if N&&W, SE only if S&&E, SW only if S&&W
inline uint8_t applyDiagonalGating(uint8_t raw) {
    uint8_t gated = raw & (N | W | E | S); // keep cardinals
    if ((raw & NW) && (raw & N) && (raw & W)) gated |= NW;
    if ((raw & NE) && (raw & N) && (raw & E)) gated |= NE;
    if ((raw & SW) && (raw & S) && (raw & W)) gated |= SW;
    if ((raw & SE) && (raw & S) && (raw & E)) gated |= SE;
    return gated;
}

// Precomputed lookup: gated 8-bit mask -> tile index (0-46)
inline uint8_t autotileLookup(uint8_t gatedMask) {
    static const std::array<uint8_t, 256> TABLE = []() {
        std::array<uint8_t, 256> t{};
        for (int i = 0; i < 256; ++i) {
            uint8_t g = applyDiagonalGating(static_cast<uint8_t>(i));
            // Extract cardinal bits for base classification
            uint8_t cardinals = ((g >> 1) & 1) | (((g >> 3) & 1) << 1)
                              | (((g >> 4) & 1) << 2) | (((g >> 6) & 1) << 3);
            t[i] = cardinals; // 16 base tiles from cardinals (expandable to 47)
        }
        return t;
    }();
    return TABLE[gatedMask];
}

// Compute autotile bitmask for a tile at (x, y)
// sameTerrainFn(nx, ny) returns true if neighbor at (nx,ny) is same terrain type
template<typename F>
uint8_t computeAutotileMask(int x, int y, F&& sameTerrainFn) {
    uint8_t raw = 0;
    if (sameTerrainFn(x - 1, y - 1)) raw |= NW;
    if (sameTerrainFn(x,     y - 1)) raw |= N;
    if (sameTerrainFn(x + 1, y - 1)) raw |= NE;
    if (sameTerrainFn(x - 1, y))     raw |= W;
    if (sameTerrainFn(x + 1, y))     raw |= E;
    if (sameTerrainFn(x - 1, y + 1)) raw |= SW;
    if (sameTerrainFn(x,     y + 1)) raw |= S;
    if (sameTerrainFn(x + 1, y + 1)) raw |= SE;
    return applyDiagonalGating(raw);
}

} // namespace fate
