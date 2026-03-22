#pragma once
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace fate {

struct Animator;
struct SpriteComponent;

struct PackedStateMeta {
    int startFrame = 0;
    int frameCount = 1;
    float frameRate = 8.0f;
    bool loop = true;
    int hitFrame = -1;
    bool flipX = false;
};

struct PackedSheetMeta {
    int frameWidth = 0;
    int frameHeight = 0;
    int columns = 1;
    int totalFrames = 0;
    std::unordered_map<std::string, PackedStateMeta> states;
};

class AnimationLoader {
public:
    static bool parsePackedMeta(const nlohmann::json& json, PackedSheetMeta& out);
    static bool loadPackedMeta(const std::string& path, PackedSheetMeta& out);
    static void applyToAnimator(const PackedSheetMeta& meta, Animator& animator);
    static void applyToSprite(const PackedSheetMeta& meta, SpriteComponent& sprite,
                              const std::string& sheetTexturePath);
    static std::unordered_map<std::string, bool> getFlipXMap(const PackedSheetMeta& meta);
};

} // namespace fate
