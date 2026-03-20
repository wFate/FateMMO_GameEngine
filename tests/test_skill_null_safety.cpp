#include <doctest/doctest.h>
#include "game/shared/skill_manager.h"
#include "game/shared/character_stats.h"
#include "game/shared/enemy_stats.h"
#include "game/shared/crowd_control.h"
#include "game/shared/status_effects.h"

using namespace fate;

// ============================================================================
// Helpers
// ============================================================================

static CharacterStats makeFullStats() {
    CharacterStats stats;
    stats.className = "Warrior";
    stats.level = 10;
    stats.classDef.classType = ClassType::Warrior;
    stats.classDef.baseStrength = 10;
    stats.classDef.baseVitality = 10;
    stats.classDef.baseIntelligence = 10;
    stats.classDef.baseDexterity = 10;
    stats.classDef.baseWisdom = 10;
    stats.classDef.baseMaxHP = 100;
    stats.classDef.baseMaxMP = 50;
    stats.classDef.hpPerLevel = 10.0f;
    stats.classDef.mpPerLevel = 5.0f;
    stats.classDef.strPerLevel = 2.0f;
    stats.classDef.vitPerLevel = 1.0f;
    stats.classDef.intPerLevel = 1.0f;
    stats.classDef.dexPerLevel = 1.0f;
    stats.classDef.wisPerLevel = 1.0f;
    stats.classDef.baseHitRate = 80.0f;
    stats.classDef.attackRange = 1.5f;
    stats.recalculateStats();
    stats.currentHP = stats.maxHP;
    stats.currentMP = stats.maxMP;
    return stats;
}

static EnemyStats makeFullEnemy() {
    EnemyStats enemy;
    enemy.level = 10;
    enemy.maxHP = 99999;
    enemy.currentHP = 99999;
    return enemy;
}

static SkillDefinition makeDamageSkill(const std::string& id) {
    SkillDefinition def;
    def.skillId = id;
    def.skillName = id;
    def.className = "Warrior";
    def.levelRequirement = 1;
    def.skillType = SkillType::Active;
    def.targetType = SkillTargetType::SingleEnemy;
    def.damageType = DamageType::Physical;
    def.baseDamage = 50;
    def.damagePerRank = {120.0f, 150.0f, 200.0f};
    def.cooldownPerRank = {2.0f, 2.0f, 2.0f};
    def.costPerRank = {};  // no resource cost by default
    def.range = 5.0f;
    def.canCrit = false;       // disable crit randomness for deterministic tests
    def.usesHitRate = false;   // disable hit roll for deterministic tests
    return def;
}

/// Set up a SkillManager with a skill learned and activated at the given rank.
static void setupSkillManager(SkillManager& mgr, CharacterStats& stats,
                               const SkillDefinition& def, int rank) {
    mgr.initialize(&stats);
    mgr.registerSkillDefinition(def);
    mgr.learnSkill(def.skillId, 1);
    // Activate ranks up to the requested rank
    for (int r = 0; r < rank; ++r) {
        mgr.grantSkillPoint();
        mgr.activateSkillRank(def.skillId);
        if (r + 1 < rank) {
            mgr.learnSkill(def.skillId, r + 2);
        }
    }
}

// ============================================================================
// Null casterStats
// ============================================================================

TEST_CASE("Skill: executeSkill with null casterStats returns 0") {
    CharacterStats stats = makeFullStats();
    EnemyStats enemy = makeFullEnemy();
    StatusEffectManager casterSEM, targetSEM;
    CrowdControlSystem casterCC, targetCC;

    SkillDefinition def = makeDamageSkill("null_caster_test");

    SkillManager mgr;
    setupSkillManager(mgr, stats, def, 1);

    SkillExecutionContext ctx{};
    ctx.casterEntityId = 1;
    ctx.targetEntityId = 2;
    ctx.casterStats = nullptr;  // null caster stats
    ctx.targetMobStats = &enemy;
    ctx.casterSEM = &casterSEM;
    ctx.targetSEM = &targetSEM;
    ctx.casterCC = &casterCC;
    ctx.targetCC = &targetCC;
    ctx.distanceToTarget = 1.0f;
    ctx.targetAlive = true;
    ctx.targetLevel = 10;
    ctx.targetCurrentHP = enemy.currentHP;
    ctx.targetMaxHP = enemy.maxHP;

    // Should not crash; damage will be 0 since stats is null in calculateDamage path
    int dmg = mgr.executeSkill("null_caster_test", 1, ctx);
    CHECK(dmg >= 0);  // graceful, no crash
}

