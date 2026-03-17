#pragma once
#include "engine/core/types.h"
#include <vector>
#include <cstdint>
#include <cmath>
#include <string>

namespace fate {

// Forward declarations from tilemap.h
struct TilemapLayer;
struct Tileset;

// ============================================================================
// Constants
// ============================================================================
constexpr int CHUNK_SIZE = 32; // tiles per chunk side

// ============================================================================
// ChunkState
// ============================================================================
enum class ChunkState : uint8_t {
    Active,   // within camera view + active buffer, fully updated
    Sleeping, // within sleep buffer, data retained but not rendered
    Evicted   // outside all buffers, can be unloaded
};

// ============================================================================
// ChunkData — a single 32x32 tile region within one layer
// ============================================================================
struct ChunkData {
    int chunkX = 0;          // chunk coordinate (not tile or pixel)
    int chunkY = 0;
    int layerIndex = 0;
    std::vector<int> tiles;  // CHUNK_SIZE * CHUNK_SIZE tile GIDs
    ChunkState state = ChunkState::Active;
    bool dirty = false;

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
    // Split flat tile data from TilemapLayers into 32x32 chunks.
    // Handles maps whose dimensions are not divisible by CHUNK_SIZE
    // (partial edge chunks are padded with 0).
    // Implemented in tilemap.cpp where TilemapLayer is fully defined.
    void buildFromLayers(const std::vector<TilemapLayer>& layers,
                         int mapWidth, int mapHeight);

    // Update every chunk's state based on camera proximity.
    //   visibleBounds – world-space rect the camera sees
    //   origin        – tilemap world-space origin
    //   tileWidth/Height – pixel size of one tile
    //   activeBuffer  – extra chunks beyond visible kept Active
    //   sleepBuffer   – extra chunks beyond active kept Sleeping
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

        for (auto& cl : chunkLayers_) {
            for (int cy = 0; cy < cl.heightInChunks; ++cy) {
                for (int cx = 0; cx < cl.widthInChunks; ++cx) {
                    ChunkData& cd = cl.chunks[cy * cl.widthInChunks + cx];

                    // Distance in chunk coords from visible range
                    int dx = 0, dy = 0;
                    if (cx < cxMin) dx = cxMin - cx;
                    else if (cx > cxMax) dx = cx - cxMax;
                    if (cy < cyMin) dy = cyMin - cy;
                    else if (cy > cyMax) dy = cy - cyMax;
                    int dist = (dx > dy) ? dx : dy; // Chebyshev distance

                    if (dist <= activeBuffer)
                        cd.state = ChunkState::Active;
                    else if (dist <= activeBuffer + sleepBuffer)
                        cd.state = ChunkState::Sleeping;
                    else
                        cd.state = ChunkState::Evicted;
                }
            }
        }
    }

    // Check if a world-space rect overlaps any collision tile via chunks.
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
