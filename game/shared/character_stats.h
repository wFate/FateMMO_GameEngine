#pragma once
#include "game/shared/game_types.h"
#include "game/shared/item_instance.h"
#include "game/shared/status_effects.h"
#include "game/shared/crowd_control.h"
#include "game/shared/faction.h"

#include <string>
#include <functional>
#include <cstdint>
#include <unordered_map>

namespace fate {

// ============================================================================
// CastingState — tracks an in-progress skill cast
// ============================================================================
struct CastingState {
    std::string skillId;
    int skillRank = 0;
    float remainingTime = 0.0f;
    float totalTime = 0.0f;
    uint32_t targetEntityId = 0;
    bool active = false;
};

// ============================================================================
// ChannelState — tracks an in-progress channeled skill (Barrage, etc.)
// ============================================================================
struct ChannelState {
    std::string skillId;
    int skillRank = 0;
    float remainingTime = 0.0f;
    float totalTime = 0.0f;
    float tickInterval = 0.5f;
    float nextTickTime = 0.0f;
    uint64_t targetEntityId = 0;
    bool active = false;
};

// ============================================================================
// LifeState — two-tick death lifecycle
// ============================================================================
enum class LifeState : uint8_t {
    Alive  = 0,
    Dying  = 1,  // one-tick window for on-death procs (DoT kill credit, thorns, etc.)
    Dead   = 2
};

// ============================================================================
// CharacterStats — server-side player character stats (plain class)
// Converted from NetworkCharacterStats.cs (3284 lines)
// ============================================================================
class CharacterStats {
public:
    // ---- Identity ----
    std::string characterId;
    std::string characterName;
    std::string className;
    std::string currentScene;  // Server tracks which scene this player is in
    std::string recallScene = "Town";  // Innkeeper-set recall destination
    ClassDefinition classDef;
    int level = 1;

    // ---- Synced State ----
    int currentHP    = 0;
    int maxHP        = 0;
    int currentMP    = 0;
    int maxMP        = 0;
    float currentFury = 0.0f;
    int maxFury       = 3;
    int64_t currentXP     = 0;
    int64_t xpToNextLevel = 100;
    int honor      = 0;
    int pvpKills   = 0;
    int pvpDeaths  = 0;
    int totalMobKills = 0;
    Faction faction = Faction::None;
    PKStatus pkStatus = PKStatus::White;
    float pkDecayTimer = 0.0f;      // seconds remaining until PK status decays
    float combatTimer  = 0.0f;      // seconds remaining "in combat" (blocks equip changes)

    // PvP attribution: if a player dies to a mob/DoT within this window after
    // being hit by another player, it counts as BOTH PvE + PvP death.
    static constexpr float PVP_ATTRIBUTION_WINDOW = 10.0f; // seconds
    float lastPvPHitTime = -999.0f;          // game-time of last hit from another player
    uint32_t lastPvPAttackerEntityId = 0;    // entity that last hit us in PvP

    /// Record that another player just hit us (called from PvP damage paths).
    void recordPvPHit(float gameTime, uint32_t attackerEntityId) {
        lastPvPHitTime = gameTime;
        lastPvPAttackerEntityId = attackerEntityId;
    }

    /// Returns true if this player was recently hit by another player.
    [[nodiscard]] bool hasRecentPvPAttacker(float gameTime) const {
        return (gameTime - lastPvPHitTime) <= PVP_ATTRIBUTION_WINDOW
            && lastPvPAttackerEntityId != 0;
    }

    LifeState lifeState = LifeState::Alive;
    bool editorPaused = false;  // Server sets this when client editor is paused (untargetable by mobs)

    // ---- Casting State ----
    CastingState castingState;

    [[nodiscard]] bool isCasting() const { return castingState.active; }

    void beginCast(const std::string& skillId, float castTime, uint32_t targetId, int rank = 0) {
        castingState.skillId = skillId;
        castingState.skillRank = rank;
        castingState.remainingTime = castTime;
        castingState.totalTime = castTime;
        castingState.targetEntityId = targetId;
        castingState.active = true;
    }

