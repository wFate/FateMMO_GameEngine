#include <doctest/doctest.h>
#include "game/shared/enemy_stats.h"
#include "game/shared/party_manager.h"

using namespace fate;

TEST_SUITE("PartyLoot") {

TEST_CASE("EnemyStats: getTopDamagerPartyAware solo players") {
    EnemyStats es;
    es.maxHP = 1000; es.currentHP = 1000; es.isAlive = true;
    es.damageByAttacker[10] = 300;
    es.damageByAttacker[20] = 500;
    es.damageByAttacker[30] = 200;

    // No parties — same as getTopThreatTarget
    auto noParty = [](uint32_t) { return -1; };
    CHECK(es.getTopDamagerPartyAware(noParty).topDamagerId == 20);
}

TEST_CASE("EnemyStats: party aggregation wins over solo") {
    EnemyStats es;
    es.maxHP = 1000; es.currentHP = 1000; es.isAlive = true;
    // Solo player did 400 damage
    es.damageByAttacker[10] = 400;
    // Party members did 200 + 250 = 450 total
    es.damageByAttacker[20] = 200;
    es.damageByAttacker[30] = 250;

    auto partyLookup = [](uint32_t id) -> int {
        if (id == 20 || id == 30) return 1; // same party
        return -1; // solo
    };

    // Party wins (450 > 400), top individual in party is 30 (250)
    CHECK(es.getTopDamagerPartyAware(partyLookup).topDamagerId == 30);
}

TEST_CASE("EnemyStats: solo beats smaller party") {
    EnemyStats es;
    es.maxHP = 1000; es.currentHP = 1000; es.isAlive = true;
    es.damageByAttacker[10] = 600; // solo
    es.damageByAttacker[20] = 100; // party member
    es.damageByAttacker[30] = 100; // party member

    auto partyLookup = [](uint32_t id) -> int {
        if (id == 20 || id == 30) return 1;
        return -1;
    };

    // Solo wins (600 > 200)
    CHECK(es.getTopDamagerPartyAware(partyLookup).topDamagerId == 10);
}

TEST_CASE("EnemyStats: empty threat table returns 0") {
    EnemyStats es;
    es.maxHP = 100; es.currentHP = 100; es.isAlive = true;
    auto noParty = [](uint32_t) { return -1; };
    CHECK(es.getTopDamagerPartyAware(noParty).topDamagerId == 0);
}

TEST_CASE("EnemyStats: two parties, winning party top individual returned") {
    EnemyStats es;
    es.maxHP = 2000; es.currentHP = 2000; es.isAlive = true;
    // Party 1: 300 + 400 = 700 total; top individual = 40 (400)
    es.damageByAttacker[40] = 400;
    es.damageByAttacker[50] = 300;
    // Party 2: 200 + 200 = 400 total
    es.damageByAttacker[60] = 200;
    es.damageByAttacker[70] = 200;

    auto partyLookup = [](uint32_t id) -> int {
        if (id == 40 || id == 50) return 1;
        if (id == 60 || id == 70) return 2;
        return -1;
    };

    // Party 1 wins (700 > 400), top in party 1 = entity 40 (400 dmg)
    CHECK(es.getTopDamagerPartyAware(partyLookup).topDamagerId == 40);
}

TEST_CASE("EnemyStats: tie-breaking favors lower entity ID") {
    EnemyStats es;
    es.maxHP = 1000; es.currentHP = 1000; es.isAlive = true;
    es.damageByAttacker[50] = 300;
    es.damageByAttacker[10] = 300; // same damage, lower ID

    auto noParty = [](uint32_t) { return -1; };
    auto result = es.getTopDamagerPartyAware(noParty);
    CHECK(result.topDamagerId == 10); // lower ID wins tie
}

TEST_CASE("EnemyStats: party tie-breaking favors lower group ID") {
    EnemyStats es;
    es.maxHP = 1000; es.currentHP = 1000; es.isAlive = true;
    es.damageByAttacker[10] = 200; // party 5
    es.damageByAttacker[20] = 200; // party 1 (lower)

    auto partyLookup = [](uint32_t id) -> int {
        if (id == 10) return 5;
        if (id == 20) return 1;
        return -1;
    };

    auto result = es.getTopDamagerPartyAware(partyLookup);
    CHECK(result.topDamagerId == 20); // party 1 < party 5
    CHECK(result.isParty == true);
}

TEST_CASE("EnemyStats: result marks solo vs party correctly") {
    EnemyStats es;
    es.maxHP = 1000; es.currentHP = 1000; es.isAlive = true;
    es.damageByAttacker[10] = 500;

    auto noParty = [](uint32_t) { return -1; };
    auto result = es.getTopDamagerPartyAware(noParty);
    CHECK(result.isParty == false);
    CHECK(result.topDamagerId == 10);
}

TEST_CASE("Party cleanup: leader leaving disbands party") {
    PartyManager pm;
    pm.createParty("leader1", "Leader", "Warrior", 10);
    PartyMemberInfo mi;
    mi.characterId = "member1";
    mi.characterName = "Member";
    mi.className = "Mage";
    mi.level = 10;
    mi.currentHP = 100; mi.maxHP = 100;
    mi.currentMP = 50; mi.maxMP = 50;
    pm.addMember(mi);
    CHECK(pm.isInParty());
    CHECK(pm.isLeader);

    // When leader leaves, party should dissolve
    pm.leaveParty();
    CHECK_FALSE(pm.isInParty());
    CHECK(pm.memberCount() == 0);
}

} // TEST_SUITE
