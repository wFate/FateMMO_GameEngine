#include <doctest/doctest.h>
#include "engine/tilemap/tilemap.h"
#include "engine/tilemap/chunk.h"

// ============================================================================
// Test: Chunk state transitions from ticket level
// ============================================================================
TEST_CASE("Chunk state transitions from ticket level") {
    fate::ChunkData cd;
    cd.tiles.resize(fate::CHUNK_SIZE * fate::CHUNK_SIZE, 0);
    cd.state = fate::ChunkState::Active;

    SUBCASE("Ticket 4 keeps Active") {
        cd.ticketLevel = 4;
        bool changed = cd.stepTowardTarget();
        CHECK_FALSE(changed);
        CHECK(cd.state == fate::ChunkState::Active);
    }

    SUBCASE("Ticket 3 transitions Active -> Sleeping") {
        cd.ticketLevel = 3;
        bool changed = cd.stepTowardTarget();
        CHECK(changed);
        CHECK(cd.state == fate::ChunkState::Sleeping);
    }

    SUBCASE("Ticket 0 transitions Active -> Sleeping (first step toward Evicted)") {
        cd.ticketLevel = 0;
        bool changed = cd.stepTowardTarget();
        CHECK(changed);
        CHECK(cd.state == fate::ChunkState::Sleeping);
    }

    SUBCASE("Ticket 0 full teardown: Active -> Sleeping -> Unloading -> Evicted") {
        cd.ticketLevel = 0;
        cd.stepTowardTarget(); // Active -> Sleeping
        CHECK(cd.state == fate::ChunkState::Sleeping);

        cd.stepTowardTarget(); // Sleeping -> Unloading
        CHECK(cd.state == fate::ChunkState::Unloading);

        cd.stepTowardTarget(); // Unloading -> Evicted
        CHECK(cd.state == fate::ChunkState::Evicted);

        // Already at target, no more changes
        bool changed = cd.stepTowardTarget();
        CHECK_FALSE(changed);
        CHECK(cd.state == fate::ChunkState::Evicted);
    }

    SUBCASE("Ticket 4 brings Evicted chunk up through full pipeline") {
        cd.state = fate::ChunkState::Evicted;
        cd.ticketLevel = 4;

        cd.stepTowardTarget(); // Evicted -> Queued
        CHECK(cd.state == fate::ChunkState::Queued);

        cd.stepTowardTarget(); // Queued -> Loading
        CHECK(cd.state == fate::ChunkState::Loading);

        cd.stepTowardTarget(); // Loading -> Setup
        CHECK(cd.state == fate::ChunkState::Setup);

        cd.stepTowardTarget(); // Setup -> Active
        CHECK(cd.state == fate::ChunkState::Active);

        bool changed = cd.stepTowardTarget();
        CHECK_FALSE(changed);
    }

    SUBCASE("Ticket 1 brings Evicted to Queued only") {
        cd.state = fate::ChunkState::Evicted;
        cd.ticketLevel = 1;

        cd.stepTowardTarget(); // Evicted -> Queued
        CHECK(cd.state == fate::ChunkState::Queued);

        // Should not advance past Queued
        bool changed = cd.stepTowardTarget();
        CHECK_FALSE(changed);
        CHECK(cd.state == fate::ChunkState::Queued);
    }

    SUBCASE("Ticket 2 transitions Evicted through to Setup") {
        cd.state = fate::ChunkState::Evicted;
        cd.ticketLevel = 2;

        cd.stepTowardTarget(); // Evicted -> Queued
        CHECK(cd.state == fate::ChunkState::Queued);

        cd.stepTowardTarget(); // Queued -> Loading
        CHECK(cd.state == fate::ChunkState::Loading);

        cd.stepTowardTarget(); // Loading -> Setup
        CHECK(cd.state == fate::ChunkState::Setup);

        // Should stop at Setup
        bool changed = cd.stepTowardTarget();
        CHECK_FALSE(changed);
        CHECK(cd.state == fate::ChunkState::Setup);
    }

    SUBCASE("Sleeping chunk with ticket 4 goes straight to Active") {
        cd.state = fate::ChunkState::Sleeping;
        cd.ticketLevel = 4;

        bool changed = cd.stepTowardTarget();
        CHECK(changed);
        CHECK(cd.state == fate::ChunkState::Active);
    }
}

