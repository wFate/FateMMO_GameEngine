#include "engine/tilemap/chunk_renderer.h"
#include "engine/tilemap/tilemap.h" // for Tileset definition
#include "engine/render/tile_texture_array.h"
#include "engine/render/gfx/backend/gl/gl_loader.h"
#include "engine/core/logger.h"
#include <algorithm>

namespace fate {

// Embedded shader sources -- identical to SpriteBatch's sprite shader.
// ChunkRenderer only uses renderType 0 (plain sprites), but we include
// the full shader so the same vertex layout is compatible.
static const char* CHUNK_VERT_SRC = R"(
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec4 aColor;
layout (location = 3) in float aRenderType;

out vec2 v_uv;
out vec4 v_color;
out float v_renderType;

uniform mat4 uViewProjection;

void main() {
    gl_Position = uViewProjection * vec4(aPos, 0.0, 1.0);
    v_uv = aTexCoord;
    v_color = aColor;
    v_renderType = aRenderType;
}
)";

static const char* CHUNK_FRAG_SRC = R"(
in vec2 v_uv;
in vec4 v_color;
in float v_renderType;

out vec4 fragColor;

uniform sampler2D uTexture;

void main() {
    fragColor = texture(uTexture, v_uv) * v_color;
    if (fragColor.a < 0.01) discard;
}
)";

// Embedded tile array shader fallbacks
static const char* TILE_ARRAY_VERT_SRC = R"(
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in float aLayerIndex;
layout(location = 3) in vec4 aColor;

uniform mat4 uViewProjection;

out vec2 v_uv;
out float v_layerIndex;
out vec4 v_color;

void main() {
    gl_Position = uViewProjection * vec4(aPos, 0.0, 1.0);
    v_uv = aTexCoord;
    v_layerIndex = aLayerIndex;
    v_color = aColor;
}
)";

static const char* TILE_ARRAY_FRAG_SRC = R"(
in vec2 v_uv;
in float v_layerIndex;
in vec4 v_color;

uniform sampler2DArray u_tileArray;

out vec4 fragColor;

void main() {
    vec4 texel = texture(u_tileArray, vec3(v_uv, v_layerIndex));
    fragColor = texel * v_color;
    if (fragColor.a < 0.01) discard;
}
)";

bool ChunkRenderer::init() {
    // Try external shader files first, then fall back to embedded
    if (!shader_.loadFromFile("assets/shaders/sprite.vert", "assets/shaders/sprite.frag")) {
        LOG_WARN("ChunkRenderer", "External shaders not found, using embedded chunk shaders");
        if (!shader_.loadFromSource(CHUNK_VERT_SRC, CHUNK_FRAG_SRC)) {
            LOG_ERROR("ChunkRenderer", "Failed to compile chunk shaders");
            return false;
        }
    }

    // Create a shared pipeline using the SpriteVertex layout
    auto& device = gfx::Device::instance();
    gfx::VertexLayout layout;
    layout.stride = sizeof(SpriteVertex);
    layout.attributes = {
        {0, 2, offsetof(SpriteVertex, x),          false}, // aPos
        {1, 2, offsetof(SpriteVertex, u),          false}, // aTexCoord
        {2, 4, offsetof(SpriteVertex, r),          false}, // aColor
        {3, 1, offsetof(SpriteVertex, renderType), false}, // aRenderType
    };

    gfx::PipelineDesc desc;
    desc.shader = shader_.gfxHandle();
    desc.vertexLayout = layout;
    desc.blendMode = gfx::BlendMode::Alpha;
    desc.depthTest = false;
    desc.depthWrite = false;
    pipeline_ = device.createPipeline(desc);

    vertexBuf_.reserve(CHUNK_SIZE * CHUNK_SIZE * 4); // 4 verts per tile max
    indexBuf_.reserve(CHUNK_SIZE * CHUNK_SIZE * 6);   // 6 indices per tile max

    LOG_INFO("ChunkRenderer", "Initialized");
    return true;
}

void ChunkRenderer::shutdown() {
    auto& device = gfx::Device::instance();

    for (auto& [key, gpu] : gpuChunks_) {
        if (gpu.vboHandle.valid()) device.destroy(gpu.vboHandle);
        if (gpu.eboHandle.valid()) device.destroy(gpu.eboHandle);
    }
    gpuChunks_.clear();

    if (pipeline_.valid()) { device.destroy(pipeline_); pipeline_ = {}; }
    if (tileArrayPipeline_.valid()) { device.destroy(tileArrayPipeline_); tileArrayPipeline_ = {}; }

    LOG_INFO("ChunkRenderer", "Shutdown");
}