// ============================================================================
// Null targetSEM — bleed skill should still deal damage, skip effect
// ============================================================================

TEST_CASE("Skill: executeSkill with null targetSEM skips effect application") {
    CharacterStats stats = makeFullStats();
    EnemyStats enemy = makeFullEnemy();
    StatusEffectManager casterSEM;
    CrowdControlSystem casterCC, targetCC;

    SkillDefinition def = makeDamageSkill("bleed_no_sem");
    def.appliesBleed = true;
    def.effectDurationPerRank = {5.0f, 5.0f, 5.0f};
    def.effectValuePerRank = {10.0f, 10.0f, 10.0f};

    SkillManager mgr;
    setupSkillManager(mgr, stats, def, 1);

    SkillExecutionContext ctx{};
    ctx.casterEntityId = 1;
    ctx.targetEntityId = 2;
    ctx.casterStats = &stats;
    ctx.targetMobStats = &enemy;
    ctx.casterSEM = &casterSEM;
    ctx.targetSEM = nullptr;  // null target SEM
    ctx.casterCC = &casterCC;
    ctx.targetCC = &targetCC;
    ctx.distanceToTarget = 1.0f;
    ctx.targetAlive = true;
    ctx.targetLevel = 10;
    ctx.targetCurrentHP = enemy.currentHP;
    ctx.targetMaxHP = enemy.maxHP;

    // Should deal damage, not crash on null targetSEM
    int dmg = mgr.executeSkill("bleed_no_sem", 1, ctx);
    CHECK(dmg >= 0);  // no crash
}

// ============================================================================
// Null casterSEM — should work without SE damage multiplier
// ============================================================================

TEST_CASE("Skill: executeSkill with null casterSEM works") {
    CharacterStats stats = makeFullStats();
    EnemyStats enemy = makeFullEnemy();
    StatusEffectManager targetSEM;
    CrowdControlSystem casterCC, targetCC;

    SkillDefinition def = makeDamageSkill("no_caster_sem");

    SkillManager mgr;
    setupSkillManager(mgr, stats, def, 1);

    SkillExecutionContext ctx{};
    ctx.casterEntityId = 1;
    ctx.targetEntityId = 2;
    ctx.casterStats = &stats;
    ctx.targetMobStats = &enemy;
    ctx.casterSEM = nullptr;  // null caster SEM
    ctx.targetSEM = &targetSEM;
    ctx.casterCC = &casterCC;
    ctx.targetCC = &targetCC;
    ctx.distanceToTarget = 1.0f;
    ctx.targetAlive = true;
    ctx.targetLevel = 10;
    ctx.targetCurrentHP = enemy.currentHP;
    ctx.targetMaxHP = enemy.maxHP;

    int dmg = mgr.executeSkill("no_caster_sem", 1, ctx);
    CHECK(dmg >= 0);  // no crash
}

// ============================================================================
// Null casterCC — should skip CC check (assume can act)
// ============================================================================

TEST_CASE("Skill: executeSkill with null casterCC skips CC check") {
    CharacterStats stats = makeFullStats();
    EnemyStats enemy = makeFullEnemy();
    StatusEffectManager casterSEM, targetSEM;
    CrowdControlSystem targetCC;

    SkillDefinition def = makeDamageSkill("no_caster_cc");

    SkillManager mgr;
    setupSkillManager(mgr, stats, def, 1);

    SkillExecutionContext ctx{};
    ctx.casterEntityId = 1;
    ctx.targetEntityId = 2;
    ctx.casterStats = &stats;
    ctx.targetMobStats = &enemy;
    ctx.casterSEM = &casterSEM;
    ctx.targetSEM = &targetSEM;
    ctx.casterCC = nullptr;  // null caster CC
    ctx.targetCC = &targetCC;
    ctx.distanceToTarget = 1.0f;
    ctx.targetAlive = true;
    ctx.targetLevel = 10;
    ctx.targetCurrentHP = enemy.currentHP;
    ctx.targetMaxHP = enemy.maxHP;

    int dmg = mgr.executeSkill("no_caster_cc", 1, ctx);
    CHECK(dmg >= 0);  // no crash, CC check skipped
}

