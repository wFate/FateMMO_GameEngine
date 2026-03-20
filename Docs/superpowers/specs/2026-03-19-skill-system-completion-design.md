# Skill System Completion — Full Port from Unity Prototype

**Date:** 2026-03-19
**Status:** Approved

## Overview

Complete the C++ port of the skill system from the Unity C# prototype (`PlayerSkillManager.cs`, `ClientSkillDefinition.cs`, `ClientSkillDefinitionCache.cs`, `SkillDefinition.cs`). The core scaffolding (enums, skill learning, cooldowns, skill bar, skill points) already exists and matches. This spec covers six areas: wiring the existing combat pipeline gaps, completing SkillDefinition fields, building the skill execution pipeline, adding skill bar utility methods + learn validation, creating a client-side skill definition cache, and integrating passive skills.

## Existing Code (No Changes Needed)

- `CombatSystem` — hit rate, spell resist, armor reduction, crit rate, block chance, damage multiplier formulas. All match C# `CombatHitRateSystem` and `CombatHitRateConfig`.
- `StatusEffectManager` — full effect application, DoT ticking, shield absorption, stacking, computed modifiers (getDamageMultiplier, getDamageReduction, getSpeedModifier, etc.). Matches C# `StatusEffectManager`.
- `CrowdControlSystem` — stun/freeze/root/taunt with priority, invulnerability/immunity checks. Matches C# `CrowdControlSystem`.
- `CharacterStats::calculateDamage()` — level-based + stat-based + weapon damage + crit. Matches C# `NetworkCharacterStats.CalculateDamage()`.
- `EnemyStats` — threat table, scaling, loot. Matches C# `NetworkEnemyStats`.
- `SkillManager` — skill learning, rank activation, cooldown tracking, skill bar (20 slots), skill points, double-cast framework, callbacks.
- All enums: SkillType, DamageType, SkillTargetType, ResourceType, EffectType, CCType — all match C# values exactly.

## Section 1: Wire Existing Combat Pipeline Gaps

The auto-attack in `CombatActionSystem::tryAttackTarget()` doesn't use several systems that already exist. These must be connected so skills and auto-attacks share the same pipeline.

### Changes to `CombatActionSystem::tryAttackTarget()`:

1. **Check CC before attacking:** Call `CrowdControlSystem::canAct()` on caster. If false, skip attack.
2. **Apply status effect damage multiplier:** After `CharacterStats::calculateDamage()`, multiply by `StatusEffectManager::getDamageMultiplier()`.
3. **Apply PvP damage multiplier:** For player-vs-player attacks, multiply by `CombatConfig::pvpDamageMultiplier` (0.05 for autos, skills will use 0.30).
4. **Roll block:** For physical attacks against players, call `CombatSystem::rollBlock()`. If blocked, spawn "Block" text, deal 0 damage.
5. **Shield absorption:** Before applying damage to HP, call `StatusEffectManager::absorbDamage()`. Only remaining damage hits HP.
6. **Hunter's Mark bonus:** After armor/MR reduction, multiply by `(1 + StatusEffectManager::getBonusDamageTaken())`.
7. **Bewitch check:** On mob targets, call `StatusEffectManager::consumeBewitch(attackerEntityId)`. If returns >0, multiply damage.
8. **Freeze break:** After dealing damage, call `CrowdControlSystem::breakFreeze()` on target.
9. **Lifesteal:** After damage dealt, heal attacker for `equipLifesteal * actualDamage`. Use `CharacterStats::heal()`.
10. **Armor shred on crit:** If isCrit, apply ArmorShred via `StatusEffectManager::applyEffect(ArmorShred, 5.0, armorShredValue, 0, attackerEntityId)`.

### Changes to `CharacterStats::takeDamage()`:

- Before deducting HP, check `StatusEffectManager::absorbDamage()` for shield. Only apply leftover to HP.

### Changes to movement system:

- Check `CrowdControlSystem::canMove()` before processing movement input (similar to the isDead check).
- Apply `StatusEffectManager::getSpeedModifier()` as a multiplier on moveSpeed.

## Section 2: Complete SkillDefinition Fields

Add missing fields to `SkillDefinition` in `skill_manager.h` to match C# `SkillDefinition.cs`:

### Resource & Scaling:
```cpp
ResourceType resourceType = ResourceType::Mana;  // which resource to spend
bool canCrit = true;                              // some skills can't crit
bool usesHitRate = true;                          // false = guaranteed hit
float furyOnHit = 0.0f;                          // fury generated per hit
bool scalesWithResource = false;                  // Cataclysm: damage scales with mana spent
```