const Tileset* ChunkRenderer::findTileset(int gid, const std::vector<Tileset>& tilesets) {
    // Tilesets are sorted by firstGid descending (same as Tilemap::findTileset)
    for (auto& ts : tilesets) {
        if (gid >= ts.firstGid) return &ts;
    }
    return nullptr;
}

ChunkGPU& ChunkRenderer::getOrCreate(int key) {
    auto it = gpuChunks_.find(key);
    if (it != gpuChunks_.end()) return it->second;
    return gpuChunks_[key];
}

void ChunkRenderer::setTileArray(TileTextureArray* array) {
    tileArray_ = array;
    useTextureArray_ = (array != nullptr);

    if (!useTextureArray_) return;

    // Load the tile array shader
    if (!tileArrayShader_.loadFromFile("assets/shaders/tile_chunk.vert",
                                        "assets/shaders/tile_chunk.frag")) {
        LOG_WARN("ChunkRenderer", "External tile_chunk shaders not found, using embedded");
        if (!tileArrayShader_.loadFromSource(TILE_ARRAY_VERT_SRC, TILE_ARRAY_FRAG_SRC)) {
            LOG_ERROR("ChunkRenderer", "Failed to compile tile array shaders, disabling");
            useTextureArray_ = false;
            tileArray_ = nullptr;
            return;
        }
    }

    // Create pipeline for TileChunkVertex layout
    auto& device = gfx::Device::instance();
    gfx::VertexLayout layout;
    layout.stride = sizeof(TileChunkVertex);
    layout.attributes = {
        {0, 2, offsetof(TileChunkVertex, x),          false}, // aPos
        {1, 2, offsetof(TileChunkVertex, u),          false}, // aTexCoord
        {2, 1, offsetof(TileChunkVertex, layerIndex), false}, // aLayerIndex
        {3, 4, offsetof(TileChunkVertex, r),          false}, // aColor
    };

    gfx::PipelineDesc desc;
    desc.shader = tileArrayShader_.gfxHandle();
    desc.vertexLayout = layout;
    desc.blendMode = gfx::BlendMode::Alpha;
    desc.depthTest = false;
    desc.depthWrite = false;
    tileArrayPipeline_ = device.createPipeline(desc);

    tileVertexBuf_.reserve(CHUNK_SIZE * CHUNK_SIZE * 4);

    LOG_INFO("ChunkRenderer", "Texture array mode enabled (%d layers)",
             tileArray_->layerCount());
}

// Helper: used for sorting vertices by tileset texture for sub-batching.
struct TileSortEntry {
    unsigned int textureId;
    int vertStart; // index into vertexBuf_ where this tile's 4 verts begin
};

