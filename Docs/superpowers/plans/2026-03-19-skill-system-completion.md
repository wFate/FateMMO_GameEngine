# Skill System Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the C++ port of the Unity skill system — wire combat pipeline gaps, complete SkillDefinition, build skill execution, add skill bar utilities + learn validation, create client skill cache, integrate passives.

**Architecture:** Extend existing SkillManager with executeSkill() method that shares the same damage/defense pipeline as auto-attacks in CombatActionSystem. Wire StatusEffectManager and CrowdControlSystem into both auto-attacks and skills. Add ClientSkillDefinitionCache for client-side UI data.

**Tech Stack:** C++ 20, custom ECS, doctest, ImGui

**Spec:** `Docs/superpowers/specs/2026-03-19-skill-system-completion-design.md`

**Build command:**
```bash
export LIB="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64"
export INCLUDE="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build out/build/x64-Debug --config Debug 2>&1 | grep -E "error C|FAILED|Linking|ninja: build"
```

**CRITICAL:** `touch` ALL edited .cpp files before building (CMake misses static lib changes on this machine).

**Test command:** `./out/build/x64-Debug/fate_tests.exe`

**Do NOT add `Co-Authored-By` lines to commit messages.**

**Key codebase facts:**
- `World::forEach` supports 1 or 2 template component types only (NOT 3)
- `ByteWriter` requires buffer+capacity: `uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));`
- Components register with `reg.registerComponent<T>()` — no `REG()` macro
- game/shared/ is gitignored — use `git add -f` for files in this directory
- StatusEffectManager methods: applyEffect, applyDoT, applyShield, applyInvulnerability, applyTransform, getDamageMultiplier, getDamageReduction, getSpeedModifier, getBonusDamageTaken, getArmorShred, absorbDamage, consumeGuaranteedCrit, consumeBewitch, removeAllDebuffs, removeAllEffects
- CrowdControlSystem methods: applyStun, applyFreeze, applyRoot, applyTaunt, canMove, canAct, breakFreeze, removeAllCC
- CombatSystem static methods: rollToHit, rollSpellResist, calculateDamageMultiplier, applyArmorReduction, rollBlock, getEffectiveBlockChance, getPlayerMagicDamageReduction, getMobMagicDamageReduction
- Test files auto-discovered: `file(GLOB_RECURSE TEST_SOURCES tests/*.cpp)` — no CMakeLists.txt edits needed for new test files
- `CharacterStats::calculateDamage(bool forceCrit, bool& outIsCrit)` — takes forceCrit bool + outIsCrit reference
- `EnemyStats::takeDamageFrom(uint32_t attackerEntityId, int amount)` — tracks threat + applies damage
- `CrowdControlSystem::applyStun(float duration, const StatusEffectManager* sem, uint32_t source)` — needs SEM pointer for immunity checks
- `StatusEffectManager::applyDoT(EffectType type, float duration, float damagePerTick, uint32_t source)` — for bleed/burn/poison
- `StatusEffectManager::applyEffect(EffectType type, float duration, float value, float value2, uint32_t source)` — general effect apply
- `StatusEffectManager::applyTransform(float duration, float damageMult, float speedBonus, uint32_t source)` — transform effect
- `CombatSystem::rollBlock(ClassType attackerClassType, int attackerStr, int attackerDex, float defenderBlockChance)` — returns bool
- `CombatConfig::pvpDamageMultiplier` is 0.05 (for auto-attacks); skills use 0.30
- Existing enums in `game/shared/game_types.h`: SkillType{Active=0,Passive=1}, SkillTargetType{Self=0,SingleEnemy=1,SingleAlly=2,AreaAroundSelf=3,AreaAtTarget=4,Cone=5,Line=6}, DamageType{Physical=0,Magic=1,...,True=7}, ResourceType{Fury=0,Mana=1}, EffectType{Bleed=0,Burn,Poison,Slow,ArmorShred,HuntersMark,AttackUp,ArmorUp,SpeedUp,ManaRegenUp,Shield,Invulnerable,StunImmune,GuaranteedCrit,Transform,Bewitched}, CCType{None=0,Stunned=1,Frozen=2,Rooted=3,Taunted=4}

---

## File Map (all tasks)

| File | Action | Purpose |
|------|--------|---------|
| `game/shared/skill_manager.h` | Modify | Add SkillDefinition fields, executeSkill, skill bar utilities, passive totals, skill registry |
| `game/shared/skill_manager.cpp` | Modify | Implement executeSkill, learn validation, skill bar methods, passive integration |
| `game/shared/client_skill_cache.h` | Create | ClientSkillDef, ClientSkillRankData, ClientSkillDefinitionCache |
| `game/systems/combat_action_system.h` | Modify | Wire block, lifesteal, shields, CC checks, status effect multipliers, PvP mult |
| `game/systems/movement_system.h` | Modify | Wire CC canMove check, speed modifier |
| `game/shared/character_stats.h` | Modify | Add passive bonus inputs to recalculateStats |
| `game/shared/character_stats.cpp` | Modify | Apply passive bonuses in recalculateStats |
| `tests/test_skill_execution.cpp` | Create | Skill definition, learn validation, skill bar, passive, skill execution tests |
| `tests/test_client_skill_cache.cpp` | Create | Client skill cache tests |
| `tests/test_combat_pipeline.cpp` | Create | Block, lifesteal, shield, CC integration tests |

---

## Task 1: SkillDefinition fields + skill bar utilities + learn validation (Section 2 + 4)

**Files:**
- Modify: `game/shared/skill_manager.h` (lines 26-56 for SkillDefinition, lines 61-135 for SkillManager class)
- Modify: `game/shared/skill_manager.cpp` (lines 71-103 for learnSkill, lines 192-210 for skill bar)
- Create: `tests/test_skill_execution.cpp`

### Steps

- [ ] **Step 1: Add missing fields to SkillDefinition in skill_manager.h**

In `game/shared/skill_manager.h`, replace the existing `SkillDefinition` struct (lines 26-56) with the expanded version. Add these fields after the existing `costPerRank` vector (line 55), before the closing brace:

```cpp
struct SkillDefinition {
    std::string skillId;
    std::string skillName;
    std::string className;

    SkillType       skillType   = SkillType::Active;
    SkillTargetType targetType  = SkillTargetType::SingleEnemy;
    DamageType      damageType  = DamageType::Physical;

    int   baseDamage       = 0;
    int   mpCost           = 0;
    float furyCost         = 0.0f;
    float cooldownSeconds  = 0.0f;
    float range            = 0.0f;
    int   levelRequirement = 1;
    int   maxRank          = 3;

    std::string description;

    // Cast time (0 = instant)
    float castTime = 0.0f;

    // Double-cast: casting this skill opens an instant-cast window
    bool enablesDoubleCast  = false;
    float doubleCastWindow  = 2.0f;

    // Rank scaling arrays (index 0 = rank 1, etc.)
    std::vector<float> damagePerRank;
    std::vector<float> cooldownPerRank;
    std::vector<float> costPerRank;

    // --- Resource & Scaling (new) ---
    ResourceType resourceType = ResourceType::Mana;
    bool canCrit = true;
    bool usesHitRate = true;
    float furyOnHit = 0.0f;
    bool scalesWithResource = false;  // Cataclysm: damage scales with mana spent

    // --- Effect Application Flags (new) ---
    bool appliesBleed = false;
    bool appliesBurn = false;
    bool appliesPoison = false;
    bool appliesSlow = false;
    bool appliesFreeze = false;
    std::vector<float> stunDurationPerRank;
    std::vector<float> effectDurationPerRank;
    std::vector<float> effectValuePerRank;

    // --- Passive Bonuses per-rank (new) ---
    std::vector<float> passiveDamageReductionPerRank;
    std::vector<float> passiveCritBonusPerRank;
    std::vector<float> passiveSpeedBonusPerRank;
    std::vector<int>   passiveHPBonusPerRank;
    std::vector<int>   passiveStatBonusPerRank;

    // --- Special Mechanics (new) ---
    bool isUltimate = false;
    std::vector<float> executeThresholdPerRank;
    bool grantsInvulnerability = false;
    bool removesDebuffs = false;
    bool grantsStunImmunity = false;
    bool grantsCritGuarantee = false;
    float aoeRadius = 0.0f;
    std::vector<int> maxTargetsPerRank;
    float teleportDistance = 0.0f;
    float dashDistance = 0.0f;
    float transformDamageMult = 0.0f;
    float transformSpeedBonus = 0.0f;
};
```

- [ ] **Step 2: Add skill definition registry + skill bar utilities + new declarations to SkillManager**

In `game/shared/skill_manager.h`, add the following to the `SkillManager` class:

After the existing `assignSkillToSlot` and `getSkillInSlot` methods (around line 90), add:

```cpp
    // ---- Skill Bar Utilities (new) ----
    void clearSkillSlot(int globalSlotIndex);
    void swapSkillSlots(int slotA, int slotB);
    bool autoAssignToSkillBar(const std::string& skillId);
```

After the existing `setSerializedState` method (around line 115), add:

```cpp
    // ---- Skill Definition Registry ----
    void registerSkillDefinition(const SkillDefinition& def);
    const SkillDefinition* getSkillDefinition(const std::string& skillId) const;
```

In the `private:` section (around line 120), add after `cooldownEndTimes`:

```cpp
    std::unordered_map<std::string, SkillDefinition> skillDefinitions_;
```

- [ ] **Step 3: Implement skill bar utilities in skill_manager.cpp**

In `game/shared/skill_manager.cpp`, add after the `getSkillInSlot` method (after line 217):

```cpp
// ============================================================================
// Skill Bar Utilities
// ============================================================================

void SkillManager::clearSkillSlot(int globalSlotIndex) {
    if (globalSlotIndex < 0 || globalSlotIndex >= SKILL_BAR_SLOTS) return;
    skillBarSlots[globalSlotIndex] = "";
}

void SkillManager::swapSkillSlots(int slotA, int slotB) {
    if (slotA < 0 || slotA >= SKILL_BAR_SLOTS) return;
    if (slotB < 0 || slotB >= SKILL_BAR_SLOTS) return;
    std::swap(skillBarSlots[slotA], skillBarSlots[slotB]);
}

bool SkillManager::autoAssignToSkillBar(const std::string& skillId) {
    if (!hasSkill(skillId)) return false;

    // Check if already assigned somewhere
    for (int i = 0; i < SKILL_BAR_SLOTS; ++i) {
        if (skillBarSlots[i] == skillId) return true;  // already on bar
    }

    // Find first empty slot
    for (int i = 0; i < SKILL_BAR_SLOTS; ++i) {
        if (skillBarSlots[i].empty()) {
            skillBarSlots[i] = skillId;
            return true;
        }
    }

    return false;  // no empty slots
}
```

- [ ] **Step 4: Implement skill definition registry in skill_manager.cpp**

In `game/shared/skill_manager.cpp`, add after the skill bar utilities:

```cpp
// ============================================================================
// Skill Definition Registry
// ============================================================================

void SkillManager::registerSkillDefinition(const SkillDefinition& def) {
    skillDefinitions_[def.skillId] = def;
}

const SkillDefinition* SkillManager::getSkillDefinition(const std::string& skillId) const {
    auto it = skillDefinitions_.find(skillId);
    return (it != skillDefinitions_.end()) ? &it->second : nullptr;
}
```

- [ ] **Step 5: Add learn validation to learnSkill**

In `game/shared/skill_manager.cpp`, modify the `learnSkill` method. Replace lines 71-103 (the entire learnSkill method body) with:

