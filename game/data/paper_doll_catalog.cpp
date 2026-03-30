#include "game/data/paper_doll_catalog.h"
#include "engine/render/texture.h"
#include "engine/core/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>

namespace fate {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static SpritePaths parsePaths(const nlohmann::json& j) {
    SpritePaths p;
    if (j.contains("front")) p.front = j["front"].get<std::string>();
    if (j.contains("back"))  p.back  = j["back"].get<std::string>();
    if (j.contains("side"))  p.side  = j["side"].get<std::string>();
    return p;
}

static nlohmann::json pathsToJson(const SpritePaths& p) {
    return nlohmann::json{
        {"front", p.front},
        {"back",  p.back},
        {"side",  p.side}
    };
}

// ---------------------------------------------------------------------------
// Load / Save
// ---------------------------------------------------------------------------

bool PaperDollCatalog::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("PaperDoll", "Failed to open catalog: %s", path.c_str());
        return false;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(file);
    } catch (const std::exception& e) {
        LOG_ERROR("PaperDoll", "JSON parse error in %s: %s", path.c_str(), e.what());
        return false;
    }

    // Frame size
    if (root.contains("frameSize") && root["frameSize"].is_array() &&
        root["frameSize"].size() >= 2) {
        frameWidth_  = root["frameSize"][0].get<int>();
        frameHeight_ = root["frameSize"][1].get<int>();
    }

    // Bodies
    bodies_.clear();
    if (root.contains("bodies") && root["bodies"].is_object()) {
        for (auto& [gender, val] : root["bodies"].items()) {
            bodies_[gender] = parsePaths(val);
        }
    }

    // Hairstyles  (preserve insertion order via items())
    hairstyles_.clear();
    if (root.contains("hairstyles") && root["hairstyles"].is_object()) {
        for (auto& [gender, styles] : root["hairstyles"].items()) {
            auto& vec = hairstyles_[gender];
            if (styles.is_object()) {
                for (auto& [name, val] : styles.items()) {
                    vec.emplace_back(name, parsePaths(val));
                }
            }
        }
    }

    // Equipment  (preserve category + style order)
    equipment_.clear();
    equipmentCategories_.clear();
    if (root.contains("equipment") && root["equipment"].is_object()) {
        for (auto& [category, styles] : root["equipment"].items()) {
            equipmentCategories_.push_back(category);
            auto& vec = equipment_[category];
            if (styles.is_object()) {
                for (auto& [style, val] : styles.items()) {
                    vec.emplace_back(style, parsePaths(val));
                }
            }
        }
    }

    // Animations  (preserve order)
    animations_.clear();
    animationNames_.clear();
    if (root.contains("animations") && root["animations"].is_object()) {
        for (auto& [name, val] : root["animations"].items()) {
            animationNames_.push_back(name);
            AnimInfo info;
            if (val.contains("startFrame"))  info.startFrame  = val["startFrame"].get<int>();
            if (val.contains("frameCount"))  info.frameCount  = val["frameCount"].get<int>();
            if (val.contains("frameRate"))   info.frameRate   = val["frameRate"].get<float>();
            if (val.contains("loop"))        info.loop        = val["loop"].get<bool>();
            if (val.contains("hitFrame"))    info.hitFrame    = val["hitFrame"].get<int>();

            if (val.contains("layerOffsets") && val["layerOffsets"].is_object()) {
                for (auto& [layer, offData] : val["layerOffsets"].items()) {
                    if (offData.contains("y") && offData["y"].is_array()) {
                        auto& offsets = info.layerOffsetsY[layer];
                        for (auto& v : offData["y"]) {
                            offsets.push_back(v.get<float>());
                        }
                    }
                }
            }
            animations_[name] = std::move(info);
        }
    }

    loaded_ = true;
    LOG_INFO("PaperDoll", "Loaded catalog: %s (frame %dx%d, %zu bodies, %zu anim)",
             path.c_str(), frameWidth_, frameHeight_, bodies_.size(), animations_.size());
    return true;
}

