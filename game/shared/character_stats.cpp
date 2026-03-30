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
    // DISABLED: allocatedSTR/INT/DEX/CON/WIS removed — stats are fixed per class,
    // only increased through equipment and collections.
    _strength     = static_cast<int>(std::round(classDef.baseStrength     + classDef.strPerLevel * (level - 1))) + equipBonusSTR + collectionBonusSTR;
    _vitality     = static_cast<int>(std::round(classDef.baseVitality     + classDef.vitPerLevel * (level - 1))) + equipBonusVIT + collectionBonusCON;
    _intelligence = static_cast<int>(std::round(classDef.baseIntelligence + classDef.intPerLevel * (level - 1))) + equipBonusINT + collectionBonusINT;
    _dexterity    = static_cast<int>(std::round(classDef.baseDexterity    + classDef.dexPerLevel * (level - 1))) + equipBonusDEX + collectionBonusDEX;
    _wisdom       = static_cast<int>(std::round(classDef.baseWisdom       + classDef.wisPerLevel * (level - 1))) + equipBonusWIS + collectionBonusWIS;

    // --- Bonus stats (NOT including base) ---
    _bonusStrength     = static_cast<int>(std::round(classDef.strPerLevel * (level - 1))) + equipBonusSTR + collectionBonusSTR;
    _bonusVitality     = static_cast<int>(std::round(classDef.vitPerLevel * (level - 1))) + equipBonusVIT + collectionBonusCON;
    _bonusIntelligence = static_cast<int>(std::round(classDef.intPerLevel * (level - 1))) + equipBonusINT + collectionBonusINT;
    _bonusDexterity    = static_cast<int>(std::round(classDef.dexPerLevel * (level - 1))) + equipBonusDEX + collectionBonusDEX;
    _bonusWisdom       = static_cast<int>(std::round(classDef.wisPerLevel * (level - 1))) + equipBonusWIS + collectionBonusWIS;

    // --- Passive stat bonus (primary stat based on class) ---
    // Must be applied BEFORE derived stats so it propagates to HP, armor, damage, etc.
    if (passiveStatBonus != 0) {
        switch (classDef.classType) {
            case ClassType::Warrior:  _strength += passiveStatBonus; _bonusStrength += passiveStatBonus; break;
            case ClassType::Mage:     _intelligence += passiveStatBonus; _bonusIntelligence += passiveStatBonus; break;
            case ClassType::Archer:   _dexterity += passiveStatBonus; _bonusDexterity += passiveStatBonus; break;
        }
    }

    // --- HP ---
    int baseHP = static_cast<int>(std::round(classDef.baseMaxHP + classDef.hpPerLevel * (level - 1)));
    float vitalityMultiplier = 1.0f + (_bonusVitality * 0.01f);
    maxHP = static_cast<int>(std::round(baseHP * vitalityMultiplier)) + equipBonusHP + collectionBonusHP;
    maxHP += passiveHPBonus;
    maxHP = (std::max)(1, maxHP);

    // --- MP (WIS multiplier mirrors VIT→HP) ---
    int baseMP = static_cast<int>(std::round(classDef.baseMaxMP + classDef.mpPerLevel * (level - 1)));
    float wisdomMultiplier = 1.0f + (_bonusWisdom * 0.01f);
    maxMP = static_cast<int>(std::round(baseMP * wisdomMultiplier)) + equipBonusMP + collectionBonusMP;
    maxMP = (std::max)(1, maxMP);

    // --- Armor ---
    _armor = static_cast<int>(std::round(_bonusVitality * 0.25f)) + equipBonusArmor + collectionBonusArmor;
    _armor += static_cast<int>(std::round(passiveArmorBonus));

    // --- Magic Resist ---
    _magicResist = equipBonusMagDef;

    // --- Hit Rate (class-dependent) ---
    switch (classDef.classType) {
        case ClassType::Warrior:
            _hitRate = classDef.baseHitRate + _strength * 0.05f + equipBonusAccuracy + passiveHitRateBonus;
            break;
        case ClassType::Archer:
            _hitRate = classDef.baseHitRate + _dexterity * 0.1f + equipBonusAccuracy + passiveHitRateBonus;
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
    _critRate = 0.05f + equipBonusCritRate + collectionBonusCritRate;
    if (classDef.classType == ClassType::Archer) {
        _critRate += _bonusDexterity * 0.005f;
    }
    _critRate += passiveCritBonus;

    // --- Speed ---
    _speed = 1.0f + equipBonusMoveSpeed + collectionBonusMoveSpeed;
    _speed *= (1.0f + passiveSpeedBonus);

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
// applyItemBonuses — accumulate one equipped item's stats onto equipBonus fields
// ============================================================================
void CharacterStats::applyItemBonuses(const ItemInstance& item,
                                       int baseWeaponMin, int baseWeaponMax,
                                       int baseArmor, float baseAttackSpeed) {
    // Base weapon/armor stats from item definition
    if (baseWeaponMin > 0 || baseWeaponMax > 0) {
        weaponDamageMin += baseWeaponMin;
        weaponDamageMax += baseWeaponMax;
        if (baseAttackSpeed > 0.0f) weaponAttackSpeed = baseAttackSpeed;
    }
    equipBonusArmor += baseArmor;

    // Rolled stats from the item instance
    for (const auto& rs : item.rolledStats) {
        switch (rs.statType) {
            case StatType::Strength:       equipBonusSTR += rs.value; break;
            case StatType::Intelligence:   equipBonusINT += rs.value; break;
            case StatType::Dexterity:      equipBonusDEX += rs.value; break;
            case StatType::Vitality:       equipBonusVIT += rs.value; break;
            case StatType::Wisdom:         equipBonusWIS += rs.value; break;
            case StatType::MaxHealth:      equipBonusHP += rs.value; break;
            case StatType::MaxMana:        equipBonusMP += rs.value; break;
            case StatType::Armor:          equipBonusArmor += rs.value; break;
            case StatType::ArmorPierce:    equipBonusArmorPierce += rs.value; break;
            case StatType::PhysicalAttack: equipBonusPhysAttack += rs.value; break;
            case StatType::MagicAttack:    equipBonusMagAttack += rs.value; break;
            case StatType::PhysicalDefense: equipBonusPhysDef += rs.value; break;
            case StatType::MagicDefense:   equipBonusMagDef += rs.value; break;
            case StatType::HealthRegen:    equipBonusHPRegen += static_cast<float>(rs.value) * 0.01f; break;
            case StatType::ManaRegen:      equipBonusMPRegen += static_cast<float>(rs.value) * 0.01f; break;
            case StatType::CriticalChance: equipBonusCritRate += static_cast<float>(rs.value) * 0.01f; break;
            case StatType::CriticalDamage: equipBonusCritDamage += static_cast<float>(rs.value) * 0.01f; break;
            case StatType::Lifesteal:      equipBonusLifesteal += static_cast<float>(rs.value) * 0.01f; break;
            case StatType::MoveSpeed:      equipBonusMoveSpeed += static_cast<float>(rs.value) * 0.01f; break;
            case StatType::AttackSpeed:    equipBonusAttackSpeed += static_cast<float>(rs.value) * 0.01f; break;
            case StatType::Accuracy:       equipBonusAccuracy += static_cast<float>(rs.value) * 0.01f; break;
            case StatType::Evasion:        equipBonusEvasion += static_cast<float>(rs.value) * 0.01f; break;
            case StatType::BlockChance:    equipBonusBlockChance += static_cast<float>(rs.value) * 0.01f; break;
            case StatType::FireResist:     equipResistFire += static_cast<float>(rs.value) * 0.01f; break;
            case StatType::WaterResist:    equipResistWater += static_cast<float>(rs.value) * 0.01f; break;
            case StatType::PoisonResist:   equipResistPoison += static_cast<float>(rs.value) * 0.01f; break;
            case StatType::LightningResist: equipResistLightning += static_cast<float>(rs.value) * 0.01f; break;
            case StatType::VoidResist:     equipResistVoid += static_cast<float>(rs.value) * 0.01f; break;
            case StatType::MagicResist:    equipResistMagic += static_cast<float>(rs.value) * 0.01f; break;
            case StatType::ExecuteThreshold: equipExecuteThreshold += static_cast<float>(rs.value) * 0.01f; break;
            case StatType::ExecuteDamageBonus: equipExecuteDamageBonus += static_cast<float>(rs.value) * 0.01f; break;
            default: break;
        }
    }

    // Socket bonus (primary stat only)
    if (!item.socket.isEmpty && item.socket.isValid()) {
        switch (item.socket.statType) {
            case StatType::Strength:     equipBonusSTR += item.socket.value; break;
            case StatType::Dexterity:    equipBonusDEX += item.socket.value; break;
            case StatType::Intelligence: equipBonusINT += item.socket.value; break;
            default: break;
        }
    }

    // Stat enchant (accessories)
    if (item.statEnchantValue > 0) {
        switch (item.statEnchantType) {
            case StatType::Strength:     equipBonusSTR += item.statEnchantValue; break;
            case StatType::Intelligence: equipBonusINT += item.statEnchantValue; break;
            case StatType::Dexterity:    equipBonusDEX += item.statEnchantValue; break;
            case StatType::Vitality:     equipBonusVIT += item.statEnchantValue; break;
            case StatType::Wisdom:       equipBonusWIS += item.statEnchantValue; break;
            default: break;
        }
    }

    // Clamp stat bonuses to prevent overflow
    constexpr int CAP = CharacterConstants::MAX_STAT_BONUS;
    equipBonusSTR   = (std::min)(CAP, equipBonusSTR);
    equipBonusVIT   = (std::min)(CAP, equipBonusVIT);
    equipBonusINT   = (std::min)(CAP, equipBonusINT);
    equipBonusDEX   = (std::min)(CAP, equipBonusDEX);
    equipBonusWIS   = (std::min)(CAP, equipBonusWIS);
    equipBonusHP    = (std::min)(CAP, equipBonusHP);
    equipBonusMP    = (std::min)(CAP, equipBonusMP);
    equipBonusArmor = (std::min)(CAP, equipBonusArmor);
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
        outIsCrit = critDist(s_rng) < getCritRate();
    }

    if (outIsCrit) {
        float critMultiplier = (std::min)(5.0f, 1.95f + equipBonusCritDamage);
        damage = static_cast<int>(std::round(damage * critMultiplier));

        // Deathwish: bonus crit damage when below 20% HP
        if (deathwishActive && currentHP < maxHP / 5) {
            damage = static_cast<int>(std::round(damage * (1.0f + deathwishCritDamageBonus / 100.0f)));
        }
    }

    // Retaliation: bonus damage on next attack after blocking
    if (retaliationActive && retaliationReady) {
        damage = static_cast<int>(std::round(damage * (1.0f + retaliationDamageBonus / 100.0f)));
        retaliationReady = false;
    }

    // Minimum damage of 1
    return (std::max)(1, damage);
}

