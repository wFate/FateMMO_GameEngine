#include <doctest/doctest.h>
#include "game/shared/skill_manager.h"
#include "game/shared/character_stats.h"
#include "game/shared/enemy_stats.h"
#include "game/shared/crowd_control.h"
#include "game/shared/status_effects.h"

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

// ============================================================================
// Task 5 Tests: Skill Execution - Validation
// ============================================================================

TEST_CASE("SkillManager: executeSkill rejects unlearned skill") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("slash", "Warrior", 1);
    sm.registerSkillDefinition(def);

    SkillExecutionContext ctx;
    ctx.casterStats = &stats;
    ctx.targetAlive = true;
    ctx.distanceToTarget = 1.0f;

    std::string failReason;
    sm.onSkillFailed = [&](const std::string&, std::string reason) {
        failReason = reason;
    };

    CHECK(sm.executeSkill("slash", 1, ctx) == 0);
    CHECK(failReason.find("not learned") != std::string::npos);
}

TEST_CASE("SkillManager: executeSkill rejects when CC'd") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("slash", "Warrior", 1);
    sm.registerSkillDefinition(def);

    sm.learnSkill("slash", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("slash");

    CrowdControlSystem cc;
    StatusEffectManager sem;
    cc.applyStun(10.0f, &sem);

    SkillExecutionContext ctx;
    ctx.casterStats = &stats;
    ctx.casterCC = &cc;
    ctx.targetAlive = true;
    ctx.distanceToTarget = 1.0f;

    std::string failReason;
    sm.onSkillFailed = [&](const std::string&, std::string reason) {
        failReason = reason;
    };

    CHECK(sm.executeSkill("slash", 1, ctx) == 0);
    CHECK(failReason == "Crowd controlled");
}

TEST_CASE("SkillManager: executeSkill rejects on cooldown") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("slash", "Warrior", 1);
    sm.registerSkillDefinition(def);

    sm.learnSkill("slash", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("slash");

    // Manually start a cooldown
    sm.startCooldown("slash", 10.0f);

    SkillExecutionContext ctx;
    ctx.casterStats = &stats;
    ctx.targetAlive = true;
    ctx.distanceToTarget = 1.0f;

    EnemyStats enemy;
    enemy.level = 10;
    enemy.maxHP = 100;
    enemy.currentHP = 100;
    ctx.targetMobStats = &enemy;
    ctx.targetLevel = enemy.level;
    ctx.targetMaxHP = enemy.maxHP;
    ctx.targetCurrentHP = enemy.currentHP;

    std::string failReason;
    sm.onSkillFailed = [&](const std::string&, std::string reason) {
        failReason = reason;
    };

    CHECK(sm.executeSkill("slash", 1, ctx) == 0);
    CHECK(failReason == "On cooldown");
}

TEST_CASE("SkillManager: executeSkill rejects insufficient mana") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    stats.currentMP = 0;  // No mana
    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("slash", "Warrior", 1);
    def.costPerRank = {10.0f, 15.0f, 20.0f};
    sm.registerSkillDefinition(def);

    sm.learnSkill("slash", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("slash");

    SkillExecutionContext ctx;
    ctx.casterStats = &stats;
    ctx.targetAlive = true;
    ctx.distanceToTarget = 1.0f;

    EnemyStats enemy;
    enemy.level = 10;
    enemy.maxHP = 100;
    enemy.currentHP = 100;
    ctx.targetMobStats = &enemy;
    ctx.targetLevel = enemy.level;

    std::string failReason;
    sm.onSkillFailed = [&](const std::string&, std::string reason) {
        failReason = reason;
    };

    CHECK(sm.executeSkill("slash", 1, ctx) == 0);
    CHECK(failReason == "Not enough resources");
}