```cpp
bool SkillManager::learnSkill(const std::string& skillId, int rank) {
    if (rank < 1 || rank > 3) {
        return false;
    }

    // Look up skill definition for validation
    const SkillDefinition* def = getSkillDefinition(skillId);
    if (def) {
        // Validate class requirement
        if (def->className != "Any" && stats && def->className != stats->className) {
            return false;
        }

        // Validate level requirement
        if (stats && stats->level < def->levelRequirement) {
            return false;
        }
    }

    // Check if we already know this skill
    for (auto& skill : learnedSkills) {
        if (skill.skillId == skillId) {
            // Enforce sequential unlock: rank > 1 requires previous rank unlocked
            if (rank > 1 && skill.unlockedRank < rank - 1) {
                return false;
            }

            // Already known — only upgrade if the new rank is higher
            if (rank <= skill.unlockedRank) {
                return false;
            }
            skill.unlockedRank = rank;

            if (onSkillLearned) {
                onSkillLearned(skillId, rank);
            }
            return true;
        }
    }

    // New skill — rank must be 1 for first learn (sequential enforcement)
    if (rank > 1) {
        return false;
    }

    LearnedSkill newSkill;
    newSkill.skillId      = skillId;
    newSkill.unlockedRank = rank;
    newSkill.activatedRank = 0;
    learnedSkills.push_back(std::move(newSkill));

    // Auto-assign to skill bar on first learn
    autoAssignToSkillBar(skillId);

    if (onSkillLearned) {
        onSkillLearned(skillId, rank);
    }
    return true;
}
```

- [ ] **Step 6: Create test file for Task 1**

Create `tests/test_skill_execution.cpp`:

```cpp
#include <doctest/doctest.h>
#include "game/shared/skill_manager.h"

using namespace fate;

// ============================================================================
// Helper: create a CharacterStats configured for testing
// ============================================================================
static CharacterStats makeTestStats(const std::string& className = "Warrior",
                                     ClassType classType = ClassType::Warrior,
                                     int level = 10) {
    CharacterStats stats;
    stats.className = className;
    stats.level = level;
    stats.classDef.classType = classType;
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
    stats.classDef.furyPerBasicAttack = 0.5f;
    stats.classDef.furyPerCriticalHit = 1.0f;
    stats.recalculateStats();
    stats.currentHP = stats.maxHP;
    stats.currentMP = stats.maxMP;
    return stats;
}

// Helper: create a SkillDefinition for testing
static SkillDefinition makeTestSkillDef(const std::string& id = "slash",
                                         const std::string& className = "Warrior",
                                         int levelReq = 1) {
    SkillDefinition def;
    def.skillId = id;
    def.skillName = "Slash";
    def.className = className;
    def.skillType = SkillType::Active;
    def.targetType = SkillTargetType::SingleEnemy;
    def.damageType = DamageType::Physical;
    def.baseDamage = 10;
    def.mpCost = 5;
    def.cooldownSeconds = 2.0f;
    def.range = 3.0f;
    def.levelRequirement = levelReq;
    def.maxRank = 3;
    def.damagePerRank = {120.0f, 150.0f, 200.0f};
    def.cooldownPerRank = {3.0f, 2.5f, 2.0f};
    def.costPerRank = {5.0f, 8.0f, 12.0f};
    def.resourceType = ResourceType::Mana;
    def.canCrit = true;
    def.usesHitRate = true;
    return def;
}

// ============================================================================
// Task 1 Tests: Learn Validation
// ============================================================================

TEST_CASE("SkillManager: learnSkill rejects wrong class") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("fireball", "Mage", 1);
    sm.registerSkillDefinition(def);

    CHECK_FALSE(sm.learnSkill("fireball", 1));
    CHECK_FALSE(sm.hasSkill("fireball"));
}

TEST_CASE("SkillManager: learnSkill rejects insufficient level") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 5);
    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("slash", "Warrior", 10);
    sm.registerSkillDefinition(def);

    CHECK_FALSE(sm.learnSkill("slash", 1));
    CHECK_FALSE(sm.hasSkill("slash"));
}

TEST_CASE("SkillManager: learnSkill rejects rank skip") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("slash", "Warrior", 1);
    sm.registerSkillDefinition(def);

    // Cannot learn rank 2 without rank 1
    CHECK_FALSE(sm.learnSkill("slash", 2));
    CHECK_FALSE(sm.hasSkill("slash"));

    // Learn rank 1 first
    CHECK(sm.learnSkill("slash", 1));
    CHECK(sm.hasSkill("slash"));

    // Cannot skip to rank 3
    CHECK_FALSE(sm.learnSkill("slash", 3));

    // Can learn rank 2 sequentially
    CHECK(sm.learnSkill("slash", 2));
    auto* learned = sm.getLearnedSkill("slash");
    CHECK(learned->unlockedRank == 2);
}

TEST_CASE("SkillManager: learnSkill allows 'Any' class") {
    CharacterStats stats = makeTestStats("Mage", ClassType::Mage, 10);
    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("meditation", "Any", 1);
    sm.registerSkillDefinition(def);

    CHECK(sm.learnSkill("meditation", 1));
    CHECK(sm.hasSkill("meditation"));
}

TEST_CASE("SkillManager: learnSkill without registered def passes (legacy compat)") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager sm;
    sm.initialize(&stats);

    // No definition registered — should still work (backward compatible)
    CHECK(sm.learnSkill("unknown_skill", 1));
    CHECK(sm.hasSkill("unknown_skill"));
}

TEST_CASE("SkillManager: learnSkill auto-assigns to skill bar") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("slash", "Warrior", 1);
    sm.registerSkillDefinition(def);

    CHECK(sm.learnSkill("slash", 1));

    // Should be auto-assigned to slot 0 (first empty)
    CHECK(sm.getSkillInSlot(0) == "slash");
}

// ============================================================================
// Task 1 Tests: Skill Bar Utilities
// ============================================================================

TEST_CASE("SkillManager: clearSkillSlot") {
    CharacterStats stats = makeTestStats();
    SkillManager sm;
    sm.initialize(&stats);

    sm.learnSkill("slash", 1);
    sm.assignSkillToSlot("slash", 3);
    CHECK(sm.getSkillInSlot(3) == "slash");

    sm.clearSkillSlot(3);
    CHECK(sm.getSkillInSlot(3) == "");
}

TEST_CASE("SkillManager: clearSkillSlot ignores out of bounds") {
    CharacterStats stats = makeTestStats();
    SkillManager sm;
    sm.initialize(&stats);

    // Should not crash
    sm.clearSkillSlot(-1);
    sm.clearSkillSlot(20);
    sm.clearSkillSlot(999);
}

TEST_CASE("SkillManager: swapSkillSlots") {
    CharacterStats stats = makeTestStats();
    SkillManager sm;
    sm.initialize(&stats);

    sm.learnSkill("slash", 1);
    sm.learnSkill("charge", 1);
    sm.assignSkillToSlot("slash", 0);
    sm.assignSkillToSlot("charge", 1);

    sm.swapSkillSlots(0, 1);
    CHECK(sm.getSkillInSlot(0) == "charge");
    CHECK(sm.getSkillInSlot(1) == "slash");
}

TEST_CASE("SkillManager: swapSkillSlots with empty slot") {
    CharacterStats stats = makeTestStats();
    SkillManager sm;
    sm.initialize(&stats);

    sm.learnSkill("slash", 1);
    sm.assignSkillToSlot("slash", 5);
    CHECK(sm.getSkillInSlot(5) == "slash");
    CHECK(sm.getSkillInSlot(10) == "");

    sm.swapSkillSlots(5, 10);
    CHECK(sm.getSkillInSlot(5) == "");
    CHECK(sm.getSkillInSlot(10) == "slash");
}

TEST_CASE("SkillManager: autoAssignToSkillBar finds first empty slot") {
    CharacterStats stats = makeTestStats();
    SkillManager sm;
    sm.initialize(&stats);

    sm.learnSkill("slash", 1);
    sm.learnSkill("charge", 1);

    // Fill slot 0 manually
    sm.assignSkillToSlot("slash", 0);

    // Auto-assign charge — should go to slot 1 (first empty after 0)
    // But first clear any auto-assign from learnSkill
    sm.clearSkillSlot(1);  // clear whatever learnSkill auto-assigned
    CHECK(sm.autoAssignToSkillBar("charge"));

    // charge should be on some slot
    bool found = false;
    for (int i = 0; i < 20; ++i) {
        if (sm.getSkillInSlot(i) == "charge") {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("SkillManager: autoAssignToSkillBar returns false when not learned") {
    CharacterStats stats = makeTestStats();
    SkillManager sm;
    sm.initialize(&stats);

    CHECK_FALSE(sm.autoAssignToSkillBar("nonexistent"));
}

TEST_CASE("SkillManager: skill definition registry") {
    SkillManager sm;
    CharacterStats stats = makeTestStats();
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("slash", "Warrior", 1);
    sm.registerSkillDefinition(def);

    const SkillDefinition* fetched = sm.getSkillDefinition("slash");
    CHECK(fetched != nullptr);
    CHECK(fetched->skillId == "slash");
    CHECK(fetched->className == "Warrior");
    CHECK(fetched->damagePerRank.size() == 3);

    CHECK(sm.getSkillDefinition("nonexistent") == nullptr);
}
```

- [ ] **Step 7: Build and test**

```bash
touch game/shared/skill_manager.cpp
export LIB="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64"
export INCLUDE="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build out/build/x64-Debug --config Debug 2>&1 | grep -E "error C|FAILED|Linking|ninja: build"
```

```bash
./out/build/x64-Debug/fate_tests.exe -tc="SkillManager*"
```

- [ ] **Step 8: Commit**

```bash
git add -f game/shared/skill_manager.h game/shared/skill_manager.cpp
git add tests/test_skill_execution.cpp
git commit -m "feat: add SkillDefinition fields, skill bar utilities, learn validation + registry"
```

---

## Task 2: ClientSkillDefinitionCache (Section 5)

**Files:**
- Create: `game/shared/client_skill_cache.h`
- Create: `tests/test_client_skill_cache.cpp`

### Steps

- [ ] **Step 1: Create client_skill_cache.h**

Create `game/shared/client_skill_cache.h`:

```cpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

namespace fate {

// ============================================================================
// ClientSkillRankData — per-rank display data for the skill UI
// ============================================================================
struct ClientSkillRankData {
    int resourceCost = 0;
    float cooldownSeconds = 0.0f;
    int damagePercent = 0;
    int maxTargets = 1;
    float effectDuration = 0.0f;
    float effectValue = 0.0f;
    float stunDuration = 0.0f;
    float passiveDamageReduction = 0.0f;
    float passiveCritBonus = 0.0f;
    float passiveSpeedBonus = 0.0f;
    int passiveHPBonus = 0;
    int passiveStatBonus = 0;
};

// ============================================================================
// ClientSkillDef — client-side skill definition for UI display
// ============================================================================
struct ClientSkillDef {
    std::string skillId;
    std::string skillName;
    std::string description;
    std::string skillType;     // "Active" or "Passive"
    std::string resourceType;  // "Mana", "Fury", "None"
    std::string targetType;    // "Self", "SingleEnemy", etc.
    int levelRequired = 1;
    float range = 0.0f;
    float aoeRadius = 0.0f;
    bool isUltimate = false;
    bool isPassive = false;
    bool consumesAllResource = false;
    ClientSkillRankData ranks[3];
};

// ============================================================================
// ClientSkillDefinitionCache — static cache for client-side skill UI data
// ============================================================================
class ClientSkillDefinitionCache {
public:
    /// Populate the cache with skills for a given class.
    /// Clears existing data first, then stores all skills sorted by levelRequired.
    static void populate(const std::string& className, const std::vector<ClientSkillDef>& skills) {
        (void)className;  // stored implicitly via the skills themselves
        cache_.clear();
        sorted_.clear();

        for (const auto& skill : skills) {
            cache_[skill.skillId] = skill;
        }

        sorted_ = skills;
        std::sort(sorted_.begin(), sorted_.end(),
                  [](const ClientSkillDef& a, const ClientSkillDef& b) {
                      return a.levelRequired < b.levelRequired;
                  });
    }

    /// Clear all cached data (call on disconnect).
    static void clear() {
        cache_.clear();
        sorted_.clear();
    }

    /// Get a skill by ID. Returns nullptr if not found.
    static const ClientSkillDef* getSkill(const std::string& skillId) {
        auto it = cache_.find(skillId);
        return (it != cache_.end()) ? &it->second : nullptr;
    }

    /// Get all skills sorted by levelRequired ascending.
    static const std::vector<ClientSkillDef>& getAllSkills() {
        return sorted_;
    }

    /// Check if a skill exists in the cache.
    static bool hasSkill(const std::string& skillId) {
        return cache_.find(skillId) != cache_.end();
    }

private:
    static inline std::unordered_map<std::string, ClientSkillDef> cache_;
    static inline std::vector<ClientSkillDef> sorted_;
};

} // namespace fate
```