### Effect Application Flags:
```cpp
bool appliesBleed = false;
bool appliesBurn = false;
bool appliesPoison = false;
bool appliesSlow = false;
bool appliesFreeze = false;
std::vector<float> stunDurationPerRank;
std::vector<float> effectDurationPerRank;
std::vector<float> effectValuePerRank;
```

### Passive Bonuses (per-rank):
```cpp
std::vector<float> passiveDamageReductionPerRank;
std::vector<float> passiveCritBonusPerRank;
std::vector<float> passiveSpeedBonusPerRank;
std::vector<int>   passiveHPBonusPerRank;
std::vector<int>   passiveStatBonusPerRank;
```

### Special Mechanics:
```cpp
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
```

## Section 3: Skill Execution Pipeline

New method: `SkillManager::executeSkill(skillId, rank, targetEntity, casterEntity, world)` returning `int` (actual damage dealt, 0 if failed/missed).

### Validation Phase (in order):
1. Check skill learned and `activatedRank >= rank`
2. Check `CrowdControlSystem::canAct()` on caster → fail reason "Crowd controlled"
3. Check cooldown not active → fail reason "On cooldown"
4. Check resource cost: MP via `CharacterStats::currentMP >= cost` or Fury via `CharacterStats::currentFury >= cost` → fail reason "Not enough resources"
5. Check target alive → fail reason "Target is dead"
6. Check target in range → fail reason "Out of range"
7. Check target type matches skill's `targetType` → fail reason "Invalid target"

Any failure calls `onSkillFailed(skillId, reason)` and returns 0.

### Execution Phase:
1. Deduct resource cost (MP or Fury based on `resourceType`)
2. Start cooldown using `cooldownPerRank[rank-1]`
3. **Hit roll** — if `usesHitRate`: mages use `CombatSystem::rollSpellResist()`, physical use `CombatSystem::rollToHit()`. Miss/resist returns 0 + spawns text.
4. **Calculate damage** — `CharacterStats::calculateDamage(canCrit)` × `damagePerRank[rank-1] / 100.0` × `StatusEffectManager::getDamageMultiplier()`
5. **Level multiplier** — `CombatSystem::calculateDamageMultiplier(attackerLevel, targetLevel)` for PvE
6. **PvP multiplier** — if target is player: damage × 0.30 (global skill PvP multiplier)
7. **Execute check** — if `executeThresholdPerRank` set and target HP% below threshold and target is not boss: instant kill
8. **Defense pipeline on target** — shield absorption → block roll (physical only, player targets) → armor/MR reduction
9. **Hunter's Mark** — multiply by `(1 + target.StatusEffectManager::getBonusDamageTaken())`
10. **Bewitch** — on mob targets, check `consumeBewitch(attackerEntityId)`
11. **Apply damage** to target HP
12. **Freeze break** — `target.CrowdControlSystem::breakFreeze()`
13. **Apply status effects** — for each flag (appliesBleed, appliesBurn, appliesPoison, appliesSlow, appliesFreeze): call `target.StatusEffectManager::applyDoT()` or `applyEffect()` using `effectDurationPerRank[rank-1]` and `effectValuePerRank[rank-1]`
14. **Apply crowd control** — if `stunDurationPerRank` set: `target.CrowdControlSystem::applyStun()` (or `applyFreeze()` if `appliesFreeze`)
15. **Apply self-buffs** — grantsInvulnerability → `self.StatusEffectManager::applyInvulnerability()`, removesDebuffs → `self.StatusEffectManager::removeAllDebuffs()` + `self.CrowdControlSystem::removeAllCC()`, grantsStunImmunity → `self.StatusEffectManager::applyEffect(StunImmune)`, grantsCritGuarantee → `self.StatusEffectManager::applyEffect(GuaranteedCrit)`, transform → `self.StatusEffectManager::applyTransform()`
16. **Lifesteal** — heal caster for `equipLifesteal * actualDamage`
17. **Fury on hit** — add `furyOnHit` to caster
18. **Armor shred on crit** — if `canCrit` and was crit, apply ArmorShred to target
19. Fire `onSkillUsed(skillId, rank)` callback
20. Return actual damage dealt

### AOE Handling:
For skills with `aoeRadius > 0`: iterate entities within radius of target position (using spatial query), apply damage/effects to each up to `maxTargetsPerRank[rank-1]`. Return total damage dealt.

### Special Skill Mechanics:
- **Cataclysm** (`scalesWithResource`): damage × (manaSpent / baseCost), spends all remaining mana
- **Double Arrow** (`enablesDoubleCast`): after execution, call `activateDoubleCast()` with window duration. Second cast is free (no cost/cooldown).
- **Teleport/Dash** (`teleportDistance`/`dashDistance`): move caster position after execution. No damage component.