void ChunkRenderer::rebuildChunk(ChunkData& chunk, const std::vector<Tileset>& tilesets,
                                  Vec2 mapOrigin, int tileW, int tileH,
                                  float /*layerDepth*/, float layerOpacity) {
    // Texture array path: single draw call per chunk, no sub-batching needed
    if (useTextureArray_) {
        rebuildChunkTextureArray(chunk, tilesets, mapOrigin, tileW, tileH, layerOpacity);
        return;
    }

    vertexBuf_.clear();
    indexBuf_.clear();

    float opacity = layerOpacity;

    // Collect tiles and sort by texture for sub-batching
    std::vector<TileSortEntry> sortEntries;
    std::vector<SpriteVertex> tempVerts;
    tempVerts.reserve(CHUNK_SIZE * CHUNK_SIZE * 4);

    for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            int idx = ly * CHUNK_SIZE + lx;
            if (idx >= (int)chunk.tiles.size()) continue;
            int gid = chunk.tiles[idx];
            if (gid <= 0) continue;

            const Tileset* ts = findTileset(gid, tilesets);
            if (!ts || !ts->texture) continue;

            int localId = gid - ts->firstGid;
            Rect uv = ts->getTileUV(localId);

            // World position of tile center
            float worldX = mapOrigin.x + (chunk.chunkX * CHUNK_SIZE + lx) * tileW + tileW * 0.5f;
            float worldY = mapOrigin.y + (chunk.chunkY * CHUNK_SIZE + ly) * tileH + tileH * 0.5f;
            float halfW = tileW * 0.5f;
            float halfH = tileH * 0.5f;

            float u0 = uv.x, v0 = uv.y;
            float u1 = uv.x + uv.w, v1 = uv.y + uv.h;

            int vertStart = (int)tempVerts.size();
            sortEntries.push_back({ts->texture->id(), vertStart});

            // 4 vertices: top-left, top-right, bottom-right, bottom-left
            tempVerts.push_back({worldX - halfW, worldY - halfH, u0, v0, 1, 1, 1, opacity, 0});
            tempVerts.push_back({worldX + halfW, worldY - halfH, u1, v0, 1, 1, 1, opacity, 0});
            tempVerts.push_back({worldX + halfW, worldY + halfH, u1, v1, 1, 1, 1, opacity, 0});
            tempVerts.push_back({worldX - halfW, worldY + halfH, u0, v1, 1, 1, 1, opacity, 0});
        }
    }

    if (sortEntries.empty()) {
        // No visible tiles in this chunk -- mark as uploaded with zero geometry
        int key = packKey(chunk.chunkX, chunk.chunkY, chunk.layerIndex);
        ChunkGPU& gpu = getOrCreate(key);
        gpu.vertexCount = 0;
        gpu.indexCount = 0;
        gpu.uploaded = true;
        gpu.subBatches.clear();
        return;
    }

    // Sort by texture to enable sub-batching
    std::sort(sortEntries.begin(), sortEntries.end(),
        [](const TileSortEntry& a, const TileSortEntry& b) {
            return a.textureId < b.textureId;
        });

    // Build final vertex/index buffers in sorted order, recording sub-batch ranges
    std::vector<ChunkGPU::SubBatch> subBatches;
    unsigned int currentTex = sortEntries[0].textureId;
    int batchIndexStart = 0;

    for (auto& entry : sortEntries) {
        if (entry.textureId != currentTex) {
            // Finish previous sub-batch
            int count = (int)indexBuf_.size() - batchIndexStart;
            subBatches.push_back({currentTex, (int)(batchIndexStart * sizeof(unsigned int)), count});
            currentTex = entry.textureId;
            batchIndexStart = (int)indexBuf_.size();
        }

        int base = (int)vertexBuf_.size();
        // Copy 4 vertices from tempVerts
        for (int v = 0; v < 4; ++v) {
            vertexBuf_.push_back(tempVerts[entry.vertStart + v]);
        }

        // 6 indices per quad
        indexBuf_.push_back(base + 0);
        indexBuf_.push_back(base + 1);
        indexBuf_.push_back(base + 2);
        indexBuf_.push_back(base + 2);
        indexBuf_.push_back(base + 3);
        indexBuf_.push_back(base + 0);
    }

    // Finish last sub-batch
    {
        int count = (int)indexBuf_.size() - batchIndexStart;
        subBatches.push_back({currentTex, (int)(batchIndexStart * sizeof(unsigned int)), count});
    }

    // Upload to GPU
    int key = packKey(chunk.chunkX, chunk.chunkY, chunk.layerIndex);
    ChunkGPU& gpu = getOrCreate(key);

    auto& device = gfx::Device::instance();

    // Create or recreate VBO
    size_t vboSize = vertexBuf_.size() * sizeof(SpriteVertex);
    if (gpu.vboHandle.valid()) {
        device.destroy(gpu.vboHandle);
    }
    gpu.vboHandle = device.createBuffer(gfx::BufferType::Vertex, gfx::BufferUsage::Static,
                                         vboSize, vertexBuf_.data());
    gpu.vbo = device.resolveGLBuffer(gpu.vboHandle);

    // Create or recreate EBO
    size_t eboSize = indexBuf_.size() * sizeof(unsigned int);
    if (gpu.eboHandle.valid()) {
        device.destroy(gpu.eboHandle);
    }
    gpu.eboHandle = device.createBuffer(gfx::BufferType::Index, gfx::BufferUsage::Static,
                                         eboSize, indexBuf_.data());
    gpu.ebo = device.resolveGLBuffer(gpu.eboHandle);

    // Re-resolve VAO from pipeline (shared across all chunks)
    gpu.vao = device.resolveGLPipelineVAO(pipeline_);

    gpu.vertexCount = (int)vertexBuf_.size();
    gpu.indexCount = (int)indexBuf_.size();
    gpu.uploaded = true;
    gpu.textureId = subBatches[0].textureId; // primary texture (for single-tileset chunks)
    gpu.subBatches = std::move(subBatches);
}

