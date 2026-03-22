#include "engine/ui/ui_manager.h"
#include "engine/ui/widgets/panel.h"
#include "engine/ui/widgets/label.h"
#include "engine/ui/widgets/button.h"
#include "engine/ui/widgets/text_input.h"
#include "engine/ui/widgets/scroll_view.h"
#include "engine/ui/widgets/progress_bar.h"
#include "engine/ui/widgets/slot.h"
#include "engine/ui/widgets/slot_grid.h"
#include "engine/ui/widgets/window.h"
#include "engine/ui/widgets/tab_container.h"
#include "engine/ui/widgets/tooltip.h"
#include "engine/ui/widgets/dpad.h"
#include "engine/ui/widgets/skill_arc.h"
#include "engine/ui/widgets/player_info_block.h"
#include "engine/ui/widgets/target_frame.h"
#include "engine/ui/widgets/exp_bar.h"
#include "engine/ui/widgets/menu_button_row.h"
#include "engine/ui/widgets/chat_ticker.h"
#include "engine/ui/widgets/left_sidebar.h"
#include "engine/ui/widgets/inventory_panel.h"
#include "engine/ui/widgets/status_panel.h"
#include "engine/ui/ui_data_binding.h"
#include "engine/core/logger.h"
#include "engine/input/input.h"
#include "engine/render/sdf_text.h"
#include <nlohmann/json.hpp>
#include <SDL.h>
#include <fstream>
#include <algorithm>
#include <functional>

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
        bool ok = loadScreenFromString(screenId, content);
        if (ok) {
            hotReload_.watchFile(filepath);
            screenFilePaths_[screenId] = filepath;
        }
        return ok;
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

void UIManager::update(float dt) {
    // Tooltip hover tracking
    if (hoveredNode_ && hoveredNode_->properties.count("tooltip")) {
        if (tooltipTarget_ != hoveredNode_) {
            tooltipTarget_ = hoveredNode_;
            tooltipHoverTime_ = 0.0f;
            tooltip_.setVisible(false);
        }
        tooltipHoverTime_ += dt;
        if (tooltipHoverTime_ >= TOOLTIP_DELAY && !tooltip_.visible()) {
            tooltip_.tooltipText = hoveredNode_->properties.at("tooltip");
            Vec2 mousePos = Input::instance().mousePosition();
            auto& sdf = SDFText::instance();
            Vec2 ts = sdf.measure(tooltip_.tooltipText, 12.0f);
            float tw = ts.x + 12.0f;
            float th = ts.y + 8.0f;
            float tx = mousePos.x + 12.0f;
            float ty = mousePos.y + 16.0f;
            // Clamp to screen
            if (tx + tw > screenWidth_) tx = screenWidth_ - tw;
            if (ty + th > screenHeight_) ty = mousePos.y - th - 4.0f;
            UIAnchor a;
            a.preset = AnchorPreset::TopLeft;
            a.offset = {tx, ty};
            a.size    = {tw, th};
            tooltip_.setAnchor(a);
            tooltip_.computeLayout({0.0f, 0.0f, screenWidth_, screenHeight_});
            tooltip_.setVisible(true);
        }
    } else {
        tooltip_.setVisible(false);
        tooltipTarget_ = nullptr;
        tooltipHoverTime_ = 0.0f;
    }

    // Resolve data bindings on all screen nodes
    for (auto& [id, root] : screens_) {
        std::function<void(UINode*)> resolveBindings = [&](UINode* node) {
            if (!node->visible()) return;
            if (node->dataBindings.count("text")) {
                auto* label = dynamic_cast<Label*>(node);
                if (label) label->text = dataBinding_.resolve(node->dataBindings["text"]);
            }
            if (node->dataBindings.count("value")) {
                auto* bar = dynamic_cast<ProgressBar*>(node);
                if (bar) {
                    std::string val = dataBinding_.getValue(node->dataBindings["value"]);
                    if (!val.empty()) bar->value = std::stof(val);
                }
            }
            if (node->dataBindings.count("max")) {
                auto* bar = dynamic_cast<ProgressBar*>(node);
                if (bar) {
                    std::string val = dataBinding_.getValue(node->dataBindings["max"]);
                    if (!val.empty()) bar->maxValue = std::stof(val);
                }
            }
            for (auto& child : node->children()) resolveBindings(child.get());
        };
        resolveBindings(root.get());
    }

    // Hot-reload check every 0.5s
    hotReloadTimer_ += dt;
    if (hotReloadTimer_ >= 0.5f) {
        hotReloadTimer_ = 0.0f;
        auto changed = hotReload_.checkForChanges();
        for (auto& path : changed) {
            LOG_INFO("UI", "Hot-reloading: %s", path.c_str());
            loadScreen(path);
        }
    }
}

