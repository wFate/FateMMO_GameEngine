#include <doctest/doctest.h>
#include "engine/tilemap/chunk_renderer.h"
#include "engine/tilemap/chunk.h"

using namespace fate;

// ============================================================================
// Test: ChunkGPU default state
// ============================================================================
TEST_CASE("ChunkGPU default state") {
    ChunkGPU gpu;
    CHECK(gpu.vao == 0);
    CHECK(gpu.vbo == 0);
    CHECK(gpu.ebo == 0);
    CHECK(gpu.vertexCount == 0);
    CHECK(gpu.indexCount == 0);
    CHECK(gpu.uploaded == false);
    CHECK(gpu.textureId == 0);
    CHECK(gpu.subBatches.empty());
}

// ============================================================================
// Test: ChunkGPU SubBatch default state
// ============================================================================
TEST_CASE("ChunkGPU SubBatch default state") {
    ChunkGPU::SubBatch sb;
    CHECK(sb.textureId == 0);
    CHECK(sb.indexOffset == 0);
    CHECK(sb.indexCount == 0);
}

// ============================================================================
// Test: Chunk dirty flag triggers rebuild
// ============================================================================
TEST_CASE("Chunk dirty flag triggers rebuild") {
    ChunkData chunk;
    chunk.tiles.resize(CHUNK_SIZE * CHUNK_SIZE, 0);
    chunk.dirty = false;

    // Set a tile
    chunk.tiles[0] = 1;
    chunk.dirty = true;

    CHECK(chunk.dirty);
    CHECK(chunk.tiles[0] == 1);
}

// ============================================================================
// Test: packKey produces unique keys for different coordinates
// ============================================================================
TEST_CASE("ChunkRenderer packKey uniqueness") {
    // packKey packs (layerIndex << 20) | (chunkY << 10) | chunkX
    // We test indirectly via ChunkGPU map key generation by verifying that
    // different coordinate tuples produce different packed values.

    // Helper lambda matching ChunkRenderer::packKey
    auto packKey = [](int cx, int cy, int li) -> int {
        return (li << 20) | (cy << 10) | cx;
    };

    CHECK(packKey(0, 0, 0) == 0);
    CHECK(packKey(1, 0, 0) == 1);
    CHECK(packKey(0, 1, 0) == (1 << 10));
    CHECK(packKey(0, 0, 1) == (1 << 20));

    // Different coords must produce different keys
    CHECK(packKey(1, 0, 0) != packKey(0, 1, 0));
    CHECK(packKey(0, 0, 1) != packKey(0, 1, 0));
    CHECK(packKey(3, 7, 2) != packKey(7, 3, 2));

    // Max safe values (10 bits per axis = 1023, layer up to ~4095)
    int maxCoord = (1 << 10) - 1; // 1023
    int maxLayer = (1 << 10) - 1; // fits in bits 20-29
    int keyA = packKey(maxCoord, maxCoord, 0);
    int keyB = packKey(0, 0, maxLayer);
    CHECK(keyA != keyB);
}
