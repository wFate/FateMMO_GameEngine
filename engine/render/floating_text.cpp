#include "engine/render/floating_text.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <string>

namespace fate {

FloatingTextManager& FloatingTextManager::instance() {
    static FloatingTextManager mgr;
    return mgr;
}

// ---------------------------------------------------------------------------
// Spawn helpers
// ---------------------------------------------------------------------------

void FloatingTextManager::addEntry(FloatingTextEntry entry) {
    if (entries_.size() >= MAX_ENTRIES) return;

    // Deterministic X offset based on current entry count to avoid stacking
    entry.offsetX = (static_cast<float>(entries_.size() % 5) - 2.0f) * 6.0f;
    entry.worldPos.x += entry.offsetX;

    entries_.push_back(std::move(entry));
}

void FloatingTextManager::spawnDamage(Vec2 worldPos, int amount, bool isCrit) {
    FloatingTextEntry e;
    e.text = std::to_string(amount);
    e.worldPos = worldPos;
    if (isCrit) {
        e.type = FloatingTextType::CritDamage;
        e.scale = 1.5f;
    } else {
        e.type = FloatingTextType::Damage;
    }
    addEntry(std::move(e));
}

void FloatingTextManager::spawnHeal(Vec2 worldPos, int amount) {
    FloatingTextEntry e;
    e.text = "+" + std::to_string(amount);
    e.type = FloatingTextType::Heal;
    e.worldPos = worldPos;
    addEntry(std::move(e));
}

void FloatingTextManager::spawnMiss(Vec2 worldPos) {
    FloatingTextEntry e;
    e.text = "MISS";
    e.type = FloatingTextType::Miss;
    e.worldPos = worldPos;
    addEntry(std::move(e));
}

void FloatingTextManager::spawnBlock(Vec2 worldPos) {
    FloatingTextEntry e;
    e.text = "BLOCK";
    e.type = FloatingTextType::Block;
    e.worldPos = worldPos;
    addEntry(std::move(e));
}

void FloatingTextManager::spawnAbsorb(Vec2 worldPos) {
    FloatingTextEntry e;
    e.text = "ABSORB";
    e.type = FloatingTextType::Absorb;
    e.worldPos = worldPos;
    addEntry(std::move(e));
}

void FloatingTextManager::spawnDodge(Vec2 worldPos) {
    FloatingTextEntry e;
    e.text = "DODGE";
    e.type = FloatingTextType::Dodge;
    e.worldPos = worldPos;
    addEntry(std::move(e));
}

void FloatingTextManager::spawnXP(Vec2 worldPos, int amount) {
    FloatingTextEntry e;
    e.text = "+" + std::to_string(amount) + " XP";
    e.type = FloatingTextType::XPGain;
    e.worldPos = worldPos;
    addEntry(std::move(e));
}

void FloatingTextManager::spawnGold(Vec2 worldPos, int amount) {
    FloatingTextEntry e;
    e.text = "+" + std::to_string(amount) + " G";
    e.type = FloatingTextType::GoldGain;
    e.worldPos = worldPos;
    addEntry(std::move(e));
}

void FloatingTextManager::spawnLevelUp(Vec2 worldPos) {
    FloatingTextEntry e;
    e.text = "LEVEL UP!";
    e.type = FloatingTextType::LevelUp;
    e.worldPos = worldPos;
    e.lifetime = 2.0f;
    e.velocityY = -30.0f;
    addEntry(std::move(e));
}

void FloatingTextManager::spawnCustom(Vec2 worldPos, const std::string& text, FloatingTextType type) {
    FloatingTextEntry e;
    e.text = text;
    e.type = type;
    e.worldPos = worldPos;
    if (type == FloatingTextType::LevelUp) {
        e.lifetime = 2.0f;
        e.velocityY = -30.0f;
    }
    if (type == FloatingTextType::CritDamage) {
        e.scale = 1.5f;
    }
    addEntry(std::move(e));
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void FloatingTextManager::update(float dt) {
    size_t write = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        FloatingTextEntry& e = entries_[i];
        e.elapsed += dt;

        // Remove expired entries
        if (e.elapsed >= e.lifetime) continue;

        // Move upward
        e.worldPos.y += e.velocityY * dt;

        // Crit scale punch: lerp from 1.5 to 1.0 over first 0.2s
        if (e.type == FloatingTextType::CritDamage && e.elapsed < 0.2f) {
            float t = e.elapsed / 0.2f;
            e.scale = 1.5f + (1.0f - 1.5f) * t; // lerp 1.5 -> 1.0
        } else if (e.type == FloatingTextType::CritDamage && e.elapsed >= 0.2f) {
            e.scale = 1.0f;
        }

        // Compact in place
        if (write != i) {
            entries_[write] = std::move(entries_[i]);
        }
        ++write;
    }
    entries_.resize(write);
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

float FloatingTextManager::computeAlpha(float elapsed, float lifetime) {
    if (lifetime <= 0.0f) return 0.0f;
    float ratio = elapsed / lifetime;
    if (ratio < 0.6f) {
        return 1.0f; // full opacity for first 60%
    }
    // Linear fade over remaining 40%
    float fadeRatio = (ratio - 0.6f) / 0.4f;
    float alpha = 1.0f - fadeRatio;
    if (alpha < 0.0f) alpha = 0.0f;
    return alpha;
}

Color FloatingTextManager::colorForType(FloatingTextType type) {
    switch (type) {
        case FloatingTextType::Damage:     return Color(1.0f, 1.0f, 1.0f);
        case FloatingTextType::CritDamage: return Color(1.0f, 0.85f, 0.2f);
        case FloatingTextType::Heal:       return Color(0.3f, 1.0f, 0.3f);
        case FloatingTextType::Miss:       return Color(0.6f, 0.6f, 0.6f);
        case FloatingTextType::Dodge:      return Color(0.6f, 0.6f, 0.6f);
        case FloatingTextType::Block:      return Color(0.5f, 0.6f, 0.8f);
        case FloatingTextType::Absorb:     return Color(0.3f, 0.8f, 1.0f);
        case FloatingTextType::XPGain:     return Color(0.7f, 0.5f, 1.0f);
        case FloatingTextType::GoldGain:   return Color(1.0f, 0.85f, 0.3f);
        case FloatingTextType::LevelUp:    return Color(1.0f, 0.9f, 0.4f);
        default:                           return Color::white();
    }
}

float FloatingTextManager::fontSizeForType(FloatingTextType type) {
    switch (type) {
        case FloatingTextType::Damage:     return 11.0f;
        case FloatingTextType::CritDamage: return 14.0f;
        case FloatingTextType::Heal:       return 11.0f;
        case FloatingTextType::Miss:       return 10.0f;
        case FloatingTextType::Dodge:      return 10.0f;
        case FloatingTextType::Block:      return 10.0f;
        case FloatingTextType::Absorb:     return 10.0f;
        case FloatingTextType::XPGain:     return 10.0f;
        case FloatingTextType::GoldGain:   return 10.0f;
        case FloatingTextType::LevelUp:    return 18.0f;
        default:                           return 11.0f;
    }
}

void FloatingTextManager::render(SpriteBatch& batch, SDFText& sdf) {
    constexpr float DEPTH = 900.0f;

    for (const auto& e : entries_) {
        float alpha = computeAlpha(e.elapsed, e.lifetime);
        if (alpha <= 0.0f) continue;

        Color c = colorForType(e.type);
        c.a = alpha;

        float fontSize = fontSizeForType(e.type) * e.scale;

        sdf.drawWorld(batch, e.text, e.worldPos, fontSize, c, DEPTH);
    }
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------

void FloatingTextManager::clear() {
    entries_.clear();
}

size_t FloatingTextManager::activeCount() const {
    return entries_.size();
}

} // namespace fate