TEST_CASE("SkillManager: executeSkill rejects dead target") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("slash", "Warrior", 1);
    sm.registerSkillDefinition(def);

    sm.learnSkill("slash", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("slash");

    SkillExecutionContext ctx;
    ctx.casterStats = &stats;
    ctx.targetAlive = false;  // dead target
    ctx.distanceToTarget = 1.0f;

    std::string failReason;
    sm.onSkillFailed = [&](const std::string&, std::string reason) {
        failReason = reason;
    };

    CHECK(sm.executeSkill("slash", 1, ctx) == 0);
    CHECK(failReason == "Target is dead");
}

TEST_CASE("SkillManager: executeSkill rejects out of range") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("slash", "Warrior", 1);
    def.range = 3.0f;
    sm.registerSkillDefinition(def);

    sm.learnSkill("slash", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("slash");

    SkillExecutionContext ctx;
    ctx.casterStats = &stats;
    ctx.targetAlive = true;
    ctx.distanceToTarget = 10.0f;  // way out of range

    EnemyStats enemy;
    enemy.level = 10;
    ctx.targetMobStats = &enemy;
    ctx.targetLevel = enemy.level;

    std::string failReason;
    sm.onSkillFailed = [&](const std::string&, std::string reason) {
        failReason = reason;
    };

    CHECK(sm.executeSkill("slash", 1, ctx) == 0);
    CHECK(failReason == "Out of range");
}

// ============================================================================
// Task 5 Tests: Skill Execution - Damage
// ============================================================================

TEST_CASE("SkillManager: executeSkill deals damage scaled by skill percent") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    stats.currentMP = 100;
    stats.weaponDamageMin = 50;
    stats.weaponDamageMax = 50;  // fixed damage for predictability
    stats.recalculateStats();
    stats.currentHP = stats.maxHP;
    stats.currentMP = 100;

    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("slash", "Warrior", 1);
    def.damagePerRank = {200.0f, 300.0f, 400.0f};  // 200% at rank 1
    def.costPerRank = {5.0f, 8.0f, 12.0f};
    def.cooldownPerRank = {3.0f, 2.5f, 2.0f};
    def.usesHitRate = false;  // guaranteed hit for test
    sm.registerSkillDefinition(def);

    sm.learnSkill("slash", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("slash");

    EnemyStats enemy;
    enemy.level = 10;
    enemy.maxHP = 99999;
    enemy.currentHP = 99999;
    enemy.armor = 0;

    SkillExecutionContext ctx;
    ctx.casterStats = &stats;
    ctx.casterEntityId = 1;
    ctx.targetMobStats = &enemy;
    ctx.targetLevel = enemy.level;
    ctx.targetArmor = enemy.armor;
    ctx.targetMaxHP = enemy.maxHP;
    ctx.targetCurrentHP = enemy.currentHP;
    ctx.targetAlive = true;
    ctx.distanceToTarget = 1.0f;

    int damage = sm.executeSkill("slash", 1, ctx);
    // Damage should be > 0 (base damage * 200%)
    CHECK(damage > 0);
}

TEST_CASE("SkillManager: executeSkill starts cooldown") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    stats.currentMP = 100;

    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("slash", "Warrior", 1);
    def.usesHitRate = false;
    def.cooldownPerRank = {5.0f, 4.0f, 3.0f};
    sm.registerSkillDefinition(def);

    sm.learnSkill("slash", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("slash");

    EnemyStats enemy;
    enemy.level = 10;
    enemy.maxHP = 99999;
    enemy.currentHP = 99999;

    SkillExecutionContext ctx;
    ctx.casterStats = &stats;
    ctx.casterEntityId = 1;
    ctx.targetMobStats = &enemy;
    ctx.targetLevel = enemy.level;
    ctx.targetMaxHP = enemy.maxHP;
    ctx.targetCurrentHP = enemy.currentHP;
    ctx.targetAlive = true;
    ctx.distanceToTarget = 1.0f;

    CHECK_FALSE(sm.isOnCooldown("slash"));
    sm.executeSkill("slash", 1, ctx);
    CHECK(sm.isOnCooldown("slash"));
    CHECK(sm.getRemainingCooldown("slash") == doctest::Approx(5.0f));
}