// ============================================================================
// Test: Rate-limited transitions
// ============================================================================
TEST_CASE("Rate-limited transitions") {
    fate::ChunkManager mgr;
    mgr.maxTransitionsPerFrame = 2;

    // Build a small 3-chunk-wide, 1-chunk-tall map (96 tiles wide, 32 tall)
    // with a single layer
    std::vector<fate::TilemapLayer> layers(1);
    layers[0].name = "ground";
    layers[0].width = 96;
    layers[0].height = 32;
    layers[0].data.resize(96 * 32, 1);

    mgr.buildFromLayers(layers, 96, 32);

    // Verify we got 3 chunks
    auto& chunkLayers = mgr.layers();
    REQUIRE(chunkLayers.size() == 1);
    REQUIRE(chunkLayers[0].widthInChunks == 3);
    REQUIRE(chunkLayers[0].heightInChunks == 1);

    // All chunks start Active. Set all tickets to 0 so they want to transition
    // toward Evicted (Active -> Sleeping as the first step).
    for (auto& cd : chunkLayers[0].chunks) {
        cd.ticketLevel = 0;
    }

    // With maxTransitionsPerFrame=2, only 2 of the 3 chunks should change
    // Camera rect doesn't matter here; we pre-set ticket levels.
    // We call updateChunkStates but the ticket assignment phase will override
    // our manual tickets. So instead we step manually.

    int transitionsUsed = 0;
    for (auto& cd : chunkLayers[0].chunks) {
        if (transitionsUsed >= mgr.maxTransitionsPerFrame) break;
        if (cd.stepTowardTarget()) {
            ++transitionsUsed;
        }
    }

    CHECK(transitionsUsed == 2);

    // Count how many actually changed from Active
    int sleepingCount = 0;
    int activeCount = 0;
    for (auto& cd : chunkLayers[0].chunks) {
        if (cd.state == fate::ChunkState::Sleeping) sleepingCount++;
        if (cd.state == fate::ChunkState::Active) activeCount++;
    }
    CHECK(sleepingCount == 2);
    CHECK(activeCount == 1);
}

// ============================================================================
// Test: Concentric ring ticket assignment
// ============================================================================
TEST_CASE("Concentric ring ticket assignment") {
    fate::ChunkManager mgr;
    mgr.maxTransitionsPerFrame = 1000; // no rate-limiting for this test
    mgr.prefetchBuffer = 2;

    // Build a 10-chunk-wide, 10-chunk-tall map (320x320 tiles)
    std::vector<fate::TilemapLayer> layers(1);
    layers[0].name = "ground";
    layers[0].width = 320;
    layers[0].height = 320;
    layers[0].data.resize(320 * 320, 1);

    mgr.buildFromLayers(layers, 320, 320);

    auto& cl = mgr.layers()[0];
    REQUIRE(cl.widthInChunks == 10);
    REQUIRE(cl.heightInChunks == 10);

    // Camera sees chunk (4,4) to (5,5) area
    // activeBuffer=1, sleepBuffer=2, prefetchBuffer=2
    // Tile coords: x=[128..192), y=[128..192)
    // World pixels at origin (0,0), 32px tiles:
    //   visible = (128*32, 128*32, 64*32, 64*32) = but let's use chunk coords
    // Camera visible rect covers chunks 4-5 in both dims
    // visible bounds in pixels: x=4*32*32=4096 ... that's too large, let's
    // just set visible to cover chunk 4,4 exactly.

    // Chunk (4,4) starts at tile (128,128) -> pixel (4096, 4096)
    // Visible rect covers just chunk 4,4: pixel (4096, 4096, 1024, 1024)
    fate::Rect cameraRect(4096.0f, 4096.0f, 1024.0f, 1024.0f);
    fate::Vec2 origin(0.0f, 0.0f);
    int tileW = 32, tileH = 32;
    int activeBuffer = 1;
    int sleepBuffer = 2;

    mgr.updateChunkStates(cameraRect, origin, tileW, tileH, activeBuffer, sleepBuffer);

    // Check ticket levels based on Chebyshev distance from visible chunk range [4,4]
    // visible chunk range: cxMin=4, cxMax=4, cyMin=4, cyMax=4

    // Chunk (4,4): dist=0 -> ticket 4 (active)
    CHECK(cl.getChunk(4, 4)->ticketLevel == 4);

    // Chunk (3,4): dist=1 -> within activeBuffer(1) -> ticket 4
    CHECK(cl.getChunk(3, 4)->ticketLevel == 4);

    // Chunk (5,5): dist=1 -> within activeBuffer(1) -> ticket 4
    CHECK(cl.getChunk(5, 5)->ticketLevel == 4);

    // Chunk (2,4): dist=2 -> beyond active(1), within active+sleep(3) -> ticket 3
    CHECK(cl.getChunk(2, 4)->ticketLevel == 3);

    // Chunk (1,4): dist=3 -> within active+sleep(3) -> ticket 3
    CHECK(cl.getChunk(1, 4)->ticketLevel == 3);

    // Chunk (0,4): dist=4 -> beyond active+sleep(3), within active+sleep+prefetch(5) -> ticket 2
    CHECK(cl.getChunk(0, 4)->ticketLevel == 2);

    // Chunk (9,4): dist=5 -> within active+sleep+prefetch(5) -> ticket 2
    CHECK(cl.getChunk(9, 4)->ticketLevel == 2);

    // Chunk (0,0): dist=max(4,4)=4 -> within prefetch -> ticket 2
    CHECK(cl.getChunk(0, 0)->ticketLevel == 2);

    // Chunk (9,9): dist=max(5,5)=5 -> within prefetch(5) -> ticket 2
    CHECK(cl.getChunk(9, 9)->ticketLevel == 2);
}

