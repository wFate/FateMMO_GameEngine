#pragma once
#include "engine/core/types.h"
#include <string>
#include <vector>
#include <cstdint>

namespace fate {

class SpriteBatch;
class SDFText;

enum class FloatingTextType : uint8_t {
    Damage,     // white number, floats up
    CritDamage, // larger, yellow-orange, floats up with scale punch
    Heal,       // green number, floats up
    Miss,       // "MISS" text, grey, smaller
    Block,      // "BLOCK" text, blue-grey
    Absorb,     // "ABSORB" text, cyan
    XPGain,     // "+XXX XP", purple
    GoldGain,   // "+XXX G", gold
    LevelUp,    // "LEVEL UP!", large, gold, dramatic
    Dodge,      // "DODGE" text, grey
    Emoticon    // emoticon floating above player, 3s lifetime
};

struct FloatingTextEntry {
    std::string text;
    FloatingTextType type = FloatingTextType::Damage;
    Vec2 worldPos;            // spawn position in world coords
    float elapsed = 0.0f;
    float lifetime = 1.2f;
    float velocityY = -60.0f; // pixels per second (negative = upward)
    float offsetX = 0.0f;     // slight horizontal offset to avoid overlap
    float scale = 1.0f;       // for crit punch effect
};

class FloatingTextManager {
public:
    static FloatingTextManager& instance();

    // Spawn methods
    void spawnDamage(Vec2 worldPos, int amount, bool isCrit);
    void spawnHeal(Vec2 worldPos, int amount);
    void spawnMiss(Vec2 worldPos);
    void spawnBlock(Vec2 worldPos);
    void spawnAbsorb(Vec2 worldPos);
    void spawnDodge(Vec2 worldPos);
    void spawnXP(Vec2 worldPos, int amount);
    void spawnGold(Vec2 worldPos, int amount);
    void spawnLevelUp(Vec2 worldPos);
    void spawnCustom(Vec2 worldPos, const std::string& text, FloatingTextType type);
    void spawnEmoticon(Vec2 worldPos, uint8_t emoticonId);

    // Per-frame
    void update(float dt);
    void render(SpriteBatch& batch, SDFText& sdf);

    void clear();
    size_t activeCount() const;

    // Read-only access for testing
    const std::vector<FloatingTextEntry>& entries() const { return entries_; }

    static constexpr size_t MAX_ENTRIES = 64;

private:
    FloatingTextManager() = default;

    void addEntry(FloatingTextEntry entry);
    static Color colorForType(FloatingTextType type);
    static float fontSizeForType(FloatingTextType type);
    static float computeAlpha(float elapsed, float lifetime);

    std::vector<FloatingTextEntry> entries_;
};

} // namespace fate