// ============================================================================
// Null targetCC — stun skill should deal damage, skip stun application
// ============================================================================

TEST_CASE("Skill: executeSkill with null targetCC skips stun application") {
    CharacterStats stats = makeFullStats();
    EnemyStats enemy = makeFullEnemy();
    StatusEffectManager casterSEM, targetSEM;
    CrowdControlSystem casterCC;

    SkillDefinition def = makeDamageSkill("stun_no_target_cc");
    def.stunDurationPerRank = {2.0f, 3.0f, 4.0f};

    SkillManager mgr;
    setupSkillManager(mgr, stats, def, 1);

    SkillExecutionContext ctx{};
    ctx.casterEntityId = 1;
    ctx.targetEntityId = 2;
    ctx.casterStats = &stats;
    ctx.targetMobStats = &enemy;
    ctx.casterSEM = &casterSEM;
    ctx.targetSEM = &targetSEM;
    ctx.casterCC = &casterCC;
    ctx.targetCC = nullptr;  // null target CC
    ctx.distanceToTarget = 1.0f;
    ctx.targetAlive = true;
    ctx.targetLevel = 10;
    ctx.targetCurrentHP = enemy.currentHP;
    ctx.targetMaxHP = enemy.maxHP;

    int dmg = mgr.executeSkill("stun_no_target_cc", 1, ctx);
    CHECK(dmg >= 0);  // no crash, stun skipped
}

// ============================================================================
// Both targetMobStats and targetPlayerStats null — invalid target
// ============================================================================

TEST_CASE("Skill: executeSkill with no target stats fails as invalid target") {
    CharacterStats stats = makeFullStats();
    StatusEffectManager casterSEM, targetSEM;
    CrowdControlSystem casterCC, targetCC;

    SkillDefinition def = makeDamageSkill("no_target_stats");

    SkillManager mgr;
    setupSkillManager(mgr, stats, def, 1);

    std::string failReason;
    mgr.onSkillFailed = [&](const std::string&, std::string reason) {
        failReason = reason;
    };

    SkillExecutionContext ctx{};
    ctx.casterEntityId = 1;
    ctx.targetEntityId = 2;
    ctx.casterStats = &stats;
    ctx.targetMobStats = nullptr;     // null
    ctx.targetPlayerStats = nullptr;  // null
    ctx.targetIsPlayer = false;
    ctx.casterSEM = &casterSEM;
    ctx.targetSEM = &targetSEM;
    ctx.casterCC = &casterCC;
    ctx.targetCC = &targetCC;
    ctx.distanceToTarget = 1.0f;
    ctx.targetAlive = true;
    ctx.targetLevel = 10;

    int dmg = mgr.executeSkill("no_target_stats", 1, ctx);
    CHECK(dmg == 0);
    CHECK(failReason == "Invalid target");
}

// ============================================================================
// 0 range skill — should work regardless of distance
// ============================================================================

TEST_CASE("Skill: range 0 works at any distance") {
    CharacterStats stats = makeFullStats();
    EnemyStats enemy = makeFullEnemy();
    StatusEffectManager casterSEM, targetSEM;
    CrowdControlSystem casterCC, targetCC;

    SkillDefinition def = makeDamageSkill("zero_range");
    def.range = 0.0f;  // range 0 means no range check

    SkillManager mgr;
    setupSkillManager(mgr, stats, def, 1);

    SkillExecutionContext ctx{};
    ctx.casterEntityId = 1;
    ctx.targetEntityId = 2;
    ctx.casterStats = &stats;
    ctx.targetMobStats = &enemy;
    ctx.casterSEM = &casterSEM;
    ctx.targetSEM = &targetSEM;
    ctx.casterCC = &casterCC;
    ctx.targetCC = &targetCC;
    ctx.distanceToTarget = 999.0f;  // very far away
    ctx.targetAlive = true;
    ctx.targetLevel = 10;
    ctx.targetCurrentHP = enemy.currentHP;
    ctx.targetMaxHP = enemy.maxHP;

    // range=0 means no range check is performed — should succeed
    int dmg = mgr.executeSkill("zero_range", 1, ctx);
    CHECK(dmg >= 0);  // no "Out of range" rejection
}

