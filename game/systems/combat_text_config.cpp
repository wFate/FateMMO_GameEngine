#include "game/systems/combat_text_config.h"
#include "engine/core/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cmath>

namespace fate {

CombatTextConfig& CombatTextConfig::instance() {
    static CombatTextConfig s;
    return s;
}

CombatTextConfig::CombatTextConfig() {
    loadDefaults();
}

void CombatTextConfig::loadDefaults() {
    damage = {};
    damage.color = Color::white();
    damage.fontSize = 11.0f;

    crit = {};
    crit.color = Color(1.0f, 0.6f, 0.1f);
    crit.fontSize = 14.0f;
    crit.scale = 1.3f;
    crit.popScale = 1.5f;

    miss = {};
    miss.text = "Miss";
    miss.color = Color(0.5f, 0.5f, 0.5f);

    resist = {};
    resist.text = "Resist";
    resist.color = Color(0.6f, 0.3f, 0.9f);

    xp = {};
    xp.text = "+{amount} XP";
    xp.color = Color::yellow();

    levelUp = {};
    levelUp.text = "LEVEL UP!";
    levelUp.color = Color(1.0f, 0.84f, 0.0f);
    levelUp.lifetime = 1.8f;
    levelUp.scale = 1.3f;
    levelUp.popScale = 1.6f;

    heal = {};
    heal.color = Color(0.2f, 0.9f, 0.3f);

    block = {};
    block.text = "Block";
    block.color = Color(0.4f, 0.7f, 1.0f);
}

static void styleToJson(nlohmann::json& j, const CombatTextStyle& s) {
    j["text"]          = s.text;
    j["color"]         = {s.color.r, s.color.g, s.color.b, s.color.a};
    j["outlineColor"]  = {s.outlineColor.r, s.outlineColor.g, s.outlineColor.b, s.outlineColor.a};
    j["fontSize"]      = s.fontSize;
    j["scale"]         = s.scale;
    j["lifetime"]      = s.lifetime;
    j["floatSpeed"]    = s.floatSpeed;
    j["floatAngle"]    = s.floatAngle;
    j["startOffsetY"]  = s.startOffsetY;
    j["randomSpreadX"] = s.randomSpreadX;
    j["fadeDelay"]     = s.fadeDelay;
    j["popScale"]      = s.popScale;
    j["popDuration"]   = s.popDuration;
}

static void styleFromJson(const nlohmann::json& j, CombatTextStyle& s) {
    if (j.contains("text"))          s.text          = j["text"].get<std::string>();
    if (j.contains("fontSize"))      s.fontSize      = j["fontSize"].get<float>();
    if (j.contains("scale"))         s.scale         = j["scale"].get<float>();
    if (j.contains("lifetime"))      s.lifetime      = j["lifetime"].get<float>();
    if (j.contains("floatSpeed"))    s.floatSpeed    = j["floatSpeed"].get<float>();
    if (j.contains("floatAngle"))    s.floatAngle    = j["floatAngle"].get<float>();
    if (j.contains("startOffsetY"))  s.startOffsetY  = j["startOffsetY"].get<float>();
    if (j.contains("randomSpreadX")) s.randomSpreadX = j["randomSpreadX"].get<float>();
    if (j.contains("fadeDelay"))     s.fadeDelay     = j["fadeDelay"].get<float>();
    if (j.contains("popScale"))      s.popScale      = j["popScale"].get<float>();
    if (j.contains("popDuration"))   s.popDuration   = j["popDuration"].get<float>();
    if (j.contains("color") && j["color"].is_array() && j["color"].size() >= 4) {
        auto& c = j["color"];
        s.color = Color(c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>());
    }
    if (j.contains("outlineColor") && j["outlineColor"].is_array() && j["outlineColor"].size() >= 4) {
        auto& c = j["outlineColor"];
        s.outlineColor = Color(c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>());
    }
}

void CombatTextConfig::loadFromJsonString(const std::string& jsonStr) {
    auto j = nlohmann::json::parse(jsonStr, nullptr, false);
    if (j.is_discarded()) return;

    if (j.contains("damage"))  styleFromJson(j["damage"],  damage);
    if (j.contains("crit"))    styleFromJson(j["crit"],    crit);
    if (j.contains("miss"))    styleFromJson(j["miss"],    miss);
    if (j.contains("resist"))  styleFromJson(j["resist"],  resist);
    if (j.contains("xp"))      styleFromJson(j["xp"],      xp);
    if (j.contains("levelUp")) styleFromJson(j["levelUp"], levelUp);
    if (j.contains("heal"))    styleFromJson(j["heal"],    heal);
    if (j.contains("block"))   styleFromJson(j["block"],   block);
}

bool CombatTextConfig::load(const std::string& path) {
    std::ifstream f(path);
    if (!f.good()) return false;
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    loadFromJsonString(content);
    LOG_INFO("CombatText", "Loaded combat text config from %s", path.c_str());
    return true;
}

bool CombatTextConfig::save(const std::string& path) const {
    nlohmann::json j;
    styleToJson(j["damage"],  damage);
    styleToJson(j["crit"],    crit);
    styleToJson(j["miss"],    miss);
    styleToJson(j["resist"],  resist);
    styleToJson(j["xp"],      xp);
    styleToJson(j["levelUp"], levelUp);
    styleToJson(j["heal"],    heal);
    styleToJson(j["block"],   block);

    std::ofstream f(path);
    if (!f.good()) {
        LOG_ERROR("CombatText", "Failed to save combat text config to %s", path.c_str());
        return false;
    }
    f << j.dump(2);
    LOG_INFO("CombatText", "Saved combat text config to %s", path.c_str());
    return true;
}

} // namespace fate
