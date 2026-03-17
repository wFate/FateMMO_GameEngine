#include "game/shared/character_stats.h"

#include <cmath>
#include <algorithm>
#include <random>

namespace fate {

// ============================================================================
// Thread-local RNG
// ============================================================================
static thread_local std::mt19937 s_rng{std::random_device{}()};

static int randomRange(int minVal, int maxVal) {
    if (minVal >= maxVal) return minVal;
    std::uniform_int_distribution<int> dist(minVal, maxVal);
    return dist(s_rng);
}

// ============================================================================
// recalculateStats
// ============================================================================
void CharacterStats::recalculateStats() {
    // --- Primary stats ---
    _strength     = static_cast<int>(std::round(classDef.baseStrength     + classDef.strPerLevel * (level - 1))) + equipBonusSTR;
    _vitality     = static_cast<int>(std::round(classDef.baseVitality     + classDef.vitPerLevel * (level - 1))) + equipBonusVIT;
    _intelligence = static_cast<int>(std::round(classDef.baseIntelligence + classDef.intPerLevel * (level - 1))) + equipBonusINT;
    _dexterity    = static_cast<int>(std::round(classDef.baseDexterity    + classDef.dexPerLevel * (level - 1))) + equipBonusDEX;
    _wisdom       = static_cast<int>(std::round(classDef.baseWisdom       + classDef.wisPerLevel * (level - 1))) + equipBonusWIS;

    // --- Bonus stats (NOT including base) ---
    _bonusStrength     = static_cast<int>(std::round(classDef.strPerLevel * (level - 1))) + equipBonusSTR;
    _bonusVitality     = static_cast<int>(std::round(classDef.vitPerLevel * (level - 1))) + equipBonusVIT;
    _bonusIntelligence = static_cast<int>(std::round(classDef.intPerLevel * (level - 1))) + equipBonusINT;
    _bonusDexterity    = static_cast<int>(std::round(classDef.dexPerLevel * (level - 1))) + equipBonusDEX;
    _bonusWisdom       = static_cast<int>(std::round(classDef.wisPerLevel * (level - 1))) + equipBonusWIS;

    // --- HP ---
    int baseHP = static_cast<int>(std::round(classDef.baseMaxHP + classDef.hpPerLevel * (level - 1)));
    float vitalityMultiplier = 1.0f + (_bonusVitality * 0.01f);
    maxHP = static_cast<int>(std::round(baseHP * vitalityMultiplier)) + equipBonusHP;

    // --- MP ---
    maxMP = static_cast<int>(std::round(classDef.baseMaxMP + classDef.mpPerLevel * (level - 1))) + equipBonusMP;

    // --- Armor ---
    _armor = static_cast<int>(std::round(_bonusVitality * 0.25f)) + equipBonusArmor;

    // --- Magic Resist ---
    _magicResist = equipBonusMagDef;

    // --- Hit Rate (class-dependent) ---
    switch (classDef.classType) {
        case ClassType::Warrior:
            _hitRate = classDef.baseHitRate + _strength * 0.05f + equipBonusAccuracy;
            break;
        case ClassType::Archer:
            _hitRate = classDef.baseHitRate + _dexterity * 0.1f + equipBonusAccuracy;
            break;
        case ClassType::Mage:
            _hitRate = 0.0f;
            break;
    }

    // --- Evasion (Archer only) ---
    if (classDef.classType == ClassType::Archer) {
        _evasion = _bonusDexterity * 0.5f + equipBonusEvasion;
    } else {
        _evasion = equipBonusEvasion;
    }

    // --- Crit Rate ---
    _critRate = 0.05f + equipBonusCritRate;
    if (classDef.classType == ClassType::Archer) {
        _critRate += _bonusDexterity * 0.005f;
    }

    // --- Speed ---
    _speed = 1.0f + equipBonusMoveSpeed;

    // --- Damage Multiplier (class-dependent) ---
    switch (classDef.classType) {
        case ClassType::Warrior:
            _damageMultiplier = 1.0f + _bonusStrength * 0.02f;
            break;
        case ClassType::Mage:
            _damageMultiplier = 1.0f + _bonusIntelligence * 0.02f;
            break;
        case ClassType::Archer:
            _damageMultiplier = 1.0f + _bonusDexterity * 0.02f;
            break;
    }

    // --- Fury cap ---
    maxFury = classDef.getMaxFuryForLevel(level);
}

// ============================================================================
// clearEquipmentBonuses
// ============================================================================
void CharacterStats::clearEquipmentBonuses() {
    equipBonusSTR         = 0;
    equipBonusVIT         = 0;
    equipBonusINT         = 0;
    equipBonusDEX         = 0;
    equipBonusWIS         = 0;
    equipBonusHP          = 0;
    equipBonusMP          = 0;
    equipBonusArmor       = 0;
    equipBonusArmorPierce = 0;
    equipBonusPhysAttack  = 0;
    equipBonusMagAttack   = 0;
    equipBonusPhysDef     = 0;
    equipBonusMagDef      = 0;

    equipBonusHPRegen     = 0.0f;
    equipBonusMPRegen     = 0.0f;
    equipBonusCritRate    = 0.0f;
    equipBonusCritDamage  = 0.0f;
    equipBonusLifesteal   = 0.0f;
    equipBonusMoveSpeed   = 0.0f;
    equipBonusAttackSpeed = 0.0f;
    equipBonusAccuracy    = 0.0f;
    equipBonusEvasion     = 0.0f;
    equipBonusBlockChance = 0.0f;

    weaponDamageMin    = 0;
    weaponDamageMax    = 0;
    weaponAttackSpeed  = 1.0f;

    equipResistFire      = 0.0f;
    equipResistWater     = 0.0f;
    equipResistPoison    = 0.0f;
    equipResistLightning = 0.0f;
    equipResistVoid      = 0.0f;
    equipResistMagic     = 0.0f;

    equipExecuteThreshold   = 0.0f;
    equipExecuteDamageBonus = 0.0f;
}

// ============================================================================
// calculateDamage
// ============================================================================
int CharacterStats::calculateDamage(bool forceCrit, bool& outIsCrit) {
    // Level-based damage range
    int levelBaseMin = 2 + static_cast<int>(std::floor((level - 1) * 0.5));
    int levelBaseMax = 4 + static_cast<int>(std::floor((level - 1) * 1.0));

    // Stat bonus (class-dependent)
    int statBonus = 0;
    switch (classDef.classType) {
        case ClassType::Warrior:
            statBonus = static_cast<int>(std::floor(_strength * 0.3));
            break;
        case ClassType::Archer:
            statBonus = static_cast<int>(std::floor(_dexterity * 0.4));
            break;
        case ClassType::Mage:
            statBonus = static_cast<int>(std::floor(_intelligence * 0.3));
            break;
    }

    int totalMin = levelBaseMin + statBonus + weaponDamageMin;
    int totalMax = levelBaseMax + statBonus + weaponDamageMax;

    int damage = randomRange(totalMin, totalMax);

    // Apply damage multiplier
    damage = static_cast<int>(std::round(damage * _damageMultiplier));

    // Critical hit check
    outIsCrit = forceCrit;
    if (!outIsCrit) {
        std::uniform_real_distribution<float> critDist(0.0f, 1.0f);
        outIsCrit = critDist(s_rng) < _critRate;
    }

    if (outIsCrit) {
        float critMultiplier = 1.95f + equipBonusCritDamage;
        damage = static_cast<int>(std::round(damage * critMultiplier));
    }

    // Minimum damage of 1
    return (std::max)(1, damage);
}

// ============================================================================
// takeDamage
// ============================================================================
int CharacterStats::takeDamage(int amount) {
    if (isDead) return 0;

    // Armor reduction: percentage = min(75, armor * 0.5)
    float reductionPct = (std::min)(75.0f, _armor * 0.5f) / 100.0f;
    int actualDamage = (std::max)(1, static_cast<int>(std::round(amount * (1.0f - reductionPct))));

    currentHP = (std::max)(0, currentHP - actualDamage);

    if (onDamaged) {
        onDamaged(actualDamage);
    }

    if (currentHP <= 0) {
        die();
    }

    return actualDamage;
}

// ============================================================================
// heal
// ============================================================================
void CharacterStats::heal(int amount) {
    if (isDead) return;
    currentHP = (std::min)(maxHP, currentHP + amount);
}

// ============================================================================
// addXP
// ============================================================================
void CharacterStats::addXP(int64_t amount) {
    if (amount <= 0) return;

    currentXP += amount;

    while (currentXP >= xpToNextLevel) {
        currentXP -= xpToNextLevel;
        level++;

        recalculateStats();
        recalculateXPRequirement();

        // Restore HP/MP on level up
        currentHP = maxHP;
        currentMP = maxMP;

        if (onLevelUp) {
            onLevelUp();
        }
    }
}

// ============================================================================
// die
// ============================================================================
void CharacterStats::die(DeathSource source) {
    if (isDead) return;
    isDead = true;
    currentHP = 0;

    // Apply death penalties based on source
    switch (source) {
        case DeathSource::PvE:
        case DeathSource::Environment:
            applyPvEDeathXPLoss();
            break;
        case DeathSource::PvP:
            // PvP deaths: honor loss only (handled externally by HonorSystem)
            // No XP loss for PvP deaths
            break;
        case DeathSource::Gauntlet:
            // No penalties in Gauntlet
            break;
    }

    if (onDied) {
        onDied();
    }
}

// ============================================================================
// getXPLossPercent — level-scaled XP loss for PvE deaths
// Matches C# NetworkCharacterStats.GetXPLossPercent() exactly
// ============================================================================
float CharacterStats::getXPLossPercent(int playerLevel) {
    if (playerLevel < 10) return 0.10f;    // 10%
    if (playerLevel < 20) return 0.05f;    // 5%
    if (playerLevel < 30) return 0.01f;    // 1%
    if (playerLevel < 40) return 0.005f;   // 0.5%
    if (playerLevel < 50) return 0.0025f;  // 0.25%
    if (playerLevel < 60) return 0.001f;   // 0.1%
    return 0.0005f;                         // 0.05% for 60+
}

// ============================================================================
// applyPvEDeathXPLoss — deduct XP on PvE death, floor at 0
// Matches C# NetworkCharacterStats.ApplyPvEDeathXPLoss() exactly
// ============================================================================
void CharacterStats::applyPvEDeathXPLoss() {
    float lossPercent = getXPLossPercent(level);
    int64_t xpLost = static_cast<int64_t>(std::round(
        static_cast<float>(currentXP) * lossPercent));

    if (xpLost > 0) {
        currentXP = (std::max)(int64_t{0}, currentXP - xpLost);
    }
}

// ============================================================================
// respawn
// ============================================================================
void CharacterStats::respawn() {
    isDead = false;
    currentHP = maxHP;
    respawnTimeRemaining = 0.0f;

    if (onRespawned) {
        onRespawned();
    }
}

// ============================================================================
// Fury / Mana
// ============================================================================
void CharacterStats::addFury(float amount) {
    currentFury = (std::min)(static_cast<float>(maxFury), currentFury + amount);
}

void CharacterStats::spendFury(float amount) {
    currentFury = (std::max)(0.0f, currentFury - amount);
}

void CharacterStats::spendMana(int amount) {
    currentMP = (std::max)(0, currentMP - amount);
}

// ============================================================================
// getPrimaryStat
// ============================================================================
int CharacterStats::getPrimaryStat() const {
    switch (classDef.classType) {
        case ClassType::Warrior: return _strength;
        case ClassType::Mage:    return _intelligence;
        case ClassType::Archer:  return _dexterity;
    }
    return _strength; // fallback
}

// ============================================================================
// recalculateXPRequirement
// ============================================================================
void CharacterStats::recalculateXPRequirement() {
    xpToNextLevel = calculateXPToNextLevel(level);
}

} // namespace fate