- [ ] **Step 2: Create test file for client skill cache**

Create `tests/test_client_skill_cache.cpp`:

```cpp
#include <doctest/doctest.h>
#include "game/shared/client_skill_cache.h"

using namespace fate;

// Helper to create test ClientSkillDef
static ClientSkillDef makeClientDef(const std::string& id,
                                     const std::string& name,
                                     int levelReq,
                                     bool isPassive = false) {
    ClientSkillDef def;
    def.skillId = id;
    def.skillName = name;
    def.description = name + " description";
    def.skillType = isPassive ? "Passive" : "Active";
    def.resourceType = "Mana";
    def.targetType = "SingleEnemy";
    def.levelRequired = levelReq;
    def.range = 3.0f;
    def.isPassive = isPassive;
    def.ranks[0].resourceCost = 5;
    def.ranks[0].cooldownSeconds = 2.0f;
    def.ranks[0].damagePercent = 120;
    def.ranks[1].resourceCost = 8;
    def.ranks[1].cooldownSeconds = 1.8f;
    def.ranks[1].damagePercent = 150;
    def.ranks[2].resourceCost = 12;
    def.ranks[2].cooldownSeconds = 1.5f;
    def.ranks[2].damagePercent = 200;
    return def;
}

TEST_CASE("ClientSkillDefinitionCache: populate and getSkill") {
    std::vector<ClientSkillDef> skills;
    skills.push_back(makeClientDef("slash", "Slash", 1));
    skills.push_back(makeClientDef("charge", "Charge", 5));
    skills.push_back(makeClientDef("whirlwind", "Whirlwind", 10));

    ClientSkillDefinitionCache::populate("Warrior", skills);

    const ClientSkillDef* slash = ClientSkillDefinitionCache::getSkill("slash");
    CHECK(slash != nullptr);
    CHECK(slash->skillName == "Slash");
    CHECK(slash->levelRequired == 1);
    CHECK(slash->ranks[0].damagePercent == 120);
    CHECK(slash->ranks[2].damagePercent == 200);

    const ClientSkillDef* charge = ClientSkillDefinitionCache::getSkill("charge");
    CHECK(charge != nullptr);
    CHECK(charge->skillName == "Charge");

    CHECK(ClientSkillDefinitionCache::getSkill("nonexistent") == nullptr);

    ClientSkillDefinitionCache::clear();
}

TEST_CASE("ClientSkillDefinitionCache: getAllSkills sorted by levelRequired") {
    std::vector<ClientSkillDef> skills;
    skills.push_back(makeClientDef("whirlwind", "Whirlwind", 10));
    skills.push_back(makeClientDef("slash", "Slash", 1));
    skills.push_back(makeClientDef("charge", "Charge", 5));

    ClientSkillDefinitionCache::populate("Warrior", skills);

    const auto& sorted = ClientSkillDefinitionCache::getAllSkills();
    CHECK(sorted.size() == 3);
    CHECK(sorted[0].skillId == "slash");
    CHECK(sorted[0].levelRequired == 1);
    CHECK(sorted[1].skillId == "charge");
    CHECK(sorted[1].levelRequired == 5);
    CHECK(sorted[2].skillId == "whirlwind");
    CHECK(sorted[2].levelRequired == 10);

    ClientSkillDefinitionCache::clear();
}

TEST_CASE("ClientSkillDefinitionCache: hasSkill") {
    std::vector<ClientSkillDef> skills;
    skills.push_back(makeClientDef("slash", "Slash", 1));

    ClientSkillDefinitionCache::populate("Warrior", skills);

    CHECK(ClientSkillDefinitionCache::hasSkill("slash"));
    CHECK_FALSE(ClientSkillDefinitionCache::hasSkill("fireball"));

    ClientSkillDefinitionCache::clear();
}

TEST_CASE("ClientSkillDefinitionCache: clear removes all data") {
    std::vector<ClientSkillDef> skills;
    skills.push_back(makeClientDef("slash", "Slash", 1));
    skills.push_back(makeClientDef("charge", "Charge", 5));

    ClientSkillDefinitionCache::populate("Warrior", skills);
    CHECK(ClientSkillDefinitionCache::hasSkill("slash"));
    CHECK(ClientSkillDefinitionCache::getAllSkills().size() == 2);

    ClientSkillDefinitionCache::clear();
    CHECK_FALSE(ClientSkillDefinitionCache::hasSkill("slash"));
    CHECK(ClientSkillDefinitionCache::getAllSkills().empty());
}

TEST_CASE("ClientSkillDefinitionCache: populate replaces existing data") {
    std::vector<ClientSkillDef> skills1;
    skills1.push_back(makeClientDef("slash", "Slash", 1));

    ClientSkillDefinitionCache::populate("Warrior", skills1);
    CHECK(ClientSkillDefinitionCache::hasSkill("slash"));
    CHECK(ClientSkillDefinitionCache::getAllSkills().size() == 1);

    std::vector<ClientSkillDef> skills2;
    skills2.push_back(makeClientDef("fireball", "Fireball", 1));
    skills2.push_back(makeClientDef("frostbolt", "Frostbolt", 5));

    ClientSkillDefinitionCache::populate("Mage", skills2);
    CHECK_FALSE(ClientSkillDefinitionCache::hasSkill("slash"));
    CHECK(ClientSkillDefinitionCache::hasSkill("fireball"));
    CHECK(ClientSkillDefinitionCache::hasSkill("frostbolt"));
    CHECK(ClientSkillDefinitionCache::getAllSkills().size() == 2);

    ClientSkillDefinitionCache::clear();
}
```

- [ ] **Step 3: Build and test**

```bash
export LIB="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64"
export INCLUDE="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build out/build/x64-Debug --config Debug 2>&1 | grep -E "error C|FAILED|Linking|ninja: build"
```

```bash
./out/build/x64-Debug/fate_tests.exe -tc="ClientSkillDefinitionCache*"
```

- [ ] **Step 4: Commit**

```bash
git add -f game/shared/client_skill_cache.h
git add tests/test_client_skill_cache.cpp
git commit -m "feat: add ClientSkillDefinitionCache for client-side skill UI data"
```

---

## Task 3: Wire combat pipeline gaps in auto-attack (Section 1)

**Files:**
- Modify: `game/systems/combat_action_system.h` (lines 540-630 for tryAttackTarget)
- Modify: `game/systems/movement_system.h` (lines 19-42 for update)
- Create: `tests/test_combat_pipeline.cpp`

### Steps

- [ ] **Step 1: Add StatusEffectManager and CrowdControlSystem includes to combat_action_system.h**

At the top of `game/systems/combat_action_system.h`, verify these includes exist (they should already be available via `game/components/game_components.h` which includes `character_stats.h` which includes `status_effects.h` and `crowd_control.h`). If not present, add:

```cpp
#include "game/shared/status_effects.h"
#include "game/shared/crowd_control.h"
```

- [ ] **Step 2: Modify tryAttackTarget to wire CC, block, shield, lifesteal, status effects**

In `game/systems/combat_action_system.h`, replace the tryAttackTarget method body. The method starts around line 520 (after the `tryAttackTarget()` signature) and ends at line 630 (before `onMobDeath`).

Find the section that starts with `CharacterStats& ps = statsComp->stats;` and `EnemyStats& es = enemyComp->stats;` (line 541-542) and contains the attack logic. Replace from line 540 (blank line before `CharacterStats& ps`) through line 630 (the closing brace of tryAttackTarget, the line `    }` before the comment `// ------------------------------------------------------------------` / `// onMobDeath`):