TEST_CASE("SkillManager: executeSkill deducts mana") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    stats.currentMP = 100;

    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("slash", "Warrior", 1);
    def.usesHitRate = false;
    def.costPerRank = {15.0f, 20.0f, 25.0f};
    sm.registerSkillDefinition(def);

    sm.learnSkill("slash", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("slash");

    EnemyStats enemy;
    enemy.level = 10;
    enemy.maxHP = 99999;
    enemy.currentHP = 99999;

    SkillExecutionContext ctx;
    ctx.casterStats = &stats;
    ctx.casterEntityId = 1;
    ctx.targetMobStats = &enemy;
    ctx.targetLevel = enemy.level;
    ctx.targetMaxHP = enemy.maxHP;
    ctx.targetCurrentHP = enemy.currentHP;
    ctx.targetAlive = true;
    ctx.distanceToTarget = 1.0f;

    sm.executeSkill("slash", 1, ctx);
    CHECK(stats.currentMP == 85);  // 100 - 15
}

TEST_CASE("SkillManager: executeSkill fires onSkillUsed callback") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    stats.currentMP = 100;

    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("slash", "Warrior", 1);
    def.usesHitRate = false;
    sm.registerSkillDefinition(def);

    sm.learnSkill("slash", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("slash");

    bool callbackFired = false;
    std::string usedSkillId;
    int usedRank = 0;
    sm.onSkillUsed = [&](const std::string& id, int r) {
        callbackFired = true;
        usedSkillId = id;
        usedRank = r;
    };

    EnemyStats enemy;
    enemy.level = 10;
    enemy.maxHP = 99999;
    enemy.currentHP = 99999;

    SkillExecutionContext ctx;
    ctx.casterStats = &stats;
    ctx.casterEntityId = 1;
    ctx.targetMobStats = &enemy;
    ctx.targetLevel = enemy.level;
    ctx.targetMaxHP = enemy.maxHP;
    ctx.targetCurrentHP = enemy.currentHP;
    ctx.targetAlive = true;
    ctx.distanceToTarget = 1.0f;

    sm.executeSkill("slash", 1, ctx);
    CHECK(callbackFired);
    CHECK(usedSkillId == "slash");
    CHECK(usedRank == 1);
}

// ============================================================================
// Task 6 Tests: Effects, CC, Self-Buffs, AOE
// ============================================================================

TEST_CASE("SkillManager: executeSkill applies bleed to target") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    stats.currentMP = 100;
    stats.weaponDamageMin = 50;
    stats.weaponDamageMax = 50;
    stats.recalculateStats();
    stats.currentHP = stats.maxHP;
    stats.currentMP = 100;

    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("rend", "Warrior", 1);
    def.usesHitRate = false;
    def.appliesBleed = true;
    def.effectDurationPerRank = {5.0f, 7.0f, 10.0f};
    def.effectValuePerRank = {10.0f, 15.0f, 20.0f};
    sm.registerSkillDefinition(def);

    sm.learnSkill("rend", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("rend");

    EnemyStats enemy;
    enemy.level = 10;
    enemy.maxHP = 99999;
    enemy.currentHP = 99999;

    StatusEffectManager targetSEM;

    SkillExecutionContext ctx;
    ctx.casterStats = &stats;
    ctx.casterEntityId = 1;
    ctx.targetMobStats = &enemy;
    ctx.targetSEM = &targetSEM;
    ctx.targetLevel = enemy.level;
    ctx.targetMaxHP = enemy.maxHP;
    ctx.targetCurrentHP = enemy.currentHP;
    ctx.targetAlive = true;
    ctx.distanceToTarget = 1.0f;

    int damage = sm.executeSkill("rend", 1, ctx);
    CHECK(damage > 0);
    CHECK(targetSEM.hasEffect(EffectType::Bleed));
}

