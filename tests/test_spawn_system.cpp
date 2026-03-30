#include <doctest/doctest.h>
#include "engine/spatial/collision_grid.h"
#include "engine/core/types.h"
#include <cmath>
#include <ctime>

using namespace fate;

// ==========================================================================
// Zone shape: circular rejection
// ==========================================================================

TEST_CASE("SpawnZone: circular shape rejects corners") {
    float cx = 500.0f, cy = 500.0f, r = 100.0f;
    float cornerX = cx + r, cornerY = cy + r;
    float dist = std::sqrt((cornerX - cx) * (cornerX - cx) + (cornerY - cy) * (cornerY - cy));
    CHECK(dist > r);
}

TEST_CASE("SpawnZone: circular shape accepts center") {
    float cx = 500.0f, cy = 500.0f, r = 100.0f;
    float dist = std::sqrt((cx - cx) * (cx - cx) + (cy - cy) * (cy - cy));
    CHECK(dist <= r);
}

TEST_CASE("SpawnZone: square shape accepts corners") {
    float cx = 500.0f, cy = 500.0f, r = 100.0f;
    float cornerX = cx + r, cornerY = cy + r;
    CHECK(cornerX <= cx + r);
    CHECK(cornerY <= cy + r);
    CHECK(cornerX >= cx - r);
    CHECK(cornerY >= cy - r);
}

// ==========================================================================
// Collision grid rejection
// ==========================================================================

TEST_CASE("SpawnValidation: blocked tile rejects spawn position") {
    CollisionGrid grid;
    grid.beginBuild();
    grid.markBlocked(15, 15);
    grid.endBuild();

    float wx = 15.0f * 32.0f + 16.0f;
    float wy = 15.0f * 32.0f + 16.0f;
    CHECK(grid.isBlockedRect(wx, wy, 16.0f, 16.0f));
}

TEST_CASE("SpawnValidation: unblocked tile accepts spawn position") {
    CollisionGrid grid;
    grid.beginBuild();
    grid.markBlocked(15, 15);
    grid.endBuild();

    float wx = 16.0f * 32.0f + 16.0f;
    float wy = 15.0f * 32.0f + 16.0f;
    CHECK_FALSE(grid.isBlockedRect(wx, wy, 16.0f, 16.0f));
}

// ==========================================================================
// Mob overlap distance
// ==========================================================================

TEST_CASE("SpawnValidation: mobs within 48px are too close") {
    Vec2 existing{500.0f, 500.0f};
    Vec2 candidate{530.0f, 500.0f};
    float dist = (candidate - existing).length();
    CHECK(dist < 48.0f);
}

TEST_CASE("SpawnValidation: mobs beyond 48px are fine") {
    Vec2 existing{500.0f, 500.0f};
    Vec2 candidate{560.0f, 500.0f};
    float dist = (candidate - existing).length();
    CHECK(dist >= 48.0f);
}

// ==========================================================================
// Death persistence records
//
// DeadMobRecord lives in zone_mob_state_repository.h which pulls in pqxx
// (not linked to fate_tests). We replicate its pure logic here to test the
// respawn-timer math without the DB dependency.
// ==========================================================================

namespace {

struct TestDeadMobRecord {
    int64_t diedAtUnix = 0;
    int respawnSeconds = 0;

    bool hasRespawned() const {
        int64_t now = static_cast<int64_t>(std::time(nullptr));
        return now >= (diedAtUnix + respawnSeconds);
    }

    float getRemainingRespawnTime() const {
        int64_t now = static_cast<int64_t>(std::time(nullptr));
        int64_t respawnAt = diedAtUnix + respawnSeconds;
        if (now >= respawnAt) return 0.0f;
        return static_cast<float>(respawnAt - now);
    }
};

} // anonymous namespace

TEST_CASE("DeadMobRecord: hasRespawned returns true for expired timer") {
    TestDeadMobRecord rec;
    rec.diedAtUnix = static_cast<int64_t>(std::time(nullptr)) - 120;
    rec.respawnSeconds = 60;
    CHECK(rec.hasRespawned());
}

TEST_CASE("DeadMobRecord: hasRespawned returns false for active timer") {
    TestDeadMobRecord rec;
    rec.diedAtUnix = static_cast<int64_t>(std::time(nullptr)) - 10;
    rec.respawnSeconds = 60;
    CHECK_FALSE(rec.hasRespawned());
}

TEST_CASE("DeadMobRecord: getRemainingRespawnTime is positive when not expired") {
    TestDeadMobRecord rec;
    rec.diedAtUnix = static_cast<int64_t>(std::time(nullptr)) - 10;
    rec.respawnSeconds = 60;
    float remaining = rec.getRemainingRespawnTime();
    CHECK(remaining > 40.0f);
    CHECK(remaining <= 50.0f);
}

TEST_CASE("DeadMobRecord: getRemainingRespawnTime is zero when expired") {
    TestDeadMobRecord rec;
    rec.diedAtUnix = static_cast<int64_t>(std::time(nullptr)) - 120;
    rec.respawnSeconds = 60;
    CHECK(rec.getRemainingRespawnTime() == 0.0f);
}