void ChunkRenderer::rebuildChunkTextureArray(ChunkData& chunk,
                                              const std::vector<Tileset>& tilesets,
                                              Vec2 mapOrigin, int tileW, int tileH,
                                              float layerOpacity) {
    tileVertexBuf_.clear();
    indexBuf_.clear();

    float opacity = layerOpacity;

    for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            int idx = ly * CHUNK_SIZE + lx;
            if (idx >= (int)chunk.tiles.size()) continue;
            int gid = chunk.tiles[idx];
            if (gid <= 0) continue;

            int layer = tileArray_->gidToLayer(gid);
            if (layer < 0) continue; // unmapped GID

            // World position of tile center
            float worldX = mapOrigin.x + (chunk.chunkX * CHUNK_SIZE + lx) * tileW + tileW * 0.5f;
            float worldY = mapOrigin.y + (chunk.chunkY * CHUNK_SIZE + ly) * tileH + tileH * 0.5f;
            float halfW = tileW * 0.5f;
            float halfH = tileH * 0.5f;

            // UVs are 0-1 since each tile is its own layer (no spritesheet sampling)
            float layerF = (float)layer;
            int base = (int)tileVertexBuf_.size();

            tileVertexBuf_.push_back({worldX - halfW, worldY - halfH, 0.0f, 0.0f, layerF, 1, 1, 1, opacity});
            tileVertexBuf_.push_back({worldX + halfW, worldY - halfH, 1.0f, 0.0f, layerF, 1, 1, 1, opacity});
            tileVertexBuf_.push_back({worldX + halfW, worldY + halfH, 1.0f, 1.0f, layerF, 1, 1, 1, opacity});
            tileVertexBuf_.push_back({worldX - halfW, worldY + halfH, 0.0f, 1.0f, layerF, 1, 1, 1, opacity});

            indexBuf_.push_back(base + 0);
            indexBuf_.push_back(base + 1);
            indexBuf_.push_back(base + 2);
            indexBuf_.push_back(base + 2);
            indexBuf_.push_back(base + 3);
            indexBuf_.push_back(base + 0);
        }
    }

    int key = packKey(chunk.chunkX, chunk.chunkY, chunk.layerIndex);
    ChunkGPU& gpu = getOrCreate(key);

    if (tileVertexBuf_.empty()) {
        gpu.vertexCount = 0;
        gpu.indexCount = 0;
        gpu.uploaded = true;
        gpu.subBatches.clear();
        return;
    }

    auto& device = gfx::Device::instance();

    // Create or recreate VBO (TileChunkVertex sized)
    size_t vboSize = tileVertexBuf_.size() * sizeof(TileChunkVertex);
    if (gpu.vboHandle.valid()) {
        device.destroy(gpu.vboHandle);
    }
    gpu.vboHandle = device.createBuffer(gfx::BufferType::Vertex, gfx::BufferUsage::Static,
                                         vboSize, tileVertexBuf_.data());
    gpu.vbo = device.resolveGLBuffer(gpu.vboHandle);

    // Create or recreate EBO
    size_t eboSize = indexBuf_.size() * sizeof(unsigned int);
    if (gpu.eboHandle.valid()) {
        device.destroy(gpu.eboHandle);
    }
    gpu.eboHandle = device.createBuffer(gfx::BufferType::Index, gfx::BufferUsage::Static,
                                         eboSize, indexBuf_.data());
    gpu.ebo = device.resolveGLBuffer(gpu.eboHandle);

    // Use tile array pipeline VAO
    gpu.vao = device.resolveGLPipelineVAO(tileArrayPipeline_);

    gpu.vertexCount = (int)tileVertexBuf_.size();
    gpu.indexCount = (int)indexBuf_.size();
    gpu.uploaded = true;
    gpu.subBatches.clear(); // no sub-batching needed -- single texture array
}