## Section 4: Skill Bar Utilities + Learn Validation

### New skill bar methods:
```cpp
void clearSkillSlot(int globalSlotIndex);           // Set slot to ""
void swapSkillSlots(int slotA, int slotB);          // Two-way swap
bool autoAssignToSkillBar(const std::string& skillId); // Find first empty slot, assign
```

`autoAssignToSkillBar` is called automatically inside `learnSkill()` when a skill is first learned (rank goes from 0 to 1).

### Learn validation additions to `learnSkill()`:
1. Look up SkillDefinition by skillId
2. Check `className` matches player's class (or className == "Any") → return false if mismatch
3. Check player `level >= skillDef.levelRequirement` → return false if too low
4. Enforce sequential unlock: if rank > 1, require `existing.unlockedRank >= rank - 1` → return false if skipping

## Section 5: ClientSkillDefinitionCache

New files: `game/shared/client_skill_cache.h`

### ClientSkillDef struct:
```cpp
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
```

### ClientSkillDefinitionCache static class:
```cpp
class ClientSkillDefinitionCache {
public:
    static void populate(const std::string& className, const std::vector<ClientSkillDef>& skills);
    static void clear();
    static const ClientSkillDef* getSkill(const std::string& skillId);
    static const std::vector<ClientSkillDef>& getAllSkills(); // sorted by levelRequired
    static bool hasSkill(const std::string& skillId);
private:
    static std::unordered_map<std::string, ClientSkillDef> cache_;
    static std::vector<ClientSkillDef> sorted_;
};
```

Populated from server on connect (via protocol message or JSON payload). Cleared on disconnect.

## Section 6: Passive Skill Integration

When `activateSkillRank()` is called for a passive skill (skillType == Passive):

1. Look up the SkillDefinition
2. Read the passive bonus for the newly activated rank
3. Accumulate on SkillManager's passive totals:
   - `passiveHPBonus_`, `passiveCritBonus_`, `passiveSpeedBonus_`, `passiveDamageReduction_`, `passiveStatBonus_`
4. Call `CharacterStats::recalculateStats()` to apply

These accumulated totals are read by `CharacterStats::recalculateStats()`:
- `maxHP += skillManager.passiveHPBonus_`
- `_critRate += skillManager.passiveCritBonus_`
- `moveSpeed *= (1 + skillManager.passiveSpeedBonus_)`
- `_damageReduction += skillManager.passiveDamageReduction_` (capped 90%)
- Primary stat += `skillManager.passiveStatBonus_`

On de-rank (if ever supported), subtract the difference.

## Testing

- Unit test: executeSkill validation rejects (no mana, on cooldown, out of range, CC'd, dead target)
- Unit test: executeSkill damage calculation matches auto-attack formula × skill percent
- Unit test: status effects applied by skills (bleed, stun, etc.)
- Unit test: passive skills modify CharacterStats correctly
- Unit test: skill bar clear/swap/auto-assign
- Unit test: learnSkill validation (class, level, sequential)
- Unit test: ClientSkillDefinitionCache populate/query/clear
- Unit test: block integration in auto-attack pipeline
- Unit test: lifesteal heals attacker after damage
- Unit test: shield absorbs damage before HP
- Unit test: CC blocks movement and actions
- Integration test: full skill cast → damage → effect → cooldown cycle

## Files Modified

| File | Action | Purpose |
|------|--------|---------|
| `game/shared/skill_manager.h` | Modify | Add SkillDefinition fields, executeSkill, skill bar utilities, passive totals |
| `game/shared/skill_manager.cpp` | Modify | Implement executeSkill, learn validation, skill bar methods, passive integration |
| `game/shared/client_skill_cache.h` | Create | ClientSkillDef, ClientSkillRankData, ClientSkillDefinitionCache |
| `game/systems/combat_action_system.h` | Modify | Wire block, lifesteal, shields, CC checks, status effect multipliers, PvP mult |
| `game/systems/movement_system.h` | Modify | Wire CC canMove check, speed modifier |
| `game/shared/character_stats.h` | Modify | Add passive bonus inputs to recalculateStats |
| `game/shared/character_stats.cpp` | Modify | Apply passive bonuses, shield check in takeDamage |
| `tests/test_skill_execution.cpp` | Create | All skill execution tests |
| `tests/test_combat_pipeline.cpp` | Create | Block, lifesteal, shield, CC integration tests |

## Security

- All skill execution is server-authoritative
- Client sends only skill ID and rank — server validates everything
- Resource costs checked before execution
- Range validated server-side
- Cooldowns tracked server-side
- Execute threshold blocked for boss mobs
