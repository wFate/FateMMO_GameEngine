#pragma once
#include "game/shared/game_types.h"
#include <vector>
#include <unordered_map>
#include <functional>

namespace fate {

// ============================================================================
// Status Effect Data
// ============================================================================
struct StatusEffect {
    EffectType type{};
    float duration      = 0.0f;
    float remainingTime = 0.0f;
    float value         = 0.0f;
    float value2        = 0.0f;
    int   stacks        = 1;
    uint32_t sourceEntityId = 0;
    float tickInterval  = 1.0f;
    float nextTickTime  = 0.0f;
};

// ============================================================================
// StatusEffectManager — standalone logic class (not a component)
// ============================================================================
class StatusEffectManager {
public:
    // --- Callbacks ---
    std::function<void(EffectType, int /*damage*/)> onDoTTick;
    std::function<void(EffectType, int /*heal*/)> onHoTTick;

    // --- Apply effects ---
    void applyEffect(EffectType type, float duration, float value,
                     float value2 = 0.0f, uint32_t source = 0);
    void applyDoT(EffectType type, float duration, float damagePerTick,
                  uint32_t source = 0);
    void applyHoT(EffectType type, float duration, float healPerTick,
                  uint32_t source = 0);
    void applyShield(float amount, float duration, uint32_t source = 0);
    void applyInvulnerability(float duration, uint32_t source = 0);
    void applyTransform(float duration, float damageMult, float speedBonus,
                        uint32_t source = 0);

    // --- Remove effects ---
    void removeEffect(EffectType type);
    void removeEffectBySource(EffectType type, uint32_t source);
    void removeAllDebuffs();
    void removeAllEffects();

    // Well-known source IDs for system-applied buffs
    static constexpr uint32_t SOURCE_AURORA = 0xAE01;

    // --- Queries ---
    [[nodiscard]] bool  hasEffect(EffectType type) const;
    [[nodiscard]] float getEffectDuration(EffectType type) const;
    [[nodiscard]] float getEffectValue(EffectType type) const;
    [[nodiscard]] int   getEffectStacks(EffectType type) const;

    [[nodiscard]] bool  isInvulnerable() const;
    [[nodiscard]] bool  isStunImmune() const;
    [[nodiscard]] bool  isStunned() const;
    [[nodiscard]] bool  isFrozen() const;
    [[nodiscard]] bool  hasGuaranteedCrit() const;
    [[nodiscard]] float currentShield() const;

    // --- Computed modifiers ---
    [[nodiscard]] float getDamageMultiplier() const;   // AttackUp + Transform
    [[nodiscard]] float getDamageReduction() const;    // ArmorUp, capped 90%
    [[nodiscard]] float getSpeedModifier() const;      // SpeedUp + Transform - Slow, min 10%
    [[nodiscard]] float getBonusDamageTaken() const;   // HuntersMark
    [[nodiscard]] float getArmorShred() const;         // value * stacks
    [[nodiscard]] float getManaRegenBonus() const;
    [[nodiscard]] float getExpGainBonus() const;
    [[nodiscard]] float getLifestealBonus() const;
    [[nodiscard]] float getCritRateBonus() const;
    [[nodiscard]] float getArmorBuff() const;
    [[nodiscard]] float getAttackBuff() const;
    [[nodiscard]] float getDamageReductionBuff() const;

    // --- Combat interactions ---
    int  absorbDamage(int damage);                     // returns remaining damage
    void consumeGuaranteedCrit();
    float consumeBewitch(uint32_t attackerEntityId);   // returns multiplier or 0

    // --- Bitmask for replication (bit N = EffectType N is active) ---
    [[nodiscard]] uint32_t getActiveEffectMask() const;

    // --- Read-only access to active effects (for buff sync serialization) ---
    [[nodiscard]] const std::vector<StatusEffect>& activeEffects() const { return activeEffects_; }

    // --- Tick ---
    void tick(float deltaTime);

private:
    std::vector<StatusEffect> activeEffects_;
    std::unordered_map<EffectType, size_t> effectLookup_; // EffectType -> index
    float currentShieldAmount_ = 0.0f;
    bool ticking_ = false;          // true while tick() is iterating activeEffects_
    bool pendingRemoveAll_ = false; // deferred removeAllEffects() during tick

    // --- Helpers ---
    void rebuildLookup();
    [[nodiscard]] static bool isDoT(EffectType type);
    [[nodiscard]] static bool isHoT(EffectType type);
    [[nodiscard]] static bool isCC(EffectType type);
    [[nodiscard]] static bool isDebuff(EffectType type);
    [[nodiscard]] static bool canStack(EffectType type);
    [[nodiscard]] static bool isAdditiveStack(EffectType type);
    [[nodiscard]] static int  getMaxStacks(EffectType type);
};

} // namespace fate
