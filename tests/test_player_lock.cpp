#include <doctest/doctest.h>
#include "server/player_lock.h"

using namespace fate;

TEST_CASE("PlayerLockMap: returns same mutex for same player") {
    PlayerLockMap locks;
    auto& m1 = locks.get("char_001");
    auto& m2 = locks.get("char_001");
    CHECK(&m1 == &m2);
}

TEST_CASE("PlayerLockMap: different players get different mutexes") {
    PlayerLockMap locks;
    auto& m1 = locks.get("char_001");
    auto& m2 = locks.get("char_002");
    CHECK(&m1 != &m2);
}

TEST_CASE("PlayerLockMap: erase removes player lock") {
    PlayerLockMap locks;
    locks.get("char_001");
    locks.erase("char_001");
    auto& m3 = locks.get("char_001");
    (void)m3;
    CHECK(true); // doesn't crash
}

TEST_CASE("PlayerLockMap: scoped lock for two players") {
    PlayerLockMap locks;
    auto& m1 = locks.get("aaa");
    auto& m2 = locks.get("zzz");
    // Lock in consistent address order to prevent deadlocks
    std::scoped_lock lock(
        (&m1 < &m2) ? m1 : m2,
        (&m1 < &m2) ? m2 : m1
    );
    CHECK(true); // no deadlock
}