void UIManager::computeLayout(float screenWidth, float screenHeight) {
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;
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
    if (tooltip_.visible()) tooltip_.render(batch, text);
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
    else if (type == "button") {
        auto button = std::make_unique<Button>(id);
        button->text = j.value("text", "");
        button->icon = j.value("icon", "");
        node = std::move(button);
    }
    else if (type == "text_input") {
        auto input = std::make_unique<TextInput>(id);
        input->text = j.value("text", "");
        input->placeholder = j.value("placeholder", "");
        input->maxLength = j.value("maxLength", 0);
        node = std::move(input);
    }
    else if (type == "scroll_view") {
        auto sv = std::make_unique<ScrollView>(id);
        sv->scrollSpeed = j.value("scrollSpeed", 30.0f);
        node = std::move(sv);
    }
    else if (type == "progress_bar") {
        auto bar = std::make_unique<ProgressBar>(id);
        bar->value = j.value("value", 0.0f);
        bar->maxValue = j.value("maxValue", 100.0f);
        if (j.contains("fillColor") && j["fillColor"].is_array()) {
            auto& c = j["fillColor"];
            bar->fillColor = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                             c.size() >= 4 ? c[3].get<float>() : 1.0f};
        }
        bar->showText = j.value("showText", false);
        std::string dir = j.value("direction", "left_to_right");
        if (dir == "right_to_left") bar->direction = BarDirection::RightToLeft;
        else if (dir == "bottom_to_top") bar->direction = BarDirection::BottomToTop;
        else if (dir == "top_to_bottom") bar->direction = BarDirection::TopToBottom;
        node = std::move(bar);
    }
    else if (type == "slot_grid") {
        auto grid = std::make_unique<SlotGrid>(id);
        grid->columns = j.value("columns", 5);
        grid->rows = j.value("rows", 3);
        grid->slotSize = j.value("slotSize", 48.0f);
        grid->slotPadding = j.value("slotPadding", 4.0f);
        grid->acceptsDragType = j.value("acceptsDrag", "");
        grid->generateSlots();
        node = std::move(grid);
    }
    else if (type == "slot") {
        auto slot = std::make_unique<Slot>(id);
        slot->itemId = j.value("itemId", "");
        slot->quantity = j.value("quantity", 0);
        slot->icon = j.value("icon", "");
        slot->slotType = j.value("slotType", "item");
        slot->acceptsDragType = j.value("acceptsDrag", "");
        node = std::move(slot);
    }
    else if (type == "window") {
        auto win = std::make_unique<Window>(id);
        win->title = j.value("title", "");
        win->closeable = j.value("closeable", true);
        win->resizable = j.value("resizable", false);
        win->minimizable = j.value("minimizable", false);
        win->titleBarHeight = j.value("titleBarHeight", 28.0f);
        node = std::move(win);
    }
    else if (type == "tab_container") {
        auto tc = std::make_unique<TabContainer>(id);
        tc->tabHeight = j.value("tabHeight", 30.0f);
        tc->activeTab = j.value("activeTab", 0);
        if (j.contains("tabs") && j["tabs"].is_array()) {
            for (auto& tab : j["tabs"]) {
                tc->tabLabels_.push_back(tab.get<std::string>());
            }
        }
        node = std::move(tc);
    }
    else if (type == "dpad") {
        auto dpad = std::make_unique<DPad>(id);
        dpad->dpadSize       = j.value("dpadSize", 140.0f);
        dpad->deadZoneRadius = j.value("deadZoneRadius", 15.0f);
        dpad->opacity        = j.value("opacity", 0.6f);
        node = std::move(dpad);
    }
    else if (type == "skill_arc") {
        auto arc = std::make_unique<SkillArc>(id);
        arc->attackButtonSize = j.value("attackButtonSize", 80.0f);
        arc->slotSize         = j.value("slotSize", 52.0f);
        arc->arcRadius        = j.value("arcRadius", 70.0f);
        arc->slotCount        = j.value("slotCount", 5);
        arc->startAngleDeg    = j.value("startAngleDeg", 210.0f);
        arc->endAngleDeg      = j.value("endAngleDeg", 330.0f);
        node = std::move(arc);
    }
    else if (type == "player_info_block") {
        auto pib = std::make_unique<PlayerInfoBlock>(id);
        pib->portraitSize = j.value("portraitSize", 48.0f);
        pib->barWidth     = j.value("barWidth", 120.0f);
        pib->barHeight    = j.value("barHeight", 16.0f);
        node = std::move(pib);
    }
    else if (type == "target_frame") {
        auto tf = std::make_unique<TargetFrame>(id);
        node = std::move(tf);
    }
    else if (type == "exp_bar") {
        auto eb = std::make_unique<EXPBar>(id);
        node = std::move(eb);
    }
    else if (type == "menu_button_row") {
        auto mbr = std::make_unique<MenuButtonRow>(id);
        mbr->buttonSize = j.value("buttonSize", 36.0f);
        mbr->spacing    = j.value("spacing", 8.0f);
        if (j.contains("labels") && j["labels"].is_array()) {
            for (auto& l : j["labels"]) mbr->labels.push_back(l.get<std::string>());
        }
        node = std::move(mbr);
    }
    else if (type == "chat_ticker") {
        auto ct = std::make_unique<ChatTicker>(id);
        ct->scrollSpeed = j.value("scrollSpeed", 40.0f);
        node = std::move(ct);
    }
    else if (type == "left_sidebar") {
        auto sb = std::make_unique<LeftSidebar>(id);
        sb->buttonSize = j.value("buttonSize", 40.0f);
        sb->spacing = j.value("spacing", 8.0f);
        if (j.contains("labels") && j["labels"].is_array())
            for (auto& l : j["labels"]) sb->panelLabels.push_back(l.get<std::string>());
        sb->activePanel = j.value("activePanel", "");
        node = std::move(sb);
    }
    else if (type == "inventory_panel") {
        auto ip = std::make_unique<InventoryPanel>(id);
        ip->gridColumns = j.value("gridColumns", 4);
        ip->gridRows = j.value("gridRows", 5);
        ip->slotSize = j.value("slotSize", 40.0f);
        node = std::move(ip);
    }
    else if (type == "status_panel") {
        auto sp = std::make_unique<StatusPanel>(id);
        node = std::move(sp);
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

        if (a.contains("offsetPercent") && a["offsetPercent"].is_array() && a["offsetPercent"].size() >= 2) {
            anchor.offsetPercent.x = a["offsetPercent"][0].get<float>();
            anchor.offsetPercent.y = a["offsetPercent"][1].get<float>();
        }

        if (a.contains("sizePercent") && a["sizePercent"].is_array() && a["sizePercent"].size() >= 2) {
            anchor.sizePercent.x = a["sizePercent"][0].get<float>();
            anchor.sizePercent.y = a["sizePercent"][1].get<float>();
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
// Input routing
// ---------------------------------------------------------------------------

void UIManager::handleInput() {
    auto& input = Input::instance();
    Vec2 mousePos = input.mousePosition();

    updateHover(mousePos);

    // Handle draggable panel/window movement while mouse is held
    if (pressedNode_ && input.isMouseDown(SDL_BUTTON_LEFT)) {
        auto* panel = dynamic_cast<Panel*>(pressedNode_);
        if (panel && panel->isDragging_) {
            Vec2 newPos = {mousePos.x - panel->dragOffset_.x,
                          mousePos.y - panel->dragOffset_.y};
            panel->anchor().offset = newPos;
        }
        if (!panel || !panel->isDragging_) {
            auto* window = dynamic_cast<Window*>(pressedNode_);
            if (window && window->isDragging()) {
                Vec2 newPos = {mousePos.x - window->dragOffset().x,
                              mousePos.y - window->dragOffset().y};
                window->anchor().offset = newPos;
            }
        }
    }

    if (input.isMousePressed(SDL_BUTTON_LEFT)) {
        handlePress(mousePos);
    }
    if (input.isMouseReleased(SDL_BUTTON_LEFT)) {
        handleRelease(mousePos);
    }
}

void UIManager::updateHover(const Vec2& mousePos) {
    UINode* newHover = hitTest(mousePos);
    if (newHover != hoveredNode_) {
        if (hoveredNode_) hoveredNode_->onHoverExit();
        hoveredNode_ = newHover;
        if (hoveredNode_) hoveredNode_->onHoverEnter();
    }
}

void UIManager::handlePress(const Vec2& mousePos) {
    UINode* target = hitTest(mousePos);

    if (target != focusedNode_) {
        if (focusedNode_) focusedNode_->onFocusLost();
        focusedNode_ = target;
        if (focusedNode_) focusedNode_->onFocusGained();
    }

    pressedNode_ = target;
    pressStartPos_ = mousePos;

    if (target) {
        Vec2 localPos = {mousePos.x - target->computedRect().x,
                        mousePos.y - target->computedRect().y};
        target->onPress(localPos);
    }
}

void UIManager::handleRelease(const Vec2& mousePos) {
    if (pressedNode_) {
        Vec2 localPos = {mousePos.x - pressedNode_->computedRect().x,
                        mousePos.y - pressedNode_->computedRect().y};
        pressedNode_->onRelease(localPos);
    }

    if (dragPayload_.active) {
        UINode* dropTarget = hitTest(mousePos);
        if (dropTarget && dropTarget->acceptsDrop(dragPayload_)) {
            dropTarget->onDrop(dragPayload_);
        }
        dragPayload_.clear();
    }

    pressedNode_ = nullptr;
}

void UIManager::handleKeyInput(int scancode, bool pressed) {
    if (focusedNode_) {
        focusedNode_->onKeyInput(scancode, pressed);
    }
}

void UIManager::handleTextInput(const std::string& text) {
    if (focusedNode_) {
        focusedNode_->onTextInput(text);
    }
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
