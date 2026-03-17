#pragma once
#include "engine/core/types.h"
#include <vector>
#include <cstdint>
#include <cmath>
#include <string>
#include <algorithm>

namespace fate {

// Forward declarations from tilemap.h
struct TilemapLayer;
struct Tileset;

// ============================================================================
// Constants
// ============================================================================
constexpr int CHUNK_SIZE = 32; // tiles per chunk side

// ============================================================================
// ChunkState — 7-state lifecycle
// ============================================================================
enum class ChunkState : uint8_t {
    Queued,     // Identified for loading, awaiting slot
    Loading,    // Tile data being read (future: async)
    Setup,      // Entities being deserialized, spatial registration
    Active,     // Fully simulated, rendered
    Sleeping,   // Data retained, entities dormant
    Unloading,  // Serializing dirty state
    Evicted     // Memory freed
};

// ============================================================================
// Ticket level -> target state mapping
//   4 -> Active
//   3 -> Sleeping
//   2 -> Loading/Setup (transition through Loading then Setup)
//   1 -> Queued
//   0 -> Unloading -> Evicted
// ============================================================================
inline ChunkState targetStateForTicket(uint8_t ticketLevel) {
    switch (ticketLevel) {
        case 4:  return ChunkState::Active;
        case 3:  return ChunkState::Sleeping;
        case 2:  return ChunkState::Setup;  // goes through Loading first
        case 1:  return ChunkState::Queued;
        default: return ChunkState::Evicted; // 0 -> Unloading -> Evicted
    }
}

// ============================================================================
// ChunkData — a single 32x32 tile region within one layer
// ============================================================================
struct ChunkData {
    int chunkX = 0;          // chunk coordinate (not tile or pixel)
    int chunkY = 0;
    int layerIndex = 0;
    std::vector<int> tiles;  // CHUNK_SIZE * CHUNK_SIZE tile GIDs
    ChunkState state = ChunkState::Active;
    uint8_t ticketLevel = 0; // highest active ticket determines target state
    bool dirty = false;      // has been modified since last save

    // Double-buffered staging for async loading
    std::vector<int> stagingTiles;
    bool stagingReady = false;

    // Get a tile GID by local tile coordinates within this chunk.
    // Returns 0 if out of bounds.
    int getTile(int localX, int localY) const {
        if (localX < 0 || localX >= CHUNK_SIZE ||
            localY < 0 || localY >= CHUNK_SIZE)
            return 0;
        int idx = localY * CHUNK_SIZE + localX;
        if (idx < 0 || idx >= (int)tiles.size()) return 0;
        return tiles[idx];
    }

    // Advance one step toward the target state for the current ticket level.
    // Returns true if the state actually changed.
    bool stepTowardTarget() {
        ChunkState target = targetStateForTicket(ticketLevel);

        if (state == target) return false;

        // Determine valid next state based on current state and target
        ChunkState next = state;

        // Upward transitions (toward Active)
        if (target == ChunkState::Active || target == ChunkState::Setup ||
            target == ChunkState::Loading || target == ChunkState::Queued) {
            switch (state) {
                case ChunkState::Evicted:   next = ChunkState::Queued;  break;
                case ChunkState::Unloading: next = ChunkState::Queued;  break;
                case ChunkState::Queued:
                    if (target == ChunkState::Queued) return false;
                    next = ChunkState::Loading;
                    break;
                case ChunkState::Loading:
                    if (target == ChunkState::Setup || target == ChunkState::Active)
                        next = ChunkState::Setup;
                    else return false;
                    break;
                case ChunkState::Setup:
                    if (target == ChunkState::Active)
                        next = ChunkState::Active;
                    else return false;
                    break;
                case ChunkState::Sleeping:
                    if (target == ChunkState::Active)
                        next = ChunkState::Active;
                    else return false;
                    break;
                case ChunkState::Active:
                    return false; // already at max
            }
        }
        // Downward transitions (toward Evicted)
        else if (target == ChunkState::Sleeping) {
            switch (state) {
                case ChunkState::Active:  next = ChunkState::Sleeping; break;
                case ChunkState::Evicted: next = ChunkState::Queued;   break;
                case ChunkState::Queued:  next = ChunkState::Loading;  break;
                case ChunkState::Loading: next = ChunkState::Setup;    break;
                case ChunkState::Setup:   next = ChunkState::Sleeping; break;
                case ChunkState::Unloading: next = ChunkState::Queued; break;
                default: return false;
            }
        }
        else if (target == ChunkState::Evicted) {
            switch (state) {
                case ChunkState::Active:    next = ChunkState::Sleeping;  break;
                case ChunkState::Sleeping:  next = ChunkState::Unloading; break;
                case ChunkState::Unloading: next = ChunkState::Evicted;   break;
                case ChunkState::Setup:     next = ChunkState::Unloading; break;
                case ChunkState::Loading:   next = ChunkState::Unloading; break;
                case ChunkState::Queued:    next = ChunkState::Evicted;   break;
                default: return false;
            }
        }

        if (next == state) return false;
        state = next;
        return true;
    }
};

// ============================================================================
// ChunkLayer — all chunks for a single tilemap layer
// ============================================================================
struct ChunkLayer {
    std::string name;
    bool visible = true;
    float opacity = 1.0f;
    bool isCollisionLayer = false;
    int widthInChunks = 0;   // number of chunks across
    int heightInChunks = 0;  // number of chunks down
    std::vector<ChunkData> chunks; // flat array: cy * widthInChunks + cx