    // Returns true when cast completes
    bool tickCast(float dt) {
        if (!castingState.active) return false;
        castingState.remainingTime -= dt;
        if (castingState.remainingTime <= 0.0f) {
            castingState.active = false;
            return true; // cast completed
        }
        return false;
    }

    void interruptCast() {
        castingState = CastingState{};
    }

    // ---- Channel State ----
    ChannelState channelState;

    [[nodiscard]] bool isChanneling() const { return channelState.active; }

    void beginChannel(const std::string& skillId, float duration, float tickInterval, uint64_t targetId, int rank) {
        channelState.skillId = skillId;
        channelState.skillRank = rank;
        channelState.remainingTime = duration;
        channelState.totalTime = duration;
        channelState.tickInterval = tickInterval;
        channelState.nextTickTime = tickInterval;
        channelState.targetEntityId = targetId;
        channelState.active = true;
    }

    void interruptChannel() {
        channelState.active = false;
        channelState.remainingTime = 0.0f;
    }
    bool isDead    = false;   // backward compat — synced from lifeState in advanceDeathTick/respawn
    float respawnTimeRemaining = 0.0f;
    bool isInPvPZone = false;

    // ---- Free Stat Points (manual allocation) ----
    // DISABLED: stat allocation removed — stats are fixed per class,
    // only increased through equipment and collections.
    // Fields kept at 0 for DB/network compat; re-enable if allocation returns.
    int16_t freeStatPoints = 0;
    int16_t allocatedSTR = 0;
    int16_t allocatedINT = 0;
    int16_t allocatedDEX = 0;
    int16_t allocatedCON = 0;
    int16_t allocatedWIS = 0;

    // ---- Equipment Bonuses (set by inventory system) ----
    int equipBonusSTR        = 0;
    int equipBonusVIT        = 0;
    int equipBonusINT        = 0;
    int equipBonusDEX        = 0;
    int equipBonusWIS        = 0;
    int equipBonusHP         = 0;
    int equipBonusMP         = 0;
    int equipBonusArmor      = 0;
    int equipBonusArmorPierce = 0;
    int equipBonusPhysAttack = 0;  // DEAD: no item stat roller mapping exists; do not use
    int equipBonusMagAttack  = 0;  // DEAD: no item stat roller mapping exists; do not use
    int equipBonusPhysDef    = 0;  // DEAD: no item stat roller mapping exists; do not use
    int equipBonusMagDef     = 0;

    float equipBonusHPRegen    = 0.0f;
    float equipBonusMPRegen    = 0.0f;
    float equipBonusCritRate   = 0.0f;
    float equipBonusCritDamage = 0.0f;
    float equipBonusLifesteal  = 0.0f;
    float equipBonusMoveSpeed  = 0.0f;
    float equipBonusAttackSpeed = 0.0f;
    float equipBonusAccuracy   = 0.0f;
    float equipBonusEvasion    = 0.0f;
    float equipBonusBlockChance = 0.0f;

    int weaponDamageMin    = 0;
    int weaponDamageMax    = 0;
    float weaponAttackSpeed = 1.0f;

    float equipResistFire      = 0.0f;
    float equipResistWater     = 0.0f;
    float equipResistPoison    = 0.0f;
    float equipResistLightning = 0.0f;
    float equipResistVoid      = 0.0f;
    float equipResistMagic     = 0.0f;

    static constexpr float MAX_ELEMENTAL_RESIST = 0.75f;

    [[nodiscard]] float getElementalResist(DamageType type) const {
        float resist = 0.0f;
        switch (type) {
            case DamageType::Fire:      resist = equipResistFire; break;
            case DamageType::Water:     resist = equipResistWater; break;
            case DamageType::Poison:    resist = equipResistPoison; break;
            case DamageType::Lightning: resist = equipResistLightning; break;
            case DamageType::Void:      resist = equipResistVoid; break;
            case DamageType::Magic:     resist = equipResistMagic; break;
            default: return 0.0f;
        }
        return (std::min)(resist, MAX_ELEMENTAL_RESIST);
    }

