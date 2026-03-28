#pragma once
#include "engine/core/types.h"
#include <string>

namespace fate {

struct CombatTextStyle {
    // Core
    std::string text;                                    // "" = use default
    Color color        = Color::white();
    Color outlineColor = Color(0.0f, 0.0f, 0.0f, 0.78f);
    float fontSize     = 14.0f;
    float scale        = 1.0f;

    // Motion
    float lifetime     = 1.2f;
    float floatSpeed   = 30.0f;
    float floatAngle   = 90.0f;   // degrees, 90 = up
    float startOffsetY = 0.0f;
    float randomSpreadX = 0.0f;

    // Fade & Pop
    float fadeDelay    = 0.0f;
    float popScale     = 1.0f;
    float popDuration  = 0.15f;
};

class CombatTextConfig {
public:
    CombatTextConfig();

    static CombatTextConfig& instance();

    CombatTextStyle damage;
    CombatTextStyle crit;
    CombatTextStyle miss;
    CombatTextStyle resist;
    CombatTextStyle xp;
    CombatTextStyle levelUp;
    CombatTextStyle heal;
    CombatTextStyle block;

    void loadDefaults();
    bool load(const std::string& path);
    bool save(const std::string& path) const;
    void loadFromJsonString(const std::string& jsonStr);

    static constexpr const char* kDefaultPath = "assets/data/combat_text.json";
};

} // namespace fate