// ============================================================================
// takeDamage
// ============================================================================
int CharacterStats::takeDamage(int amount) {
    if (lifeState != LifeState::Alive) return 0;

    // Armor reduction: percentage = min(75, armor * 0.5), clamp armor >= 0
    float effectiveArmor = (std::max)(0.0f, static_cast<float>(_armor));
    float reductionPct = (std::min)(75.0f, effectiveArmor * 0.5f) / 100.0f;
    int actualDamage = (std::max)(1, static_cast<int>(std::round(amount * (1.0f - reductionPct))));

    // Passive damage reduction from skills (capped at 90%)
    if (passiveDamageReduction > 0.0f) {
        float pdr = (std::min)(0.90f, passiveDamageReduction);
        actualDamage = (std::max)(1, static_cast<int>(std::round(actualDamage * (1.0f - pdr))));
    }

    currentHP = (std::max)(0, currentHP - actualDamage);

    if (onDamaged) {
        onDamaged(actualDamage);
    }

    // Fury generation on damage received (Warriors only — classDef.furyPerDamageReceived > 0)
    if (classDef.furyPerDamageReceived > 0.0f && actualDamage > 0) {
        addFury(classDef.furyPerDamageReceived);
    }

    // Undying Will: survive lethal damage at 1 HP (internal cooldown)
    if (currentHP <= 0 && undyingWillActive && undyingWillCooldownEnd <= 0.0f) {
        currentHP = 1;
        undyingWillCooldownEnd = undyingWillCooldown; // server ticks this down
        return actualDamage;
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
    if (lifeState != LifeState::Alive) return;

    // Deathwish: bonus healing when below 20% HP
    if (deathwishActive && currentHP < maxHP / 5) {
        amount = static_cast<int>(std::round(amount * (1.0f + deathwishHealingBonus / 100.0f)));
    }

    currentHP = (std::min)(maxHP, currentHP + amount);
}

// ============================================================================
// addXP
// ============================================================================
void CharacterStats::addXP(int64_t amount) {
    if (amount <= 0) return;

    currentXP += amount;

    while (currentXP >= xpToNextLevel && xpToNextLevel > 0
           && level < CharacterConstants::MAX_PLAYER_LEVEL) {
        currentXP -= xpToNextLevel;
        level++;
        // DISABLED: stat allocation removed — stats are fixed per class
        // freeStatPoints += 5;

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
    if (lifeState != LifeState::Alive) return;
    lifeState = LifeState::Dead;
    isDead = true;
    currentHP = 0;
    combatTimer = 0.0f;         // exit combat on death
    respawnTimeRemaining = 5.0f; // 5 second respawn timer

    // Apply death penalties based on source
    switch (source) {
        case DeathSource::PvE:
        case DeathSource::Environment:
            if (!shouldPreventXPLoss || !shouldPreventXPLoss()) {
                applyPvEDeathXPLoss();
            }
            break;
        case DeathSource::PvP:
            // PvP deaths: honor loss only (handled externally by HonorSystem)
            // No XP loss for PvP deaths
            break;
        case DeathSource::Gauntlet:
            // No penalties in Gauntlet
            break;
        case DeathSource::Battlefield:
        case DeathSource::Arena:
        case DeathSource::Dungeon:
            // No penalties in instanced content
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
        static_cast<double>(currentXP) * static_cast<double>(lossPercent)));

    if (xpLost > 0) {
        currentXP = (std::max)(int64_t{0}, currentXP - xpLost);
    }
}

// ============================================================================
// respawn
// ============================================================================
void CharacterStats::respawn() {
    lifeState = LifeState::Alive;
    isDead = false;
    currentHP = maxHP;
    currentMP = maxMP;
    respawnTimeRemaining = 0.0f;

    if (onRespawned) {
        onRespawned();
    }
}

// ============================================================================
// advanceDeathTick — two-tick death lifecycle: Dying → Dead
// ============================================================================
void CharacterStats::advanceDeathTick() {
    if (lifeState == LifeState::Dying) {
        lifeState = LifeState::Dead;
        isDead = true;
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

// ============================================================================
// applyServerSnapshot — client trusts server-authoritative derived stats
// ============================================================================
void CharacterStats::applyServerSnapshot(int armor, int magicResist, float critRate,
                                          float hitRate, float evasion, float speed,
                                          float damageMult) {
    _armor           = armor;
    _magicResist     = magicResist;
    _critRate        = critRate;
    _hitRate         = hitRate;
    _evasion         = evasion;
    _speed           = speed;
    _damageMultiplier = damageMult;
}

// ============================================================================
// PK Status Transitions
// ============================================================================

void CharacterStats::flagAsAggressor() {
    if (pkStatus == PKStatus::White) {
        pkStatus = PKStatus::Purple;
        pkDecayTimer = PKCooldowns::PURPLE_DECAY_SECONDS;
    }
}

void CharacterStats::flagAsMurderer() {
    pkStatus = PKStatus::Red;
    pkDecayTimer = PKCooldowns::RED_TO_WHITE_SECONDS;
}

void CharacterStats::enterCombat() {
    combatTimer = 5.0f;
}

void CharacterStats::tickTimers(float dt) {
    // PK decay
    if (pkDecayTimer > 0.0f) {
        pkDecayTimer -= dt;
        if (pkDecayTimer <= 0.0f) {
            pkDecayTimer = 0.0f;
            pkStatus = PKStatus::White;
        }
    }

    // Combat timer
    if (combatTimer > 0.0f) {
        combatTimer -= dt;
        if (combatTimer < 0.0f) combatTimer = 0.0f;
    }

    // Undying Will internal cooldown
    if (undyingWillCooldownEnd > 0.0f) {
        undyingWillCooldownEnd -= dt;
        if (undyingWillCooldownEnd < 0.0f) undyingWillCooldownEnd = 0.0f;
    }

    // Steady Aim timer (incremented here; reset on movement in movement_handler)
    if (steadyAimActive) {
        steadyAimTimer += dt;
        if (steadyAimTimer >= 5.0f) {
            steadyAimReady = true;
        }
    }

    // Respawn countdown (ticks in both Dying and Dead states)
    if (lifeState != LifeState::Alive && respawnTimeRemaining > 0.0f) {
        respawnTimeRemaining -= dt;
        if (respawnTimeRemaining < 0.0f) respawnTimeRemaining = 0.0f;
    }
}

} // namespace fate
