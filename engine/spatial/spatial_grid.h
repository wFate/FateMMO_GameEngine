#pragma once
#include "engine/spatial/spatial_index.h"
#include "engine/ecs/entity_handle.h"
#include "engine/memory/arena.h"
#include "engine/memory/scratch_arena.h"
#include "engine/core/types.h"

#include <cstdint>
#include <cmath>
#include <vector>
#include <span>
#include <limits>
#include <algorithm>
#include <bit>

// std::expected polyfill -- MSVC may not have <expected> in all C++23 modes
#if __has_include(<expected>) && defined(__cpp_lib_expected)
#include <expected>
namespace fate { namespace detail {
    template<typename T, typename E>
    using Expected = std::expected<T, E>;
    template<typename E>
    using Unexpected = std::unexpected<E>;
    template<typename E>
    auto makeUnexpected(E e) { return std::unexpected(e); }
}}
#else
namespace fate { namespace detail {
    template<typename T, typename E>
    struct Expected {
        union {
            T val_;
            E err_;
        };
        bool has_;

        Expected(T v) : val_(v), has_(true) {}
        Expected(struct Unexpected<E> u) : err_(u.value), has_(false) {}

        bool has_value() const { return has_; }
        const T& value() const { return val_; }
        const E& error() const { return err_; }
        explicit operator bool() const { return has_; }
        const T& operator*() const { return val_; }
    };

    template<typename E>
    struct Unexpected {
        E value;
        explicit Unexpected(E e) : value(e) {}
    };

    template<typename E>
    auto makeUnexpected(E e) { return Unexpected<E>(e); }
}}
#endif

namespace fate {

// ============================================================================
// SpatialGrid -- fixed power-of-two grid for bounded tile worlds
//
// Zero-hash cell lookup: cellIndex = (ty << gridBits_) | tx
// Uses the same Mueller counting-sort rebuild pattern as SpatialHashEngine,
// but replaces the hash with a direct bitshift index for bounded worlds.
//
// Frame loop:
//   1. beginRebuild(maxEntities)
//   2. addEntity(handle, px, py) -- for each entity
//   3. endRebuild()              -- counting sort via prefix sums
//   4. queryRadius / findNearest / findAtPoint
// ============================================================================
class SpatialGrid {
public:
    SpatialGrid() = default;

    // Initialize the grid for a bounded tile world.
    // mapWidthTiles/mapHeightTiles: map dimensions in tiles
    // tileSize: pixels per tile
    // cellSizePx: spatial cell size in pixels (should be power of two or will be rounded up)
    void init(Arena& arena, uint32_t mapWidthTiles, uint32_t mapHeightTiles,
              float tileSize, float cellSizePx) {
        tileSize_    = tileSize;
        cellSizePx_  = cellSizePx;
        invCellSize_ = 1.0f / cellSizePx;

        // Map dimensions in pixels
        float mapWidthPx  = static_cast<float>(mapWidthTiles) * tileSize;
        float mapHeightPx = static_cast<float>(mapHeightTiles) * tileSize;
        mapWidthPx_  = mapWidthPx;
        mapHeightPx_ = mapHeightPx;

        // Grid dimensions in cells
        gridW_ = static_cast<uint32_t>(std::ceil(mapWidthPx / cellSizePx));
        gridH_ = static_cast<uint32_t>(std::ceil(mapHeightPx / cellSizePx));

        // Compute gridBits_ = ceil(log2(max(gridW_, gridH_)))
        // This gives us the number of bits needed for each axis
        uint32_t maxDim = (gridW_ > gridH_) ? gridW_ : gridH_;
        if (maxDim == 0) maxDim = 1;
        gridBits_ = ceilLog2(maxDim);

        // Total cells = (1 << gridBits_) ^ 2 = 1 << (2 * gridBits_)
        totalCells_ = 1u << (gridBits_ * 2);
        cellMask_ = (1u << gridBits_) - 1;

        // Allocate cell arrays from the persistent arena
        cellCounts_ = arena.pushArray<uint32_t>(totalCells_);
        cellStarts_ = arena.pushArray<uint32_t>(totalCells_ + 1);

        initialized_ = true;
    }

    // -- Configuration --------------------------------------------------------

    float cellSize()  const { return cellSizePx_; }
    float mapWidth()  const { return mapWidthPx_; }
    float mapHeight() const { return mapHeightPx_; }
    uint32_t gridW()  const { return gridW_; }
    uint32_t gridH()  const { return gridH_; }
    uint32_t totalCells() const { return totalCells_; }
    uint32_t entityCount() const { return count_; }
    bool isInitialized() const { return initialized_; }

