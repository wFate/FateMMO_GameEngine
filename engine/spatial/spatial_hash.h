#pragma once
#include "engine/core/types.h"
#include <vector>
#include <cmath>
#include <cstdint>
#include <limits>
#include <functional>

namespace fate {

// ============================================================================
// SpatialHashEngine -- dense spatial hash rebuilt each frame via prefix sums
//
// Mueller-style counting-sort approach: two flat arrays (cellStart_ / entries_)
// rebuilt every frame. No per-cell heap allocations, no hash-map overhead.
//
// Frame loop:
//   1. beginRebuild(maxEntities)
//   2. addEntity(id, pos)  -- for each entity
//   3. endRebuild()        -- counting sort via prefix sums
//   4. findNearest / queryRadius / findAtPoint
// ============================================================================
class SpatialHashEngine {
public:
    explicit SpatialHashEngine(float cellSize = 128.0f, uint32_t tableSize = 4096)
        : cellSize_(cellSize), invCellSize_(1.0f / cellSize), tableSize_(tableSize)
    {
        cellCount_.resize(tableSize_, 0);
        cellStart_.resize(tableSize_ + 1, 0);
    }

    // -- Configuration --------------------------------------------------------

    float cellSize() const { return cellSize_; }
    uint32_t tableSize() const { return tableSize_; }

    // -- Build API ------------------------------------------------------------

    void beginRebuild(uint32_t maxEntities) {
        count_ = 0;
        entries_.clear();
        entries_.reserve(maxEntities);
        cellHashes_.clear();
        cellHashes_.reserve(maxEntities);
        std::fill(cellCount_.begin(), cellCount_.end(), 0);
    }

    void addEntity(EntityId id, Vec2 position) {
        uint32_t h = hashCell(toCell(position.x), toCell(position.y));
        entries_.push_back({ id, position, 0 });  // sortedIndex filled later
        cellHashes_.push_back(h);
        cellCount_[h]++;
        count_++;
    }

    void endRebuild() {
        // Prefix sum: cellStart_[h] = where bucket h begins in sorted_
        cellStart_[0] = 0;
        for (uint32_t i = 0; i < tableSize_; i++) {
            cellStart_[i + 1] = cellStart_[i] + cellCount_[i];
        }

        // Build sorted array via counting sort
        sorted_.resize(count_);
        // Use a running write-cursor per bucket (reuse cellCount_ as cursor)
        std::vector<uint32_t> cursor(cellStart_.begin(), cellStart_.begin() + tableSize_);

        for (uint32_t i = 0; i < count_; i++) {
            uint32_t h = cellHashes_[i];
            uint32_t dest = cursor[h]++;
            sorted_[dest] = i;  // index into entries_
        }
    }

    // -- Query API ------------------------------------------------------------

    // Find the nearest entity within radius. Returns INVALID_ENTITY if none.
    // filterFn(EntityId) -> bool: return true to consider this entity.
    template<typename FilterFn>
    EntityId findNearest(Vec2 center, float radius, FilterFn&& filter) const {
        EntityId best = INVALID_ENTITY;
        float bestDistSq = radius * radius;

        int minCX, minCY, maxCX, maxCY;
        cellRange(center, radius, minCX, minCY, maxCX, maxCY);

        for (int cy = minCY; cy <= maxCY; cy++) {
            for (int cx = minCX; cx <= maxCX; cx++) {
                uint32_t h = hashCell(cx, cy);
                uint32_t start = cellStart_[h];
                uint32_t end   = cellStart_[h + 1];

                for (uint32_t s = start; s < end; s++) {
                    const auto& e = entries_[sorted_[s]];
                    float dSq = (e.position - center).lengthSq();
                    if (dSq < bestDistSq && filter(e.id)) {
                        bestDistSq = dSq;
                        best = e.id;
                    }
                }
            }
        }
        return best;
    }

    // Overload without filter -- considers all entities
    EntityId findNearest(Vec2 center, float radius) const {
        return findNearest(center, radius, [](EntityId) { return true; });
    }

    // Collect all entities within radius into results vector.
    // filterFn(EntityId) -> bool: return true to include.
    template<typename FilterFn>
    void queryRadius(Vec2 center, float radius,
                     std::vector<EntityId>& results, FilterFn&& filter) const {
        float radiusSq = radius * radius;

        int minCX, minCY, maxCX, maxCY;
        cellRange(center, radius, minCX, minCY, maxCX, maxCY);

        for (int cy = minCY; cy <= maxCY; cy++) {
            for (int cx = minCX; cx <= maxCX; cx++) {
                uint32_t h = hashCell(cx, cy);
                uint32_t start = cellStart_[h];
                uint32_t end   = cellStart_[h + 1];

                for (uint32_t s = start; s < end; s++) {
                    const auto& e = entries_[sorted_[s]];
                    float dSq = (e.position - center).lengthSq();
                    if (dSq <= radiusSq && filter(e.id)) {
                        results.push_back(e.id);
                    }
                }
            }
        }
    }

    // Find entity whose sprite bounds contain a point (for click/touch targeting).
    // boundsCheckFn(EntityId, Vec2 point) -> bool: return true if point is inside entity.
    template<typename BoundsCheckFn>
    EntityId findAtPoint(Vec2 point, BoundsCheckFn&& boundsCheck) const {
        EntityId best = INVALID_ENTITY;
        float bestDistSq = std::numeric_limits<float>::max();

        int cx = toCell(point.x);
        int cy = toCell(point.y);

        // Check the cell containing the point and all 8 neighbors
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                uint32_t h = hashCell(cx + dx, cy + dy);
                uint32_t start = cellStart_[h];
                uint32_t end   = cellStart_[h + 1];

                for (uint32_t s = start; s < end; s++) {
                    const auto& e = entries_[sorted_[s]];
                    if (boundsCheck(e.id, point)) {
                        float dSq = (e.position - point).lengthSq();
                        if (dSq < bestDistSq) {
                            bestDistSq = dSq;
                            best = e.id;
                        }
                    }
                }
            }
        }
        return best;
    }

    // -- Stats ----------------------------------------------------------------

    uint32_t entityCount() const { return count_; }

private:
    struct Entry {
        EntityId id;
        Vec2 position;
        uint32_t sortedIndex;  // reserved for future use
    };

    float cellSize_;
    float invCellSize_;
    uint32_t tableSize_;
    uint32_t count_ = 0;

    std::vector<Entry>    entries_;      // unsorted entity list
    std::vector<uint32_t> cellHashes_;   // parallel to entries_: hash per entity
    std::vector<uint32_t> sorted_;       // indices into entries_, sorted by bucket
    std::vector<uint32_t> cellCount_;    // count per bucket (reset each frame)
    std::vector<uint32_t> cellStart_;    // prefix sum: start offset per bucket

    int toCell(float v) const {
        return static_cast<int>(std::floor(v * invCellSize_));
    }

    uint32_t hashCell(int cx, int cy) const {
        // Mueller-style spatial hash
        uint32_t raw = static_cast<uint32_t>(
            std::abs(cx * 92837111 ^ cy * 689287499));
        return raw % tableSize_;
    }

    void cellRange(Vec2 center, float radius,
                   int& minCX, int& minCY, int& maxCX, int& maxCY) const {
        minCX = toCell(center.x - radius);
        minCY = toCell(center.y - radius);
        maxCX = toCell(center.x + radius);
        maxCY = toCell(center.y + radius);
    }
};

} // namespace fate