TEST_CASE("SkillManager: executeSkill applies stun to target") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    stats.currentMP = 100;
    stats.weaponDamageMin = 50;
    stats.weaponDamageMax = 50;
    stats.recalculateStats();
    stats.currentHP = stats.maxHP;
    stats.currentMP = 100;

    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("bash", "Warrior", 1);
    def.usesHitRate = false;
    def.stunDurationPerRank = {2.0f, 3.0f, 4.0f};
    sm.registerSkillDefinition(def);

    sm.learnSkill("bash", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("bash");

    EnemyStats enemy;
    enemy.level = 10;
    enemy.maxHP = 99999;
    enemy.currentHP = 99999;

    StatusEffectManager targetSEM;
    CrowdControlSystem targetCC;

    SkillExecutionContext ctx;
    ctx.casterStats = &stats;
    ctx.casterEntityId = 1;
    ctx.targetMobStats = &enemy;
    ctx.targetSEM = &targetSEM;
    ctx.targetCC = &targetCC;
    ctx.targetLevel = enemy.level;
    ctx.targetMaxHP = enemy.maxHP;
    ctx.targetCurrentHP = enemy.currentHP;
    ctx.targetAlive = true;
    ctx.distanceToTarget = 1.0f;

    sm.executeSkill("bash", 1, ctx);
    CHECK(targetCC.isStunned());
}

TEST_CASE("SkillManager: executeSkill grants invulnerability to caster") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    stats.currentMP = 100;

    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def;
    def.skillId = "divine_shield";
    def.skillName = "Divine Shield";
    def.className = "Warrior";
    def.skillType = SkillType::Active;
    def.targetType = SkillTargetType::Self;
    def.levelRequirement = 1;
    def.maxRank = 3;
    def.costPerRank = {10.0f, 10.0f, 10.0f};
    def.cooldownPerRank = {30.0f, 25.0f, 20.0f};
    def.usesHitRate = false;
    def.grantsInvulnerability = true;
    def.effectDurationPerRank = {3.0f, 5.0f, 7.0f};
    // No damage arrays — self buff only
    sm.registerSkillDefinition(def);

    sm.learnSkill("divine_shield", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("divine_shield");

    StatusEffectManager casterSEM;

    SkillExecutionContext ctx;
    ctx.casterStats = &stats;
    ctx.casterEntityId = 1;
    ctx.casterSEM = &casterSEM;
    ctx.targetAlive = true;

    sm.executeSkill("divine_shield", 1, ctx);
    CHECK(casterSEM.isInvulnerable());
}

TEST_CASE("SkillManager: AOE hits multiple targets capped by maxTargets") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    stats.currentMP = 100;
    stats.weaponDamageMin = 50;
    stats.weaponDamageMax = 50;
    stats.recalculateStats();
    stats.currentHP = stats.maxHP;
    stats.currentMP = 100;

    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("whirlwind", "Warrior", 1);
    def.usesHitRate = false;
    def.aoeRadius = 5.0f;
    def.maxTargetsPerRank = {3, 5, 8};
    sm.registerSkillDefinition(def);

    sm.learnSkill("whirlwind", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("whirlwind");

    // Create 5 enemies
    EnemyStats enemies[5];
    StatusEffectManager sems[5];
    std::vector<SkillExecutionContext> targets;

    for (int i = 0; i < 5; ++i) {
        enemies[i].level = 10;
        enemies[i].maxHP = 99999;
        enemies[i].currentHP = 99999;

        SkillExecutionContext tctx;
        tctx.targetMobStats = &enemies[i];
        tctx.targetSEM = &sems[i];
        tctx.targetLevel = 10;
        tctx.targetMaxHP = 99999;
        tctx.targetCurrentHP = 99999;
        tctx.targetAlive = true;
        targets.push_back(tctx);
    }

    SkillExecutionContext primaryCtx;
    primaryCtx.casterStats = &stats;
    primaryCtx.casterEntityId = 1;

    int totalDamage = sm.executeSkillAOE("whirlwind", 1, primaryCtx, targets);
    CHECK(totalDamage > 0);

    // maxTargets at rank 1 is 3, so only first 3 should take damage
    int hitCount = 0;
    for (int i = 0; i < 5; ++i) {
        if (enemies[i].currentHP < 99999) {
            hitCount++;
        }
    }
    CHECK(hitCount == 3);
}