    // -- Build API ------------------------------------------------------------

    void beginRebuild(uint32_t maxEntities) {
        count_ = 0;
        handles_.clear();
        posX_.clear();
        posY_.clear();
        cellHashes_.clear();

        handles_.reserve(maxEntities);
        posX_.reserve(maxEntities);
        posY_.reserve(maxEntities);
        cellHashes_.reserve(maxEntities);

        // Zero out cell counts
        for (uint32_t i = 0; i < totalCells_; ++i) {
            cellCounts_[i] = 0;
        }
    }

    void addEntity(EntityHandle handle, float px, float py) {
        uint32_t ci = cellIndex(px, py);
        handles_.push_back(handle);
        posX_.push_back(px);
        posY_.push_back(py);
        cellHashes_.push_back(ci);
        cellCounts_[ci]++;
        count_++;
    }

    void endRebuild() {
        // Prefix sum: cellStarts_[i] = where bucket i begins in sorted_
        cellStarts_[0] = 0;
        for (uint32_t i = 0; i < totalCells_; ++i) {
            cellStarts_[i + 1] = cellStarts_[i] + cellCounts_[i];
        }

        // Build sorted array via counting sort
        sorted_.resize(count_);
        std::vector<uint32_t> cursor(cellStarts_, cellStarts_ + totalCells_);

        for (uint32_t i = 0; i < count_; ++i) {
            uint32_t ci = cellHashes_[i];
            uint32_t dest = cursor[ci]++;
            sorted_[dest] = i; // index into handles_/posX_/posY_
        }
    }

    // -- Query API ------------------------------------------------------------

    // Collect all entities within radius of (cx, cy) into scratch arena.
    // Returns a span valid until the caller's ScratchScope destructs.
    std::span<const EntityHandle> queryRadius(float cx, float cy, float radius,
                                              ScratchScope& scratch) const {
        float radiusSq = radius * radius;

        // Determine cell range
        int minTX, minTY, maxTX, maxTY;
        cellRange(cx, cy, radius, minTX, minTY, maxTX, maxTY);

        // First pass: count matches to know how much to allocate
        uint32_t matchCount = 0;
        for (int ty = minTY; ty <= maxTY; ++ty) {
            for (int tx = minTX; tx <= maxTX; ++tx) {
                uint32_t ci = cellIndexFromCell(tx, ty);
                uint32_t start = cellStarts_[ci];
                uint32_t end   = cellStarts_[ci + 1];
                for (uint32_t s = start; s < end; ++s) {
                    uint32_t idx = sorted_[s];
                    float dx = posX_[idx] - cx;
                    float dy = posY_[idx] - cy;
                    if (dx * dx + dy * dy <= radiusSq) {
                        matchCount++;
                    }
                }
            }
        }

        if (matchCount == 0) {
            return {};
        }

        // Allocate results from scratch arena
        void* mem = scratch.push(sizeof(EntityHandle) * matchCount, alignof(EntityHandle));
        EntityHandle* results = static_cast<EntityHandle*>(mem);

        // Second pass: fill results
        uint32_t writeIdx = 0;
        for (int ty = minTY; ty <= maxTY; ++ty) {
            for (int tx = minTX; tx <= maxTX; ++tx) {
                uint32_t ci = cellIndexFromCell(tx, ty);
                uint32_t start = cellStarts_[ci];
                uint32_t end   = cellStarts_[ci + 1];
                for (uint32_t s = start; s < end; ++s) {
                    uint32_t idx = sorted_[s];
                    float dx = posX_[idx] - cx;
                    float dy = posY_[idx] - cy;
                    if (dx * dx + dy * dy <= radiusSq) {
                        results[writeIdx++] = handles_[idx];
                    }
                }
            }
        }

        return std::span<const EntityHandle>(results, matchCount);
    }

    // Find the nearest entity within radius of (cx, cy).
    detail::Expected<EntityHandle, SpatialError>
    findNearest(float cx, float cy, float radius) const {
        EntityHandle best = NULL_ENTITY_HANDLE;
        float bestDistSq = radius * radius;

        int minTX, minTY, maxTX, maxTY;
        cellRange(cx, cy, radius, minTX, minTY, maxTX, maxTY);

        for (int ty = minTY; ty <= maxTY; ++ty) {
            for (int tx = minTX; tx <= maxTX; ++tx) {
                uint32_t ci = cellIndexFromCell(tx, ty);
                uint32_t start = cellStarts_[ci];
                uint32_t end   = cellStarts_[ci + 1];
                for (uint32_t s = start; s < end; ++s) {
                    uint32_t idx = sorted_[s];
                    float dx = posX_[idx] - cx;
                    float dy = posY_[idx] - cy;
                    float dSq = dx * dx + dy * dy;
                    if (dSq < bestDistSq) {
                        bestDistSq = dSq;
                        best = handles_[idx];
                    }
                }
            }
        }

        if (best.isNull()) {
            return detail::makeUnexpected(SpatialError::NotFound);
        }
        return best;
    }

