#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

namespace fate {

class Texture;

struct SpriteSet {
    std::shared_ptr<Texture> front;
    std::shared_ptr<Texture> back;
    std::shared_ptr<Texture> side;
};

struct SpritePaths {
    std::string front;
    std::string back;
    std::string side;
};

struct AnimInfo {
    int startFrame = 0;
    int frameCount = 1;
    float frameRate = 1.0f;
    bool loop = true;
    int hitFrame = -1;
    // layerOffsetsY[layerName] = per-frame Y offsets
    std::unordered_map<std::string, std::vector<float>> layerOffsetsY;
};

class PaperDollCatalog {
public:
    static PaperDollCatalog& instance() {
        static PaperDollCatalog s_instance;
        return s_instance;
    }

    // --- Load / Save ---
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    bool isLoaded() const { return loaded_; }

    // --- Frame size ---
    int frameWidth() const { return frameWidth_; }
    int frameHeight() const { return frameHeight_; }

    // --- Body ---
    SpriteSet getBody(const std::string& gender) const;
    SpritePaths getBodyPaths(const std::string& gender) const;
    void setBodyPath(const std::string& gender, const std::string& direction,
                     const std::string& path);

    // --- Hairstyles ---
    SpriteSet getHairstyle(const std::string& gender, const std::string& name) const;
    SpritePaths getHairstylePaths(const std::string& gender, const std::string& name) const;
    std::vector<std::string> getHairstyleNames(const std::string& gender) const;
    size_t getHairstyleCount(const std::string& gender) const;
    std::string getHairstyleNameByIndex(const std::string& gender, uint8_t index) const;
    void addHairstyle(const std::string& gender, const std::string& name);
    void setHairstylePath(const std::string& gender, const std::string& name,
                          const std::string& direction, const std::string& path);
    void removeHairstyle(const std::string& gender, const std::string& name);

    // --- Equipment ---
    SpriteSet getEquipment(const std::string& category, const std::string& style) const;
    SpritePaths getEquipmentPaths(const std::string& category, const std::string& style) const;
    std::vector<std::string> getEquipmentCategories() const;
    std::vector<std::string> getEquipmentStyles(const std::string& category) const;
    void addEquipmentStyle(const std::string& category, const std::string& style);
    void setEquipmentPath(const std::string& category, const std::string& style,
                          const std::string& direction, const std::string& path);
    void removeEquipmentStyle(const std::string& category, const std::string& style);

    // --- Animations ---
    AnimInfo getAnimation(const std::string& name) const;
    std::vector<std::string> getAnimationNames() const;
    float getLayerOffsetY(const std::string& animName, const std::string& layer,
                          int frameIndex) const;

private:
    PaperDollCatalog() = default;

    SpriteSet loadSpriteSet(const SpritePaths& paths) const;

    bool loaded_ = false;
    std::string loadedPath_;  // absolute path used for save
    int frameWidth_ = 48;
    int frameHeight_ = 96;

    // bodies_[gender] = paths
    std::unordered_map<std::string, SpritePaths> bodies_;
    // hairstyles_[gender] = ordered list of (name, paths)
    std::unordered_map<std::string, std::vector<std::pair<std::string, SpritePaths>>> hairstyles_;
    // equipment_[category] = ordered list of (style, paths)
    std::unordered_map<std::string, std::vector<std::pair<std::string, SpritePaths>>> equipment_;
    // Ordered category list (preserves JSON order)
    std::vector<std::string> equipmentCategories_;
    // animations_[name] = AnimInfo
    std::unordered_map<std::string, AnimInfo> animations_;
    // Ordered animation name list
    std::vector<std::string> animationNames_;

    // Texture caches (populated on load, updated on setPath)
    mutable std::unordered_map<std::string, SpriteSet> bodyTexCache_;
    mutable std::unordered_map<std::string, SpriteSet> hairTexCache_;   // key: "gender/name"
    mutable std::unordered_map<std::string, SpriteSet> equipTexCache_;  // key: "category/style"

    void rebuildTextureCache();
};

} // namespace fate