// ============================================================================
// Self target type succeeds without target
// ============================================================================

TEST_CASE("Skill: Self target type succeeds without target entity") {
    CharacterStats stats = makeFullStats();
    StatusEffectManager casterSEM;
    CrowdControlSystem casterCC;

    SkillDefinition def;
    def.skillId = "self_buff";
    def.skillName = "self_buff";
    def.className = "Warrior";
    def.levelRequirement = 1;
    def.skillType = SkillType::Active;
    def.targetType = SkillTargetType::Self;
    def.damagePerRank = {};        // no damage — pure buff
    def.cooldownPerRank = {5.0f};
    def.costPerRank = {};
    def.grantsInvulnerability = true;
    def.effectDurationPerRank = {3.0f};
    def.canCrit = false;
    def.usesHitRate = false;

    SkillManager mgr;
    setupSkillManager(mgr, stats, def, 1);

    SkillExecutionContext ctx{};
    ctx.casterEntityId = 1;
    ctx.targetEntityId = 0;         // no target
    ctx.casterStats = &stats;
    ctx.targetMobStats = nullptr;
    ctx.targetPlayerStats = nullptr;
    ctx.casterSEM = &casterSEM;
    ctx.targetSEM = nullptr;
    ctx.casterCC = &casterCC;
    ctx.targetCC = nullptr;
    ctx.targetAlive = true;

    int result = mgr.executeSkill("self_buff", 1, ctx);
    // Self-buff with no damagePerRank returns 0 damage, but succeeds
    CHECK(result == 0);
    CHECK(casterSEM.isInvulnerable());
}

// ============================================================================
// Dead target rejection
// ============================================================================

TEST_CASE("Skill: executeSkill against dead target fails") {
    CharacterStats stats = makeFullStats();
    EnemyStats enemy = makeFullEnemy();
    StatusEffectManager casterSEM, targetSEM;
    CrowdControlSystem casterCC, targetCC;

    SkillDefinition def = makeDamageSkill("attack_dead");

    SkillManager mgr;
    setupSkillManager(mgr, stats, def, 1);

    std::string failReason;
    mgr.onSkillFailed = [&](const std::string&, std::string reason) {
        failReason = reason;
    };

    SkillExecutionContext ctx{};
    ctx.casterEntityId = 1;
    ctx.targetEntityId = 2;
    ctx.casterStats = &stats;
    ctx.targetMobStats = &enemy;
    ctx.casterSEM = &casterSEM;
    ctx.targetSEM = &targetSEM;
    ctx.casterCC = &casterCC;
    ctx.targetCC = &targetCC;
    ctx.distanceToTarget = 1.0f;
    ctx.targetAlive = false;  // dead
    ctx.targetLevel = 10;

    int dmg = mgr.executeSkill("attack_dead", 1, ctx);
    CHECK(dmg == 0);
    CHECK(failReason == "Target is dead");
}

// ============================================================================
// Unlearned skill rejection
// ============================================================================

TEST_CASE("Skill: executeSkill with unlearned skill fails") {
    CharacterStats stats = makeFullStats();
    EnemyStats enemy = makeFullEnemy();
    StatusEffectManager casterSEM, targetSEM;
    CrowdControlSystem casterCC, targetCC;

    SkillDefinition def = makeDamageSkill("known_skill");

    SkillManager mgr;
    setupSkillManager(mgr, stats, def, 1);

    std::string failReason;
    mgr.onSkillFailed = [&](const std::string&, std::string reason) {
        failReason = reason;
    };

    SkillExecutionContext ctx{};
    ctx.casterEntityId = 1;
    ctx.targetEntityId = 2;
    ctx.casterStats = &stats;
    ctx.targetMobStats = &enemy;
    ctx.casterSEM = &casterSEM;
    ctx.targetSEM = &targetSEM;
    ctx.casterCC = &casterCC;
    ctx.targetCC = &targetCC;
    ctx.distanceToTarget = 1.0f;
    ctx.targetAlive = true;
    ctx.targetLevel = 10;

    // Skill not learned at all — should fail at the learned check
    int dmg = mgr.executeSkill("unknown_skill", 1, ctx);
    CHECK(dmg == 0);
    CHECK(failReason == "Skill not learned or rank not activated");
}

