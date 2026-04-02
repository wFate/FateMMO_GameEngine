#pragma once
#include "engine/core/types.h"
#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>

namespace fate {

enum class TextStyle : uint8_t {
    Normal   = 1,
    Outlined = 2,
    Glow     = 3,
    Shadow   = 4
};

struct TextEffects {
    Color outlineColor = Color::clear();
    float outlineWidth = 0.0f;
    Color glowColor    = Color::clear();
    float glowRadius   = 0.0f;
    Vec2  shadowOffset = {0.0f, 0.0f};
    Color shadowColor  = Color::clear();
};

// --- Serialization helpers ---

inline std::string textStyleToString(TextStyle s) {
    switch (s) {
        case TextStyle::Normal:   return "Normal";
        case TextStyle::Outlined: return "Outlined";
        case TextStyle::Glow:     return "Glow";
        case TextStyle::Shadow:   return "Shadow";
        default:                  return "Normal";
    }
}

inline TextStyle textStyleFromString(const std::string& s) {
    if (s == "Outlined") return TextStyle::Outlined;
    if (s == "Glow")     return TextStyle::Glow;
    if (s == "Shadow")   return TextStyle::Shadow;
    return TextStyle::Normal;
}

inline nlohmann::json textEffectsToJson(const TextEffects& fx) {
    nlohmann::json j;
    j["outlineColor"] = {fx.outlineColor.r, fx.outlineColor.g, fx.outlineColor.b, fx.outlineColor.a};
    j["outlineWidth"] = fx.outlineWidth;
    j["glowColor"]    = {fx.glowColor.r, fx.glowColor.g, fx.glowColor.b, fx.glowColor.a};
    j["glowRadius"]   = fx.glowRadius;
    j["shadowOffset"] = {fx.shadowOffset.x, fx.shadowOffset.y};
    j["shadowColor"]  = {fx.shadowColor.r, fx.shadowColor.g, fx.shadowColor.b, fx.shadowColor.a};
    return j;
}

inline TextEffects textEffectsFromJson(const nlohmann::json& j) {
    TextEffects fx;
    auto readColor = [](const nlohmann::json& a, Color def) -> Color {
        if (!a.is_array() || a.size() < 3) return def;
        return {a[0].get<float>(), a[1].get<float>(), a[2].get<float>(),
                a.size() >= 4 ? a[3].get<float>() : 1.0f};
    };
    if (j.contains("outlineColor")) fx.outlineColor = readColor(j["outlineColor"], fx.outlineColor);
    if (j.contains("outlineWidth")) fx.outlineWidth = j["outlineWidth"].get<float>();
    if (j.contains("glowColor"))    fx.glowColor    = readColor(j["glowColor"], fx.glowColor);
    if (j.contains("glowRadius"))   fx.glowRadius   = j["glowRadius"].get<float>();
    if (j.contains("shadowOffset") && j["shadowOffset"].is_array() && j["shadowOffset"].size() >= 2)
        fx.shadowOffset = {j["shadowOffset"][0].get<float>(), j["shadowOffset"][1].get<float>()};
    if (j.contains("shadowColor"))  fx.shadowColor  = readColor(j["shadowColor"], fx.shadowColor);
    return fx;
}

// Count of styles for combo dropdowns
inline constexpr int kTextStyleCount = 4;
inline const char* kTextStyleNames[] = { "Normal", "Outlined", "Glow", "Shadow" };

} // namespace fate