TEST_CASE("SkillManager: Cataclysm scales damage with mana spent") {
    CharacterStats stats = makeTestStats("Mage", ClassType::Mage, 10);
    stats.currentMP = 200;
    stats.weaponDamageMin = 10;
    stats.weaponDamageMax = 10;
    stats.recalculateStats();
    stats.currentHP = stats.maxHP;
    stats.currentMP = 200;

    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def;
    def.skillId = "cataclysm";
    def.skillName = "Cataclysm";
    def.className = "Mage";
    def.skillType = SkillType::Active;
    def.targetType = SkillTargetType::SingleEnemy;
    def.damageType = DamageType::Magic;
    def.levelRequirement = 1;
    def.maxRank = 3;
    def.damagePerRank = {150.0f, 200.0f, 300.0f};
    def.costPerRank = {50.0f, 75.0f, 100.0f};
    def.cooldownPerRank = {10.0f, 10.0f, 10.0f};
    def.resourceType = ResourceType::Mana;
    def.scalesWithResource = true;
    def.usesHitRate = false;
    def.canCrit = false;  // no crit for predictability
    sm.registerSkillDefinition(def);

    sm.learnSkill("cataclysm", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("cataclysm");

    EnemyStats enemy;
    enemy.level = 10;
    enemy.maxHP = 99999;
    enemy.currentHP = 99999;
    enemy.magicResist = 0;

    SkillExecutionContext ctx;
    ctx.casterStats = &stats;
    ctx.casterEntityId = 1;
    ctx.targetMobStats = &enemy;
    ctx.targetLevel = enemy.level;
    ctx.targetArmor = 0;
    ctx.targetMagicResist = 0;
    ctx.targetMaxHP = enemy.maxHP;
    ctx.targetCurrentHP = enemy.currentHP;
    ctx.targetAlive = true;
    ctx.distanceToTarget = 1.0f;

    int damage = sm.executeSkill("cataclysm", 1, ctx);
    // Should have spent all 200 mana, and damage scaled by 200/50 = 4x
    CHECK(stats.currentMP == 0);
    CHECK(damage > 0);
}

TEST_CASE("SkillManager: double-cast skips cost and cooldown") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    stats.currentMP = 100;
    stats.weaponDamageMin = 50;
    stats.weaponDamageMax = 50;
    stats.recalculateStats();
    stats.currentHP = stats.maxHP;
    stats.currentMP = 100;

    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("double_arrow", "Warrior", 1);
    def.usesHitRate = false;
    def.enablesDoubleCast = true;
    def.doubleCastWindow = 2.0f;
    def.costPerRank = {20.0f, 25.0f, 30.0f};
    def.cooldownPerRank = {5.0f, 4.0f, 3.0f};
    sm.registerSkillDefinition(def);

    sm.learnSkill("double_arrow", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("double_arrow");

    EnemyStats enemy;
    enemy.level = 10;
    enemy.maxHP = 99999;
    enemy.currentHP = 99999;

    SkillExecutionContext ctx;
    ctx.casterStats = &stats;
    ctx.casterEntityId = 1;
    ctx.targetMobStats = &enemy;
    ctx.targetLevel = enemy.level;
    ctx.targetMaxHP = enemy.maxHP;
    ctx.targetCurrentHP = enemy.currentHP;
    ctx.targetAlive = true;
    ctx.distanceToTarget = 1.0f;

    // First cast: costs mana and starts cooldown
    int mp_before = stats.currentMP;
    sm.executeSkill("double_arrow", 1, ctx);
    CHECK(stats.currentMP == mp_before - 20);
    CHECK(sm.isDoubleCastReady());

    // Second cast (double-cast): free — no cost, no cooldown check
    int mp_before2 = stats.currentMP;
    sm.executeSkill("double_arrow", 1, ctx);
    CHECK(stats.currentMP == mp_before2);  // no mana spent
    CHECK_FALSE(sm.isDoubleCastReady());   // consumed
}
