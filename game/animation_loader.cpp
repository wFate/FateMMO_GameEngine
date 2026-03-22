#include "game/animation_loader.h"
#include "game/components/animator.h"
#include "game/components/sprite_component.h"
#include "engine/core/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace fate {

bool AnimationLoader::parsePackedMeta(const nlohmann::json& json, PackedSheetMeta& out) {
    int version = json.value("version", 0);
    if (version != 1) {
        LOG_WARN("AnimationLoader", "Unsupported packed meta version: %d", version);
        return false;
    }
    out.frameWidth = json.value("frameWidth", 0);
    out.frameHeight = json.value("frameHeight", 0);
    out.columns = json.value("columns", 1);
    out.totalFrames = json.value("totalFrames", 0);
    out.states.clear();
    if (!json.contains("states") || !json["states"].is_object()) return false;
    for (auto& [key, val] : json["states"].items()) {
        PackedStateMeta s;
        s.startFrame = val.value("startFrame", 0);
        s.frameCount = val.value("frameCount", 1);
        s.frameRate = val.value("frameRate", 8.0f);
        s.loop = val.value("loop", true);
        s.hitFrame = val.value("hitFrame", -1);
        s.flipX = val.value("flipX", false);
        out.states[key] = s;
    }
    return true;
}

bool AnimationLoader::loadPackedMeta(const std::string& path, PackedSheetMeta& out) {
    std::ifstream f(path);
    if (!f.is_open()) {
        LOG_WARN("AnimationLoader", "Cannot open packed meta: %s", path.c_str());
        return false;
    }
    nlohmann::json json;
    try { f >> json; } catch (...) {
        LOG_WARN("AnimationLoader", "Invalid JSON in packed meta: %s", path.c_str());
        return false;
    }
    return parsePackedMeta(json, out);
}

void AnimationLoader::applyToAnimator(const PackedSheetMeta& meta, Animator& animator) {
    for (auto& [name, s] : meta.states) {
        animator.addAnimation(name, s.startFrame, s.frameCount, s.frameRate, s.loop, s.hitFrame);
    }
}

void AnimationLoader::applyToSprite(const PackedSheetMeta& meta, SpriteComponent& sprite,
                                     const std::string& sheetTexturePath) {
    sprite.texturePath = sheetTexturePath;
    sprite.frameWidth = meta.frameWidth;
    sprite.frameHeight = meta.frameHeight;
    sprite.columns = meta.columns;
    sprite.totalFrames = meta.totalFrames;
}

std::unordered_map<std::string, bool> AnimationLoader::getFlipXMap(const PackedSheetMeta& meta) {
    std::unordered_map<std::string, bool> result;
    for (auto& [name, s] : meta.states) {
        result[name] = s.flipX;
    }
    return result;
}

} // namespace fate
