#include "engine/ui/ui_manager.h"
#include "engine/ui/widgets/panel.h"
#include "engine/ui/widgets/label.h"
#include "engine/core/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>

namespace fate {

// ---------------------------------------------------------------------------
// Screen loading
// ---------------------------------------------------------------------------

bool UIManager::loadScreen(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR("UI", "UIManager: failed to open screen file: %s", filepath.c_str());
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    try {
        auto data = nlohmann::json::parse(content);
        if (!data.contains("screen") || !data["screen"].is_string()) {
            LOG_ERROR("UI", "UIManager: screen file missing 'screen' id: %s", filepath.c_str());
            return false;
        }
        std::string screenId = data["screen"].get<std::string>();
        return loadScreenFromString(screenId, content);
    }
    catch (const nlohmann::json::exception& e) {
        LOG_ERROR("UI", "UIManager: JSON parse error in %s: %s", filepath.c_str(), e.what());
        return false;
    }
}

bool UIManager::loadScreenFromString(const std::string& screenId, const std::string& jsonStr) {
    try {
        auto data = nlohmann::json::parse(jsonStr);

        if (!data.contains("root") || !data["root"].is_object()) {
            LOG_ERROR("UI", "UIManager: screen '%s' missing 'root' object", screenId.c_str());
            return false;
        }

        auto root = parseNode(data["root"]);
        if (!root) {
            LOG_ERROR("UI", "UIManager: failed to parse root node for screen '%s'", screenId.c_str());
            return false;
        }

        applyThemeStyles(root.get());

        // Track insertion order (replace if already present)
        if (screens_.find(screenId) == screens_.end()) {
            screenOrder_.push_back(screenId);
        }
        screens_[screenId] = std::move(root);

        LOG_INFO("UI", "UIManager: loaded screen '%s'", screenId.c_str());
        return true;
    }
    catch (const nlohmann::json::exception& e) {
        LOG_ERROR("UI", "UIManager: JSON parse error for screen '%s': %s", screenId.c_str(), e.what());
        return false;
    }
}

void UIManager::unloadScreen(const std::string& screenId) {
    screens_.erase(screenId);
    screenOrder_.erase(
        std::remove(screenOrder_.begin(), screenOrder_.end(), screenId),
        screenOrder_.end());
}

UINode* UIManager::getScreen(const std::string& screenId) {
    auto it = screens_.find(screenId);
    if (it != screens_.end()) return it->second.get();
    return nullptr;
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

bool UIManager::loadTheme(const std::string& filepath) {
    return theme_.loadFromFile(filepath);
}

// ---------------------------------------------------------------------------
// Per-frame
// ---------------------------------------------------------------------------

void UIManager::update(float /*dt*/) {
    // Placeholder for future animation / data-binding updates
}

void UIManager::computeLayout(float screenWidth, float screenHeight) {
    Rect screenRect{0.0f, 0.0f, screenWidth, screenHeight};
    for (const auto& id : screenOrder_) {
        auto it = screens_.find(id);
        if (it != screens_.end() && it->second->visible()) {
            it->second->computeLayout(screenRect);
        }
    }
}

void UIManager::render(SpriteBatch& batch, SDFText& text) {
    for (const auto& id : screenOrder_) {
        auto it = screens_.find(id);
        if (it != screens_.end() && it->second->visible()) {
            it->second->render(batch, text);
        }
    }
}

// ---------------------------------------------------------------------------
// Hit-testing (reverse z-order: back-to-front, children last-to-first)
// ---------------------------------------------------------------------------

static UINode* hitTestNode(UINode* node, const Vec2& point) {
    if (!node->visible()) return nullptr;

    // Check children in reverse order (front-most first)
    const auto& children = node->children();
    for (int i = static_cast<int>(children.size()) - 1; i >= 0; --i) {
        if (UINode* hit = hitTestNode(children[i].get(), point)) {
            return hit;
        }
    }

    // Check self
    if (node->hitTest(point)) return node;
    return nullptr;
}

UINode* UIManager::hitTest(const Vec2& point) {
    // Iterate screens back-to-front (last screen in order = topmost)
    for (int i = static_cast<int>(screenOrder_.size()) - 1; i >= 0; --i) {
        auto it = screens_.find(screenOrder_[i]);
        if (it == screens_.end()) continue;
        if (UINode* hit = hitTestNode(it->second.get(), point)) {
            return hit;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// JSON parsing
// ---------------------------------------------------------------------------

AnchorPreset UIManager::parsePreset(const std::string& name) {
    if (name == "TopLeft")      return AnchorPreset::TopLeft;
    if (name == "TopCenter")    return AnchorPreset::TopCenter;
    if (name == "TopRight")     return AnchorPreset::TopRight;
    if (name == "CenterLeft")   return AnchorPreset::CenterLeft;
    if (name == "Center")       return AnchorPreset::Center;
    if (name == "CenterRight")  return AnchorPreset::CenterRight;
    if (name == "BottomLeft")   return AnchorPreset::BottomLeft;
    if (name == "BottomCenter") return AnchorPreset::BottomCenter;
    if (name == "BottomRight")  return AnchorPreset::BottomRight;
    if (name == "StretchX")     return AnchorPreset::StretchX;
    if (name == "StretchY")     return AnchorPreset::StretchY;
    if (name == "StretchAll")   return AnchorPreset::StretchAll;

    LOG_WARN("UI", "UIManager: unknown anchor preset '%s', defaulting to TopLeft", name.c_str());
    return AnchorPreset::TopLeft;
}

std::unique_ptr<UINode> UIManager::parseNode(const nlohmann::json& j) {
    std::string id   = j.value("id", "");
    std::string type = j.value("type", "node");

    // --- Create appropriate widget ---
    std::unique_ptr<UINode> node;

    if (type == "panel") {
        auto panel = std::make_unique<Panel>(id);
        panel->title     = j.value("title", "");
        panel->draggable = j.value("draggable", false);
        panel->closeable = j.value("closeable", false);
        node = std::move(panel);
    }
    else if (type == "label") {
        auto label = std::make_unique<Label>(id);
        label->text     = j.value("text", "");
        label->wordWrap = j.value("wordWrap", false);

        std::string alignStr = j.value("align", "left");
        if (alignStr == "center")      label->align = TextAlign::Center;
        else if (alignStr == "right")  label->align = TextAlign::Right;
        else                           label->align = TextAlign::Left;

        node = std::move(label);
    }
    else {
        node = std::make_unique<UINode>(id, type);
    }

    // --- Anchor ---
    if (j.contains("anchor") && j["anchor"].is_object()) {
        const auto& a = j["anchor"];
        UIAnchor anchor;

        if (a.contains("preset") && a["preset"].is_string()) {
            anchor.preset = parsePreset(a["preset"].get<std::string>());
        }

        if (a.contains("offset") && a["offset"].is_array() && a["offset"].size() >= 2) {
            anchor.offset.x = a["offset"][0].get<float>();
            anchor.offset.y = a["offset"][1].get<float>();
        }

        if (a.contains("size") && a["size"].is_array() && a["size"].size() >= 2) {
            anchor.size.x = a["size"][0].get<float>();
            anchor.size.y = a["size"][1].get<float>();
        }

        if (a.contains("margin") && a["margin"].is_array() && a["margin"].size() >= 4) {
            // x=top, y=right, z=bottom, w=left
            anchor.margin.x = a["margin"][0].get<float>();
            anchor.margin.y = a["margin"][1].get<float>();
            anchor.margin.z = a["margin"][2].get<float>();
            anchor.margin.w = a["margin"][3].get<float>();
        }

        if (a.contains("padding") && a["padding"].is_array() && a["padding"].size() >= 4) {
            anchor.padding.x = a["padding"][0].get<float>();
            anchor.padding.y = a["padding"][1].get<float>();
            anchor.padding.z = a["padding"][2].get<float>();
            anchor.padding.w = a["padding"][3].get<float>();
        }

        node->setAnchor(anchor);
    }

    // --- Style name ---
    if (j.contains("style") && j["style"].is_string()) {
        node->setStyleName(j["style"].get<std::string>());
    }

    // --- Z-order ---
    if (j.contains("zOrder") && j["zOrder"].is_number_integer()) {
        node->setZOrder(j["zOrder"].get<int>());
    }

    // --- Visible ---
    if (j.contains("visible") && j["visible"].is_boolean()) {
        node->setVisible(j["visible"].get<bool>());
    }

    // --- Event bindings ---
    if (j.contains("events") && j["events"].is_object()) {
        for (auto& [evName, handler] : j["events"].items()) {
            if (handler.is_string()) {
                node->eventBindings[evName] = handler.get<std::string>();
            }
        }
    }

    // --- Data bindings ---
    if (j.contains("bind") && j["bind"].is_object()) {
        for (auto& [prop, path] : j["bind"].items()) {
            if (path.is_string()) {
                node->dataBindings[prop] = path.get<std::string>();
            }
        }
    }

    // --- Custom properties ---
    if (j.contains("properties") && j["properties"].is_object()) {
        for (auto& [key, val] : j["properties"].items()) {
            if (val.is_string()) {
                node->properties[key] = val.get<std::string>();
            }
        }
    }

    // --- Children (recursive) ---
    if (j.contains("children") && j["children"].is_array()) {
        for (const auto& childJson : j["children"]) {
            auto child = parseNode(childJson);
            if (child) {
                node->addChild(std::move(child));
            }
        }
    }

    return node;
}

// ---------------------------------------------------------------------------
// Theme style application (recursive)
// ---------------------------------------------------------------------------

void UIManager::applyThemeStyles(UINode* node) {
    if (!node) return;

    const std::string& name = node->styleName();
    if (!name.empty() && theme_.hasStyle(name)) {
        node->setResolvedStyle(theme_.getStyle(name));
    }

    for (size_t i = 0; i < node->childCount(); ++i) {
        applyThemeStyles(node->childAt(i));
    }
}

} // namespace fate
