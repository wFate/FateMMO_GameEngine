#include <doctest/doctest.h>
#include "game/shared/skill_manager.h"
#include "game/shared/character_stats.h"

using namespace fate;

// ============================================================================
// Test Helpers
// ============================================================================

static CharacterStats makeTestStats(const std::string& className, ClassType classType, int level) {
    CharacterStats stats;
    stats.className = className;
    stats.classDef.classType = classType;
    stats.classDef.displayName = className;
    stats.level = level;
    stats.recalculateStats();
    return stats;
}

static SkillDefinition makeTestSkillDef(const std::string& id, const std::string& className, int levelReq) {
    SkillDefinition def;
    def.skillId = id;
    def.skillName = id;
    def.className = className;
    def.levelRequirement = levelReq;
    def.damagePerRank = {120.0f, 150.0f, 200.0f};
    return def;
}

// ============================================================================
// Learn Validation
// ============================================================================

TEST_CASE("learnSkill: rejects wrong class") {
    auto stats = makeTestStats("Mage", ClassType::Mage, 10);
    SkillManager mgr;
    mgr.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("warrior_slash", "Warrior", 1);
    mgr.registerSkillDefinition(def);

    CHECK_FALSE(mgr.learnSkill("warrior_slash", 1));
    CHECK_FALSE(mgr.hasSkill("warrior_slash"));
}

TEST_CASE("learnSkill: rejects insufficient level") {
    auto stats = makeTestStats("Warrior", ClassType::Warrior, 5);
    SkillManager mgr;
    mgr.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("high_skill", "Warrior", 10);
    mgr.registerSkillDefinition(def);

    CHECK_FALSE(mgr.learnSkill("high_skill", 1));
    CHECK_FALSE(mgr.hasSkill("high_skill"));
}

TEST_CASE("learnSkill: rejects rank skip") {
    auto stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager mgr;
    mgr.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("slash", "Warrior", 1);
    mgr.registerSkillDefinition(def);

    // New skill must start at rank 1
    CHECK_FALSE(mgr.learnSkill("slash", 2));

    // Learn rank 1 first
    CHECK(mgr.learnSkill("slash", 1));

    // Cannot skip to rank 3
    CHECK_FALSE(mgr.learnSkill("slash", 3));

    // Can learn rank 2 sequentially
    CHECK(mgr.learnSkill("slash", 2));
}

TEST_CASE("learnSkill: allows Any class") {
    auto stats = makeTestStats("Mage", ClassType::Mage, 5);
    SkillManager mgr;
    mgr.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("basic_attack", "Any", 1);
    mgr.registerSkillDefinition(def);

    CHECK(mgr.learnSkill("basic_attack", 1));
    CHECK(mgr.hasSkill("basic_attack"));
}

TEST_CASE("learnSkill: without registered def passes (backward compat)") {
    auto stats = makeTestStats("Warrior", ClassType::Warrior, 1);
    SkillManager mgr;
    mgr.initialize(&stats);

    // No definition registered — should still learn
    CHECK(mgr.learnSkill("unregistered_skill", 1));
    CHECK(mgr.hasSkill("unregistered_skill"));
}

TEST_CASE("learnSkill: auto-assigns to skill bar") {
    auto stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager mgr;
    mgr.initialize(&stats);

    CHECK(mgr.learnSkill("skill_a", 1));

    // Should be auto-assigned to slot 0
    CHECK(mgr.getSkillInSlot(0) == "skill_a");
}

// ============================================================================
// Skill Bar Utilities
// ============================================================================

TEST_CASE("clearSkillSlot: clears and ignores out of bounds") {
    auto stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager mgr;
    mgr.initialize(&stats);

    CHECK(mgr.learnSkill("skill_a", 1));
    CHECK(mgr.getSkillInSlot(0) == "skill_a");

    mgr.clearSkillSlot(0);
    CHECK(mgr.getSkillInSlot(0) == "");

    // Out of bounds — should not crash
    mgr.clearSkillSlot(-1);
    mgr.clearSkillSlot(20);
    mgr.clearSkillSlot(999);
}

TEST_CASE("swapSkillSlots: swaps and works with empty") {
    auto stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager mgr;
    mgr.initialize(&stats);

    CHECK(mgr.learnSkill("skill_a", 1));
    CHECK(mgr.learnSkill("skill_b", 1));

    // skill_a in slot 0, skill_b in slot 1 (auto-assigned)
    CHECK(mgr.getSkillInSlot(0) == "skill_a");
    CHECK(mgr.getSkillInSlot(1) == "skill_b");

    mgr.swapSkillSlots(0, 1);
    CHECK(mgr.getSkillInSlot(0) == "skill_b");
    CHECK(mgr.getSkillInSlot(1) == "skill_a");

    SUBCASE("swap with empty slot") {
        mgr.clearSkillSlot(1);
        CHECK(mgr.getSkillInSlot(1) == "");

        mgr.swapSkillSlots(0, 1);
        CHECK(mgr.getSkillInSlot(0) == "");
        CHECK(mgr.getSkillInSlot(1) == "skill_b");
    }

    SUBCASE("out of bounds is ignored") {
        mgr.swapSkillSlots(-1, 0);
        mgr.swapSkillSlots(0, 20);
        // Original values unchanged
        CHECK(mgr.getSkillInSlot(0) == "skill_b");
    }
}