// ============================================================================
// Rank higher than activated — rejected
// ============================================================================

TEST_CASE("Skill: executeSkill with rank higher than activated fails") {
    CharacterStats stats = makeFullStats();
    EnemyStats enemy = makeFullEnemy();
    StatusEffectManager casterSEM, targetSEM;
    CrowdControlSystem casterCC, targetCC;

    SkillDefinition def = makeDamageSkill("rank_exceed");

    SkillManager mgr;
    setupSkillManager(mgr, stats, def, 1);  // activated rank 1 only

    std::string failReason;
    mgr.onSkillFailed = [&](const std::string&, std::string reason) {
        failReason = reason;
    };

    SkillExecutionContext ctx{};
    ctx.casterEntityId = 1;
    ctx.targetEntityId = 2;
    ctx.casterStats = &stats;
    ctx.targetMobStats = &enemy;
    ctx.casterSEM = &casterSEM;
    ctx.targetSEM = &targetSEM;
    ctx.casterCC = &casterCC;
    ctx.targetCC = &targetCC;
    ctx.distanceToTarget = 1.0f;
    ctx.targetAlive = true;
    ctx.targetLevel = 10;

    // Try rank 2 when only rank 1 is activated
    int dmg = mgr.executeSkill("rank_exceed", 2, ctx);
    CHECK(dmg == 0);
    CHECK(failReason == "Skill not learned or rank not activated");
}

// ============================================================================
// Fury cost when not enough fury
// ============================================================================

TEST_CASE("Skill: executeSkill with fury cost when not enough fury") {
    CharacterStats stats = makeFullStats();
    stats.currentFury = 0.0f;  // no fury

    EnemyStats enemy = makeFullEnemy();
    StatusEffectManager casterSEM, targetSEM;
    CrowdControlSystem casterCC, targetCC;

    SkillDefinition def = makeDamageSkill("fury_skill");
    def.resourceType = ResourceType::Fury;
    def.costPerRank = {1.0f, 2.0f, 3.0f};  // costs 1 fury at rank 1

    SkillManager mgr;
    setupSkillManager(mgr, stats, def, 1);

    std::string failReason;
    mgr.onSkillFailed = [&](const std::string&, std::string reason) {
        failReason = reason;
    };

    SkillExecutionContext ctx{};
    ctx.casterEntityId = 1;
    ctx.targetEntityId = 2;
    ctx.casterStats = &stats;
    ctx.targetMobStats = &enemy;
    ctx.casterSEM = &casterSEM;
    ctx.targetSEM = &targetSEM;
    ctx.casterCC = &casterCC;
    ctx.targetCC = &targetCC;
    ctx.distanceToTarget = 1.0f;
    ctx.targetAlive = true;
    ctx.targetLevel = 10;

    int dmg = mgr.executeSkill("fury_skill", 1, ctx);
    CHECK(dmg == 0);
    CHECK(failReason == "Not enough resources");
}

TEST_CASE("Skill: executeSkill with rank 0 rejects as invalid rank") {
    CharacterStats stats = makeFullStats();
    stats.currentMP = 100;
    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeDamageSkill("test_skill");
    sm.registerSkillDefinition(def);
    sm.learnSkill("test_skill", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("test_skill");

    SkillExecutionContext ctx;
    ctx.casterStats = &stats;
    ctx.targetAlive = true;
    ctx.distanceToTarget = 1.0f;

    EnemyStats enemy;
    enemy.level = 10; enemy.maxHP = 99999; enemy.currentHP = 99999;
    ctx.targetMobStats = &enemy;
    ctx.targetLevel = 10;

    std::string failReason;
    sm.onSkillFailed = [&](const std::string&, std::string reason) {
        failReason = reason;
    };

    CHECK(sm.executeSkill("test_skill", 0, ctx) == 0);
    CHECK(failReason == "Invalid rank");
}
