#pragma once

#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

namespace fate {

// ============================================================================
// CollisionGrid -- per-scene packed bitgrid for tile-based collision
//
// Dynamic bounding box: supports negative tile coordinates.
// Storage: 1 bit per tile in a packed uint8_t vector.
//
// Usage:
//   1. beginBuild()
//   2. markBlocked(tileX, tileY) -- for each blocked tile
//   3. endBuild()                -- computes bounds, allocates bitset
//   4. isBlocked / isBlockedTile / isBlockedRect -- queries
// ============================================================================
class CollisionGrid {
public:
    static constexpr float TILE_SIZE = 32.0f;

    CollisionGrid() = default;

    // -- Build API ------------------------------------------------------------

    void beginBuild() {
        pendingTiles_.clear();
        bits_.clear();
        minTileX_ = 0;
        minTileY_ = 0;
        width_    = 0;
        height_   = 0;
        built_    = false;
    }

    void markBlocked(int tileX, int tileY) {
        pendingTiles_.push_back({tileX, tileY});
    }

    void endBuild() {
        if (pendingTiles_.empty()) {
            built_ = true;
            return;
        }

        // Compute bounding box from all marked tiles
        int minX = pendingTiles_[0].x;
        int maxX = pendingTiles_[0].x;
        int minY = pendingTiles_[0].y;
        int maxY = pendingTiles_[0].y;

        for (size_t i = 1; i < pendingTiles_.size(); ++i) {
            minX = (std::min)(minX, pendingTiles_[i].x);
            maxX = (std::max)(maxX, pendingTiles_[i].x);
            minY = (std::min)(minY, pendingTiles_[i].y);
            maxY = (std::max)(maxY, pendingTiles_[i].y);
        }

        minTileX_ = minX;
        minTileY_ = minY;
        width_    = maxX - minX + 1;
        height_   = maxY - minY + 1;

        // Allocate packed bitset: 1 bit per tile, rounded up to bytes
        size_t totalBits = static_cast<size_t>(width_) * static_cast<size_t>(height_);
        size_t totalBytes = (totalBits + 7) / 8;
        bits_.assign(totalBytes, 0);

        // Set bits for each marked tile
        for (const auto& t : pendingTiles_) {
            int lx = t.x - minTileX_;
            int ly = t.y - minTileY_;
            size_t bitIndex = static_cast<size_t>(ly) * static_cast<size_t>(width_) + static_cast<size_t>(lx);
            bits_[bitIndex / 8] |= (1u << (bitIndex % 8));
        }

        pendingTiles_.clear();
        pendingTiles_.shrink_to_fit();
        built_ = true;
    }

    // -- Query API ------------------------------------------------------------

    // Point check: converts world pixels to tile coords via floor(pos / 32).
    bool isBlocked(float worldX, float worldY) const {
        int tileX = static_cast<int>(std::floor(worldX / TILE_SIZE));
        int tileY = static_cast<int>(std::floor(worldY / TILE_SIZE));
        return isBlockedTile(tileX, tileY);
    }

    // Direct tile check. Out-of-bounds returns false.
    bool isBlockedTile(int tileX, int tileY) const {
        if (!built_ || width_ == 0) return false;

        int lx = tileX - minTileX_;
        int ly = tileY - minTileY_;

        if (lx < 0 || lx >= width_ || ly < 0 || ly >= height_) return false;

        size_t bitIndex = static_cast<size_t>(ly) * static_cast<size_t>(width_) + static_cast<size_t>(lx);
        return (bits_[bitIndex / 8] & (1u << (bitIndex % 8))) != 0;
    }

    // AABB check: tests all tiles overlapped by the rect.
    // Returns true if any tile in the rect footprint is blocked.
    bool isBlockedRect(float worldX, float worldY, float halfW, float halfH) const {
        if (!built_ || width_ == 0) return false;

        float left   = worldX - halfW;
        float right  = worldX + halfW;
        float top    = worldY - halfH;
        float bottom = worldY + halfH;

        int minTX = static_cast<int>(std::floor(left   / TILE_SIZE));
        int maxTX = static_cast<int>(std::floor(right  / TILE_SIZE));
        int minTY = static_cast<int>(std::floor(top    / TILE_SIZE));
        int maxTY = static_cast<int>(std::floor(bottom / TILE_SIZE));

        for (int ty = minTY; ty <= maxTY; ++ty) {
            for (int tx = minTX; tx <= maxTX; ++tx) {
                if (isBlockedTile(tx, ty)) return true;
            }
        }

        return false;
    }

    // True if no tiles have been marked.
    bool empty() const {
        return width_ == 0;
    }

private:
    struct TilePos {
        int x;
        int y;
    };

    // Build-phase accumulator
    std::vector<TilePos> pendingTiles_;

    // Packed bitset (1 bit per tile)
    std::vector<uint8_t> bits_;

    // Bounding box in tile coordinates
    int minTileX_ = 0;
    int minTileY_ = 0;
    int width_    = 0;  // number of tiles in X
    int height_   = 0;  // number of tiles in Y

    bool built_ = false;
};

} // namespace fate
