#pragma once
#include "engine/core/types.h"
#include <string>

namespace fate {

struct VFXParticleConfig {
    int count = 0;
    float lifetime = 0.5f;
    Color colorStart = Color::white();
    Color colorEnd = Color::clear();
    float speed = 20.0f;
    float gravity = 0.0f;
    float sizeStart = 3.0f;
    float sizeEnd = 1.0f;
    bool enabled = false;
};

struct VFXPhaseDef {
    std::string spriteSheet;      // path to PNG strip
    int frameCount = 1;
    Vec2 frameSize = {32, 32};
    float frameRate = 10.0f;
    Vec2 offset = {0, 0};         // pixel offset from anchor
    float speed = 0.0f;           // projectile only (px/sec)
    float duration = 0.0f;        // area only (seconds)
    bool looping = false;         // area only
    VFXParticleConfig particles;
    bool enabled = false;         // false if phase omitted/null in JSON
};

struct SkillVFXDef {
    std::string id;
    VFXPhaseDef cast;
    VFXPhaseDef projectile;
    VFXPhaseDef impact;
    VFXPhaseDef area;
};

// Parse from JSON string (for tests)
bool parseSkillVFXDef(const std::string& jsonStr, SkillVFXDef& outDef);

// Load from file
bool loadSkillVFXDef(const std::string& jsonPath, SkillVFXDef& outDef);

} // namespace fate
