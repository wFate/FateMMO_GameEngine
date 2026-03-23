#include "engine/vfx/skill_vfx_def.h"
#include "engine/core/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

namespace fate {

static VFXParticleConfig parseParticles(const nlohmann::json& j) {
    VFXParticleConfig p;
    p.count     = j.value("count", 0);
    p.lifetime  = j.value("lifetime", 0.5f);
    p.speed     = j.value("speed", 20.0f);
    p.gravity   = j.value("gravity", 0.0f);
    p.sizeStart = j.value("sizeStart", 3.0f);
    p.sizeEnd   = j.value("sizeEnd", 1.0f);

    if (j.contains("colorStart") && j["colorStart"].is_array() && j["colorStart"].size() >= 4) {
        auto& c = j["colorStart"];
        p.colorStart = Color(c[0].get<float>(), c[1].get<float>(),
                             c[2].get<float>(), c[3].get<float>());
    }
    if (j.contains("colorEnd") && j["colorEnd"].is_array() && j["colorEnd"].size() >= 4) {
        auto& c = j["colorEnd"];
        p.colorEnd = Color(c[0].get<float>(), c[1].get<float>(),
                           c[2].get<float>(), c[3].get<float>());
    }

    p.enabled = true;
    return p;
}

static VFXPhaseDef parsePhase(const nlohmann::json& j) {
    VFXPhaseDef phase;
    phase.spriteSheet = j.value("spriteSheet", std::string{});
    phase.frameCount  = j.value("frameCount", 1);
    phase.frameRate   = j.value("frameRate", 10.0f);
    phase.speed       = j.value("speed", 0.0f);
    phase.duration    = j.value("duration", 0.0f);
    phase.looping     = j.value("looping", false);

    if (j.contains("frameSize") && j["frameSize"].is_array() && j["frameSize"].size() >= 2) {
        phase.frameSize = Vec2(j["frameSize"][0].get<float>(),
                               j["frameSize"][1].get<float>());
    }
    if (j.contains("offset") && j["offset"].is_array() && j["offset"].size() >= 2) {
        phase.offset = Vec2(j["offset"][0].get<float>(),
                            j["offset"][1].get<float>());
    }

    if (j.contains("particles") && j["particles"].is_object()) {
        phase.particles = parseParticles(j["particles"]);
    }

    phase.enabled = true;
    return phase;
}

bool parseSkillVFXDef(const std::string& jsonStr, SkillVFXDef& outDef) {
    try {
        auto j = nlohmann::json::parse(jsonStr);

        outDef = SkillVFXDef{};
        outDef.id = j.value("id", std::string{});

        const char* phaseKeys[] = {"cast", "projectile", "impact", "area"};
        VFXPhaseDef* phases[]   = {&outDef.cast, &outDef.projectile,
                                   &outDef.impact, &outDef.area};

        for (int i = 0; i < 4; ++i) {
            if (j.contains(phaseKeys[i]) && !j[phaseKeys[i]].is_null()) {
                *phases[i] = parsePhase(j[phaseKeys[i]]);
            }
        }

        return true;
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("VFX", "Failed to parse SkillVFXDef JSON: %s", e.what());
        return false;
    }
}

bool loadSkillVFXDef(const std::string& jsonPath, SkillVFXDef& outDef) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        LOG_ERROR("VFX", "Failed to open VFX file: %s", jsonPath.c_str());
        return false;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return parseSkillVFXDef(ss.str(), outDef);
}

} // namespace fate
