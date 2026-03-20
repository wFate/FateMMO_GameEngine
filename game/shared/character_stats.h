#pragma once
#include "game/shared/game_types.h"
#include "game/shared/status_effects.h"
#include "game/shared/crowd_control.h"

#include <string>
#include <functional>
#include <cstdint>

namespace fate {

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
    PKStatus pkStatus = PKStatus::White;
    bool isDead    = false;
    float respawnTimeRemaining = 0.0f;
    bool isInPvPZone = false;

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
    int equipBonusPhysAttack = 0;
    int equipBonusMagAttack  = 0;
    int equipBonusPhysDef    = 0;
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

    float equipExecuteThreshold   = 0.0f;
    float equipExecuteDamageBonus = 0.0f;

    // ---- Passive Skill Bonuses (set by SkillManager) ----
    int   passiveHPBonus          = 0;
    float passiveCritBonus        = 0.0f;
    float passiveSpeedBonus       = 0.0f;
    float passiveDamageReduction  = 0.0f;
    int   passiveStatBonus        = 0;

    // ---- Callbacks ----
    std::function<void()>    onDied;
    std::function<void()>    onRespawned;
    std::function<void()>    onLevelUp;
    std::function<void(int)> onDamaged;

    // ---- Core Methods ----
    void recalculateStats();
    void clearEquipmentBonuses();

    /// Returns total damage dealt. Sets outIsCrit if the hit was critical.
    int calculateDamage(bool forceCrit, bool& outIsCrit);

    /// Apply damage after armor reduction. Returns actual damage dealt.
    int takeDamage(int amount);

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
    [[nodiscard]] float getCritRate() const      { return _critRate; }
    [[nodiscard]] float getSpeed() const         { return _speed; }
    [[nodiscard]] float getDamageMultiplier() const { return _damageMultiplier; }
    [[nodiscard]] float getBlockChance() const   { return equipBonusBlockChance; }

    /// Returns the primary stat value for this class (STR/INT/DEX).
    [[nodiscard]] int getPrimaryStat() const;

    [[nodiscard]] bool isAlive() const { return !isDead; }

    void recalculateXPRequirement();

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