    // Find entity whose bounds contain a point (for click/touch targeting).
    // boundsCheckFn(EntityHandle, float px, float py) -> bool
    // Checks the cell containing the point and all 8 neighbors.
    template<typename BoundsCheckFn>
    EntityHandle findAtPoint(float px, float py, BoundsCheckFn&& boundsCheck) const {
        EntityHandle best = NULL_ENTITY_HANDLE;
        float bestDistSq = (std::numeric_limits<float>::max)();

        int cx = toCell(px);
        int cy = toCell(py);

        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                int nx = cx + dx;
                int ny = cy + dy;
                // Clamp to grid bounds
                if (nx < 0 || ny < 0 ||
                    static_cast<uint32_t>(nx) >= (1u << gridBits_) ||
                    static_cast<uint32_t>(ny) >= (1u << gridBits_)) {
                    continue;
                }
                uint32_t ci = cellIndexFromCell(nx, ny);
                uint32_t start = cellStarts_[ci];
                uint32_t end   = cellStarts_[ci + 1];
                for (uint32_t s = start; s < end; ++s) {
                    uint32_t idx = sorted_[s];
                    if (boundsCheck(handles_[idx], px, py)) {
                        float ddx = posX_[idx] - px;
                        float ddy = posY_[idx] - py;
                        float dSq = ddx * ddx + ddy * ddy;
                        if (dSq < bestDistSq) {
                            bestDistSq = dSq;
                            best = handles_[idx];
                        }
                    }
                }
            }
        }

        return best;
    }

private:
    // Grid configuration
    float tileSize_    = 0.0f;
    float cellSizePx_  = 0.0f;
    float invCellSize_ = 0.0f;
    float mapWidthPx_  = 0.0f;
    float mapHeightPx_ = 0.0f;
    uint32_t gridW_      = 0;
    uint32_t gridH_      = 0;
    uint32_t gridBits_   = 0;
    uint32_t totalCells_ = 0;
    uint32_t cellMask_   = 0;
    bool initialized_    = false;

    // Per-frame entity data (rebuilt every frame)
    uint32_t count_ = 0;
    std::vector<EntityHandle> handles_;
    std::vector<float>        posX_;
    std::vector<float>        posY_;
    std::vector<uint32_t>     cellHashes_;
    std::vector<uint32_t>     sorted_;

    // Cell arrays (allocated from Arena, persistent)
    uint32_t* cellCounts_ = nullptr;
    uint32_t* cellStarts_ = nullptr;

    // Convert pixel coordinate to cell index (clamped)
    int toCell(float v) const {
        int c = static_cast<int>(std::floor(v * invCellSize_));
        if (c < 0) c = 0;
        if (static_cast<uint32_t>(c) > cellMask_) c = static_cast<int>(cellMask_);
        return c;
    }

    // Zero-hash cell index: (ty << gridBits_) | tx
    uint32_t cellIndex(float px, float py) const {
        int tx = toCell(px);
        int ty = toCell(py);
        return (static_cast<uint32_t>(ty) << gridBits_) | static_cast<uint32_t>(tx);
    }

    // Cell index from already-computed cell coordinates (assumed in bounds)
    uint32_t cellIndexFromCell(int tx, int ty) const {
        return (static_cast<uint32_t>(ty) << gridBits_) | static_cast<uint32_t>(tx);
    }

    // Compute the range of cells that overlap a circle at (cx, cy) with radius.
    // Results are clamped to valid grid bounds.
    void cellRange(float cx, float cy, float radius,
                   int& minTX, int& minTY, int& maxTX, int& maxTY) const {
        minTX = toCell(cx - radius);
        minTY = toCell(cy - radius);
        maxTX = toCell(cx + radius);
        maxTY = toCell(cy + radius);
    }

    // ceil(log2(n)) for n >= 1
    static uint32_t ceilLog2(uint32_t n) {
        if (n <= 1) return 0;
        return static_cast<uint32_t>(std::bit_width(n - 1));
    }
};

} // namespace fate