    // Mutable access to a chunk by chunk coordinates
    ChunkData* getChunk(int cx, int cy) {
        if (cx < 0 || cx >= widthInChunks || cy < 0 || cy >= heightInChunks)
            return nullptr;
        return &chunks[cy * widthInChunks + cx];
    }

    // Const access to a chunk by chunk coordinates
    const ChunkData* getChunk(int cx, int cy) const {
        if (cx < 0 || cx >= widthInChunks || cy < 0 || cy >= heightInChunks)
            return nullptr;
        return &chunks[cy * widthInChunks + cx];
    }
};

// ============================================================================
// ChunkManager — builds, manages, and queries chunk data
// ============================================================================
class ChunkManager {
public:
    // Tuning knobs
    int maxTransitionsPerFrame = 4; // rate-limit state transitions per update
    int prefetchBuffer = 2;        // extra ring for pre-loading beyond sleep

    // Split flat tile data from TilemapLayers into 32x32 chunks.
    // Handles maps whose dimensions are not divisible by CHUNK_SIZE
    // (partial edge chunks are padded with 0).
    // Implemented in tilemap.cpp where TilemapLayer is fully defined.
    void buildFromLayers(const std::vector<TilemapLayer>& layers,
                         int mapWidth, int mapHeight);

    // Update every chunk's ticket level based on concentric rings around
    // the camera, then step rate-limited state transitions.
    //   visibleBounds – world-space rect the camera sees
    //   origin        – tilemap world-space origin
    //   tileWidth/Height – pixel size of one tile
    //   activeBuffer  – extra chunks beyond visible kept Active (ticket 4)
    //   sleepBuffer   – extra chunks beyond active kept Sleeping (ticket 3)
    // Prefetch ring (prefetchBuffer) beyond sleep gets ticket 2.
    // Everything beyond gets ticket 0.
    void updateChunkStates(Rect visibleBounds, Vec2 origin,
                           int tileWidth, int tileHeight,
                           int activeBuffer = 2, int sleepBuffer = 4) {
        // Visible bounds in tile space
        float tLeft   = (visibleBounds.x - origin.x) / tileWidth;
        float tTop    = (visibleBounds.y - origin.y) / tileHeight;
        float tRight  = (visibleBounds.x + visibleBounds.w - origin.x) / tileWidth;
        float tBottom = (visibleBounds.y + visibleBounds.h - origin.y) / tileHeight;

        // Visible chunk range
        int cxMin = (int)std::floor(tLeft   / CHUNK_SIZE);
        int cyMin = (int)std::floor(tTop    / CHUNK_SIZE);
        int cxMax = (int)std::floor(tRight  / CHUNK_SIZE);
        int cyMax = (int)std::floor(tBottom / CHUNK_SIZE);

        // Phase 1: Assign ticket levels based on concentric ring distance
        for (auto& cl : chunkLayers_) {
            for (int cy = 0; cy < cl.heightInChunks; ++cy) {
                for (int cx = 0; cx < cl.widthInChunks; ++cx) {
                    ChunkData& cd = cl.chunks[cy * cl.widthInChunks + cx];

                    // Chebyshev distance from visible range
                    int dx = 0, dy = 0;
                    if (cx < cxMin) dx = cxMin - cx;
                    else if (cx > cxMax) dx = cx - cxMax;
                    if (cy < cyMin) dy = cyMin - cy;
                    else if (cy > cyMax) dy = cy - cyMax;
                    int dist = (dx > dy) ? dx : dy;

                    if (dist <= activeBuffer)
                        cd.ticketLevel = 4; // Active
                    else if (dist <= activeBuffer + sleepBuffer)
                        cd.ticketLevel = 3; // Sleeping
                    else if (dist <= activeBuffer + sleepBuffer + prefetchBuffer)
                        cd.ticketLevel = 2; // Loading/Setup (prefetch)
                    else
                        cd.ticketLevel = 0; // Unloading -> Evicted
                }
            }
        }

        // Phase 2: Rate-limited state transitions
        int transitionsUsed = 0;
        for (auto& cl : chunkLayers_) {
            for (auto& cd : cl.chunks) {
                if (transitionsUsed >= maxTransitionsPerFrame) return;
                if (cd.stepTowardTarget()) {
                    ++transitionsUsed;
                }
            }
        }
    }