bool PaperDollCatalog::save(const std::string& path) const {
    nlohmann::json root;

    // Frame size
    root["frameSize"] = { frameWidth_, frameHeight_ };

    // Bodies
    root["bodies"] = nlohmann::json::object();
    for (auto& [gender, p] : bodies_) {
        root["bodies"][gender] = pathsToJson(p);
    }

    // Hairstyles
    root["hairstyles"] = nlohmann::json::object();
    for (auto& [gender, vec] : hairstyles_) {
        auto& gj = root["hairstyles"][gender] = nlohmann::json::object();
        for (auto& [name, p] : vec) {
            gj[name] = pathsToJson(p);
        }
    }

    // Equipment (use ordered categories)
    root["equipment"] = nlohmann::json::object();
    for (auto& cat : equipmentCategories_) {
        auto it = equipment_.find(cat);
        if (it == equipment_.end()) continue;
        auto& cj = root["equipment"][cat] = nlohmann::json::object();
        for (auto& [style, p] : it->second) {
            cj[style] = pathsToJson(p);
        }
    }

    // Animations (use ordered names)
    root["animations"] = nlohmann::json::object();
    for (auto& animName : animationNames_) {
        auto it = animations_.find(animName);
        if (it == animations_.end()) continue;
        auto& info = it->second;
        auto& aj = root["animations"][animName];
        aj["startFrame"] = info.startFrame;
        aj["frameCount"] = info.frameCount;
        aj["frameRate"]  = info.frameRate;
        aj["loop"]       = info.loop;
        if (info.hitFrame >= 0) {
            aj["hitFrame"] = info.hitFrame;
        }
        aj["layerOffsets"] = nlohmann::json::object();
        for (auto& [layer, offsets] : info.layerOffsetsY) {
            aj["layerOffsets"][layer] = { {"y", offsets} };
        }
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        LOG_ERROR("PaperDoll", "Failed to write catalog: %s", path.c_str());
        return false;
    }
    file << root.dump(2) << "\n";
    file.close();

    LOG_INFO("PaperDoll", "Saved catalog: %s", path.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Texture loading helper
// ---------------------------------------------------------------------------

SpriteSet PaperDollCatalog::loadSpriteSet(const SpritePaths& paths) const {
    SpriteSet set;
    if (!paths.front.empty()) set.front = TextureCache::instance().load(paths.front);
    if (!paths.back.empty())  set.back  = TextureCache::instance().load(paths.back);
    if (!paths.side.empty())  set.side  = TextureCache::instance().load(paths.side);
    return set;
}

// ---------------------------------------------------------------------------
// Body
// ---------------------------------------------------------------------------

SpriteSet PaperDollCatalog::getBody(const std::string& gender) const {
    auto it = bodies_.find(gender);
    if (it == bodies_.end()) return {};
    return loadSpriteSet(it->second);
}

SpritePaths PaperDollCatalog::getBodyPaths(const std::string& gender) const {
    auto it = bodies_.find(gender);
    if (it == bodies_.end()) return {};
    return it->second;
}

void PaperDollCatalog::setBodyPath(const std::string& gender, const std::string& direction,
                                   const std::string& path) {
    auto& p = bodies_[gender];
    if (direction == "front")      p.front = path;
    else if (direction == "back")  p.back  = path;
    else if (direction == "side")  p.side  = path;
}

// ---------------------------------------------------------------------------
// Hairstyles
// ---------------------------------------------------------------------------

SpriteSet PaperDollCatalog::getHairstyle(const std::string& gender,
                                          const std::string& name) const {
    auto git = hairstyles_.find(gender);
    if (git == hairstyles_.end()) return {};
    for (auto& [n, paths] : git->second) {
        if (n == name) return loadSpriteSet(paths);
    }
    return {};
}

SpritePaths PaperDollCatalog::getHairstylePaths(const std::string& gender,
                                                 const std::string& name) const {
    auto git = hairstyles_.find(gender);
    if (git == hairstyles_.end()) return {};
    for (auto& [n, paths] : git->second) {
        if (n == name) return paths;
    }
    return {};
}

std::vector<std::string> PaperDollCatalog::getHairstyleNames(const std::string& gender) const {
    std::vector<std::string> names;
    auto git = hairstyles_.find(gender);
    if (git == hairstyles_.end()) return names;
    names.reserve(git->second.size());
    for (auto& [n, _] : git->second) {
        names.push_back(n);
    }
    return names;
}

size_t PaperDollCatalog::getHairstyleCount(const std::string& gender) const {
    auto git = hairstyles_.find(gender);
    if (git == hairstyles_.end()) return 0;
    return git->second.size();
}

std::string PaperDollCatalog::getHairstyleNameByIndex(const std::string& gender,
                                                       uint8_t index) const {
    auto git = hairstyles_.find(gender);
    if (git == hairstyles_.end()) return {};
    if (index >= git->second.size()) return {};
    return git->second[index].first;
}

void PaperDollCatalog::addHairstyle(const std::string& gender, const std::string& name) {
    auto& vec = hairstyles_[gender];
    // Don't add duplicates
    for (auto& [n, _] : vec) {
        if (n == name) return;
    }
    vec.emplace_back(name, SpritePaths{});
}

void PaperDollCatalog::setHairstylePath(const std::string& gender, const std::string& name,
                                         const std::string& direction, const std::string& path) {
    auto git = hairstyles_.find(gender);
    if (git == hairstyles_.end()) return;
    for (auto& [n, p] : git->second) {
        if (n == name) {
            if (direction == "front")      p.front = path;
            else if (direction == "back")  p.back  = path;
            else if (direction == "side")  p.side  = path;
            return;
        }
    }
}

void PaperDollCatalog::removeHairstyle(const std::string& gender, const std::string& name) {
    auto git = hairstyles_.find(gender);
    if (git == hairstyles_.end()) return;
    auto& vec = git->second;
    vec.erase(std::remove_if(vec.begin(), vec.end(),
        [&](const std::pair<std::string, SpritePaths>& entry) {
            return entry.first == name;
        }), vec.end());
}

// ---------------------------------------------------------------------------
// Equipment
// ---------------------------------------------------------------------------

SpriteSet PaperDollCatalog::getEquipment(const std::string& category,
                                          const std::string& style) const {
    auto cit = equipment_.find(category);
    if (cit == equipment_.end()) return {};
    for (auto& [s, paths] : cit->second) {
        if (s == style) return loadSpriteSet(paths);
    }
    return {};
}

SpritePaths PaperDollCatalog::getEquipmentPaths(const std::string& category,
                                                 const std::string& style) const {
    auto cit = equipment_.find(category);
    if (cit == equipment_.end()) return {};
    for (auto& [s, paths] : cit->second) {
        if (s == style) return paths;
    }
    return {};
}

std::vector<std::string> PaperDollCatalog::getEquipmentCategories() const {
    return equipmentCategories_;
}

std::vector<std::string> PaperDollCatalog::getEquipmentStyles(const std::string& category) const {
    std::vector<std::string> styles;
    auto cit = equipment_.find(category);
    if (cit == equipment_.end()) return styles;
    styles.reserve(cit->second.size());
    for (auto& [s, _] : cit->second) {
        styles.push_back(s);
    }
    return styles;
}

void PaperDollCatalog::addEquipmentStyle(const std::string& category, const std::string& style) {
    // Ensure category is tracked in ordered list
    if (std::find(equipmentCategories_.begin(), equipmentCategories_.end(), category)
            == equipmentCategories_.end()) {
        equipmentCategories_.push_back(category);
    }
    auto& vec = equipment_[category];
    for (auto& [s, _] : vec) {
        if (s == style) return;
    }
    vec.emplace_back(style, SpritePaths{});
}

void PaperDollCatalog::setEquipmentPath(const std::string& category, const std::string& style,
                                         const std::string& direction, const std::string& path) {
    auto cit = equipment_.find(category);
    if (cit == equipment_.end()) return;
    for (auto& [s, p] : cit->second) {
        if (s == style) {
            if (direction == "front")      p.front = path;
            else if (direction == "back")  p.back  = path;
            else if (direction == "side")  p.side  = path;
            return;
        }
    }
}

void PaperDollCatalog::removeEquipmentStyle(const std::string& category,
                                             const std::string& style) {
    auto cit = equipment_.find(category);
    if (cit == equipment_.end()) return;
    auto& vec = cit->second;
    vec.erase(std::remove_if(vec.begin(), vec.end(),
        [&](const std::pair<std::string, SpritePaths>& entry) {
            return entry.first == style;
        }), vec.end());
    // If category is now empty, optionally keep it (editor may still want it listed)
}

// ---------------------------------------------------------------------------
// Animations
// ---------------------------------------------------------------------------

AnimInfo PaperDollCatalog::getAnimation(const std::string& name) const {
    auto it = animations_.find(name);
    if (it == animations_.end()) return {};
    return it->second;
}

std::vector<std::string> PaperDollCatalog::getAnimationNames() const {
    return animationNames_;
}

float PaperDollCatalog::getLayerOffsetY(const std::string& animName, const std::string& layer,
                                         int frameIndex) const {
    auto ait = animations_.find(animName);
    if (ait == animations_.end()) return 0.0f;
    auto lit = ait->second.layerOffsetsY.find(layer);
    if (lit == ait->second.layerOffsetsY.end()) return 0.0f;
    if (frameIndex < 0 || frameIndex >= static_cast<int>(lit->second.size())) return 0.0f;
    return lit->second[frameIndex];
}

} // namespace fate