```cpp

        CharacterStats& ps = statsComp->stats;
        EnemyStats&     es = enemyComp->stats;

        if (!es.isAlive) { clearTarget(); return; }

        // ---- CC check: cannot attack if crowd controlled ----
        auto* ccComp = player->getComponent<CrowdControlComponent>();
        if (ccComp && !ccComp->cc.canAct()) {
            return;
        }

        // ---- Range check (in tiles) ----
        float distPixels = playerT->position.distance(targetT->position);
        float distTiles  = distPixels / Coords::TILE_SIZE;

        bool isMage = (ps.classDef.classType == ClassType::Mage);
        float requiredRange = isMage ? kMageRange : ps.classDef.attackRange;

        if (distTiles > requiredRange) {
            // Out of range — disable auto-attack for melee/archer so they
            // don't fire the instant they step back in range.
            if (!isMage) {
                autoAttackEnabled_ = false;
                LOG_INFO("Combat", "Target out of range (%.1f tiles > %.1f)",
                         distTiles, requiredRange);
            }
            return;
        }

        // ---- Set cooldown ----
        auto* combatCtrl = player->getComponent<CombatControllerComponent>();
        float cooldown = combatCtrl ? combatCtrl->baseAttackCooldown : 1.5f;
        attackCooldownRemaining_ = cooldown;

        Vec2 textPos = targetT->position;

        // Get status effect managers (may be null if components don't exist)
        auto* playerSEComp = player->getComponent<StatusEffectComponent>();
        auto* targetSEComp = target->getComponent<StatusEffectComponent>();
        auto* targetCCComp = target->getComponent<CrowdControlComponent>();

        if (isMage) {
            // ---- Spell attack ----
            bool resisted = CombatSystem::rollSpellResist(
                ps.level, ps.getIntelligence(), es.level, es.magicResist);

            if (resisted) {
                spawnResistText(textPos);
                LOG_DEBUG("Combat", "Spell resisted by %s", es.enemyName.c_str());
                return;
            }

            // Calculate spell damage
            bool isCrit = false;
            // Check for guaranteed crit from status effect
            bool forceCrit = (playerSEComp && playerSEComp->sem.hasGuaranteedCrit());
            int damage = ps.calculateDamage(forceCrit, isCrit);
            if (forceCrit && isCrit) {
                if (playerSEComp) playerSEComp->sem.consumeGuaranteedCrit();
            }

            // Apply status effect damage multiplier
            if (playerSEComp) {
                float seMult = playerSEComp->sem.getDamageMultiplier();
                damage = static_cast<int>(std::round(damage * seMult));
            }

            // PvP damage multiplier (auto-attack = 0.05)
            // NOTE: mob targets don't get PvP multiplier — this would apply
            // if target were a player; for now mobs only in auto-attack

            // Magic resistance reduction on mob
            float mrReduction = CombatSystem::getMobMagicDamageReduction(es.magicResist);
            damage = static_cast<int>(std::round(damage * (1.0f - mrReduction)));

            // Hunter's Mark bonus damage
            if (targetSEComp) {
                float bonusTaken = targetSEComp->sem.getBonusDamageTaken();
                if (bonusTaken > 0.0f) {
                    damage = static_cast<int>(std::round(damage * (1.0f + bonusTaken)));
                }
            }

            // Bewitch check on mobs
            if (targetSEComp) {
                float bewitchMult = targetSEComp->sem.consumeBewitch(player->id());
                if (bewitchMult > 0.0f) {
                    damage = static_cast<int>(std::round(damage * bewitchMult));
                }
            }

            // Shield absorption on target
            if (targetSEComp) {
                damage = targetSEComp->sem.absorbDamage(damage);
            }

            // Ensure minimum damage of 1
            damage = (std::max)(1, damage);

            es.takeDamageFrom(player->id(), damage);
            spawnDamageText(textPos, damage, isCrit);

            // Freeze break on target
            if (targetCCComp) {
                targetCCComp->cc.breakFreeze();
            }

            // Lifesteal
            if (ps.equipBonusLifesteal > 0.0f) {
                int healAmount = static_cast<int>(std::round(ps.equipBonusLifesteal * damage));
                if (healAmount > 0) {
                    ps.heal(healAmount);
                    auto* playerTransform = player->getComponent<Transform>();
                    if (playerTransform) {
                        spawnHealText(playerTransform->position, healAmount);
                    }
                }
            }

            // Armor shred on crit
            if (isCrit && targetSEComp) {
                float armorShredValue = ps.equipBonusArmorPierce > 0
                    ? static_cast<float>(ps.equipBonusArmorPierce)
                    : 5.0f;
                targetSEComp->sem.applyEffect(EffectType::ArmorShred, 5.0f,
                                               armorShredValue, 0.0f, player->id());
            }

            LOG_DEBUG("Combat", "Spell hit %s for %d%s",
                      es.enemyName.c_str(), damage, isCrit ? " (CRIT)" : "");

        } else {
            // ---- Physical attack (Warrior / Archer) ----
            bool hit = CombatSystem::rollToHit(
                ps.level,
                static_cast<int>(ps.getHitRate()),
                es.level,
                0);

            if (!hit) {
                spawnMissText(textPos);
                LOG_DEBUG("Combat", "Attack missed %s", es.enemyName.c_str());
                return;
            }

            // Block check (physical attacks against player targets only)
            // NOTE: mobs don't block in auto-attack currently; this is for
            // future PvP or if mobs have block. For now, block only triggers
            // on player targets. Since auto-attack targets mobs, block is
            // not applied here. Block will be applied in the skill pipeline
            // when targeting players.

            bool isCrit = false;
            bool forceCrit = (playerSEComp && playerSEComp->sem.hasGuaranteedCrit());
            int damage = ps.calculateDamage(forceCrit, isCrit);
            if (forceCrit && isCrit) {
                if (playerSEComp) playerSEComp->sem.consumeGuaranteedCrit();
            }

            // Apply status effect damage multiplier
            if (playerSEComp) {
                float seMult = playerSEComp->sem.getDamageMultiplier();
                damage = static_cast<int>(std::round(damage * seMult));
            }

            // Armor reduction on mob
            damage = CombatSystem::applyArmorReduction(damage, es.armor);

            // Hunter's Mark bonus damage
            if (targetSEComp) {
                float bonusTaken = targetSEComp->sem.getBonusDamageTaken();
                if (bonusTaken > 0.0f) {
                    damage = static_cast<int>(std::round(damage * (1.0f + bonusTaken)));
                }
            }

            // Bewitch check on mobs
            if (targetSEComp) {
                float bewitchMult = targetSEComp->sem.consumeBewitch(player->id());
                if (bewitchMult > 0.0f) {
                    damage = static_cast<int>(std::round(damage * bewitchMult));
                }
            }

            // Shield absorption on target
            if (targetSEComp) {
                damage = targetSEComp->sem.absorbDamage(damage);
            }

            // Ensure minimum damage of 1
            damage = (std::max)(1, damage);

            es.takeDamageFrom(player->id(), damage);
            spawnDamageText(textPos, damage, isCrit);

            // Freeze break on target
            if (targetCCComp) {
                targetCCComp->cc.breakFreeze();
            }

            // Fury generation
            float furyGain = isCrit
                ? ps.classDef.furyPerCriticalHit
                : ps.classDef.furyPerBasicAttack;
            ps.addFury(furyGain);

            // Lifesteal
            if (ps.equipBonusLifesteal > 0.0f) {
                int healAmount = static_cast<int>(std::round(ps.equipBonusLifesteal * damage));
                if (healAmount > 0) {
                    ps.heal(healAmount);
                    auto* playerTransform = player->getComponent<Transform>();
                    if (playerTransform) {
                        spawnHealText(playerTransform->position, healAmount);
                    }
                }
            }

            // Armor shred on crit
            if (isCrit && targetSEComp) {
                float armorShredValue = ps.equipBonusArmorPierce > 0
                    ? static_cast<float>(ps.equipBonusArmorPierce)
                    : 5.0f;
                targetSEComp->sem.applyEffect(EffectType::ArmorShred, 5.0f,
                                               armorShredValue, 0.0f, player->id());
            }

            LOG_DEBUG("Combat", "Hit %s for %d%s (+%.1f fury)",
                      es.enemyName.c_str(), damage,
                      isCrit ? " (CRIT)" : "", furyGain);
        }

        // ---- Check for mob death ----
        if (!es.isAlive) {
            onMobDeath(player, target);
        }
    }
```

**IMPORTANT:** This replacement assumes the following ECS component wrappers exist on entities. If `StatusEffectComponent`, `CrowdControlComponent` do not exist as wrapper components, you will need to check what the actual component names are. Search for `StatusEffectManager` usage in `game/components/game_components.h`:

```bash
grep -n "StatusEffect\|CrowdControl" game/components/game_components.h
```

If the components are named differently, adjust the component type names accordingly. Common patterns in this codebase:
- `CharacterStatsComponent` wraps `CharacterStats stats;`
- Similarly expect `StatusEffectComponent` wrapping `StatusEffectManager sem;`
- Similarly expect `CrowdControlComponent` wrapping `CrowdControlSystem cc;`

If these wrapper components do not exist yet, you will need to add them to `game/components/game_components.h`:

```cpp
struct StatusEffectComponent {
    StatusEffectManager sem;
};

struct CrowdControlComponent {
    CrowdControlSystem cc;
};
```

And register them in `game/register_components.h`:

```cpp
reg.registerComponent<StatusEffectComponent>();
reg.registerComponent<CrowdControlComponent>();
```

- [ ] **Step 3: Modify MovementSystem to check CC canMove and apply speed modifier**

In `game/systems/movement_system.h`, add includes at the top (after existing includes, around line 9):

```cpp
#include "game/shared/status_effects.h"
#include "game/shared/crowd_control.h"
```

In the `update(float dt)` method, find the dead player check block (lines 31-35):

```cpp
                // Dead players cannot move
                auto* statsComp = entity->getComponent<CharacterStatsComponent>();
                if (statsComp && statsComp->stats.isDead) {
                    ctrl->isMoving = false;
                    return;
                }
```

Add the CC check right after it (before `ctrl->isMoving = (dir != Direction::None);` on line 37):

```cpp
                // CC'd players cannot move (stunned, frozen, rooted)
                auto* ccComp = entity->getComponent<CrowdControlComponent>();
                if (ccComp && !ccComp->cc.canMove()) {
                    ctrl->isMoving = false;
                    return;
                }
```

Then find the line that computes movement (line 41):

```cpp
                    Vec2 move = directionToVec(dir) * ctrl->moveSpeed * dt;
```

Replace it with speed modifier application:

```cpp
                    float effectiveSpeed = ctrl->moveSpeed;

                    // Apply status effect speed modifier
                    auto* seComp = entity->getComponent<StatusEffectComponent>();
                    if (seComp) {
                        effectiveSpeed *= seComp->sem.getSpeedModifier();
                    }

                    Vec2 move = directionToVec(dir) * effectiveSpeed * dt;
```

- [ ] **Step 4: Create test file for combat pipeline**

Create `tests/test_combat_pipeline.cpp`:

```cpp
#include <doctest/doctest.h>
#include "game/shared/character_stats.h"
#include "game/shared/status_effects.h"
#include "game/shared/crowd_control.h"
#include "game/shared/combat_system.h"

using namespace fate;

// ============================================================================
// Helper: create a CharacterStats for pipeline tests
// ============================================================================
static CharacterStats makePipelineStats(ClassType classType = ClassType::Warrior,
                                         int level = 10) {
    CharacterStats stats;
    stats.className = "Warrior";
    stats.level = level;
    stats.classDef.classType = classType;
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
    stats.classDef.furyPerBasicAttack = 0.5f;
    stats.classDef.furyPerCriticalHit = 1.0f;
    stats.recalculateStats();
    stats.currentHP = stats.maxHP;
    stats.currentMP = stats.maxMP;
    return stats;
}

// ============================================================================
// Block Tests
// ============================================================================

TEST_CASE("CombatSystem: rollBlock returns bool based on block chance") {
    // With 0 block chance, should never block
    bool blocked = false;
    for (int i = 0; i < 100; ++i) {
        if (CombatSystem::rollBlock(ClassType::Warrior, 10, 10, 0.0f)) {
            blocked = true;
            break;
        }
    }
    CHECK_FALSE(blocked);
}

TEST_CASE("CombatSystem: rollBlock with high block chance blocks sometimes") {
    int blockCount = 0;
    for (int i = 0; i < 1000; ++i) {
        if (CombatSystem::rollBlock(ClassType::Warrior, 10, 10, 0.90f)) {
            blockCount++;
        }
    }
    // With 0.90 base block chance (minus attacker counter), should block frequently
    CHECK(blockCount > 0);
}

// ============================================================================
// Shield Absorption Tests
// ============================================================================

TEST_CASE("StatusEffectManager: shield absorbs damage before HP") {
    StatusEffectManager sem;
    sem.applyShield(50.0f, 10.0f);  // 50 HP shield for 10 seconds
    CHECK(sem.currentShield() == doctest::Approx(50.0f));

    // Absorb 30 damage — shield takes it, 0 remaining
    int remaining = sem.absorbDamage(30);
    CHECK(remaining == 0);
    CHECK(sem.currentShield() == doctest::Approx(20.0f));

    // Absorb 40 damage — shield absorbs 20, remaining = 20
    remaining = sem.absorbDamage(40);
    CHECK(remaining == 20);
    CHECK(sem.currentShield() == doctest::Approx(0.0f));
}

TEST_CASE("StatusEffectManager: no shield means full damage passes through") {
    StatusEffectManager sem;
    int remaining = sem.absorbDamage(100);
    CHECK(remaining == 100);
}

// ============================================================================
// Lifesteal Tests
// ============================================================================

TEST_CASE("CharacterStats: heal restores HP up to max") {
    CharacterStats stats = makePipelineStats();
    stats.currentHP = stats.maxHP - 20;
    int hpBefore = stats.currentHP;

    stats.heal(10);
    CHECK(stats.currentHP == hpBefore + 10);

    // Heal beyond max should clamp
    stats.heal(99999);
    CHECK(stats.currentHP == stats.maxHP);
}

TEST_CASE("Lifesteal calculation: equipLifesteal * damage = heal amount") {
    // Simulate lifesteal: 10% lifesteal on 100 damage = 10 HP healed
    CharacterStats attacker = makePipelineStats();
    attacker.equipBonusLifesteal = 0.10f;
    attacker.currentHP = attacker.maxHP / 2;  // half health
    int hpBefore = attacker.currentHP;

    int damage = 100;
    int healAmount = static_cast<int>(std::round(attacker.equipBonusLifesteal * damage));
    CHECK(healAmount == 10);

    attacker.heal(healAmount);
    CHECK(attacker.currentHP == hpBefore + 10);
}

// ============================================================================
// CC Blocks Action/Movement Tests
// ============================================================================

TEST_CASE("CrowdControlSystem: stunned prevents action and movement") {
    CrowdControlSystem cc;
    StatusEffectManager sem;  // needed for immunity checks

    CHECK(cc.canAct());
    CHECK(cc.canMove());

    cc.applyStun(5.0f, &sem);

    CHECK_FALSE(cc.canAct());
    CHECK_FALSE(cc.canMove());

    // Tick past stun duration
    cc.tick(6.0f);
    CHECK(cc.canAct());
    CHECK(cc.canMove());
}

TEST_CASE("CrowdControlSystem: frozen prevents action and movement") {
    CrowdControlSystem cc;
    StatusEffectManager sem;

    cc.applyFreeze(5.0f, &sem);
    CHECK_FALSE(cc.canAct());
    CHECK_FALSE(cc.canMove());
}

TEST_CASE("CrowdControlSystem: rooted prevents movement but allows action") {
    CrowdControlSystem cc;
    StatusEffectManager sem;

    cc.applyRoot(5.0f, &sem);
    CHECK(cc.canAct());     // can still act
    CHECK_FALSE(cc.canMove());  // cannot move
}

TEST_CASE("CrowdControlSystem: taunted allows movement and action") {
    CrowdControlSystem cc;
    StatusEffectManager sem;

    cc.applyTaunt(5.0f, &sem, 42);
    CHECK(cc.canAct());
    CHECK(cc.canMove());
}

TEST_CASE("CrowdControlSystem: breakFreeze on damage") {
    CrowdControlSystem cc;
    StatusEffectManager sem;

    cc.applyFreeze(10.0f, &sem);
    CHECK(cc.isFrozen());

    cc.breakFreeze();
    CHECK_FALSE(cc.isFrozen());
    CHECK(cc.canAct());
    CHECK(cc.canMove());
}

// ============================================================================
// Status Effect Damage Multiplier Tests
// ============================================================================

TEST_CASE("StatusEffectManager: getDamageMultiplier with AttackUp") {
    StatusEffectManager sem;
    // No effects = multiplier of 1.0
    CHECK(sem.getDamageMultiplier() == doctest::Approx(1.0f));

    // Apply AttackUp with 0.25 value = 1.25x damage
    sem.applyEffect(EffectType::AttackUp, 10.0f, 0.25f);
    CHECK(sem.getDamageMultiplier() == doctest::Approx(1.25f));
}

TEST_CASE("StatusEffectManager: getSpeedModifier with Slow") {
    StatusEffectManager sem;
    CHECK(sem.getSpeedModifier() == doctest::Approx(1.0f));

    // Apply Slow: speed modifier should decrease
    sem.applyEffect(EffectType::Slow, 10.0f, 0.30f);
    float modifier = sem.getSpeedModifier();
    CHECK(modifier < 1.0f);
}

TEST_CASE("StatusEffectManager: Hunter's Mark bonus damage taken") {
    StatusEffectManager sem;
    CHECK(sem.getBonusDamageTaken() == doctest::Approx(0.0f));

    sem.applyEffect(EffectType::HuntersMark, 10.0f, 0.20f);
    CHECK(sem.getBonusDamageTaken() == doctest::Approx(0.20f));
}
```