TEST_CASE("autoAssignToSkillBar: finds first empty") {
    auto stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager mgr;
    mgr.initialize(&stats);

    // Manually learn and assign to fill slots 0-2
    CHECK(mgr.learnSkill("s1", 1));  // auto-assigns to slot 0
    CHECK(mgr.learnSkill("s2", 1));  // auto-assigns to slot 1
    CHECK(mgr.learnSkill("s3", 1));  // auto-assigns to slot 2

    CHECK(mgr.getSkillInSlot(0) == "s1");
    CHECK(mgr.getSkillInSlot(1) == "s2");
    CHECK(mgr.getSkillInSlot(2) == "s3");

    // Learn another — should go to slot 3
    CHECK(mgr.learnSkill("s4", 1));
    CHECK(mgr.getSkillInSlot(3) == "s4");
}

TEST_CASE("autoAssignToSkillBar: returns false when skill not learned") {
    auto stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager mgr;
    mgr.initialize(&stats);

    CHECK_FALSE(mgr.autoAssignToSkillBar("unknown_skill"));
}

TEST_CASE("autoAssignToSkillBar: returns true if already on bar") {
    auto stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager mgr;
    mgr.initialize(&stats);

    CHECK(mgr.learnSkill("skill_a", 1));
    CHECK(mgr.getSkillInSlot(0) == "skill_a");

    // Already assigned — should return true without duplicating
    CHECK(mgr.autoAssignToSkillBar("skill_a"));
    // Verify no duplicate in slot 1
    CHECK(mgr.getSkillInSlot(1) == "");
}

// ============================================================================
// Skill Definition Registry
// ============================================================================

TEST_CASE("skill definition registry: stores and retrieves") {
    SkillManager mgr;
    CharacterStats stats;
    stats.recalculateStats();
    mgr.initialize(&stats);

    CHECK(mgr.getSkillDefinition("nonexistent") == nullptr);

    SkillDefinition def = makeTestSkillDef("fireball", "Mage", 5);
    def.appliesBurn = true;
    def.aoeRadius = 3.0f;
    mgr.registerSkillDefinition(def);

    const SkillDefinition* retrieved = mgr.getSkillDefinition("fireball");
    REQUIRE(retrieved != nullptr);
    CHECK(retrieved->skillId == "fireball");
    CHECK(retrieved->className == "Mage");
    CHECK(retrieved->levelRequirement == 5);
    CHECK(retrieved->appliesBurn == true);
    CHECK(retrieved->aoeRadius == doctest::Approx(3.0f));
    CHECK(retrieved->damagePerRank.size() == 3);
    CHECK(retrieved->damagePerRank[0] == doctest::Approx(120.0f));
}

// ============================================================================
// Passive Skill Bonuses
// ============================================================================

TEST_CASE("Passive: increases maxHP") {
    auto stats = makeTestStats("Warrior", ClassType::Warrior, 1);
    SkillManager mgr;
    mgr.initialize(&stats);

    SkillDefinition def;
    def.skillId = "iron_body";
    def.skillName = "Iron Body";
    def.className = "Warrior";
    def.skillType = SkillType::Passive;
    def.levelRequirement = 1;
    def.passiveHPBonusPerRank = {50, 100, 200};
    mgr.registerSkillDefinition(def);

    int baseHP = stats.maxHP;  // 120 for level 1 warrior
    mgr.grantSkillPoint();
    CHECK(mgr.learnSkill("iron_body", 1));
    CHECK(mgr.activateSkillRank("iron_body"));
    CHECK(stats.maxHP == baseHP + 50);
}

TEST_CASE("Passive: increases crit rate") {
    auto stats = makeTestStats("Archer", ClassType::Archer, 1);
    SkillManager mgr;
    mgr.initialize(&stats);

    SkillDefinition def;
    def.skillId = "keen_eye";
    def.skillName = "Keen Eye";
    def.className = "Archer";
    def.skillType = SkillType::Passive;
    def.levelRequirement = 1;
    def.passiveCritBonusPerRank = {0.05f, 0.05f, 0.10f};
    mgr.registerSkillDefinition(def);

    float baseCrit = stats.getCritRate();
    mgr.grantSkillPoint();
    CHECK(mgr.learnSkill("keen_eye", 1));
    CHECK(mgr.activateSkillRank("keen_eye"));
    CHECK(stats.getCritRate() == doctest::Approx(baseCrit + 0.05f));
}

