#pragma once
#include "engine/tilemap/chunk.h"
#include "engine/render/sprite_batch.h" // for SpriteVertex
#include "engine/render/shader.h"
#include "engine/render/gfx/types.h"
#include "engine/render/gfx/device.h"
#include "engine/core/types.h"
#include <vector>
#include <cstdint>
#include <unordered_map>

namespace fate {

struct Tileset; // forward decl (defined in tilemap.h)

// Per-chunk GPU resources
struct ChunkGPU {
    gfx::BufferHandle vboHandle{};
    gfx::BufferHandle eboHandle{};
    gfx::PipelineHandle pipeline{};
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ebo = 0;
    int vertexCount = 0;
    int indexCount = 0;
    bool uploaded = false;
    unsigned int textureId = 0; // the tileset GL texture for this chunk

    // Multi-texture sub-batches: when a chunk has tiles from multiple tilesets,
    // we record per-texture draw ranges.
    struct SubBatch {
        unsigned int textureId = 0;
        int indexOffset = 0; // byte offset into the EBO
        int indexCount = 0;
    };
    std::vector<SubBatch> subBatches;
};

class ChunkRenderer {
public:
    bool init();
    void shutdown();

    // Rebuild vertex data for a chunk. Call when chunk.dirty is set or on first load.
    void rebuildChunk(ChunkData& chunk, const std::vector<Tileset>& tilesets,
                      Vec2 mapOrigin, int tileW, int tileH, float layerDepth,
                      float layerOpacity);

    // Render all uploaded chunks for one layer.
    void renderLayer(const ChunkLayer& layer, const Mat4& viewProjection,
                     const Rect& visibleBounds, Vec2 mapOrigin,
                     int tileW, int tileH);

    // Release GPU resources for evicted chunks
    void releaseChunk(int chunkX, int chunkY, int layerIndex);

private:
    // Key: packed (layerIndex << 20) | (chunkY << 10) | chunkX
    static int packKey(int cx, int cy, int li) { return (li << 20) | (cy << 10) | cx; }
    std::unordered_map<int, ChunkGPU> gpuChunks_;

    // Shader (reuses the existing sprite shader source)
    Shader shader_;
    gfx::PipelineHandle pipeline_{}; // shared pipeline for all chunk draws

    // Temp buffers to avoid per-frame allocation
    std::vector<SpriteVertex> vertexBuf_;
    std::vector<unsigned int> indexBuf_;

    ChunkGPU& getOrCreate(int key);

    // The Tileset* finder helper -- matches Tilemap::findTileset logic
    static const Tileset* findTileset(int gid, const std::vector<Tileset>& tilesets);
};

} // namespace fate