void ChunkRenderer::renderLayer(const ChunkLayer& layer, const Mat4& viewProjection,
                                 const Rect& visibleBounds, Vec2 mapOrigin,
                                 int tileW, int tileH) {
    if (useTextureArray_) {
        renderLayerTextureArray(layer, viewProjection, visibleBounds, mapOrigin, tileW, tileH);
        return;
    }

    shader_.bind();
    shader_.setMat4("uViewProjection", viewProjection);
    shader_.setInt("uTexture", 0);

    for (auto& chunk : layer.chunks) {
        if (chunk.state != ChunkState::Active) continue;

        int key = packKey(chunk.chunkX, chunk.chunkY, chunk.layerIndex);
        auto it = gpuChunks_.find(key);
        if (it == gpuChunks_.end() || !it->second.uploaded) continue;

        ChunkGPU& gpu = it->second;
        if (gpu.indexCount == 0) continue;

        // Frustum cull the chunk bounds
        float chunkWorldX = mapOrigin.x + chunk.chunkX * CHUNK_SIZE * tileW;
        float chunkWorldY = mapOrigin.y + chunk.chunkY * CHUNK_SIZE * tileH;
        float chunkWorldW = CHUNK_SIZE * (float)tileW;
        float chunkWorldH = CHUNK_SIZE * (float)tileH;
        Rect chunkBounds(chunkWorldX, chunkWorldY, chunkWorldW, chunkWorldH);

        if (!visibleBounds.overlaps(chunkBounds)) continue;

        // Bind this chunk's VBO to the shared VAO
        glBindVertexArray(gpu.vao);
        glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);

        // Re-set vertex attribute pointers for this VBO
        // aPos (location 0): 2 floats at offset 0
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                              (void*)offsetof(SpriteVertex, x));
        glEnableVertexAttribArray(0);
        // aTexCoord (location 1): 2 floats
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                              (void*)offsetof(SpriteVertex, u));
        glEnableVertexAttribArray(1);
        // aColor (location 2): 4 floats
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                              (void*)offsetof(SpriteVertex, r));
        glEnableVertexAttribArray(2);
        // aRenderType (location 3): 1 float
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                              (void*)offsetof(SpriteVertex, renderType));
        glEnableVertexAttribArray(3);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);

        // Draw sub-batches (one per tileset texture)
        for (auto& sb : gpu.subBatches) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sb.textureId);
            glDrawElements(GL_TRIANGLES, sb.indexCount, GL_UNSIGNED_INT,
                           (void*)(intptr_t)sb.indexOffset);
        }
    }

    glBindVertexArray(0);
    shader_.unbind();
}

void ChunkRenderer::renderLayerTextureArray(const ChunkLayer& layer,
                                             const Mat4& viewProjection,
                                             const Rect& visibleBounds, Vec2 mapOrigin,
                                             int tileW, int tileH) {
    tileArrayShader_.bind();
    tileArrayShader_.setMat4("uViewProjection", viewProjection);
    tileArrayShader_.setInt("u_tileArray", 0);

    // Bind the texture array once for all chunks
    tileArray_->bind(0);

    for (auto& chunk : layer.chunks) {
        if (chunk.state != ChunkState::Active) continue;

        int key = packKey(chunk.chunkX, chunk.chunkY, chunk.layerIndex);
        auto it = gpuChunks_.find(key);
        if (it == gpuChunks_.end() || !it->second.uploaded) continue;

        ChunkGPU& gpu = it->second;
        if (gpu.indexCount == 0) continue;

        // Frustum cull the chunk bounds
        float chunkWorldX = mapOrigin.x + chunk.chunkX * CHUNK_SIZE * tileW;
        float chunkWorldY = mapOrigin.y + chunk.chunkY * CHUNK_SIZE * tileH;
        float chunkWorldW = CHUNK_SIZE * (float)tileW;
        float chunkWorldH = CHUNK_SIZE * (float)tileH;
        Rect chunkBounds(chunkWorldX, chunkWorldY, chunkWorldW, chunkWorldH);

        if (!visibleBounds.overlaps(chunkBounds)) continue;

        // Bind this chunk's VBO and set up TileChunkVertex layout
        glBindVertexArray(gpu.vao);
        glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);

        // aPos (location 0): 2 floats
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TileChunkVertex),
                              (void*)offsetof(TileChunkVertex, x));
        glEnableVertexAttribArray(0);
        // aTexCoord (location 1): 2 floats
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TileChunkVertex),
                              (void*)offsetof(TileChunkVertex, u));
        glEnableVertexAttribArray(1);
        // aLayerIndex (location 2): 1 float
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(TileChunkVertex),
                              (void*)offsetof(TileChunkVertex, layerIndex));
        glEnableVertexAttribArray(2);
        // aColor (location 3): 4 floats
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(TileChunkVertex),
                              (void*)offsetof(TileChunkVertex, r));
        glEnableVertexAttribArray(3);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);

        // Single draw call per chunk -- no sub-batching needed
        glDrawElements(GL_TRIANGLES, gpu.indexCount, GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray(0);
    tileArray_->unbind(0);
    tileArrayShader_.unbind();
}

void ChunkRenderer::releaseChunk(int chunkX, int chunkY, int layerIndex) {
    int key = packKey(chunkX, chunkY, layerIndex);
    auto it = gpuChunks_.find(key);
    if (it == gpuChunks_.end()) return;

    auto& device = gfx::Device::instance();
    ChunkGPU& gpu = it->second;
    if (gpu.vboHandle.valid()) device.destroy(gpu.vboHandle);
    if (gpu.eboHandle.valid()) device.destroy(gpu.eboHandle);
    gpuChunks_.erase(it);
}

} // namespace fate