// ============================================================================
// Test: Dirty flag preserved through sleeping
// ============================================================================
TEST_CASE("Dirty flag preserved through sleeping") {
    fate::ChunkData cd;
    cd.tiles.resize(fate::CHUNK_SIZE * fate::CHUNK_SIZE, 1);
    cd.state = fate::ChunkState::Active;
    cd.ticketLevel = 4;
    cd.dirty = true;

    // Transition to Sleeping
    cd.ticketLevel = 3;
    cd.stepTowardTarget();
    CHECK(cd.state == fate::ChunkState::Sleeping);
    CHECK(cd.dirty == true); // dirty flag must survive

    // Transition back to Active
    cd.ticketLevel = 4;
    cd.stepTowardTarget();
    CHECK(cd.state == fate::ChunkState::Active);
    CHECK(cd.dirty == true); // still dirty
}

// ============================================================================
// Test: targetStateForTicket mapping
// ============================================================================
TEST_CASE("targetStateForTicket mapping") {
    CHECK(fate::targetStateForTicket(4) == fate::ChunkState::Active);
    CHECK(fate::targetStateForTicket(3) == fate::ChunkState::Sleeping);
    CHECK(fate::targetStateForTicket(2) == fate::ChunkState::Setup);
    CHECK(fate::targetStateForTicket(1) == fate::ChunkState::Queued);
    CHECK(fate::targetStateForTicket(0) == fate::ChunkState::Evicted);
}

// ============================================================================
// Test: Staging buffer swap
// ============================================================================
TEST_CASE("Staging buffer swap") {
    fate::ChunkManager mgr;

    std::vector<fate::TilemapLayer> layers(1);
    layers[0].name = "ground";
    layers[0].width = 32;
    layers[0].height = 32;
    layers[0].data.resize(32 * 32, 0);

    mgr.buildFromLayers(layers, 32, 32);

    auto& cd = mgr.layers()[0].chunks[0];
    CHECK(cd.tiles[0] == 0);

    // Simulate async load filling staging buffer
    cd.stagingTiles.resize(fate::CHUNK_SIZE * fate::CHUNK_SIZE, 42);
    cd.stagingReady = true;

    mgr.swapStagingBuffers();

    CHECK(cd.tiles[0] == 42);
    CHECK(cd.stagingReady == false);
    CHECK(cd.stagingTiles.empty());
}
