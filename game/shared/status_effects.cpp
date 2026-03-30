#include "game/shared/status_effects.h"
#include "engine/core/logger.h"
#include <algorithm>
#include <cmath>

namespace fate {

// ============================================================================
// Helpers
// ============================================================================

bool StatusEffectManager::isDoT(EffectType type) {
    return type == EffectType::Bleed ||
           type == EffectType::Burn  ||
           type == EffectType::Poison;
}

bool StatusEffectManager::isHoT(EffectType type) {
    return type == EffectType::HealOverTime;
}

bool StatusEffectManager::isCC(EffectType type) {
    return type == EffectType::Stun || type == EffectType::Freeze;
}

bool StatusEffectManager::isDebuff(EffectType type) {
    switch (type) {
        case EffectType::Bleed:
        case EffectType::Burn:
        case EffectType::Poison:
        case EffectType::Slow:
        case EffectType::ArmorShred:
        case EffectType::HuntersMark:
        case EffectType::Bewitched:
        case EffectType::Stun:
        case EffectType::Freeze:
            return true;
        default:
            return false;
    }
}

bool StatusEffectManager::canStack(EffectType type) {
    return type == EffectType::ArmorShred;
}

bool StatusEffectManager::isAdditiveStack(EffectType type) {
    return type == EffectType::ExpGainUp;
}

int StatusEffectManager::getMaxStacks(EffectType type) {
    if (type == EffectType::ArmorShred) return 3;
    return 1;
}

void StatusEffectManager::rebuildLookup() {
    effectLookup_.clear();
    for (size_t i = 0; i < activeEffects_.size(); ++i) {
        effectLookup_[activeEffects_[i].type] = i;
    }
}

// ============================================================================
// Apply effects
// ============================================================================

void StatusEffectManager::applyEffect(EffectType type, float duration,
                                       float value, float value2,
                                       uint32_t source) {
    // Invulnerability blocks debuffs
    if (isDebuff(type) && isInvulnerable()) return;

    // StunImmune blocks Slow
    if (type == EffectType::Slow && isStunImmune()) return;

    // StunImmune also blocks hard CC
    if (isCC(type) && isStunImmune()) return;

    // Additive stacking: each source gets its own entry with independent duration
    if (isAdditiveStack(type)) {
        for (auto& existing : activeEffects_) {
            if (existing.type == type && existing.sourceEntityId == source) {
                // Same source — update value, refresh duration
                existing.value = value;
                existing.remainingTime = (std::max)(existing.remainingTime, duration);
                existing.duration = (std::max)(existing.duration, duration);
                return;
            }
        }
        // New source — add separate entry
        StatusEffect effect;
        effect.type           = type;
        effect.duration       = duration;
        effect.remainingTime  = duration;
        effect.value          = value;
        effect.value2         = value2;
        effect.stacks         = 1;
        effect.sourceEntityId = source;
        effect.tickInterval   = 1.0f;
        effect.nextTickTime   = 0.0f;
        activeEffects_.push_back(effect);
        effectLookup_[type] = activeEffects_.size() - 1;
        return;
    }

    auto it = effectLookup_.find(type);
    if (it != effectLookup_.end()) {
        // Existing effect
        auto& existing = activeEffects_[it->second];
        if (canStack(type)) {
            // Stack up to max
            if (existing.stacks < getMaxStacks(type)) {
                existing.stacks++;
            }
            // Refresh duration (take longer)
            existing.remainingTime = (std::max)(existing.remainingTime, duration);
            existing.duration = (std::max)(existing.duration, duration);
        } else {
            // Refresh duration (take longer), update value if higher
            existing.remainingTime = (std::max)(existing.remainingTime, duration);
            existing.duration = (std::max)(existing.duration, duration);
            if (value > existing.value) {
                existing.value = value;
            }
            if (value2 > existing.value2) {
                existing.value2 = value2;
            }
        }
        existing.sourceEntityId = source;
    } else {
        // New effect
        StatusEffect effect;
        effect.type           = type;
        effect.duration       = duration;
        effect.remainingTime  = duration;
        effect.value          = value;
        effect.value2         = value2;
        effect.stacks         = 1;
        effect.sourceEntityId = source;
        effect.tickInterval   = 1.0f;
        effect.nextTickTime   = 0.0f;

        activeEffects_.push_back(effect);
        effectLookup_[type] = activeEffects_.size() - 1;
    }
}

void StatusEffectManager::applyDoT(EffectType type, float duration,
                                    float damagePerTick, uint32_t source) {
    if (isInvulnerable()) return;

    auto it = effectLookup_.find(type);
    if (it != effectLookup_.end()) {
        auto& existing = activeEffects_[it->second];
        existing.remainingTime = (std::max)(existing.remainingTime, duration);
        existing.duration = (std::max)(existing.duration, duration);
        if (damagePerTick > existing.value) {
            existing.value = damagePerTick;
        }
        existing.sourceEntityId = source;
    } else {
        StatusEffect effect;
        effect.type           = type;
        effect.duration       = duration;
        effect.remainingTime  = duration;
        effect.value          = damagePerTick;
        effect.stacks         = 1;
        effect.sourceEntityId = source;
        effect.tickInterval   = 1.0f;
        effect.nextTickTime   = 1.0f; // first tick after 1s

        activeEffects_.push_back(effect);
        effectLookup_[type] = activeEffects_.size() - 1;
    }
}

void StatusEffectManager::applyHoT(EffectType type, float duration,
                                    float healPerTick, uint32_t source) {
    auto it = effectLookup_.find(type);
    if (it != effectLookup_.end()) {
        auto& existing = activeEffects_[it->second];
        existing.remainingTime = (std::max)(existing.remainingTime, duration);
        existing.duration = (std::max)(existing.duration, duration);
        if (healPerTick > existing.value) {
            existing.value = healPerTick;
        }
        existing.sourceEntityId = source;
    } else {
        StatusEffect effect;
        effect.type           = type;
        effect.duration       = duration;
        effect.remainingTime  = duration;
        effect.value          = healPerTick;
        effect.stacks         = 1;
        effect.sourceEntityId = source;
        effect.tickInterval   = 1.0f;
        effect.nextTickTime   = 1.0f; // first tick after 1s
        activeEffects_.push_back(effect);
        effectLookup_[type] = activeEffects_.size() - 1;
    }
}

void StatusEffectManager::applyShield(float amount, float duration,
                                       uint32_t source) {
    currentShieldAmount_ = (std::max)(currentShieldAmount_, amount);
    applyEffect(EffectType::Shield, duration, amount, 0.0f, source);
}

void StatusEffectManager::applyInvulnerability(float duration, uint32_t source) {
    applyEffect(EffectType::Invulnerable, duration, 1.0f, 0.0f, source);
}

void StatusEffectManager::applyTransform(float duration, float damageMult,
                                          float speedBonus, uint32_t source) {
    applyEffect(EffectType::Transform, duration, damageMult, speedBonus, source);
}

// ============================================================================
// Remove effects
// ============================================================================

void StatusEffectManager::removeEffect(EffectType type) {
    auto it = effectLookup_.find(type);
    if (it == effectLookup_.end()) return;

    if (type == EffectType::Shield) {
        currentShieldAmount_ = 0.0f;
    }

    // Additive types may have multiple entries — remove them all
    if (isAdditiveStack(type)) {
        std::erase_if(activeEffects_, [type](const StatusEffect& e) {
            return e.type == type;
        });
        rebuildLookup();
        return;
    }

    size_t idx = it->second;
    if (idx >= activeEffects_.size()) {
        // Stale lookup — rebuild and bail
        rebuildLookup();
        return;
    }
    activeEffects_.erase(activeEffects_.begin() + static_cast<ptrdiff_t>(idx));
    rebuildLookup();
}

void StatusEffectManager::removeEffectBySource(EffectType type, uint32_t source) {
    // Additive types may have multiple entries — scan by source
    if (isAdditiveStack(type)) {
        bool found = false;
        for (auto it = activeEffects_.begin(); it != activeEffects_.end(); ++it) {
            if (it->type == type && it->sourceEntityId == source) {
                activeEffects_.erase(it);
                found = true;
                break;
            }
        }
        if (found) rebuildLookup();
        return;
    }

    auto it = effectLookup_.find(type);
    if (it == effectLookup_.end()) return;

    size_t idx = it->second;
    if (idx >= activeEffects_.size()) {
        rebuildLookup();
        return;
    }

    if (activeEffects_[idx].sourceEntityId != source) return;  // Not ours — leave it

    if (type == EffectType::Shield) {
        currentShieldAmount_ = 0.0f;
    }
    activeEffects_.erase(activeEffects_.begin() + static_cast<ptrdiff_t>(idx));
    rebuildLookup();
}

void StatusEffectManager::removeAllDebuffs() {
    std::erase_if(activeEffects_, [](const StatusEffect& e) {
        return isDebuff(e.type);
    });
    rebuildLookup();
}

void StatusEffectManager::removeAllEffects() {
    if (ticking_) {
        // Defer — tick() will clear after iteration completes
        pendingRemoveAll_ = true;
        return;
    }
    activeEffects_.clear();
    effectLookup_.clear();
    currentShieldAmount_ = 0.0f;
}

// ============================================================================
// Queries
// ============================================================================

bool StatusEffectManager::hasEffect(EffectType type) const {
    return effectLookup_.contains(type);
}

float StatusEffectManager::getEffectDuration(EffectType type) const {
    auto it = effectLookup_.find(type);
    if (it == effectLookup_.end()) return 0.0f;
    return activeEffects_[it->second].remainingTime;
}

float StatusEffectManager::getEffectValue(EffectType type) const {
    auto it = effectLookup_.find(type);
    if (it == effectLookup_.end()) return 0.0f;
    return activeEffects_[it->second].value;
}

int StatusEffectManager::getEffectStacks(EffectType type) const {
    auto it = effectLookup_.find(type);
    if (it == effectLookup_.end()) return 0;
    return activeEffects_[it->second].stacks;
}

bool StatusEffectManager::isInvulnerable() const {
    return hasEffect(EffectType::Invulnerable);
}

bool StatusEffectManager::isStunImmune() const {
    return hasEffect(EffectType::StunImmune);
}

bool StatusEffectManager::hasGuaranteedCrit() const {
    return hasEffect(EffectType::GuaranteedCrit);
}

float StatusEffectManager::currentShield() const {
    return currentShieldAmount_;
}

// ============================================================================
// Computed modifiers
// ============================================================================

float StatusEffectManager::getDamageMultiplier() const {
    float mult = 1.0f;
    if (hasEffect(EffectType::AttackUp)) {
        mult += getEffectValue(EffectType::AttackUp);
    }
    if (hasEffect(EffectType::Transform)) {
        mult += getEffectValue(EffectType::Transform);
    }
    return mult;
}

float StatusEffectManager::getDamageReduction() const {
    float reduction = 0.0f;
    if (hasEffect(EffectType::ArmorUp)) {
        reduction += getEffectValue(EffectType::ArmorUp);
    }
    return (std::min)(reduction, 0.9f); // capped at 90%
}

float StatusEffectManager::getSpeedModifier() const {
    float modifier = 1.0f;
    if (hasEffect(EffectType::SpeedUp)) {
        modifier += getEffectValue(EffectType::SpeedUp);
    }
    if (hasEffect(EffectType::Transform)) {
        auto it = effectLookup_.find(EffectType::Transform);
        if (it != effectLookup_.end()) {
            modifier += activeEffects_[it->second].value2; // speedBonus in value2
        }
    }
    if (hasEffect(EffectType::Slow)) {
        modifier -= getEffectValue(EffectType::Slow);
    }
    return (std::max)(modifier, 0.1f); // minimum 10%
}

float StatusEffectManager::getBonusDamageTaken() const {
    return getEffectValue(EffectType::HuntersMark);
}

float StatusEffectManager::getArmorShred() const {
    if (!hasEffect(EffectType::ArmorShred)) return 0.0f;
    auto it = effectLookup_.find(EffectType::ArmorShred);
    const auto& effect = activeEffects_[it->second];
    return effect.value * static_cast<float>(effect.stacks);
}

float StatusEffectManager::getManaRegenBonus() const {
    return getEffectValue(EffectType::ManaRegenUp);
}

float StatusEffectManager::getExpGainBonus() const {
    float total = 0.0f;
    for (const auto& effect : activeEffects_) {
        if (effect.type == EffectType::ExpGainUp) {
            total += effect.value;
        }
    }
    return total;
}

float StatusEffectManager::getLifestealBonus() const {
    return getEffectValue(EffectType::LifestealUp);
}

float StatusEffectManager::getCritRateBonus() const {
    return getEffectValue(EffectType::CritRateUp);
}

float StatusEffectManager::getArmorBuff() const {
    float total = 0.0f;
    for (const auto& e : activeEffects_) {
        if (e.type == EffectType::ArmorBuff) total += e.value;
    }
    return total;
}

float StatusEffectManager::getAttackBuff() const {
    float total = 0.0f;
    for (const auto& e : activeEffects_) {
        if (e.type == EffectType::AttackBuff) total += e.value;
    }
    return total;
}

float StatusEffectManager::getDamageReductionBuff() const {
    float total = 0.0f;
    for (const auto& e : activeEffects_) {
        if (e.type == EffectType::DamageReductionBuff) total += e.value;
    }
    return total;
}

bool StatusEffectManager::isStunned() const {
    return hasEffect(EffectType::Stun);
}

bool StatusEffectManager::isFrozen() const {
    return hasEffect(EffectType::Freeze);
}

// ============================================================================
// Combat interactions
// ============================================================================

int StatusEffectManager::absorbDamage(int damage) {
    if (currentShieldAmount_ <= 0.0f) return damage;

    float remaining = currentShieldAmount_ - static_cast<float>(damage);
    if (remaining <= 0.0f) {
        // Shield broken
        int leftover = static_cast<int>(-remaining);
        currentShieldAmount_ = 0.0f;
        removeEffect(EffectType::Shield);
        return leftover;
    }

    currentShieldAmount_ = remaining;
    return 0;
}

void StatusEffectManager::consumeGuaranteedCrit() {
    removeEffect(EffectType::GuaranteedCrit);
}

float StatusEffectManager::consumeBewitch(uint32_t attackerEntityId) {
    auto it = effectLookup_.find(EffectType::Bewitched);
    if (it == effectLookup_.end()) return 0.0f;

    const auto& effect = activeEffects_[it->second];
    if (effect.sourceEntityId != attackerEntityId) return 0.0f;

    float multiplier = effect.value;
    removeEffect(EffectType::Bewitched);
    return multiplier;
}

// ============================================================================
// Bitmask for replication
// ============================================================================

uint32_t StatusEffectManager::getActiveEffectMask() const {
    uint32_t mask = 0;
    for (const auto& effect : activeEffects_) {
        mask |= (1u << static_cast<uint8_t>(effect.type));
    }
    return mask;
}

// ============================================================================
// Tick
// ============================================================================

void StatusEffectManager::tick(float deltaTime) {
    ticking_ = true;
    bool needsRebuild = false;

    for (auto it = activeEffects_.begin(); it != activeEffects_.end(); ) {
        // If a callback (e.g. onDied) requested full clear, stop iterating
        if (pendingRemoveAll_) break;

        it->remainingTime -= deltaTime;

        // Process DoT ticks
        if (isDoT(it->type)) {
            it->nextTickTime -= deltaTime;
            if (it->nextTickTime <= 0.0f) {
                it->nextTickTime += it->tickInterval;
                if (onDoTTick) {
                    int damage = static_cast<int>(it->value);
                    onDoTTick(it->type, damage);
                }
            }
        }

        // Process HoT ticks
        if (isHoT(it->type)) {
            it->nextTickTime -= deltaTime;
            if (it->nextTickTime <= 0.0f) {
                it->nextTickTime += it->tickInterval;
                if (onHoTTick) {
                    int heal = static_cast<int>(it->value);
                    onHoTTick(it->type, heal);
                }
            }
        }

        // If a callback (e.g. onDied) requested full clear, stop iterating
        if (pendingRemoveAll_) break;

        // Remove expired effects
        if (it->remainingTime <= 0.0f) {
            if (it->type == EffectType::Shield) {
                currentShieldAmount_ = 0.0f;
            }
            it = activeEffects_.erase(it);
            needsRebuild = true;
        } else {
            ++it;
        }
    }

    ticking_ = false;

    // Apply deferred removeAllEffects() if requested during iteration
    if (pendingRemoveAll_) {
        pendingRemoveAll_ = false;
        activeEffects_.clear();
        effectLookup_.clear();
        currentShieldAmount_ = 0.0f;
    } else if (needsRebuild) {
        rebuildLookup();
    }
}

} // namespace fate