TEST_CASE("Passive: increases move speed") {
    auto stats = makeTestStats("Warrior", ClassType::Warrior, 1);
    SkillManager mgr;
    mgr.initialize(&stats);

    SkillDefinition def;
    def.skillId = "swift_feet";
    def.skillName = "Swift Feet";
    def.className = "Warrior";
    def.skillType = SkillType::Passive;
    def.levelRequirement = 1;
    def.passiveSpeedBonusPerRank = {0.05f, 0.10f, 0.15f};
    mgr.registerSkillDefinition(def);

    float baseSpeed = stats.getSpeed();  // 1.0 with no equip bonuses
    mgr.grantSkillPoint();
    CHECK(mgr.learnSkill("swift_feet", 1));
    CHECK(mgr.activateSkillRank("swift_feet"));
    CHECK(stats.getSpeed() == doctest::Approx(baseSpeed * 1.05f));
}

TEST_CASE("Passive: increases primary stat (Warrior STR)") {
    auto stats = makeTestStats("Warrior", ClassType::Warrior, 1);
    SkillManager mgr;
    mgr.initialize(&stats);

    SkillDefinition def;
    def.skillId = "brute_force";
    def.skillName = "Brute Force";
    def.className = "Warrior";
    def.skillType = SkillType::Passive;
    def.levelRequirement = 1;
    def.passiveStatBonusPerRank = {5, 10, 15};
    mgr.registerSkillDefinition(def);

    int baseStr = stats.getStrength();
    mgr.grantSkillPoint();
    CHECK(mgr.learnSkill("brute_force", 1));
    CHECK(mgr.activateSkillRank("brute_force"));
    CHECK(stats.getStrength() == baseStr + 5);
}

TEST_CASE("Passive: accumulates across ranks") {
    auto stats = makeTestStats("Warrior", ClassType::Warrior, 1);
    SkillManager mgr;
    mgr.initialize(&stats);

    SkillDefinition def;
    def.skillId = "iron_body";
    def.skillName = "Iron Body";
    def.className = "Warrior";
    def.skillType = SkillType::Passive;
    def.levelRequirement = 1;
    def.passiveHPBonusPerRank = {50, 100, 200};
    mgr.registerSkillDefinition(def);

    int baseHP = stats.maxHP;

    // Learn all 3 ranks via skillbooks
    CHECK(mgr.learnSkill("iron_body", 1));
    CHECK(mgr.learnSkill("iron_body", 2));
    CHECK(mgr.learnSkill("iron_body", 3));

    // Activate rank 1
    mgr.grantSkillPoint();
    CHECK(mgr.activateSkillRank("iron_body"));
    CHECK(stats.maxHP == baseHP + 50);

    // Activate rank 2
    mgr.grantSkillPoint();
    CHECK(mgr.activateSkillRank("iron_body"));
    CHECK(stats.maxHP == baseHP + 150);

    // Activate rank 3
    mgr.grantSkillPoint();
    CHECK(mgr.activateSkillRank("iron_body"));
    CHECK(stats.maxHP == baseHP + 350);
}

TEST_CASE("Active skill: does NOT add passive bonuses") {
    auto stats = makeTestStats("Warrior", ClassType::Warrior, 1);
    SkillManager mgr;
    mgr.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("slash", "Warrior", 1);
    def.skillType = SkillType::Active;
    def.passiveHPBonusPerRank = {50, 100, 200};
    mgr.registerSkillDefinition(def);

    int baseHP = stats.maxHP;
    mgr.grantSkillPoint();
    CHECK(mgr.learnSkill("slash", 1));
    CHECK(mgr.activateSkillRank("slash"));

    // Active skill should NOT change HP via passive bonuses
    CHECK(stats.maxHP == baseHP);
    CHECK(mgr.getPassiveHPBonus() == 0);
}

TEST_CASE("SkillDefinition: new fields default correctly") {
    SkillDefinition def;
    CHECK(def.canCrit == true);
    CHECK(def.usesHitRate == true);
    CHECK(def.furyOnHit == doctest::Approx(0.0f));
    CHECK(def.scalesWithResource == false);

    CHECK(def.appliesBleed == false);
    CHECK(def.appliesBurn == false);
    CHECK(def.appliesPoison == false);
    CHECK(def.appliesSlow == false);
    CHECK(def.appliesFreeze == false);

    CHECK(def.isUltimate == false);
    CHECK(def.grantsInvulnerability == false);
    CHECK(def.removesDebuffs == false);
    CHECK(def.grantsStunImmunity == false);
    CHECK(def.grantsCritGuarantee == false);

    CHECK(def.aoeRadius == doctest::Approx(0.0f));
    CHECK(def.teleportDistance == doctest::Approx(0.0f));
    CHECK(def.dashDistance == doctest::Approx(0.0f));
    CHECK(def.transformDamageMult == doctest::Approx(0.0f));
    CHECK(def.transformSpeedBonus == doctest::Approx(0.0f));
}