    float equipExecuteThreshold   = 0.0f;
    float equipExecuteDamageBonus = 0.0f;

    // ---- Passive Skill Bonuses (set by SkillManager) ----
    int   passiveHPBonus          = 0;
    float passiveCritBonus        = 0.0f;
    float passiveSpeedBonus       = 0.0f;
    float passiveDamageReduction  = 0.0f;
    int   passiveStatBonus        = 0;
    float passiveArmorBonus       = 0.0f;
    float passiveHitRateBonus     = 0.0f;
    float passiveSpellDamageBonus = 0.0f;

    // ---- Unique Passive State ----
    // Undying Will (Warrior)
    bool undyingWillActive = false;
    float undyingWillCooldown = 300.0f;
    float undyingWillCooldownEnd = 0.0f;

    // Bloodlust (Warrior)
    bool bloodlustActive = false;
    int bloodlustStacks = 0;
    float bloodlustCritPerStack = 0.0f;

    // Retaliation (Warrior)
    bool retaliationActive = false;
    bool retaliationReady = false;
    float retaliationDamageBonus = 0.0f;

    // Deathwish (Warrior)
    bool deathwishActive = false;
    float deathwishCritDamageBonus = 0.0f;
    float deathwishHealingBonus = 0.0f;

    // Steady Aim (Archer)
    bool steadyAimActive = false;
    float steadyAimTimer = 0.0f;
    bool steadyAimReady = false;
    float steadyAimBonus = 0.0f;

    // Exploit Weakness (Archer)
    bool exploitWeaknessActive = false;
    float exploitWeaknessValue = 0.0f;

    // Predator's Instinct (Archer)
    bool predatorsInstinctActive = false;
    float predatorsInstinctCooldown = 10.0f;
    std::unordered_map<uint64_t, float> predatorsInstinctTargets;

    // Arcane Mastery (Mage)
    bool arcaneMasteryActive = false;
    float arcaneMasteryChance = 0.0f;

    // ---- Collection Bonuses (set by CollectionSystem) ----
    int collectionBonusSTR = 0;
    int collectionBonusINT = 0;
    int collectionBonusDEX = 0;
    int collectionBonusCON = 0;
    int collectionBonusWIS = 0;
    int collectionBonusHP = 0;
    int collectionBonusMP = 0;
    int collectionBonusDamage = 0;
    int collectionBonusArmor = 0;
    float collectionBonusCritRate = 0.0f;
    float collectionBonusMoveSpeed = 0.0f;

    // ---- Callbacks ----
    std::function<void()>    onDied;
    std::function<void()>    onRespawned;
    std::function<void()>    onLevelUp;
    std::function<void(int)> onDamaged;

    // Soul Anchor callback: if set, called before XP loss on PvE death.
    // Returns true if XP loss should be prevented (item consumed externally).
    std::function<bool()> shouldPreventXPLoss;

    // ---- Core Methods ----
    void recalculateStats();
    void clearEquipmentBonuses();

    /// Apply a single item's stats to the equipment bonus fields.
    /// Call clearEquipmentBonuses() first, then applyItemBonuses() for each equipped item,
    /// then recalculateStats().
    void applyItemBonuses(const ItemInstance& item, int baseWeaponMin, int baseWeaponMax,
                          int baseArmor, float baseAttackSpeed);

    /// Returns total damage dealt. Sets outIsCrit if the hit was critical.
    /// @param bonusCritRate  Additional crit rate from status effects (CritRateUp buff).
    int calculateDamage(bool forceCrit, bool& outIsCrit, float bonusCritRate = 0.0f);

    /// Apply pre-reduced damage. Armor is NOT applied here (callers handle it).
    /// @param deathSource  Death type if this hit is lethal (determines XP loss).
    int takeDamage(int amount, DeathSource deathSource = DeathSource::PvE);