    // Swap staging buffers into active tile data for chunks that have
    // finished async loading. Call once per frame after loading completes.
    void swapStagingBuffers() {
        for (auto& cl : chunkLayers_) {
            for (auto& cd : cl.chunks) {
                if (cd.stagingReady) {
                    cd.tiles = std::move(cd.stagingTiles);
                    cd.stagingTiles.clear();
                    cd.stagingReady = false;
                }
            }
        }
    }

    // Check if a world-space rect overlaps any collision tile via Active chunks.
    bool checkCollision(const Rect& worldRect, Vec2 origin,
                        int tileWidth, int tileHeight) const {
        for (auto& cl : chunkLayers_) {
            if (!cl.isCollisionLayer) continue;

            // World rect to tile range
            int startCol = (int)std::floor((worldRect.x - origin.x) / tileWidth);
            int startRow = (int)std::floor((worldRect.y - origin.y) / tileHeight);
            int endCol   = (int)std::floor((worldRect.x + worldRect.w - origin.x) / tileWidth);
            int endRow   = (int)std::floor((worldRect.y + worldRect.h - origin.y) / tileHeight);

            if (startCol < 0) startCol = 0;
            if (startRow < 0) startRow = 0;

            for (int row = startRow; row <= endRow; ++row) {
                for (int col = startCol; col <= endCol; ++col) {
                    int cx = col / CHUNK_SIZE;
                    int cy = row / CHUNK_SIZE;
                    const ChunkData* cd = cl.getChunk(cx, cy);
                    if (!cd) continue;
                    // Only query Active chunks for collision
                    if (cd->state != ChunkState::Active) continue;
                    int localX = col - cx * CHUNK_SIZE;
                    int localY = row - cy * CHUNK_SIZE;
                    if (cd->getTile(localX, localY) > 0)
                        return true;
                }
            }
        }
        return false;
    }

    // Get a tile GID at a world position within a named layer.
    // Only queries Active chunks.
    int getTileAt(const std::string& layerName, float worldX, float worldY,
                  Vec2 origin, int tileW, int tileH) const {
        for (auto& cl : chunkLayers_) {
            if (cl.name != layerName) continue;

            int col = (int)std::floor((worldX - origin.x) / tileW);
            int row = (int)std::floor((worldY - origin.y) / tileH);
            if (col < 0 || row < 0) return 0;

            int cx = col / CHUNK_SIZE;
            int cy = row / CHUNK_SIZE;
            const ChunkData* cd = cl.getChunk(cx, cy);
            if (!cd) return 0;
            // Only query Active chunks
            if (cd->state != ChunkState::Active) return 0;
            return cd->getTile(col - cx * CHUNK_SIZE, row - cy * CHUNK_SIZE);
        }
        return 0;
    }

    // Accessors
    std::vector<ChunkLayer>& layers() { return chunkLayers_; }
    const std::vector<ChunkLayer>& layers() const { return chunkLayers_; }

private:
    std::vector<ChunkLayer> chunkLayers_;
};

} // namespace fate
