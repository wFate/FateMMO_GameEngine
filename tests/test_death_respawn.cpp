#include <doctest/doctest.h>
#include "engine/net/game_messages.h"
#include "engine/net/packet.h"
#include "game/shared/character_stats.h"

using namespace fate;

// ============================================================================
// Serialization round-trip tests
// ============================================================================

TEST_CASE("SvDeathNotifyMsg round-trip serialization") {
    uint8_t buf[256];
    SvDeathNotifyMsg src;
    src.deathSource  = 1;   // PvP
    src.respawnTimer = 7.5f;
    src.xpLost       = 1200;
    src.honorLost    = 50;

    ByteWriter w(buf, sizeof(buf));
    src.write(w);
    CHECK_FALSE(w.overflowed());

    ByteReader r(buf, w.size());
    auto dst = SvDeathNotifyMsg::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);

    CHECK(dst.deathSource  == 1);
    CHECK(dst.respawnTimer == doctest::Approx(7.5f));
    CHECK(dst.xpLost       == 1200);
    CHECK(dst.honorLost    == 50);
}

TEST_CASE("CmdRespawnMsg round-trip serialization") {
    uint8_t buf[256];
    CmdRespawnMsg src;
    src.respawnType = 2;  // Phoenix Down (here)

    ByteWriter w(buf, sizeof(buf));
    src.write(w);
    CHECK_FALSE(w.overflowed());

    ByteReader r(buf, w.size());
    auto dst = CmdRespawnMsg::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);

    CHECK(dst.respawnType == 2);
}

TEST_CASE("SvRespawnMsg round-trip serialization") {
    uint8_t buf[256];
    SvRespawnMsg src;
    src.respawnType = 1;    // map spawn
    src.spawnX      = 123.5f;
    src.spawnY      = 456.75f;

    ByteWriter w(buf, sizeof(buf));
    src.write(w);
    CHECK_FALSE(w.overflowed());

    ByteReader r(buf, w.size());
    auto dst = SvRespawnMsg::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);

    CHECK(dst.respawnType == 1);
    CHECK(dst.spawnX      == doctest::Approx(123.5f));
    CHECK(dst.spawnY      == doctest::Approx(456.75f));
}

// ============================================================================
// Packet ID uniqueness
// ============================================================================

TEST_CASE("Packet ID uniqueness: 0x1B, 0xA0, 0xA1 are distinct from all others") {
    CHECK(PacketType::CmdRespawn    == 0x1B);
    CHECK(PacketType::SvDeathNotify == 0xA0);
    CHECK(PacketType::SvRespawn     == 0xA1);

    // Verify they don't collide with adjacent IDs
    CHECK(PacketType::CmdRespawn    != PacketType::CmdZoneTransition);  // 0x1A
    CHECK(PacketType::SvDeathNotify != PacketType::SvQuestUpdate);       // 0x9F
    CHECK(PacketType::SvRespawn     != PacketType::SvDeathNotify);       // 0xA0
    CHECK(PacketType::CmdRespawn    != PacketType::SvDeathNotify);
    CHECK(PacketType::CmdRespawn    != PacketType::SvRespawn);
}

// ============================================================================
// CharacterStats: respawn() restores HP and MP
// ============================================================================

TEST_CASE("respawn() restores HP and MP to full") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Warrior;
    stats.level = 1;
    stats.recalculateStats();

    // Drain HP and MP before death
    stats.currentHP = 0;
    stats.currentMP = 0;
    stats.isDead    = true;
    stats.lifeState = LifeState::Dead;

    stats.respawn();

    CHECK(stats.isDead    == false);
    CHECK(stats.lifeState == LifeState::Alive);
    CHECK(stats.currentHP == stats.maxHP);
    CHECK(stats.currentMP == stats.maxMP);
    CHECK(stats.maxHP     >  0);
    CHECK(stats.maxMP     >  0);
}

// ============================================================================
// CharacterStats: die() behaviour
// ============================================================================

TEST_CASE("die() XP loss and idempotency") {
    SUBCASE("die(PvE) applies XP loss") {
        CharacterStats stats;
        stats.classDef.classType = ClassType::Warrior;
        stats.level = 1;
        stats.recalculateStats();
        stats.currentHP = stats.maxHP;
        stats.currentXP = 80;

        stats.die(DeathSource::PvE);

        CHECK(stats.lifeState == LifeState::Dying);
        CHECK(stats.currentHP == 0);
        // Level 1 loses 10% of currentXP; 80 * 0.10 = 8 → XP = 72
        CHECK(stats.currentXP == 72);

        // Advance to Dead
        stats.advanceDeathTick();
        CHECK(stats.isDead == true);
        CHECK(stats.lifeState == LifeState::Dead);
    }

    SUBCASE("die(PvP) does NOT apply XP loss") {
        CharacterStats stats;
        stats.classDef.classType = ClassType::Warrior;
        stats.level = 1;
        stats.recalculateStats();
        stats.currentHP = stats.maxHP;
        stats.currentXP = 80;

        stats.die(DeathSource::PvP);

        CHECK(stats.lifeState == LifeState::Dying);
        CHECK(stats.currentHP == 0);
        CHECK(stats.currentXP == 80);  // no XP loss for PvP
    }

    SUBCASE("die() is idempotent (second call doesn't double-penalize)") {
        CharacterStats stats;
        stats.classDef.classType = ClassType::Warrior;
        stats.level = 1;
        stats.recalculateStats();
        stats.currentHP = stats.maxHP;
        stats.currentXP = 80;

        stats.die(DeathSource::PvE);
        int64_t xpAfterFirst = stats.currentXP;

        // Second die() call should be a no-op because lifeState is Dying
        stats.die(DeathSource::PvE);

        CHECK(stats.currentXP == xpAfterFirst);
    }
}