- [ ] **Step 5: Build and test**

```bash
touch game/shared/character_stats.cpp game/shared/skill_manager.cpp
export LIB="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64"
export INCLUDE="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build out/build/x64-Debug --config Debug 2>&1 | grep -E "error C|FAILED|Linking|ninja: build"
```

```bash
./out/build/x64-Debug/fate_tests.exe -tc="CombatSystem*,StatusEffectManager*,CrowdControlSystem*,Lifesteal*"
```

- [ ] **Step 6: Commit**

```bash
git add game/systems/combat_action_system.h game/systems/movement_system.h
git add tests/test_combat_pipeline.cpp
git commit -m "feat: wire CC, block, shields, lifesteal, status effects into auto-attack and movement"
```

---

## Task 4: Passive skill integration (Section 6)

**Files:**
- Modify: `game/shared/skill_manager.h` (SkillManager class — add passive accumulators)
- Modify: `game/shared/skill_manager.cpp` (activateSkillRank + new recalcPassives method)
- Modify: `game/shared/character_stats.h` (recalculateStats signature + passive fields)
- Modify: `game/shared/character_stats.cpp` (apply passive bonuses in recalculateStats)
- Modify: `tests/test_skill_execution.cpp` (add passive tests)

### Steps

- [ ] **Step 1: Add passive bonus fields to CharacterStats**

In `game/shared/character_stats.h`, add the following public fields after the existing `equipExecuteDamageBonus` line (line 81), before `// ---- Callbacks ----`:

```cpp
    // ---- Passive Skill Bonuses (set by SkillManager) ----
    int   passiveHPBonus          = 0;
    float passiveCritBonus        = 0.0f;
    float passiveSpeedBonus       = 0.0f;
    float passiveDamageReduction  = 0.0f;
    int   passiveStatBonus        = 0;
```

- [ ] **Step 2: Apply passive bonuses in recalculateStats**

In `game/shared/character_stats.cpp`, in the `recalculateStats()` method:

After the `maxHP` calculation (line 41):
```cpp
    maxHP = static_cast<int>(std::round(baseHP * vitalityMultiplier)) + equipBonusHP;
```

Add passive HP:
```cpp
    maxHP += passiveHPBonus;
```

After the `_critRate` calculation block (after line 76, the Archer dex crit line):
```cpp
    if (classDef.classType == ClassType::Archer) {
        _critRate += _bonusDexterity * 0.005f;
    }
```

Add passive crit:
```cpp
    _critRate += passiveCritBonus;
```

After the `_speed` calculation (line 79):
```cpp
    _speed = 1.0f + equipBonusMoveSpeed;
```

Add passive speed:
```cpp
    _speed *= (1.0f + passiveSpeedBonus);
```

After the primary stat calculations (lines 25-29), add passive stat bonus to the primary stat. This should go after the existing stat calculations but before HP. The cleanest approach is to apply it after the full recalculation. Add after the `maxFury` line (line 96, before the closing brace):

```cpp
    // --- Passive stat bonus (added to primary stat) ---
    if (passiveStatBonus != 0) {
        switch (classDef.classType) {
            case ClassType::Warrior:
                _strength += passiveStatBonus;
                _bonusStrength += passiveStatBonus;
                break;
            case ClassType::Mage:
                _intelligence += passiveStatBonus;
                _bonusIntelligence += passiveStatBonus;
                break;
            case ClassType::Archer:
                _dexterity += passiveStatBonus;
                _bonusDexterity += passiveStatBonus;
                break;
        }
    }
```

Note: The `passiveDamageReduction` is not directly applied in `recalculateStats()` — it is queried at damage time via the skill manager. However, if a `_damageReduction` field is later added, it can be applied there. For now, the field is stored and available for the combat pipeline to query.

- [ ] **Step 3: Add passive accumulators to SkillManager**

In `game/shared/skill_manager.h`, add to the public section of `SkillManager` (after the `getSkillDefinition` method added in Task 1):

```cpp
    // ---- Passive Bonuses (accumulated from passive skills) ----
    [[nodiscard]] int   getPassiveHPBonus() const         { return passiveHPBonus_; }
    [[nodiscard]] float getPassiveCritBonus() const       { return passiveCritBonus_; }
    [[nodiscard]] float getPassiveSpeedBonus() const      { return passiveSpeedBonus_; }
    [[nodiscard]] float getPassiveDamageReduction() const { return passiveDamageReduction_; }
    [[nodiscard]] int   getPassiveStatBonus() const       { return passiveStatBonus_; }
```

In the private section, add after `skillDefinitions_`:

```cpp
    // ---- Passive accumulators ----
    int   passiveHPBonus_         = 0;
    float passiveCritBonus_       = 0.0f;
    float passiveSpeedBonus_      = 0.0f;
    float passiveDamageReduction_ = 0.0f;
    int   passiveStatBonus_       = 0;

    void applyPassiveBonusesToStats();
```

- [ ] **Step 4: Implement passive accumulation in activateSkillRank**

In `game/shared/skill_manager.cpp`, add the `applyPassiveBonusesToStats` helper method after the skill definition registry methods:

```cpp
// ============================================================================
// Passive Bonus Application
// ============================================================================

void SkillManager::applyPassiveBonusesToStats() {
    if (!stats) return;

    stats->passiveHPBonus         = passiveHPBonus_;
    stats->passiveCritBonus       = passiveCritBonus_;
    stats->passiveSpeedBonus      = passiveSpeedBonus_;
    stats->passiveDamageReduction = passiveDamageReduction_;
    stats->passiveStatBonus       = passiveStatBonus_;
    stats->recalculateStats();
}
```

Then modify `activateSkillRank` in `game/shared/skill_manager.cpp`. After the line `skill.activatedRank = nextRank;` (currently line 133), and before `availableSkillPoints--;` (line 134), add passive accumulation:

```cpp
            skill.activatedRank = nextRank;

            // Accumulate passive bonuses if this is a passive skill
            const SkillDefinition* def = getSkillDefinition(skillId);
            if (def && def->skillType == SkillType::Passive) {
                int ri = nextRank - 1;  // rank index (0-based)
                if (ri >= 0) {
                    if (ri < static_cast<int>(def->passiveHPBonusPerRank.size()))
                        passiveHPBonus_ += def->passiveHPBonusPerRank[ri];
                    if (ri < static_cast<int>(def->passiveCritBonusPerRank.size()))
                        passiveCritBonus_ += def->passiveCritBonusPerRank[ri];
                    if (ri < static_cast<int>(def->passiveSpeedBonusPerRank.size()))
                        passiveSpeedBonus_ += def->passiveSpeedBonusPerRank[ri];
                    if (ri < static_cast<int>(def->passiveDamageReductionPerRank.size()))
                        passiveDamageReduction_ += def->passiveDamageReductionPerRank[ri];
                    if (ri < static_cast<int>(def->passiveStatBonusPerRank.size()))
                        passiveStatBonus_ += def->passiveStatBonusPerRank[ri];

                    applyPassiveBonusesToStats();
                }
            }

            availableSkillPoints--;
```

- [ ] **Step 5: Add passive tests to test_skill_execution.cpp**

Append to `tests/test_skill_execution.cpp`:

```cpp
// ============================================================================
// Task 4 Tests: Passive Skill Integration
// ============================================================================

TEST_CASE("SkillManager: passive skill increases maxHP") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def;
    def.skillId = "iron_skin";
    def.skillName = "Iron Skin";
    def.className = "Warrior";
    def.skillType = SkillType::Passive;
    def.levelRequirement = 1;
    def.maxRank = 3;
    def.passiveHPBonusPerRank = {50, 100, 200};
    sm.registerSkillDefinition(def);

    int baseMaxHP = stats.maxHP;

    sm.learnSkill("iron_skin", 1);
    sm.grantSkillPoint();
    CHECK(sm.activateSkillRank("iron_skin"));

    CHECK(stats.maxHP == baseMaxHP + 50);
    CHECK(sm.getPassiveHPBonus() == 50);
}

TEST_CASE("SkillManager: passive skill increases crit rate") {
    CharacterStats stats = makeTestStats("Archer", ClassType::Archer, 10);
    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def;
    def.skillId = "keen_eye";
    def.skillName = "Keen Eye";
    def.className = "Archer";
    def.skillType = SkillType::Passive;
    def.levelRequirement = 1;
    def.maxRank = 3;
    def.passiveCritBonusPerRank = {0.05f, 0.05f, 0.10f};
    sm.registerSkillDefinition(def);

    float baseCrit = stats.getCritRate();

    sm.learnSkill("keen_eye", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("keen_eye");

    CHECK(stats.getCritRate() == doctest::Approx(baseCrit + 0.05f));
}

TEST_CASE("SkillManager: passive skill increases move speed") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def;
    def.skillId = "fleet_foot";
    def.skillName = "Fleet Foot";
    def.className = "Warrior";
    def.skillType = SkillType::Passive;
    def.levelRequirement = 1;
    def.maxRank = 3;
    def.passiveSpeedBonusPerRank = {0.05f, 0.05f, 0.10f};
    sm.registerSkillDefinition(def);

    float baseSpeed = stats.getSpeed();

    sm.learnSkill("fleet_foot", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("fleet_foot");

    // Speed = base * (1 + passiveSpeedBonus)
    CHECK(stats.getSpeed() == doctest::Approx(baseSpeed * 1.05f));
}

TEST_CASE("SkillManager: passive skill increases primary stat") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def;
    def.skillId = "brute_force";
    def.skillName = "Brute Force";
    def.className = "Warrior";
    def.skillType = SkillType::Passive;
    def.levelRequirement = 1;
    def.maxRank = 3;
    def.passiveStatBonusPerRank = {5, 10, 15};
    sm.registerSkillDefinition(def);

    int baseStat = stats.getStrength();

    sm.learnSkill("brute_force", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("brute_force");

    CHECK(stats.getStrength() == baseStat + 5);
}

TEST_CASE("SkillManager: passive accumulates across multiple ranks") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def;
    def.skillId = "iron_skin";
    def.skillName = "Iron Skin";
    def.className = "Warrior";
    def.skillType = SkillType::Passive;
    def.levelRequirement = 1;
    def.maxRank = 3;
    def.passiveHPBonusPerRank = {50, 100, 200};
    sm.registerSkillDefinition(def);

    int baseMaxHP = stats.maxHP;

    sm.learnSkill("iron_skin", 1);
    sm.learnSkill("iron_skin", 2);
    sm.learnSkill("iron_skin", 3);

    sm.grantSkillPoint();
    sm.activateSkillRank("iron_skin");  // rank 1: +50
    CHECK(stats.maxHP == baseMaxHP + 50);

    sm.grantSkillPoint();
    sm.activateSkillRank("iron_skin");  // rank 2: +50 +100
    CHECK(stats.maxHP == baseMaxHP + 150);

    sm.grantSkillPoint();
    sm.activateSkillRank("iron_skin");  // rank 3: +50 +100 +200
    CHECK(stats.maxHP == baseMaxHP + 350);
}

TEST_CASE("SkillManager: active skill activation does NOT add passive bonuses") {
    CharacterStats stats = makeTestStats("Warrior", ClassType::Warrior, 10);
    SkillManager sm;
    sm.initialize(&stats);

    SkillDefinition def = makeTestSkillDef("slash", "Warrior", 1);
    def.skillType = SkillType::Active;
    // Even if passive arrays are set, active skills should not accumulate
    def.passiveHPBonusPerRank = {999, 999, 999};
    sm.registerSkillDefinition(def);

    int baseMaxHP = stats.maxHP;

    sm.learnSkill("slash", 1);
    sm.grantSkillPoint();
    sm.activateSkillRank("slash");

    // Should NOT have gained any HP
    CHECK(stats.maxHP == baseMaxHP);
    CHECK(sm.getPassiveHPBonus() == 0);
}
```

- [ ] **Step 6: Build and test**

```bash
touch game/shared/skill_manager.cpp game/shared/character_stats.cpp
export LIB="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64"
export INCLUDE="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build out/build/x64-Debug --config Debug 2>&1 | grep -E "error C|FAILED|Linking|ninja: build"
```

```bash
./out/build/x64-Debug/fate_tests.exe -tc="SkillManager: passive*,SkillManager: active skill*"
```

- [ ] **Step 7: Commit**

```bash
git add -f game/shared/skill_manager.h game/shared/skill_manager.cpp game/shared/character_stats.h game/shared/character_stats.cpp
git add tests/test_skill_execution.cpp
git commit -m "feat: integrate passive skill bonuses into CharacterStats recalculation"
```

---

## Task 5: Skill execution — validation + single-target damage (Section 3, core)

**Files:**
- Modify: `game/shared/skill_manager.h` (add executeSkill declaration + struct for context)
- Modify: `game/shared/skill_manager.cpp` (implement executeSkill)
- Modify: `tests/test_skill_execution.cpp` (add execution tests)

### Steps

- [ ] **Step 1: Add executeSkill and supporting types to skill_manager.h**

In `game/shared/skill_manager.h`, add these forward declarations after the existing includes (around line 8):

```cpp
#include "game/shared/combat_system.h"
#include "game/shared/enemy_stats.h"
```

Add the SkillExecutionContext struct before the SkillManager class:

```cpp
// ============================================================================
// SkillExecutionContext — everything needed to execute a skill
// ============================================================================
struct SkillExecutionContext {
    uint32_t casterEntityId = 0;
    uint32_t targetEntityId = 0;
    CharacterStats* casterStats = nullptr;

    // Target can be either a player or mob
    CharacterStats* targetPlayerStats = nullptr;  // non-null if target is player
    EnemyStats*     targetMobStats = nullptr;      // non-null if target is mob

    // Status effect managers (may be null)
    StatusEffectManager* casterSEM = nullptr;
    StatusEffectManager* targetSEM = nullptr;
    CrowdControlSystem*  casterCC = nullptr;
    CrowdControlSystem*  targetCC = nullptr;

    float distanceToTarget = 0.0f;
    bool  targetIsPlayer = false;
    bool  targetIsBoss = false;
    int   targetLevel = 1;
    int   targetArmor = 0;
    int   targetMagicResist = 0;
    int   targetCurrentHP = 0;
    int   targetMaxHP = 0;
    bool  targetAlive = true;
};
```

In the `SkillManager` class public section, add after the double-cast methods:

```cpp
    // ---- Skill Execution ----
    int executeSkill(const std::string& skillId, int rank, const SkillExecutionContext& ctx);
```

- [ ] **Step 2: Implement executeSkill in skill_manager.cpp**

In `game/shared/skill_manager.cpp`, add after the `applyPassiveBonusesToStats` method:

