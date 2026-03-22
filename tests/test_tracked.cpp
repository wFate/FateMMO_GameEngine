#include <doctest/doctest.h>
#include "server/db/tracked.h"
#include "server/db/player_dirty_flags.h"

using namespace fate;

TEST_CASE("Tracked starts clean") {
    Tracked<int> val(42);
    CHECK(!val.dirty());
    CHECK(val.get() == 42);
    CHECK(val.version() == 0);
}

TEST_CASE("Tracked mutate marks dirty and increments version") {
    Tracked<int> val(0);
    val.mutate() = 100;
    CHECK(val.dirty());
    CHECK(val.get() == 100);
    CHECK(val.version() == 1);
}

TEST_CASE("Tracked clearDirty resets flag but not version") {
    Tracked<int> val(0);
    val.mutate() = 1;
    val.clearDirty();
    CHECK(!val.dirty());
    CHECK(val.version() == 1); // version persists
}

TEST_CASE("Tracked multiple mutations increment version") {
    Tracked<int> val(0);
    val.mutate() = 1;
    val.mutate() = 2;
    val.mutate() = 3;
    CHECK(val.version() == 3);
    CHECK(val.get() == 3);
}

TEST_CASE("Tracked implicit conversion") {
    Tracked<int> val(42);
    int x = val; // implicit conversion via operator const T&
    CHECK(x == 42);
}

TEST_CASE("Tracked with struct type") {
    struct Stats { int hp = 100; int mp = 50; };
    Tracked<Stats> stats;
    CHECK(stats.get().hp == 100);
    stats.mutate().hp = 200;
    CHECK(stats.dirty());
    CHECK(stats.get().hp == 200);
}

TEST_CASE("PlayerDirtyFlags starts all clean") {
    PlayerDirtyFlags flags;
    CHECK(!flags.any());
}

TEST_CASE("PlayerDirtyFlags any() detects single flag") {
    PlayerDirtyFlags flags;
    flags.inventory = true;
    CHECK(flags.any());
}

TEST_CASE("PlayerDirtyFlags clearAll resets everything") {
    PlayerDirtyFlags flags;
    flags.position = true;
    flags.vitals = true;
    flags.inventory = true;
    flags.clearAll();
    CHECK(!flags.any());
}