    void heal(int amount);
    void addXP(int64_t amount);
    void die(DeathSource source = DeathSource::PvE);
    void respawn();

    /// Returns the XP loss percentage for PvE deaths based on player level.
    /// Higher levels lose less XP percentage (matches C# prototype exactly).
    [[nodiscard]] static float getXPLossPercent(int playerLevel);

    /// Applies XP loss on PvE death. XP cannot go below 0 (no de-leveling).
    void applyPvEDeathXPLoss();
    void addFury(float amount);
    void spendFury(float amount);
    void spendMana(int amount);

    // PK status transitions
    void flagAsAggressor();  // White → Purple on attacking innocent
    void flagAsMurderer();   // → Red on killing non-flagged player
    void tickTimers(float dt); // decay PK timer, combat timer
    void enterCombat();       // refresh 5s combat timer
    [[nodiscard]] bool isInCombat() const { return combatTimer > 0.0f; }

    // ---- Getters (computed stats) ----
    [[nodiscard]] int   getStrength() const     { return _strength; }
    [[nodiscard]] int   getVitality() const     { return _vitality; }
    [[nodiscard]] int   getIntelligence() const { return _intelligence; }
    [[nodiscard]] int   getDexterity() const    { return _dexterity; }
    [[nodiscard]] int   getWisdom() const       { return _wisdom; }

    [[nodiscard]] int   getBonusStrength() const     { return _bonusStrength; }
    [[nodiscard]] int   getBonusVitality() const     { return _bonusVitality; }
    [[nodiscard]] int   getBonusIntelligence() const { return _bonusIntelligence; }
    [[nodiscard]] int   getBonusDexterity() const    { return _bonusDexterity; }
    [[nodiscard]] int   getBonusWisdom() const       { return _bonusWisdom; }

    [[nodiscard]] int   getArmor() const        { return _armor; }
    [[nodiscard]] int   getMagicResist() const   { return _magicResist; }
    [[nodiscard]] float getHitRate() const       { return _hitRate; }
    [[nodiscard]] float getEvasion() const       { return _evasion; }
    [[nodiscard]] float getCritRate() const {
        float rate = _critRate;
        // Bloodlust: dynamic crit bonus from auto-attack stacks
        if (bloodlustActive && bloodlustStacks > 0) {
            rate += bloodlustStacks * bloodlustCritPerStack / 100.0f;
        }
        return rate;
    }
    [[nodiscard]] float getSpeed() const         { return _speed; }
    [[nodiscard]] float getDamageMultiplier() const { return _damageMultiplier; }
    [[nodiscard]] float getBlockChance() const   { return equipBonusBlockChance; }

    /// Returns the primary stat value for this class (STR/INT/DEX).
    [[nodiscard]] int getPrimaryStat() const;

    [[nodiscard]] bool isAlive() const { return lifeState == LifeState::Alive; }
    [[nodiscard]] bool isDying() const { return lifeState == LifeState::Dying; }
    void advanceDeathTick();

    void recalculateXPRequirement();

    // Apply server-authoritative derived stats directly (client-side only).
    void applyServerSnapshot(int armor, int magicResist, float critRate,
                             float hitRate, float evasion, float speed,
                             float damageMult);

private:
    // ---- Computed Stats ----
    int _strength     = 0;
    int _vitality     = 0;
    int _intelligence = 0;
    int _dexterity    = 0;
    int _wisdom       = 0;

    int _bonusStrength     = 0;
    int _bonusVitality     = 0;
    int _bonusIntelligence = 0;
    int _bonusDexterity    = 0;
    int _bonusWisdom       = 0;

    int _armor       = 0;
    int _magicResist = 0;

    float _hitRate          = 0.0f;
    float _evasion          = 0.0f;
    float _critRate         = 0.05f;
    float _speed            = 1.0f;
    float _damageMultiplier = 1.0f;
};

} // namespace fate