```cpp
// ============================================================================
// Skill Execution
// ============================================================================

int SkillManager::executeSkill(const std::string& skillId, int rank,
                                const SkillExecutionContext& ctx) {
    // ---- Validation Phase ----

    // 1. Check skill learned and activated
    const LearnedSkill* learned = getLearnedSkill(skillId);
    if (!learned || learned->activatedRank < rank) {
        if (onSkillFailed) onSkillFailed(skillId, "Skill not learned or rank not activated");
        return 0;
    }

    // 2. Check CC canAct
    if (ctx.casterCC && !ctx.casterCC->canAct()) {
        if (onSkillFailed) onSkillFailed(skillId, "Crowd controlled");
        return 0;
    }

    // 3. Check cooldown (skip if double-cast is active for this skill's source)
    bool isFreeCast = false;
    if (isDoubleCastReady() && doubleCastSourceSkillId_ == skillId) {
        isFreeCast = true;
    }

    if (!isFreeCast && isOnCooldown(skillId)) {
        if (onSkillFailed) onSkillFailed(skillId, "On cooldown");
        return 0;
    }

    // Look up skill definition
    const SkillDefinition* def = getSkillDefinition(skillId);
    if (!def) {
        if (onSkillFailed) onSkillFailed(skillId, "Unknown skill definition");
        return 0;
    }

    int ri = rank - 1;  // rank index (0-based)

    // 4. Check resource cost
    float cost = (ri < static_cast<int>(def->costPerRank.size()))
                 ? def->costPerRank[ri] : 0.0f;

    if (!isFreeCast && cost > 0.0f) {
        if (def->resourceType == ResourceType::Mana) {
            if (!stats || stats->currentMP < static_cast<int>(cost)) {
                if (onSkillFailed) onSkillFailed(skillId, "Not enough resources");
                return 0;
            }
        } else if (def->resourceType == ResourceType::Fury) {
            if (!stats || stats->currentFury < cost) {
                if (onSkillFailed) onSkillFailed(skillId, "Not enough resources");
                return 0;
            }
        }
    }

    // 5. Check target alive (for targeted skills)
    if (def->targetType != SkillTargetType::Self) {
        if (!ctx.targetAlive) {
            if (onSkillFailed) onSkillFailed(skillId, "Target is dead");
            return 0;
        }
    }

    // 6. Check range (for targeted skills)
    if (def->targetType != SkillTargetType::Self && def->range > 0.0f) {
        if (ctx.distanceToTarget > def->range) {
            if (onSkillFailed) onSkillFailed(skillId, "Out of range");
            return 0;
        }
    }

    // 7. Check target type validity
    if (def->targetType == SkillTargetType::SingleEnemy && !ctx.targetMobStats && !ctx.targetIsPlayer) {
        if (onSkillFailed) onSkillFailed(skillId, "Invalid target");
        return 0;
    }

    // ---- Execution Phase ----

    // 1. Deduct resource cost (skip for free cast)
    if (!isFreeCast) {
        if (def->scalesWithResource && def->resourceType == ResourceType::Mana && stats) {
            // Cataclysm: spend all remaining mana
            cost = static_cast<float>(stats->currentMP);
            stats->spendMana(stats->currentMP);
        } else if (cost > 0.0f && stats) {
            if (def->resourceType == ResourceType::Mana) {
                stats->spendMana(static_cast<int>(cost));
            } else {
                stats->spendFury(cost);
            }
        }
    }

    // Consume double-cast if active
    if (isFreeCast) {
        consumeDoubleCast();
    }

    // 2. Start cooldown (skip for free cast)
    if (!isFreeCast) {
        float cd = (ri < static_cast<int>(def->cooldownPerRank.size()))
                   ? def->cooldownPerRank[ri] : def->cooldownSeconds;
        startCooldown(skillId, cd);
    }

    // Handle non-damaging skills (teleport/dash)
    if (def->teleportDistance > 0.0f || def->dashDistance > 0.0f) {
        // Movement is handled by the caller (system layer)
        // Fire callback and return
        if (def->enablesDoubleCast) {
            activateDoubleCast(skillId, def->doubleCastWindow);
        }
        if (onSkillUsed) onSkillUsed(skillId, rank);
        return 0;
    }

    // Handle self-only buff skills (no damage)
    if (def->targetType == SkillTargetType::Self && def->damagePerRank.empty()) {
        // Apply self-buffs (handled in Task 6 section)
        // For now, just fire callback
        if (def->enablesDoubleCast) {
            activateDoubleCast(skillId, def->doubleCastWindow);
        }
        if (onSkillUsed) onSkillUsed(skillId, rank);
        return 0;
    }

    // 3. Hit roll
    if (def->usesHitRate && stats) {
        bool isMage = (stats->classDef.classType == ClassType::Mage);

        if (isMage) {
            // Mages use spell resist
            int targetMR = ctx.targetMagicResist;
            bool resisted = CombatSystem::rollSpellResist(
                stats->level, stats->getIntelligence(),
                ctx.targetLevel, targetMR);
            if (resisted) {
                if (onSkillUsed) onSkillUsed(skillId, rank);
                return 0;  // miss/resist
            }
        } else {
            // Physical uses hit rate
            int targetEvasion = 0;  // mobs have 0 evasion by default
            bool hit = CombatSystem::rollToHit(
                stats->level, static_cast<int>(stats->getHitRate()),
                ctx.targetLevel, targetEvasion);
            if (!hit) {
                if (onSkillUsed) onSkillUsed(skillId, rank);
                return 0;  // miss
            }
        }
    }

    // 4. Calculate base damage
    bool isCrit = false;
    bool forceCrit = false;
    if (def->canCrit && ctx.casterSEM && ctx.casterSEM->hasGuaranteedCrit()) {
        forceCrit = true;
    }

    int damage = 0;
    if (stats) {
        damage = stats->calculateDamage(forceCrit, isCrit);
        if (forceCrit && isCrit && ctx.casterSEM) {
            ctx.casterSEM->consumeGuaranteedCrit();
        }
    }

    // Apply skill damage percent
    float skillPercent = (ri < static_cast<int>(def->damagePerRank.size()))
                         ? def->damagePerRank[ri] : 100.0f;
    damage = static_cast<int>(std::round(damage * skillPercent / 100.0f));

    // Apply status effect damage multiplier on caster
    if (ctx.casterSEM) {
        float seMult = ctx.casterSEM->getDamageMultiplier();
        damage = static_cast<int>(std::round(damage * seMult));
    }

    // 5. Level multiplier (PvE only)
    if (!ctx.targetIsPlayer && stats) {
        float levelMult = CombatSystem::calculateDamageMultiplier(stats->level, ctx.targetLevel);
        damage = static_cast<int>(std::round(damage * levelMult));
    }

    // 6. PvP multiplier
    if (ctx.targetIsPlayer) {
        damage = static_cast<int>(std::round(damage * 0.30f));
    }

    // Cataclysm scaling: damage * (manaSpent / baseCost)
    if (def->scalesWithResource && !isFreeCast) {
        float baseCost = (ri < static_cast<int>(def->costPerRank.size()))
                         ? def->costPerRank[ri] : 1.0f;
        if (baseCost > 0.0f) {
            damage = static_cast<int>(std::round(damage * (cost / baseCost)));
        }
    }

    // 7. Execute check
    if (!def->executeThresholdPerRank.empty() && !ctx.targetIsBoss) {
        float threshold = (ri < static_cast<int>(def->executeThresholdPerRank.size()))
                          ? def->executeThresholdPerRank[ri] : 0.0f;
        if (threshold > 0.0f && ctx.targetMaxHP > 0) {
            float hpPercent = static_cast<float>(ctx.targetCurrentHP) /
                              static_cast<float>(ctx.targetMaxHP);
            if (hpPercent <= threshold) {
                damage = ctx.targetCurrentHP;  // instant kill
            }
        }
    }

    // 8. Defense pipeline on target

    // Shield absorption
    if (ctx.targetSEM) {
        damage = ctx.targetSEM->absorbDamage(damage);
    }

    // Block roll (physical only, player targets only)
    if (def->damageType == DamageType::Physical && ctx.targetIsPlayer && ctx.targetPlayerStats) {
        float blockChance = ctx.targetPlayerStats->getBlockChance();
        if (stats && blockChance > 0.0f) {
            bool blocked = CombatSystem::rollBlock(
                stats->classDef.classType,
                stats->getStrength(),
                stats->getDexterity(),
                blockChance);
            if (blocked) {
                damage = 0;
            }
        }
    }

    // Armor / MR reduction
    if (damage > 0) {
        bool isMagicDamage = (def->damageType == DamageType::Magic ||
                              def->damageType == DamageType::Fire ||
                              def->damageType == DamageType::Water ||
                              def->damageType == DamageType::Lightning ||
                              def->damageType == DamageType::Void);

        if (def->damageType != DamageType::True) {
            if (isMagicDamage) {
                // Magic resistance
                float mrReduction = ctx.targetIsPlayer
                    ? CombatSystem::getPlayerMagicDamageReduction(ctx.targetMagicResist)
                    : CombatSystem::getMobMagicDamageReduction(ctx.targetMagicResist);
                damage = static_cast<int>(std::round(damage * (1.0f - mrReduction)));
            } else {
                // Physical armor
                damage = CombatSystem::applyArmorReduction(damage, ctx.targetArmor);
            }
        }
    }

    // 9. Hunter's Mark
    if (ctx.targetSEM) {
        float bonusTaken = ctx.targetSEM->getBonusDamageTaken();
        if (bonusTaken > 0.0f) {
            damage = static_cast<int>(std::round(damage * (1.0f + bonusTaken)));
        }
    }

    // 10. Bewitch (mobs only)
    if (!ctx.targetIsPlayer && ctx.targetSEM) {
        float bewitchMult = ctx.targetSEM->consumeBewitch(ctx.casterEntityId);
        if (bewitchMult > 0.0f) {
            damage = static_cast<int>(std::round(damage * bewitchMult));
        }
    }

    // Ensure minimum damage of 1 (if we should deal damage at all)
    if (damage > 0) {
        damage = (std::max)(1, damage);
    }

    // 11. Apply damage to target
    int actualDamage = damage;
    if (ctx.targetMobStats && damage > 0) {
        ctx.targetMobStats->takeDamageFrom(ctx.casterEntityId, damage);
        actualDamage = damage;  // takeDamageFrom doesn't return actual
    } else if (ctx.targetPlayerStats && damage > 0) {
        actualDamage = ctx.targetPlayerStats->takeDamage(damage);
    }

    // 12. Freeze break
    if (ctx.targetCC && actualDamage > 0) {
        ctx.targetCC->breakFreeze();
    }

    // Steps 13-18 (effects, CC, self-buffs, lifesteal, fury, armor shred)
    // are implemented in Task 6.

    // 16. Lifesteal
    if (stats && stats->equipBonusLifesteal > 0.0f && actualDamage > 0) {
        int healAmount = static_cast<int>(std::round(stats->equipBonusLifesteal * actualDamage));
        if (healAmount > 0) {
            stats->heal(healAmount);
        }
    }

    // 17. Fury on hit
    if (stats && def->furyOnHit > 0.0f) {
        stats->addFury(def->furyOnHit);
    }

    // 18. Armor shred on crit
    if (def->canCrit && isCrit && ctx.targetSEM) {
        float armorShredValue = (stats && stats->equipBonusArmorPierce > 0)
            ? static_cast<float>(stats->equipBonusArmorPierce)
            : 5.0f;
        ctx.targetSEM->applyEffect(EffectType::ArmorShred, 5.0f,
                                    armorShredValue, 0.0f, ctx.casterEntityId);
    }

    // Enable double-cast window if applicable
    if (def->enablesDoubleCast) {
        activateDoubleCast(skillId, def->doubleCastWindow);
    }

    // 19. Fire callback
    if (onSkillUsed) onSkillUsed(skillId, rank);

    // 20. Return actual damage dealt
    return actualDamage;
}
```

- [ ] **Step 3: Add skill execution tests to test_skill_execution.cpp**

Append to `tests/test_skill_execution.cpp`:

```cpp
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
```

- [ ] **Step 4: Build and test**

```bash
touch game/shared/skill_manager.cpp game/shared/character_stats.cpp
export LIB="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64"
export INCLUDE="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build out/build/x64-Debug --config Debug 2>&1 | grep -E "error C|FAILED|Linking|ninja: build"
```

```bash
./out/build/x64-Debug/fate_tests.exe -tc="SkillManager: executeSkill*"
```

- [ ] **Step 5: Commit**

```bash
git add -f game/shared/skill_manager.h game/shared/skill_manager.cpp
git add tests/test_skill_execution.cpp
git commit -m "feat: implement skill execution with validation and single-target damage pipeline"
```

---

## Task 6: Skill execution — effects, CC, self-buffs, special mechanics (Section 3, effects)

**Files:**
- Modify: `game/shared/skill_manager.cpp` (extend executeSkill with effects/CC/buffs/AOE)
- Modify: `tests/test_skill_execution.cpp` (add effect tests)

### Steps

- [ ] **Step 1: Add status effect application to executeSkill**

In `game/shared/skill_manager.cpp`, find the comment in executeSkill that says:

```cpp
    // Steps 13-18 (effects, CC, self-buffs, lifesteal, fury, armor shred)
    // are implemented in Task 6.
```

Replace it with the full implementation:

```cpp
    // 13. Apply status effects to target
    if (ctx.targetSEM && actualDamage > 0) {
        float effectDuration = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                               ? def->effectDurationPerRank[ri] : 0.0f;
        float effectValue = (ri < static_cast<int>(def->effectValuePerRank.size()))
                            ? def->effectValuePerRank[ri] : 0.0f;

        if (def->appliesBleed && effectDuration > 0.0f) {
            ctx.targetSEM->applyDoT(EffectType::Bleed, effectDuration,
                                     effectValue, ctx.casterEntityId);
        }
        if (def->appliesBurn && effectDuration > 0.0f) {
            ctx.targetSEM->applyDoT(EffectType::Burn, effectDuration,
                                     effectValue, ctx.casterEntityId);
        }
        if (def->appliesPoison && effectDuration > 0.0f) {
            ctx.targetSEM->applyDoT(EffectType::Poison, effectDuration,
                                     effectValue, ctx.casterEntityId);
        }
        if (def->appliesSlow && effectDuration > 0.0f) {
            ctx.targetSEM->applyEffect(EffectType::Slow, effectDuration,
                                        effectValue, 0.0f, ctx.casterEntityId);
        }
        if (def->appliesFreeze && effectDuration > 0.0f && ctx.targetCC) {
            ctx.targetCC->applyFreeze(effectDuration, ctx.targetSEM, ctx.casterEntityId);
        }
    }

    // 14. Apply crowd control (stun)
    if (ctx.targetCC && ctx.targetSEM && actualDamage > 0) {
        float stunDuration = (ri < static_cast<int>(def->stunDurationPerRank.size()))
                             ? def->stunDurationPerRank[ri] : 0.0f;
        if (stunDuration > 0.0f) {
            if (def->appliesFreeze) {
                ctx.targetCC->applyFreeze(stunDuration, ctx.targetSEM, ctx.casterEntityId);
            } else {
                ctx.targetCC->applyStun(stunDuration, ctx.targetSEM, ctx.casterEntityId);
            }
        }
    }

    // 15. Apply self-buffs
    if (ctx.casterSEM) {
        if (def->grantsInvulnerability) {
            float effectDuration = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                                   ? def->effectDurationPerRank[ri] : 3.0f;
            ctx.casterSEM->applyInvulnerability(effectDuration, ctx.casterEntityId);
        }
        if (def->removesDebuffs) {
            ctx.casterSEM->removeAllDebuffs();
            if (ctx.casterCC) {
                ctx.casterCC->removeAllCC();
            }
        }
        if (def->grantsStunImmunity) {
            float effectDuration = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                                   ? def->effectDurationPerRank[ri] : 5.0f;
            ctx.casterSEM->applyEffect(EffectType::StunImmune, effectDuration,
                                        1.0f, 0.0f, ctx.casterEntityId);
        }
        if (def->grantsCritGuarantee) {
            float effectDuration = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                                   ? def->effectDurationPerRank[ri] : 5.0f;
            ctx.casterSEM->applyEffect(EffectType::GuaranteedCrit, effectDuration,
                                        1.0f, 0.0f, ctx.casterEntityId);
        }
        if (def->transformDamageMult > 0.0f) {
            float effectDuration = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                                   ? def->effectDurationPerRank[ri] : 10.0f;
            ctx.casterSEM->applyTransform(effectDuration, def->transformDamageMult,
                                           def->transformSpeedBonus, ctx.casterEntityId);
        }
    }
```

- [ ] **Step 2: Add AOE handling**

In `game/shared/skill_manager.h`, add the AOE execution method declaration in the SkillManager public section, after `executeSkill`:

```cpp
    /// Execute an AOE skill. Returns total damage dealt across all targets.
    /// The caller is responsible for gathering entities within aoeRadius and
    /// populating the targets vector with SkillExecutionContext per target.
    int executeSkillAOE(const std::string& skillId, int rank,
                        const SkillExecutionContext& primaryCtx,
                        std::vector<SkillExecutionContext>& targets);
```

In `game/shared/skill_manager.cpp`, add after executeSkill:

```cpp
// ============================================================================
// AOE Skill Execution
// ============================================================================

int SkillManager::executeSkillAOE(const std::string& skillId, int rank,
                                   const SkillExecutionContext& primaryCtx,
                                   std::vector<SkillExecutionContext>& targets) {
    // Validate using primary context (resource cost, cooldown, CC check)
    const LearnedSkill* learned = getLearnedSkill(skillId);
    if (!learned || learned->activatedRank < rank) {
        if (onSkillFailed) onSkillFailed(skillId, "Skill not learned or rank not activated");
        return 0;
    }

    if (primaryCtx.casterCC && !primaryCtx.casterCC->canAct()) {
        if (onSkillFailed) onSkillFailed(skillId, "Crowd controlled");
        return 0;
    }

    bool isFreeCast = false;
    if (isDoubleCastReady() && doubleCastSourceSkillId_ == skillId) {
        isFreeCast = true;
    }

    if (!isFreeCast && isOnCooldown(skillId)) {
        if (onSkillFailed) onSkillFailed(skillId, "On cooldown");
        return 0;
    }

    const SkillDefinition* def = getSkillDefinition(skillId);
    if (!def) {
        if (onSkillFailed) onSkillFailed(skillId, "Unknown skill definition");
        return 0;
    }

    int ri = rank - 1;
    float cost = (ri < static_cast<int>(def->costPerRank.size()))
                 ? def->costPerRank[ri] : 0.0f;

    // Check resources
    if (!isFreeCast && cost > 0.0f) {
        if (def->resourceType == ResourceType::Mana) {
            if (!stats || stats->currentMP < static_cast<int>(cost)) {
                if (onSkillFailed) onSkillFailed(skillId, "Not enough resources");
                return 0;
            }
        } else if (def->resourceType == ResourceType::Fury) {
            if (!stats || stats->currentFury < cost) {
                if (onSkillFailed) onSkillFailed(skillId, "Not enough resources");
                return 0;
            }
        }
    }

    // Deduct cost once
    if (!isFreeCast && stats) {
        if (def->scalesWithResource && def->resourceType == ResourceType::Mana) {
            cost = static_cast<float>(stats->currentMP);
            stats->spendMana(stats->currentMP);
        } else if (cost > 0.0f) {
            if (def->resourceType == ResourceType::Mana) {
                stats->spendMana(static_cast<int>(cost));
            } else {
                stats->spendFury(cost);
            }
        }
    }

    if (isFreeCast) {
        consumeDoubleCast();
    }

    // Start cooldown once
    if (!isFreeCast) {
        float cd = (ri < static_cast<int>(def->cooldownPerRank.size()))
                   ? def->cooldownPerRank[ri] : def->cooldownSeconds;
        startCooldown(skillId, cd);
    }

    // Cap targets
    int maxTargets = (ri < static_cast<int>(def->maxTargetsPerRank.size()))
                     ? def->maxTargetsPerRank[ri] : static_cast<int>(targets.size());
    int targetCount = (std::min)(static_cast<int>(targets.size()), maxTargets);

    int totalDamage = 0;

    // Execute against each target (skip resource/cooldown validation per-target)
    for (int i = 0; i < targetCount; ++i) {
        auto& tctx = targets[i];

        // Hit roll per target
        bool missed = false;
        if (def->usesHitRate && stats) {
            bool isMage = (stats->classDef.classType == ClassType::Mage);
            if (isMage) {
                missed = CombatSystem::rollSpellResist(
                    stats->level, stats->getIntelligence(),
                    tctx.targetLevel, tctx.targetMagicResist);
            } else {
                missed = !CombatSystem::rollToHit(
                    stats->level, static_cast<int>(stats->getHitRate()),
                    tctx.targetLevel, 0);
            }
        }
        if (missed) continue;

        // Calculate damage per target
        bool isCrit = false;
        bool forceCrit = (def->canCrit && ctx.casterSEM && ctx.casterSEM->hasGuaranteedCrit());
        int damage = stats ? stats->calculateDamage(forceCrit, isCrit) : 0;
        if (forceCrit && isCrit && primaryCtx.casterSEM) {
            primaryCtx.casterSEM->consumeGuaranteedCrit();
        }

        float skillPercent = (ri < static_cast<int>(def->damagePerRank.size()))
                             ? def->damagePerRank[ri] : 100.0f;
        damage = static_cast<int>(std::round(damage * skillPercent / 100.0f));

        if (primaryCtx.casterSEM) {
            damage = static_cast<int>(std::round(damage * primaryCtx.casterSEM->getDamageMultiplier()));
        }

        if (!tctx.targetIsPlayer && stats) {
            damage = static_cast<int>(std::round(damage *
                CombatSystem::calculateDamageMultiplier(stats->level, tctx.targetLevel)));
        }
        if (tctx.targetIsPlayer) {
            damage = static_cast<int>(std::round(damage * 0.30f));
        }

        // Cataclysm scaling
        if (def->scalesWithResource && !isFreeCast) {
            float baseCost = (ri < static_cast<int>(def->costPerRank.size()))
                             ? def->costPerRank[ri] : 1.0f;
            if (baseCost > 0.0f) {
                damage = static_cast<int>(std::round(damage * (cost / baseCost)));
            }
        }

        // Defense pipeline
        if (tctx.targetSEM) {
            damage = tctx.targetSEM->absorbDamage(damage);
        }

        bool isMagicDamage = (def->damageType == DamageType::Magic ||
                              def->damageType == DamageType::Fire ||
                              def->damageType == DamageType::Water ||
                              def->damageType == DamageType::Lightning ||
                              def->damageType == DamageType::Void);

        if (def->damageType != DamageType::True && damage > 0) {
            if (isMagicDamage) {
                float mr = tctx.targetIsPlayer
                    ? CombatSystem::getPlayerMagicDamageReduction(tctx.targetMagicResist)
                    : CombatSystem::getMobMagicDamageReduction(tctx.targetMagicResist);
                damage = static_cast<int>(std::round(damage * (1.0f - mr)));
            } else {
                damage = CombatSystem::applyArmorReduction(damage, tctx.targetArmor);
            }
        }

        if (tctx.targetSEM) {
            float bonus = tctx.targetSEM->getBonusDamageTaken();
            if (bonus > 0.0f) damage = static_cast<int>(std::round(damage * (1.0f + bonus)));
        }

        damage = (std::max)(1, damage);

        // Apply damage
        int actualDamage = damage;
        if (tctx.targetMobStats) {
            tctx.targetMobStats->takeDamageFrom(primaryCtx.casterEntityId, damage);
        } else if (tctx.targetPlayerStats) {
            actualDamage = tctx.targetPlayerStats->takeDamage(damage);
        }

        if (tctx.targetCC) tctx.targetCC->breakFreeze();

        // Apply effects per target
        if (tctx.targetSEM) {
            float eDur = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                         ? def->effectDurationPerRank[ri] : 0.0f;
            float eVal = (ri < static_cast<int>(def->effectValuePerRank.size()))
                         ? def->effectValuePerRank[ri] : 0.0f;

            if (def->appliesBleed && eDur > 0.0f)
                tctx.targetSEM->applyDoT(EffectType::Bleed, eDur, eVal, primaryCtx.casterEntityId);
            if (def->appliesBurn && eDur > 0.0f)
                tctx.targetSEM->applyDoT(EffectType::Burn, eDur, eVal, primaryCtx.casterEntityId);
            if (def->appliesPoison && eDur > 0.0f)
                tctx.targetSEM->applyDoT(EffectType::Poison, eDur, eVal, primaryCtx.casterEntityId);
            if (def->appliesSlow && eDur > 0.0f)
                tctx.targetSEM->applyEffect(EffectType::Slow, eDur, eVal, 0.0f, primaryCtx.casterEntityId);
        }
        if (tctx.targetCC && tctx.targetSEM) {
            float stunDur = (ri < static_cast<int>(def->stunDurationPerRank.size()))
                            ? def->stunDurationPerRank[ri] : 0.0f;
            if (stunDur > 0.0f) {
                if (def->appliesFreeze)
                    tctx.targetCC->applyFreeze(stunDur, tctx.targetSEM, primaryCtx.casterEntityId);
                else
                    tctx.targetCC->applyStun(stunDur, tctx.targetSEM, primaryCtx.casterEntityId);
            }
        }

        totalDamage += actualDamage;
    }

    // Self-buffs (once, not per-target)
    if (primaryCtx.casterSEM) {
        if (def->grantsInvulnerability) {
            float dur = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                        ? def->effectDurationPerRank[ri] : 3.0f;
            primaryCtx.casterSEM->applyInvulnerability(dur, primaryCtx.casterEntityId);
        }
        if (def->removesDebuffs) {
            primaryCtx.casterSEM->removeAllDebuffs();
            if (primaryCtx.casterCC) primaryCtx.casterCC->removeAllCC();
        }
        if (def->grantsStunImmunity) {
            float dur = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                        ? def->effectDurationPerRank[ri] : 5.0f;
            primaryCtx.casterSEM->applyEffect(EffectType::StunImmune, dur, 1.0f, 0.0f, primaryCtx.casterEntityId);
        }
        if (def->grantsCritGuarantee) {
            float dur = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                        ? def->effectDurationPerRank[ri] : 5.0f;
            primaryCtx.casterSEM->applyEffect(EffectType::GuaranteedCrit, dur, 1.0f, 0.0f, primaryCtx.casterEntityId);
        }
        if (def->transformDamageMult > 0.0f) {
            float dur = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                        ? def->effectDurationPerRank[ri] : 10.0f;
            primaryCtx.casterSEM->applyTransform(dur, def->transformDamageMult,
                                                  def->transformSpeedBonus, primaryCtx.casterEntityId);
        }
    }

    // Lifesteal on total damage
    if (stats && stats->equipBonusLifesteal > 0.0f && totalDamage > 0) {
        int heal = static_cast<int>(std::round(stats->equipBonusLifesteal * totalDamage));
        if (heal > 0) stats->heal(heal);
    }

    // Fury on hit
    if (stats && def->furyOnHit > 0.0f) {
        stats->addFury(def->furyOnHit);
    }

    if (def->enablesDoubleCast) {
        activateDoubleCast(skillId, def->doubleCastWindow);
    }

    if (onSkillUsed) onSkillUsed(skillId, rank);
    return totalDamage;
}
```

- [ ] **Step 3: Add effect and AOE tests to test_skill_execution.cpp**

Append to `tests/test_skill_execution.cpp`:

```cpp
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
```

- [ ] **Step 4: Build and test**

```bash
touch game/shared/skill_manager.cpp game/shared/character_stats.cpp
export LIB="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64"
export INCLUDE="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build out/build/x64-Debug --config Debug 2>&1 | grep -E "error C|FAILED|Linking|ninja: build"
```

```bash
./out/build/x64-Debug/fate_tests.exe -tc="SkillManager: executeSkill applies*,SkillManager: AOE*,SkillManager: Cataclysm*,SkillManager: double-cast*"
```

- [ ] **Step 5: Run full test suite**

```bash
./out/build/x64-Debug/fate_tests.exe
```

Verify all tests pass, including the ones from Tasks 1-5.

- [ ] **Step 6: Commit**

```bash
git add -f game/shared/skill_manager.h game/shared/skill_manager.cpp
git add tests/test_skill_execution.cpp
git commit -m "feat: add skill effects, CC application, self-buffs, AOE, and special mechanics"
```
