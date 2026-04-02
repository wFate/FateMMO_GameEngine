#include "engine/ui/ui_manager.h"
#include "engine/ui/ui_safe_area.h"
#ifdef FATE_HAS_GAME
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
#include "engine/ui/widgets/skill_panel.h"
#include "engine/ui/widgets/character_select_screen.h"
#include "engine/ui/widgets/character_creation_screen.h"
#include "engine/ui/widgets/chat_panel.h"
#include "engine/ui/widgets/trade_window.h"
#include "engine/ui/widgets/party_frame.h"
#include "engine/ui/widgets/guild_panel.h"
#include "engine/ui/widgets/npc_dialogue_panel.h"
#include "engine/ui/widgets/shop_panel.h"
#include "engine/ui/widgets/bank_panel.h"
#include "engine/ui/widgets/teleporter_panel.h"
#include "engine/ui/widgets/arena_panel.h"
#include "engine/ui/widgets/battlefield_panel.h"
#include "engine/ui/widgets/leaderboard_panel.h"
#include "engine/ui/widgets/pet_panel.h"
#include "engine/ui/widgets/crafting_panel.h"
#include "engine/ui/widgets/collection_panel.h"
#include "engine/ui/widgets/player_context_menu.h"
#include "engine/ui/widgets/image_box.h"
#include "engine/ui/widgets/buff_bar.h"
#include "engine/ui/widgets/boss_hp_bar.h"
#include "engine/ui/widgets/confirm_dialog.h"
#include "engine/ui/widgets/notification_toast.h"
#include "engine/ui/widgets/checkbox.h"
#include "engine/ui/widgets/login_screen.h"
#include "engine/ui/widgets/death_overlay.h"
#include "engine/ui/widgets/fate_status_bar.h"
#include "engine/ui/widgets/menu_tab_bar.h"
#include "engine/ui/widgets/costume_panel.h"
#include "engine/ui/widgets/settings_panel.h"
#include "engine/ui/widgets/fps_counter.h"
#include "engine/ui/widgets/loading_panel.h"
#include "engine/ui/widgets/invite_prompt_panel.h"
#endif // FATE_HAS_GAME
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

        // Clear stale focus/hover/press pointers before replacing the old screen
        // (the old widget tree is about to be destroyed by the assignment below)
        auto existingIt = screens_.find(screenId);
        if (existingIt != screens_.end()) {
            auto* oldRoot = existingIt->second.get();
            auto isChild = [&](UINode* n) -> bool {
                for (UINode* p = n; p; p = p->parent())
                    if (p == oldRoot) return true;
                return false;
            };
            if (focusedNode_ && isChild(focusedNode_))  focusedNode_ = nullptr;
            if (hoveredNode_ && isChild(hoveredNode_))  hoveredNode_ = nullptr;
            if (pressedNode_ && isChild(pressedNode_))  pressedNode_ = nullptr;
        } else {
            screenOrder_.push_back(screenId);
        }
        screens_[screenId] = std::move(root);

        LOG_INFO("UI", "UIManager: loaded screen '%s'", screenId.c_str());

        // Notify listeners (e.g. UI editor, game code) so they can revalidate stale pointers
        for (auto& fn : screenReloadListeners_) fn(screenId);

        return true;
    }
    catch (const nlohmann::json::exception& e) {
        LOG_ERROR("UI", "UIManager: JSON parse error for screen '%s': %s", screenId.c_str(), e.what());
        return false;
    }
}

void UIManager::unloadScreen(const std::string& screenId) {
    // Clear focus/hover/press if they point into the screen being removed
    auto it = screens_.find(screenId);
    if (it != screens_.end()) {
        auto* root = it->second.get();
        auto isChild = [&](UINode* n) -> bool {
            for (UINode* p = n; p; p = p->parent())
                if (p == root) return true;
            return false;
        };
        if (focusedNode_ && isChild(focusedNode_))  focusedNode_ = nullptr;
        if (hoveredNode_ && isChild(hoveredNode_))   hoveredNode_ = nullptr;
        if (pressedNode_ && isChild(pressedNode_))   pressedNode_ = nullptr;
    }
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

#ifdef FATE_HAS_GAME
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
#endif // FATE_HAS_GAME

    // Hot-reload check every 0.5s (suppressed briefly after editor save)
    if (hotReloadSuppressTimer_ > 0.0f) {
        hotReloadSuppressTimer_ -= dt;
        hotReload_.checkForChanges(); // drain pending changes so they don't fire later
    } else {
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
}

void UIManager::computeLayout(float screenWidth, float screenHeight) {
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;
    float scale = screenHeight / UI_REFERENCE_HEIGHT;
    Rect screenRect{0.0f, 0.0f, screenWidth, screenHeight};
    for (const auto& id : screenOrder_) {
        auto it = screens_.find(id);
        if (it != screens_.end() && it->second->visible()) {
            Rect rootRect = screenRect;

            // Safe area: shrink root rect by platform insets
            if (it->second->anchor().useSafeArea) {
                auto insets = getPlatformSafeArea();
                rootRect.x += insets.left;
                rootRect.y += insets.top;
                rootRect.w -= (insets.left + insets.right);
                rootRect.h -= (insets.top + insets.bottom);
            }

            // Aspect ratio cap: letterbox ultrawide displays
            float maxAR = it->second->anchor().maxAspectRatio;
            if (maxAR > 0.0f && rootRect.h > 0.0f) {
                float currentAR = rootRect.w / rootRect.h;
                if (currentAR > maxAR) {
                    float targetW = rootRect.h * maxAR;
                    float excess = rootRect.w - targetW;
                    rootRect.x += excess * 0.5f;
                    rootRect.w = targetW;
                }
            }

            it->second->computeLayout(rootRect, scale);
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

#ifdef FATE_HAS_GAME
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
        input->masked = j.value("masked", false);
        node = std::move(input);
    }
    else if (type == "scroll_view") {
        auto sv = std::make_unique<ScrollView>(id);
        sv->scrollSpeed   = j.value("scrollSpeed", 30.0f);
        sv->contentHeight = j.value("contentHeight", 0.0f);
        node = std::move(sv);
    }
    else if (type == "image_box") {
        auto img = std::make_unique<ImageBox>(id);
        img->textureKey = j.value("textureKey", "");
        std::string fm = j.value("fitMode", "fit");
        img->fitMode = (fm == "stretch") ? ImageFitMode::Stretch : ImageFitMode::Fit;
        if (j.contains("tint") && j["tint"].is_array()) {
            auto& c = j["tint"];
            img->tint = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                         c.size() >= 4 ? c[3].get<float>() : 1.0f};
        }
        if (j.contains("sourceRect") && j["sourceRect"].is_array() && j["sourceRect"].size() >= 4) {
            auto& r = j["sourceRect"];
            img->sourceRect = {r[0].get<float>(), r[1].get<float>(),
                               r[2].get<float>(), r[3].get<float>()};
        }
        node = std::move(img);
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
        arc->pickUpButtonSize = j.value("pickUpButtonSize", 60.0f);
        arc->slotSize         = j.value("slotSize", 60.0f);
        arc->arcRadius        = j.value("arcRadius", 180.0f);
        arc->slotCount        = j.value("slotCount", 5);
        arc->startAngleDeg    = j.value("startAngleDeg", 290.0f);
        arc->endAngleDeg      = j.value("endAngleDeg", 190.0f);
        if (j.contains("skillArcOffset")) {
            auto& sao = j["skillArcOffset"];
            arc->skillArcOffset = {sao[0].get<float>(), sao[1].get<float>()};
        }
        // Individual button offsets (relative to widget center, in unscaled pixels)
        if (j.contains("attackOffset")) {
            auto& ao = j["attackOffset"];
            arc->attackOffset = {ao[0].get<float>(), ao[1].get<float>()};
        }
        if (j.contains("pickUpOffset")) {
            auto& po = j["pickUpOffset"];
            arc->pickUpOffset = {po[0].get<float>(), po[1].get<float>()};
        }
        // SlotArc (page selector arc)
        arc->slotArcRadius   = j.value("slotArcRadius", 50.0f);
        arc->slotArcStartDeg = j.value("slotArcStartDeg", 290.0f);
        arc->slotArcEndDeg   = j.value("slotArcEndDeg", 190.0f);
        if (j.contains("slotArcOffset")) {
            auto& so = j["slotArcOffset"];
            arc->slotArcOffset = {so[0].get<float>(), so[1].get<float>()};
        }
        node = std::move(arc);
    }
    else if (type == "player_info_block") {
        auto pib = std::make_unique<PlayerInfoBlock>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        pib->portraitOffset = readVec2("portraitOffset", pib->portraitOffset);
        pib->barOffset      = readVec2("barOffset",      pib->barOffset);
        pib->levelOffset    = readVec2("levelOffset",    pib->levelOffset);
        pib->goldOffset     = readVec2("goldOffset",     pib->goldOffset);
        pib->portraitSize = j.value("portraitSize", 48.0f);
        pib->barWidth     = j.value("barWidth", 120.0f);
        pib->barHeight    = j.value("barHeight", 16.0f);
        pib->barSpacing   = j.value("barSpacing", 2.0f);
        pib->overlayFontSize = j.value("overlayFontSize", 12.0f);
        pib->goldFontSize    = j.value("goldFontSize", 11.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        pib->portraitFillColor   = readColor("portraitFillColor",   pib->portraitFillColor);
        pib->portraitBorderColor = readColor("portraitBorderColor", pib->portraitBorderColor);
        pib->barBgColor          = readColor("barBgColor",          pib->barBgColor);
        pib->barBorderColor      = readColor("barBorderColor",      pib->barBorderColor);
        pib->hpFillColor         = readColor("hpFillColor",         pib->hpFillColor);
        pib->mpFillColor         = readColor("mpFillColor",         pib->mpFillColor);
        pib->textShadowColor     = readColor("textShadowColor",     pib->textShadowColor);
        pib->goldTextColor       = readColor("goldTextColor",       pib->goldTextColor);
        node = std::move(pib);
    }
    else if (type == "target_frame") {
        auto tf = std::make_unique<TargetFrame>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        tf->nameOffset  = readVec2("nameOffset",  tf->nameOffset);
        tf->hpBarOffset = readVec2("hpBarOffset", tf->hpBarOffset);
        tf->hpTextOffset = readVec2("hpTextOffset", tf->hpTextOffset);
        tf->targetName = j.value("targetName", "");
        tf->nameFontSize = j.value("nameFontSize", 11.0f);
        tf->hpFontSize   = j.value("hpFontSize", 9.0f);
        tf->barPadding   = j.value("barPadding", 6.0f);
        tf->barHeight    = j.value("barHeight", 12.0f);
        tf->nameTopPad   = j.value("nameTopPad", 4.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        tf->hpBarBgColor = readColor("hpBarBgColor", tf->hpBarBgColor);
        tf->hpFillColor  = readColor("hpFillColor",  tf->hpFillColor);
        node = std::move(tf);
    }
    else if (type == "exp_bar") {
        auto eb = std::make_unique<EXPBar>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        eb->textOffset = readVec2("textOffset", eb->textOffset);
        eb->xp        = j.value("xp", 0.0f);
        eb->xpToLevel = j.value("xpToLevel", 1.0f);
        eb->fontSize  = j.value("fontSize", 9.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        eb->fillColor   = readColor("fillColor",   eb->fillColor);
        eb->shadowColor = readColor("shadowColor", eb->shadowColor);
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
    else if (type == "menu_tab_bar") {
        auto mtb = std::make_unique<MenuTabBar>(id);
        mtb->activeTab = j.value("activeTab", 0);
        mtb->tabSize   = j.value("tabSize", 50.0f);
        mtb->arrowSize = j.value("arrowSize", 28.0f);
        mtb->tabFontSize   = j.value("tabFontSize", 9.0f);
        mtb->arrowFontSize = j.value("arrowFontSize", 12.0f);
        mtb->borderWidth   = j.value("borderWidth", 1.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 4) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>()};
            }
            return def;
        };
        mtb->activeTabBg       = readColor("activeTabBg",       mtb->activeTabBg);
        mtb->inactiveTabBg     = readColor("inactiveTabBg",     mtb->inactiveTabBg);
        mtb->arrowBg           = readColor("arrowBg",           mtb->arrowBg);
        mtb->borderColor       = readColor("borderColor",       mtb->borderColor);
        mtb->activeTextColor   = readColor("activeTextColor",   mtb->activeTextColor);
        mtb->inactiveTextColor = readColor("inactiveTextColor", mtb->inactiveTextColor);
        mtb->arrowTextColor    = readColor("arrowTextColor",    mtb->arrowTextColor);
        mtb->highlightColor    = readColor("highlightColor",    mtb->highlightColor);
        if (j.contains("tabLabels") && j["tabLabels"].is_array()) {
            mtb->tabLabels.clear();
            for (auto& l : j["tabLabels"]) mtb->tabLabels.push_back(l.get<std::string>());
        }
        node = std::move(mtb);
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
        ip->gridColumns    = j.value("gridColumns", 4);
        ip->gridRows       = j.value("gridRows", 4);
        ip->slotSize       = j.value("slotSize", 40.0f);
        ip->equipSlotSize  = j.value("equipSlotSize", 36.0f);
        ip->dollWidthRatio = j.value("dollWidthRatio", 0.45f);
        ip->contentPadding = j.value("contentPadding", 4.0f);
        ip->currencyHeight = j.value("currencyHeight", 30.0f);
        ip->platOffsetX    = j.value("platOffsetX", 0.0f);
        ip->platOffsetY    = j.value("platOffsetY", 14.0f);
        ip->gridPadding    = j.value("gridPadding", 4.0f);
        ip->dollCenterY    = j.value("dollCenterY", 0.45f);
        ip->characterScale = j.value("characterScale", 5.0f);
        if (j.contains("equipLayout") && j["equipLayout"].is_array()) {
            auto& arr = j["equipLayout"];
            for (int i = 0; i < InventoryPanel::NUM_EQUIP_SLOTS && i < static_cast<int>(arr.size()); ++i) {
                ip->equipLayout[i].offsetX = arr[i].value("offsetX", ip->equipLayout[i].offsetX);
                ip->equipLayout[i].offsetY = arr[i].value("offsetY", ip->equipLayout[i].offsetY);
                ip->equipLayout[i].sizeMul = arr[i].value("sizeMul", ip->equipLayout[i].sizeMul);
            }
        }
        // Font sizes
        ip->itemFontSize          = j.value("itemFontSize", 14.0f);
        ip->quantityFontSize      = j.value("quantityFontSize", 9.0f);
        ip->currencyFontSize      = j.value("currencyFontSize", 11.0f);
        ip->currencyLabelFontSize = j.value("currencyLabelFontSize", 10.0f);
        ip->equipLabelFontSize    = j.value("equipLabelFontSize", 7.0f);

        // Colors
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        // Slot appearance
        ip->slotFilledBgColor    = readColor("slotFilledBgColor",    ip->slotFilledBgColor);
        ip->slotEmptyBgColor     = readColor("slotEmptyBgColor",     ip->slotEmptyBgColor);
        ip->slotEmptyBorderColor = readColor("slotEmptyBorderColor", ip->slotEmptyBorderColor);
        ip->slotBorderWidth      = j.value("slotBorderWidth", 1.0f);

        // Paper doll inset
        ip->dollInsetBgColor     = readColor("dollInsetBgColor",     ip->dollInsetBgColor);
        ip->dollInsetBorderColor = readColor("dollInsetBorderColor", ip->dollInsetBorderColor);
        ip->dollInsetBorderW     = j.value("dollInsetBorderW", 1.0f);
        ip->equipLabelGap        = j.value("equipLabelGap", 1.0f);

        ip->quantityColor   = readColor("quantityColor",   ip->quantityColor);
        ip->itemTextColor   = readColor("itemTextColor",   ip->itemTextColor);
        ip->equipLabelColor = readColor("equipLabelColor", ip->equipLabelColor);
        ip->goldLabelColor  = readColor("goldLabelColor",  ip->goldLabelColor);
        ip->goldValueColor  = readColor("goldValueColor",  ip->goldValueColor);
        ip->platLabelColor  = readColor("platLabelColor",  ip->platLabelColor);
        ip->platValueColor  = readColor("platValueColor",  ip->platValueColor);

        // Tooltip layout
        ip->tooltipWidth        = j.value("tooltipWidth", 200.0f);
        ip->tooltipPadding      = j.value("tooltipPadding", 8.0f);
        ip->tooltipOffset       = j.value("tooltipOffset", 4.0f);
        ip->tooltipShadowOffset = j.value("tooltipShadowOffset", 2.0f);
        ip->tooltipLineSpacing  = j.value("tooltipLineSpacing", 2.0f);
        ip->tooltipBorderWidth  = j.value("tooltipBorderWidth", 1.5f);
        ip->tooltipSepHeight    = j.value("tooltipSepHeight", 1.5f);

        // Tooltip font sizes
        ip->tooltipNameFontSize  = j.value("tooltipNameFontSize", 13.0f);
        ip->tooltipStatFontSize  = j.value("tooltipStatFontSize", 11.0f);
        ip->tooltipLevelFontSize = j.value("tooltipLevelFontSize", 10.0f);

        // Tooltip colors
        ip->tooltipBgColor      = readColor("tooltipBgColor",     ip->tooltipBgColor);
        ip->tooltipBorderColor  = readColor("tooltipBorderColor", ip->tooltipBorderColor);
        ip->tooltipShadowColor  = readColor("tooltipShadowColor", ip->tooltipShadowColor);
        ip->tooltipStatColor    = readColor("tooltipStatColor",   ip->tooltipStatColor);
        ip->tooltipSepColor     = readColor("tooltipSepColor",    ip->tooltipSepColor);
        ip->tooltipLevelColor   = readColor("tooltipLevelColor",  ip->tooltipLevelColor);

        // Rarity colors
        ip->rarityCommonColor    = readColor("rarityCommonColor",    ip->rarityCommonColor);
        ip->rarityUncommonColor  = readColor("rarityUncommonColor",  ip->rarityUncommonColor);
        ip->rarityRareColor      = readColor("rarityRareColor",      ip->rarityRareColor);
        ip->rarityEpicColor      = readColor("rarityEpicColor",      ip->rarityEpicColor);
        ip->rarityLegendaryColor = readColor("rarityLegendaryColor", ip->rarityLegendaryColor);

        // Close button
        ip->closeBtnRadius   = j.value("closeBtnRadius", 12.0f);
        ip->closeBtnOffset   = j.value("closeBtnOffset", 6.0f);
        ip->closeBtnBorderW  = j.value("closeBtnBorderW", 1.5f);
        ip->closeBtnFontSize = j.value("closeBtnFontSize", 12.0f);
        ip->closeBtnBgColor     = readColor("closeBtnBgColor",     ip->closeBtnBgColor);
        ip->closeBtnBorderColor = readColor("closeBtnBorderColor", ip->closeBtnBorderColor);
        ip->closeBtnTextColor   = readColor("closeBtnTextColor",   ip->closeBtnTextColor);

        // Context menu
        ip->ctxMenuWidth      = j.value("ctxMenuWidth", 130.0f);
        ip->ctxMenuItemHeight = j.value("ctxMenuItemHeight", 28.0f);
        ip->ctxMenuPadding    = j.value("ctxMenuPadding", 4.0f);
        ip->ctxMenuBorderW    = j.value("ctxMenuBorderW", 1.5f);
        ip->ctxMenuFontSize   = j.value("ctxMenuFontSize", 13.0f);
        ip->ctxMenuTextPadX   = j.value("ctxMenuTextPadX", 10.0f);
        ip->ctxMenuBgColor       = readColor("ctxMenuBgColor",       ip->ctxMenuBgColor);
        ip->ctxMenuBorderColor   = readColor("ctxMenuBorderColor",   ip->ctxMenuBorderColor);
        ip->ctxMenuTextColor     = readColor("ctxMenuTextColor",     ip->ctxMenuTextColor);
        ip->ctxMenuDisabledColor = readColor("ctxMenuDisabledColor", ip->ctxMenuDisabledColor);

        // Panel colors
        ip->panelBgColor     = readColor("panelBgColor",     ip->panelBgColor);
        ip->panelBorderColor = readColor("panelBorderColor", ip->panelBorderColor);
        ip->panelBorderWidth = j.value("panelBorderWidth", 2.0f);

        // Section visibility
        ip->equipAreaVisible = j.value("equipAreaVisible", true);
        ip->gridAreaVisible  = j.value("gridAreaVisible",  true);

        // Icon atlas
        ip->iconAtlasKey  = j.value("iconAtlasKey", std::string{});
        ip->iconAtlasCols = j.value("iconAtlasCols", 8);
        ip->iconAtlasRows = j.value("iconAtlasRows", 4);

        node = std::move(ip);
    }
    else if (type == "status_panel") {
        auto sp = std::make_unique<StatusPanel>(id);

        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2) {
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            }
            return def;
        };
        sp->titleOffset    = readVec2("titleOffset",    sp->titleOffset);
        sp->nameOffset     = readVec2("nameOffset",     sp->nameOffset);
        sp->levelOffset    = readVec2("levelOffset",    sp->levelOffset);
        sp->statGridOffset = readVec2("statGridOffset", sp->statGridOffset);
        sp->factionOffset  = readVec2("factionOffset",  sp->factionOffset);

        sp->titleFontSize     = j.value("titleFontSize", 16.0f);
        sp->nameFontSize      = j.value("nameFontSize", 15.0f);
        sp->levelFontSize     = j.value("levelFontSize", 11.0f);
        sp->statLabelFontSize = j.value("statLabelFontSize", 9.0f);
        sp->statValueFontSize = j.value("statValueFontSize", 11.0f);
        sp->factionFontSize   = j.value("factionFontSize", 9.0f);

        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        sp->titleColor     = readColor("titleColor",     sp->titleColor);
        sp->nameColor      = readColor("nameColor",      sp->nameColor);
        sp->levelColor     = readColor("levelColor",     sp->levelColor);
        sp->statLabelColor = readColor("statLabelColor", sp->statLabelColor);
        sp->factionColor   = readColor("factionColor",   sp->factionColor);

        node = std::move(sp);
    }
    else if (type == "skill_panel") {
        auto skp = std::make_unique<SkillPanel>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        skp->titleOffset        = readVec2("titleOffset",        skp->titleOffset);
        skp->tabOffset          = readVec2("tabOffset",          skp->tabOffset);
        skp->pointsBadgeOffset  = readVec2("pointsBadgeOffset",  skp->pointsBadgeOffset);
        skp->skillsHeaderOffset = readVec2("skillsHeaderOffset", skp->skillsHeaderOffset);
        skp->activeSetPage    = j.value("activeSetPage", 0);
        skp->splitRatio       = j.value("splitRatio", 0.42f);
        skp->gridColumns      = j.value("gridColumns", 4);
        skp->circleRadiusMul  = j.value("circleRadiusMul", 0.28f);
        skp->dotSize          = j.value("dotSize", 4.0f);
        skp->dotSpacing       = j.value("dotSpacing", 6.0f);
        skp->headerHeight     = j.value("headerHeight", 28.0f);
        skp->borderWidth      = j.value("borderWidth", 3.0f);
        skp->contentPadding   = j.value("contentPadding", 4.0f);
        skp->gridMargin       = j.value("gridMargin", 4.0f);
        skp->dividerWidth     = j.value("dividerWidth", 1.5f);
        skp->ringWidthNormal    = j.value("ringWidthNormal", 1.5f);
        skp->ringWidthSelected  = j.value("ringWidthSelected", 2.5f);
        skp->tabRadius        = j.value("tabRadius", 14.0f);
        skp->tabSpacingMul    = j.value("tabSpacingMul", 2.5f);
        skp->wheelStartDeg    = j.value("wheelStartDeg", 210.0f);
        skp->wheelEndDeg      = j.value("wheelEndDeg", 330.0f);
        skp->wheelSlotSizeMul = j.value("wheelSlotSizeMul", 0.40f);
        skp->closeBtnRadius   = j.value("closeBtnRadius", 12.0f);
        skp->closeBtnOffset   = j.value("closeBtnOffset", 6.0f);
        skp->closeBtnBorderW  = j.value("closeBtnBorderW", 1.5f);
        skp->closeBtnFontSize = j.value("closeBtnFontSize", 12.0f);
        skp->ptsBadgeRadius   = j.value("ptsBadgeRadius", 12.0f);
        skp->ptsFontSize      = j.value("ptsFontSize", 8.0f);
        skp->slotNameFontSize = j.value("slotNameFontSize", 7.0f);
        skp->titleFontSize    = j.value("titleFontSize", 16.0f);
        skp->headerFontSize   = j.value("headerFontSize", 13.0f);
        skp->nameFontSize     = j.value("nameFontSize", 9.0f);
        skp->tabFontSize      = j.value("tabFontSize", 11.0f);
        skp->pointsFontSize   = j.value("pointsFontSize", 10.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 4)
                return {j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>(), j[key][3].get<float>()};
            return def;
        };
        skp->titleColor       = readColor("titleColor",      skp->titleColor);
        skp->headerColor      = readColor("headerColor",     skp->headerColor);
        skp->skillBgUnlocked  = readColor("skillBgUnlocked", skp->skillBgUnlocked);
        skp->skillBgLocked    = readColor("skillBgLocked",   skp->skillBgLocked);
        skp->ringSelected     = readColor("ringSelected",    skp->ringSelected);
        skp->ringNormal       = readColor("ringNormal",      skp->ringNormal);
        skp->dotActivated     = readColor("dotActivated",    skp->dotActivated);
        skp->dotUnlocked      = readColor("dotUnlocked",     skp->dotUnlocked);
        skp->dotLocked        = readColor("dotLocked",       skp->dotLocked);
        skp->nameUnlocked     = readColor("nameUnlocked",    skp->nameUnlocked);
        skp->nameLocked       = readColor("nameLocked",      skp->nameLocked);
        skp->pointsBadge      = readColor("pointsBadge",     skp->pointsBadge);
        skp->pointsEmpty      = readColor("pointsEmpty",     skp->pointsEmpty);
        skp->dividerColor     = readColor("dividerColor",    skp->dividerColor);
        skp->ptsBadgeRingColor = readColor("ptsBadgeRingColor", skp->ptsBadgeRingColor);
        skp->ptsTextColor     = readColor("ptsTextColor",    skp->ptsTextColor);
        skp->tabBgActive      = readColor("tabBgActive",     skp->tabBgActive);
        skp->tabBgInactive    = readColor("tabBgInactive",   skp->tabBgInactive);
        skp->tabRingActive    = readColor("tabRingActive",   skp->tabRingActive);
        skp->tabRingInactive  = readColor("tabRingInactive", skp->tabRingInactive);
        skp->tabTextActive    = readColor("tabTextActive",   skp->tabTextActive);
        skp->tabTextInactive  = readColor("tabTextInactive", skp->tabTextInactive);
        skp->closeBtnBgColor     = readColor("closeBtnBgColor",     skp->closeBtnBgColor);
        skp->closeBtnBorderColor = readColor("closeBtnBorderColor", skp->closeBtnBorderColor);
        skp->closeBtnTextColor   = readColor("closeBtnTextColor",   skp->closeBtnTextColor);
        node = std::move(skp);
    }
    else if (type == "character_select_screen") {
        auto css = std::make_unique<CharacterSelectScreen>(id);
        css->selectedSlot     = j.value("selectedSlot", 0);
        // Layout
        css->slotCircleSize    = j.value("slotCircleSize", 52.0f);
        css->entryButtonWidth  = j.value("entryButtonWidth", 120.0f);
        css->slotSpacing       = j.value("slotSpacing", 12.0f);
        css->slotBottomMargin  = j.value("slotBottomMargin", 32.0f);
        css->selectedRingWidth = j.value("selectedRingWidth", 3.0f);
        css->normalRingWidth   = j.value("normalRingWidth", 2.0f);
        css->displayWidthRatio  = j.value("displayWidthRatio", 0.45f);
        css->displayHeightRatio = j.value("displayHeightRatio", 0.55f);
        css->displayTopRatio    = j.value("displayTopRatio", 0.08f);
        css->displayBorderWidth = j.value("displayBorderWidth", 2.0f);
        css->nameBgHeight      = j.value("nameBgHeight", 28.0f);
        css->nameBgWidthRatio  = j.value("nameBgWidthRatio", 0.7f);
        css->nameTextY         = j.value("nameTextY", 8.0f);
        css->classTextY        = j.value("classTextY", 42.0f);
        css->levelTextY        = j.value("levelTextY", 60.0f);
        css->previewScale      = j.value("previewScale", 3.0f);
        css->previewCenterYRatio = j.value("previewCenterYRatio", 0.55f);
        css->entryBtnBorderWidth = j.value("entryBtnBorderWidth", 1.5f);
        css->swapDeleteScale   = j.value("swapDeleteScale", 0.75f);
        css->swapDeleteMargin  = j.value("swapDeleteMargin", 20.0f);
        css->swapBtnRingWidth  = j.value("swapBtnRingWidth", 1.5f);
        css->deleteBtnRingWidth = j.value("deleteBtnRingWidth", 1.5f);
        // Dialog layout
        css->dialogWidth          = j.value("dialogWidth", 400.0f);
        css->dialogHeight         = j.value("dialogHeight", 250.0f);
        css->dialogBorderWidth    = j.value("dialogBorderWidth", 2.0f);
        css->dialogInputHeight    = j.value("dialogInputHeight", 28.0f);
        css->dialogInputPadding   = j.value("dialogInputPadding", 60.0f);
        css->dialogInputBorderWidth = j.value("dialogInputBorderWidth", 1.0f);
        css->dialogBtnWidth       = j.value("dialogBtnWidth", 100.0f);
        css->dialogBtnHeight      = j.value("dialogBtnHeight", 30.0f);
        css->dialogBtnMargin      = j.value("dialogBtnMargin", 15.0f);
        // Font sizes
        css->nameFontSize         = j.value("nameFontSize", 16.0f);
        css->classFontSize        = j.value("classFontSize", 13.0f);
        css->levelFontSize        = j.value("levelFontSize", 12.0f);
        css->emptyPromptFontSize  = j.value("emptyPromptFontSize", 14.0f);
        css->plusFontSize          = j.value("plusFontSize", 20.0f);
        css->slotLevelFontSize    = j.value("slotLevelFontSize", 10.0f);
        css->entryFontSize        = j.value("entryFontSize", 15.0f);
        css->swapFontSize         = j.value("swapFontSize", 10.0f);
        css->deleteFontSize       = j.value("deleteFontSize", 10.0f);
        css->dialogTitleFontSize  = j.value("dialogTitleFontSize", 16.0f);
        css->dialogPromptFontSize = j.value("dialogPromptFontSize", 12.0f);
        css->dialogRefNameFontSize = j.value("dialogRefNameFontSize", 14.0f);
        css->dialogInputFontSize  = j.value("dialogInputFontSize", 13.0f);
        css->dialogBtnFontSize    = j.value("dialogBtnFontSize", 13.0f);
        // Colors
        auto readColor = [&](const std::string& key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 4)
                return {j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>(), j[key][3].get<float>()};
            return def;
        };
        css->backgroundColor    = readColor("backgroundColor",    {0.05f, 0.05f, 0.08f, 1.0f});
        css->displayBgColor     = readColor("displayBgColor",     {0.12f, 0.10f, 0.08f, 0.85f});
        css->displayBorderColor = readColor("displayBorderColor", {0.55f, 0.48f, 0.35f, 0.9f});
        css->nameBgColor        = readColor("nameBgColor",        {0.08f, 0.08f, 0.12f, 0.9f});
        css->nameColor          = readColor("nameColor",          {1.0f, 0.92f, 0.75f, 1.0f});
        css->classColor         = readColor("classColor",         {0.75f, 0.75f, 0.9f, 1.0f});
        css->levelColor         = readColor("levelColor",         {0.6f, 0.9f, 0.6f, 1.0f});
        css->emptyPromptColor   = readColor("emptyPromptColor",   {0.45f, 0.45f, 0.55f, 1.0f});
        css->emptySlotColor     = readColor("emptySlotColor",     {0.22f, 0.22f, 0.28f, 1.0f});
        css->filledSlotColor    = readColor("filledSlotColor",    {0.12f, 0.12f, 0.18f, 1.0f});
        css->selectedRingColor  = readColor("selectedRingColor",  {0.95f, 0.8f, 0.2f, 1.0f});
        css->emptyRingColor     = readColor("emptyRingColor",     {0.38f, 0.38f, 0.48f, 1.0f});
        css->plusColor           = readColor("plusColor",           {0.5f, 0.5f, 0.6f, 1.0f});
        css->slotLevelColor     = readColor("slotLevelColor",     {0.75f, 0.75f, 0.85f, 1.0f});
        css->entryBtnColor      = readColor("entryBtnColor",      {0.2f, 0.6f, 0.9f, 1.0f});
        css->entryBtnBorderColor = readColor("entryBtnBorderColor", {0.5f, 0.8f, 1.0f, 1.0f});
        css->swapBtnColor       = readColor("swapBtnColor",       {0.2f, 0.45f, 0.65f, 1.0f});
        css->swapBtnRingColor   = readColor("swapBtnRingColor",   {0.4f, 0.65f, 0.85f, 1.0f});
        css->deleteBtnColor     = readColor("deleteBtnColor",     {0.5f, 0.15f, 0.15f, 1.0f});
        css->deleteBtnRingColor = readColor("deleteBtnRingColor", {0.85f, 0.3f, 0.3f, 1.0f});
        css->dialogOverlayColor = readColor("dialogOverlayColor", {0.0f, 0.0f, 0.0f, 0.7f});
        css->dialogBgColor      = readColor("dialogBgColor",      {0.12f, 0.10f, 0.14f, 0.98f});
        css->dialogBorderColor  = readColor("dialogBorderColor",  {0.7f, 0.3f, 0.3f, 0.9f});
        css->dialogTitleColor   = readColor("dialogTitleColor",   {0.95f, 0.35f, 0.35f, 1.0f});
        css->dialogPromptColor  = readColor("dialogPromptColor",  {0.75f, 0.75f, 0.85f, 1.0f});
        css->dialogRefNameColor = readColor("dialogRefNameColor", {1.0f, 0.92f, 0.75f, 1.0f});
        css->dialogInputBgColor = readColor("dialogInputBgColor", {0.08f, 0.08f, 0.12f, 1.0f});
        css->dialogInputBorderColor = readColor("dialogInputBorderColor", {0.5f, 0.5f, 0.6f, 1.0f});
        css->dialogConfirmColor = readColor("dialogConfirmColor", {0.7f, 0.2f, 0.2f, 1.0f});
        css->dialogConfirmDisabledColor = readColor("dialogConfirmDisabledColor", {0.3f, 0.15f, 0.15f, 0.6f});
        css->dialogConfirmDisabledTextColor = readColor("dialogConfirmDisabledTextColor", {0.5f, 0.5f, 0.5f, 0.7f});
        css->dialogCancelColor  = readColor("dialogCancelColor",  {0.25f, 0.35f, 0.55f, 1.0f});
        node = std::move(css);
    }
    else if (type == "character_creation_screen") {
        auto ccs = std::make_unique<CharacterCreationScreen>(id);
        ccs->leftPanelRatio      = j.value("leftPanelRatio", 0.45f);
        ccs->previewScale        = j.value("previewScale", 3.0f);
        ccs->headerY             = j.value("headerY", 20.0f);
        ccs->genderRowY          = j.value("genderRowY", 55.0f);
        ccs->genderBtnWidth      = j.value("genderBtnWidth", 70.0f);
        ccs->genderBtnHeight     = j.value("genderBtnHeight", 26.0f);
        ccs->hairstyleRowY       = j.value("hairstyleRowY", 90.0f);
        ccs->hairstyleBtnSize    = j.value("hairstyleBtnSize", 30.0f);
        ccs->classRowY           = j.value("classRowY", 140.0f);
        ccs->classBtnSize        = j.value("classBtnSize", 50.0f);
        ccs->factionRowY         = j.value("factionRowY", 300.0f);
        ccs->factionRadius       = j.value("factionRadius", 22.0f);
        ccs->genderGap           = j.value("genderGap", 8.0f);
        ccs->genderBorderWidth   = j.value("genderBorderWidth", 1.0f);
        ccs->genderSelBorderWidth = j.value("genderSelBorderWidth", 2.0f);
        ccs->hairstyleGap        = j.value("hairstyleGap", 10.0f);
        ccs->hairstyleRingWidth  = j.value("hairstyleRingWidth", 1.5f);
        ccs->hairstyleSelRingWidth = j.value("hairstyleSelRingWidth", 2.5f);
        ccs->hairstyleLabelGap   = j.value("hairstyleLabelGap", 2.0f);
        ccs->classGap            = j.value("classGap", 16.0f);
        ccs->classRingWidth      = j.value("classRingWidth", 1.5f);
        ccs->classSelRingWidth   = j.value("classSelRingWidth", 3.0f);
        ccs->classNameGap        = j.value("classNameGap", 10.0f);
        ccs->classDescGap        = j.value("classDescGap", 6.0f);
        ccs->classDescPadX       = j.value("classDescPadX", 10.0f);
        ccs->factionGap          = j.value("factionGap", 12.0f);
        ccs->factionSelScale     = j.value("factionSelScale", 1.2f);
        ccs->factionSelRingWidth = j.value("factionSelRingWidth", 2.5f);
        ccs->factionNameGap      = j.value("factionNameGap", 3.0f);
        ccs->backBtnRadius       = j.value("backBtnRadius", 18.0f);
        ccs->backBtnOffsetX      = j.value("backBtnOffsetX", 14.0f);
        ccs->backBtnOffsetY      = j.value("backBtnOffsetY", 14.0f);
        ccs->backBtnRingWidth    = j.value("backBtnRingWidth", 1.5f);
        ccs->nameFieldY          = j.value("nameFieldY", 370.0f);
        ccs->nameFieldHeight     = j.value("nameFieldHeight", 36.0f);
        ccs->nameFieldPadX       = j.value("nameFieldPadX", 20.0f);
        ccs->nameFieldBorderWidth = j.value("nameFieldBorderWidth", 1.5f);
        ccs->nameFieldLabelGap   = j.value("nameFieldLabelGap", 2.0f);
        ccs->nameFieldTextPad    = j.value("nameFieldTextPad", 8.0f);
        ccs->nameFieldCursorWidth = j.value("nameFieldCursorWidth", 1.0f);
        ccs->nameFieldCursorPad  = j.value("nameFieldCursorPad", 8.0f);
        ccs->nextBtnHeight       = j.value("nextBtnHeight", 40.0f);
        ccs->nextBtnBottomMargin = j.value("nextBtnBottomMargin", 60.0f);
        ccs->nextBtnPadX         = j.value("nextBtnPadX", 20.0f);
        ccs->nextBtnBorderWidth  = j.value("nextBtnBorderWidth", 1.5f);
        ccs->statusGap           = j.value("statusGap", 8.0f);
        ccs->dividerWidth        = j.value("dividerWidth", 3.0f);
        ccs->headerFontSize         = j.value("headerFontSize", 18.0f);
        ccs->classFontSize          = j.value("classFontSize", 17.0f);
        ccs->classInitialFontSize   = j.value("classInitialFontSize", 16.0f);
        ccs->descFontSize           = j.value("descFontSize", 11.0f);
        ccs->buttonFontSize         = j.value("buttonFontSize", 12.0f);
        ccs->labelFontSize          = j.value("labelFontSize", 10.0f);
        ccs->nameLabelFontSize      = j.value("nameLabelFontSize", 11.0f);
        ccs->nameInputFontSize      = j.value("nameInputFontSize", 14.0f);
        ccs->nextBtnFontSize        = j.value("nextBtnFontSize", 15.0f);
        ccs->statusFontSize         = j.value("statusFontSize", 12.0f);
        ccs->factionInitialFontSize = j.value("factionInitialFontSize", 11.0f);
        ccs->factionNameFontSize    = j.value("factionNameFontSize", 10.0f);
        ccs->backBtnFontSize        = j.value("backBtnFontSize", 14.0f);
        auto readColor = [&](const std::string& key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 4)
                return {j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>(), j[key][3].get<float>()};
            return def;
        };
        ccs->backgroundColor           = readColor("backgroundColor",           {0.04f, 0.04f, 0.07f, 1.0f});
        ccs->leftPanelColor            = readColor("leftPanelColor",            {0.07f, 0.07f, 0.10f, 1.0f});
        ccs->headerColor               = readColor("headerColor",               {1.0f, 0.92f, 0.75f, 1.0f});
        ccs->selectedColor             = readColor("selectedColor",             {0.95f, 0.80f, 0.20f, 1.0f});
        ccs->selectedBgColor           = readColor("selectedBgColor",           {0.20f, 0.18f, 0.08f, 1.0f});
        ccs->unselectedBgColor         = readColor("unselectedBgColor",         {0.12f, 0.12f, 0.18f, 1.0f});
        ccs->unselectedBorderColor     = readColor("unselectedBorderColor",     {0.35f, 0.35f, 0.50f, 1.0f});
        ccs->unselectedTextColor       = readColor("unselectedTextColor",       {0.60f, 0.60f, 0.70f, 1.0f});
        ccs->labelColor                = readColor("labelColor",                {0.55f, 0.55f, 0.65f, 1.0f});
        ccs->descColor                 = readColor("descColor",                 {0.70f, 0.70f, 0.80f, 1.0f});
        ccs->nameFieldBgColor          = readColor("nameFieldBgColor",          {0.10f, 0.10f, 0.15f, 1.0f});
        ccs->nameFieldFocusBgColor     = readColor("nameFieldFocusBgColor",     {0.15f, 0.15f, 0.22f, 1.0f});
        ccs->nameFieldBorderColor      = readColor("nameFieldBorderColor",      {0.35f, 0.35f, 0.50f, 1.0f});
        ccs->nameFieldFocusBorderColor = readColor("nameFieldFocusBorderColor", {0.6f, 0.5f, 0.3f, 1.0f});
        ccs->nameLabelColor            = readColor("nameLabelColor",            {0.65f, 0.65f, 0.75f, 1.0f});
        ccs->placeholderColor          = readColor("placeholderColor",          {0.40f, 0.40f, 0.50f, 1.0f});
        ccs->nextBtnColor              = readColor("nextBtnColor",              {0.2f, 0.6f, 0.9f, 1.0f});
        ccs->nextBtnBorderColor        = readColor("nextBtnBorderColor",        {0.5f, 0.8f, 1.0f, 1.0f});
        ccs->backBtnColor              = readColor("backBtnColor",              {0.2f, 0.45f, 0.7f, 1.0f});
        ccs->backBtnBorderColor        = readColor("backBtnBorderColor",        {0.4f, 0.65f, 0.9f, 1.0f});
        ccs->errorColor                = readColor("errorColor",                {1.0f, 0.35f, 0.35f, 1.0f});
        ccs->successColor              = readColor("successColor",              {0.35f, 0.95f, 0.45f, 1.0f});
        node = std::move(ccs);
    }
    else if (type == "chat_panel") {
        auto cp = std::make_unique<ChatPanel>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        cp->messageOffset = readVec2("messageOffset", cp->messageOffset);
        cp->inputOffset   = readVec2("inputOffset",   cp->inputOffset);
        cp->channelOffset = readVec2("channelOffset", cp->channelOffset);
        cp->chatIdleLines    = j.value("chatIdleLines", 3);
        cp->fullPanelWidth   = j.value("fullPanelWidth", 1000.0f);
        cp->fullPanelHeight  = j.value("fullPanelHeight", 350.0f);
        cp->inputBarHeight   = j.value("inputBarHeight", 28.0f);
        cp->inputBarWidth    = j.value("inputBarWidth", 0.0f);
        cp->channelBtnWidth  = j.value("channelBtnWidth", 44.0f);
        cp->channelBtnHeight = j.value("channelBtnHeight", 0.0f);
        cp->closeBtnSize     = j.value("closeBtnSize", 20.0f);
        cp->messageFontSize       = j.value("messageFontSize", 11.0f);
        cp->inputFontSize         = j.value("inputFontSize", 11.0f);
        cp->channelLabelFontSize  = j.value("channelLabelFontSize", 9.0f);
        cp->messageLineSpacing    = j.value("messageLineSpacing", 3.0f);
        cp->messageShadowOffset   = j.value("messageShadowOffset", 2.0f);

        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };

        // Channel colors
        cp->colorAll      = readColor("colorAll",     cp->colorAll);
        cp->colorMap      = readColor("colorMap",     cp->colorMap);
        cp->colorGlobal   = readColor("colorGlobal",  cp->colorGlobal);
        cp->colorTrade    = readColor("colorTrade",   cp->colorTrade);
        cp->colorParty    = readColor("colorParty",   cp->colorParty);
        cp->colorGuild    = readColor("colorGuild",   cp->colorGuild);
        cp->colorPrivate  = readColor("colorPrivate", cp->colorPrivate);

        // Faction colors
        cp->factionNoneColor   = readColor("factionNoneColor",   cp->factionNoneColor);
        cp->factionXyrosColor  = readColor("factionXyrosColor",  cp->factionXyrosColor);
        cp->factionFenorColor  = readColor("factionFenorColor",  cp->factionFenorColor);
        cp->factionZethosColor = readColor("factionZethosColor", cp->factionZethosColor);
        cp->factionSolisColor  = readColor("factionSolisColor",  cp->factionSolisColor);

        // Message colors
        cp->messageTextColor   = readColor("messageTextColor",   cp->messageTextColor);
        cp->messageShadowColor = readColor("messageShadowColor", cp->messageShadowColor);

        // Close button colors
        cp->closeBtnBgColor     = readColor("closeBtnBgColor",     cp->closeBtnBgColor);
        cp->closeBtnBorderColor = readColor("closeBtnBorderColor", cp->closeBtnBorderColor);
        cp->closeBtnIconColor   = readColor("closeBtnIconColor",   cp->closeBtnIconColor);

        // Input bar colors
        cp->inputBarBgColor       = readColor("inputBarBgColor",       cp->inputBarBgColor);
        cp->inputFieldBgColor     = readColor("inputFieldBgColor",     cp->inputFieldBgColor);
        cp->inputBorderColor      = readColor("inputBorderColor",      cp->inputBorderColor);
        cp->inputBorderFocusColor = readColor("inputBorderFocusColor", cp->inputBorderFocusColor);
        cp->channelBtnBgColor     = readColor("channelBtnBgColor",     cp->channelBtnBgColor);
        cp->placeholderColor      = readColor("placeholderColor",      cp->placeholderColor);

        node = std::move(cp);
    }
    else if (type == "loading_panel") {
        auto lp = std::make_unique<LoadingPanel>(id);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        lp->barHeight    = j.value("barHeight",    lp->barHeight);
        lp->barPadX      = j.value("barPadX",      lp->barPadX);
        lp->barBottomY   = j.value("barBottomY",   lp->barBottomY);
        lp->nameFontSize = j.value("nameFontSize", lp->nameFontSize);
        lp->pctFontSize  = j.value("pctFontSize",  lp->pctFontSize);
        lp->shadowOffset = j.value("shadowOffset", lp->shadowOffset);
        lp->bgColor      = readColor("bgColor",      lp->bgColor);
        lp->barBgColor   = readColor("barBgColor",   lp->barBgColor);
        lp->barFillColor = readColor("barFillColor", lp->barFillColor);
        lp->nameColor    = readColor("nameColor",    lp->nameColor);
        lp->pctColor     = readColor("pctColor",     lp->pctColor);
        lp->shadowColor  = readColor("shadowColor",  lp->shadowColor);
        node = std::move(lp);
    }
    else if (type == "trade_window") {
        auto tw = std::make_unique<TradeWindow>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        tw->titleOffset      = readVec2("titleOffset",      tw->titleOffset);
        tw->myOfferOffset    = readVec2("myOfferOffset",    tw->myOfferOffset);
        tw->theirOfferOffset = readVec2("theirOfferOffset", tw->theirOfferOffset);
        tw->buttonOffset     = readVec2("buttonOffset",     tw->buttonOffset);
        tw->titleFontSize    = j.value("titleFontSize", 15.0f);
        tw->labelFontSize    = j.value("labelFontSize", 12.0f);
        tw->bodyFontSize     = j.value("bodyFontSize", 11.0f);
        tw->smallFontSize    = j.value("smallFontSize", 9.0f);
        tw->headerHeight     = j.value("headerHeight", 28.0f);
        tw->buttonRowHeight  = j.value("buttonRowHeight", 30.0f);
        tw->padding          = j.value("padding", 6.0f);
        tw->borderWidth      = j.value("borderWidth", 3.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        tw->bgColor        = readColor("bgColor",        tw->bgColor);
        tw->borderColor    = readColor("borderColor",    tw->borderColor);
        tw->titleColor     = readColor("titleColor",     tw->titleColor);
        tw->labelColor     = readColor("labelColor",     tw->labelColor);
        tw->goldColor      = readColor("goldColor",      tw->goldColor);
        tw->acceptBtnColor = readColor("acceptBtnColor", tw->acceptBtnColor);
        tw->cancelBtnColor = readColor("cancelBtnColor", tw->cancelBtnColor);
        tw->dividerColor   = readColor("dividerColor",   tw->dividerColor);
        node = std::move(tw);
    }
    else if (type == "party_frame") {
        auto pf = std::make_unique<PartyFrame>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        pf->nameOffset     = readVec2("nameOffset",     pf->nameOffset);
        pf->levelOffset    = readVec2("levelOffset",    pf->levelOffset);
        pf->portraitOffset = readVec2("portraitOffset", pf->portraitOffset);
        pf->barOffset      = readVec2("barOffset",      pf->barOffset);
        pf->cardWidth   = j.value("cardWidth",   170.0f);
        pf->cardHeight  = j.value("cardHeight",   48.0f);
        pf->cardSpacing = j.value("cardSpacing",   4.0f);
        pf->nameFontSize   = j.value("nameFontSize", 11.0f);
        pf->levelFontSize  = j.value("levelFontSize", 10.0f);
        pf->portraitRadius = j.value("portraitRadius", 10.0f);
        pf->hpBarHeight    = j.value("hpBarHeight", 8.0f);
        pf->mpBarHeight    = j.value("mpBarHeight", 6.0f);
        pf->borderWidth    = j.value("borderWidth", 1.0f);
        pf->portraitPadLeft      = j.value("portraitPadLeft", 6.0f);
        pf->portraitRimWidth     = j.value("portraitRimWidth", 1.5f);
        pf->crownSize            = j.value("crownSize", 5.0f);
        pf->textGapAfterPortrait = j.value("textGapAfterPortrait", 6.0f);
        pf->textPadRight         = j.value("textPadRight", 4.0f);
        pf->namePadTop           = j.value("namePadTop", 6.0f);
        pf->levelPadRight        = j.value("levelPadRight", 5.0f);
        pf->barOffsetY           = j.value("barOffsetY", 13.0f);
        pf->barGap               = j.value("barGap", 2.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        pf->cardBgColor       = readColor("cardBgColor",       pf->cardBgColor);
        pf->cardBorderColor   = readColor("cardBorderColor",   pf->cardBorderColor);
        pf->portraitFillColor = readColor("portraitFillColor", pf->portraitFillColor);
        pf->portraitRimColor  = readColor("portraitRimColor",  pf->portraitRimColor);
        pf->crownColor        = readColor("crownColor",        pf->crownColor);
        pf->nameColor         = readColor("nameColor",         pf->nameColor);
        pf->levelColor        = readColor("levelColor",        pf->levelColor);
        pf->hpBarBgColor      = readColor("hpBarBgColor",      pf->hpBarBgColor);
        pf->hpFillColor       = readColor("hpFillColor",       pf->hpFillColor);
        pf->mpBarBgColor      = readColor("mpBarBgColor",      pf->mpBarBgColor);
        pf->mpFillColor       = readColor("mpFillColor",       pf->mpFillColor);
        node = std::move(pf);
    }
    else if (type == "guild_panel") {
        auto gp = std::make_unique<GuildPanel>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        gp->titleOffset     = readVec2("titleOffset",     gp->titleOffset);
        gp->emblemOffset    = readVec2("emblemOffset",    gp->emblemOffset);
        gp->guildInfoOffset = readVec2("guildInfoOffset", gp->guildInfoOffset);
        gp->rosterOffset    = readVec2("rosterOffset",    gp->rosterOffset);
        gp->titleFontSize        = j.value("titleFontSize", 16.0f);
        gp->guildNameFontSize    = j.value("guildNameFontSize", 14.0f);
        gp->infoFontSize         = j.value("infoFontSize", 11.0f);
        gp->rosterHeaderFontSize = j.value("rosterHeaderFontSize", 9.5f);
        gp->rosterRowFontSize    = j.value("rosterRowFontSize", 10.0f);
        gp->headerHeight         = j.value("headerHeight", 32.0f);
        gp->emblemSize           = j.value("emblemSize", 64.0f);
        gp->closeRadius          = j.value("closeRadius", 11.0f);
        gp->rosterHeaderHeight   = j.value("rosterHeaderHeight", 18.0f);
        gp->rosterRowHeight      = j.value("rosterRowHeight", 22.0f);
        gp->borderWidth          = j.value("borderWidth", 3.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        gp->bgColor              = readColor("bgColor",              gp->bgColor);
        gp->borderColor          = readColor("borderColor",          gp->borderColor);
        gp->titleColor           = readColor("titleColor",           gp->titleColor);
        gp->closeBgColor         = readColor("closeBgColor",         gp->closeBgColor);
        gp->closeBorderColor     = readColor("closeBorderColor",     gp->closeBorderColor);
        gp->dividerColor         = readColor("dividerColor",         gp->dividerColor);
        gp->guildNameColor       = readColor("guildNameColor",       gp->guildNameColor);
        gp->infoColor            = readColor("infoColor",            gp->infoColor);
        gp->rosterHeaderBgColor  = readColor("rosterHeaderBgColor",  gp->rosterHeaderBgColor);
        gp->rosterHeaderTextColor = readColor("rosterHeaderTextColor", gp->rosterHeaderTextColor);
        gp->rosterRowTextColor   = readColor("rosterRowTextColor",   gp->rosterRowTextColor);
        gp->onlineColor          = readColor("onlineColor",          gp->onlineColor);
        gp->offlineColor         = readColor("offlineColor",         gp->offlineColor);
        node = std::move(gp);
    }
    else if (type == "npc_dialogue_panel") {
        auto ndp = std::make_unique<NpcDialoguePanel>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        ndp->titleOffset  = readVec2("titleOffset",  ndp->titleOffset);
        ndp->textOffset   = readVec2("textOffset",   ndp->textOffset);
        ndp->buttonOffset = readVec2("buttonOffset", ndp->buttonOffset);
        ndp->titleFontSize      = j.value("titleFontSize", 14.0f);
        ndp->bodyFontSize       = j.value("bodyFontSize", 11.0f);
        ndp->buttonFontSize     = j.value("buttonFontSize", 12.0f);
        ndp->closeFontSize      = j.value("closeFontSize", 10.0f);
        ndp->questNameFontSize  = j.value("questNameFontSize", 11.0f);
        ndp->questStatusFontSize = j.value("questStatusFontSize", 9.0f);
        ndp->titleBarHeight     = j.value("titleBarHeight", 28.0f);
        ndp->buttonHeight       = j.value("buttonHeight", 36.0f);
        ndp->buttonMargin       = j.value("buttonMargin", 4.0f);
        ndp->buttonGap          = j.value("buttonGap", 4.0f);
        ndp->questRowHeight     = j.value("questRowHeight", 30.0f);
        ndp->closeCircleRadius  = j.value("closeCircleRadius", 10.0f);
        ndp->borderWidth        = j.value("borderWidth", 2.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        ndp->bgColor          = readColor("bgColor",          ndp->bgColor);
        ndp->textColor        = readColor("textColor",        ndp->textColor);
        ndp->titleColor       = readColor("titleColor",       ndp->titleColor);
        ndp->goldColor        = readColor("goldColor",        ndp->goldColor);
        ndp->buttonBgColor    = readColor("buttonBgColor",    ndp->buttonBgColor);
        ndp->buttonBorderColor = readColor("buttonBorderColor", ndp->buttonBorderColor);
        ndp->closeBgColor     = readColor("closeBgColor",     ndp->closeBgColor);
        ndp->dividerColor     = readColor("dividerColor",     ndp->dividerColor);
        node = std::move(ndp);
    }
    else if (type == "shop_panel") {
        auto shp = std::make_unique<ShopPanel>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        shp->titleOffset     = readVec2("titleOffset",     shp->titleOffset);
        shp->shopListOffset  = readVec2("shopListOffset",  shp->shopListOffset);
        shp->goldOffset      = readVec2("goldOffset",      shp->goldOffset);
        shp->priceOffset     = readVec2("priceOffset",     shp->priceOffset);
        shp->stockOffset     = readVec2("stockOffset",     shp->stockOffset);
        shp->itemNameOffset  = readVec2("itemNameOffset",  shp->itemNameOffset);
        shp->subHeaderLabel  = j.value("subHeaderLabel", std::string{"Shop Items"});
        shp->titleFontSize   = j.value("titleFontSize", 14.0f);
        shp->headerFontSize  = j.value("headerFontSize", 11.0f);
        shp->itemFontSize    = j.value("itemFontSize", 10.0f);
        shp->priceFontSize   = j.value("priceFontSize", 9.0f);
        shp->goldFontSize    = j.value("goldFontSize", 12.0f);
        shp->stockFontSize   = j.value("stockFontSize", 8.0f);
        shp->subHeaderFontSize = j.value("subHeaderFontSize", 11.0f);
        shp->headerHeight    = j.value("headerHeight", 30.0f);
        shp->rowHeight       = j.value("rowHeight", 36.0f);
        shp->goldBarHeight   = j.value("goldBarHeight", 28.0f);
        shp->buyBtnWidth     = j.value("buyBtnWidth", 42.0f);
        shp->buyBtnHeight    = j.value("buyBtnHeight", 22.0f);
        shp->contentPadding  = j.value("contentPadding", 6.0f);
        shp->subHeaderHeight = j.value("subHeaderHeight", 22.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        shp->bgColor             = readColor("bgColor",             shp->bgColor);
        shp->borderColor         = readColor("borderColor",         shp->borderColor);
        shp->titleColor          = readColor("titleColor",          shp->titleColor);
        shp->headerBgColor       = readColor("headerBgColor",       shp->headerBgColor);
        shp->textColor           = readColor("textColor",           shp->textColor);
        shp->goldColor           = readColor("goldColor",           shp->goldColor);
        shp->buyBtnColor         = readColor("buyBtnColor",         shp->buyBtnColor);
        shp->buyBtnDisabledColor = readColor("buyBtnDisabledColor", shp->buyBtnDisabledColor);
        shp->dividerColor        = readColor("dividerColor",        shp->dividerColor);
        shp->errorColor          = readColor("errorColor",          shp->errorColor);
        shp->rowBgColor          = readColor("rowBgColor",          shp->rowBgColor);
        shp->rowAltBgColor       = readColor("rowAltBgColor",       shp->rowAltBgColor);
        shp->subHeaderColor      = readColor("subHeaderColor",      shp->subHeaderColor);
        shp->goldBarBgColor      = readColor("goldBarBgColor",      shp->goldBarBgColor);
        shp->stockColor          = readColor("stockColor",          shp->stockColor);
        shp->priceColor          = readColor("priceColor",          shp->priceColor);

        // Panel border
        shp->panelBorderWidth = j.value("panelBorderWidth", 2.0f);

        // Close button
        shp->closeBtnRadius      = j.value("closeBtnRadius", 10.0f);
        shp->closeBtnOffset      = j.value("closeBtnOffset", 5.0f);
        shp->closeBtnBorderW     = j.value("closeBtnBorderW", 1.0f);
        shp->closeBtnFontSize    = j.value("closeBtnFontSize", 10.0f);
        shp->closeBtnBgColor     = readColor("closeBtnBgColor",     shp->closeBtnBgColor);
        shp->closeBtnBorderColor = readColor("closeBtnBorderColor", shp->closeBtnBorderColor);
        shp->closeBtnTextColor   = readColor("closeBtnTextColor",   shp->closeBtnTextColor);

        // Buy button text/border
        shp->buyBtnBorderColor         = readColor("buyBtnBorderColor",         shp->buyBtnBorderColor);
        shp->buyBtnDisabledBorderColor = readColor("buyBtnDisabledBorderColor", shp->buyBtnDisabledBorderColor);
        shp->buyBtnTextColor           = readColor("buyBtnTextColor",           shp->buyBtnTextColor);
        shp->buyBtnDisabledTextColor   = readColor("buyBtnDisabledTextColor",   shp->buyBtnDisabledTextColor);
        shp->buyBtnLabel               = j.value("buyBtnLabel", std::string{"Buy"});

        // Gold bar
        shp->goldLabelPrefix = j.value("goldLabelPrefix", std::string{"Gold: "});

        // Sell confirmation popup
        shp->confirmPopupW          = j.value("confirmPopupW", 220.0f);
        shp->confirmPopupH          = j.value("confirmPopupH", 120.0f);
        shp->confirmBtnW            = j.value("confirmBtnW", 70.0f);
        shp->confirmBtnH            = j.value("confirmBtnH", 24.0f);
        shp->confirmQtyBtnSize      = j.value("confirmQtyBtnSize", 20.0f);
        shp->confirmBorderW         = j.value("confirmBorderW", 1.5f);
        shp->confirmTitleFontSize   = j.value("confirmTitleFontSize", 11.0f);
        shp->confirmQtyFontSize     = j.value("confirmQtyFontSize", 14.0f);
        shp->confirmPriceFontSize   = j.value("confirmPriceFontSize", 10.0f);
        shp->confirmBtnFontSize     = j.value("confirmBtnFontSize", 10.0f);
        shp->confirmQtyBtnFontSize  = j.value("confirmQtyBtnFontSize", 12.0f);
        shp->confirmBgColor         = readColor("confirmBgColor",     shp->confirmBgColor);
        shp->confirmBorderColor     = readColor("confirmBorderColor", shp->confirmBorderColor);
        shp->confirmBtnColor        = readColor("confirmBtnColor",    shp->confirmBtnColor);
        shp->cancelBtnColor         = readColor("cancelBtnColor",     shp->cancelBtnColor);
        shp->confirmBtnLabel        = j.value("confirmBtnLabel", std::string{"Confirm"});
        shp->cancelBtnLabel         = j.value("cancelBtnLabel",  std::string{"Cancel"});

        node = std::move(shp);
    }
    else if (type == "bank_panel") {
        auto bnk = std::make_unique<BankPanel>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        bnk->titleOffset     = readVec2("titleOffset",     bnk->titleOffset);
        bnk->bankListOffset  = readVec2("bankListOffset",  bnk->bankListOffset);
        bnk->inventoryOffset = readVec2("inventoryOffset", bnk->inventoryOffset);
        bnk->goldOffset      = readVec2("goldOffset",      bnk->goldOffset);
        bnk->titleFontSize   = j.value("titleFontSize", 14.0f);
        bnk->headerFontSize  = j.value("headerFontSize", 12.0f);
        bnk->bodyFontSize    = j.value("bodyFontSize", 11.0f);
        bnk->smallFontSize   = j.value("smallFontSize", 9.0f);
        bnk->headerHeight    = j.value("headerHeight", 28.0f);
        bnk->bottomBarHeight = j.value("bottomBarHeight", 80.0f);
        bnk->rowHeight       = j.value("rowHeight", 36.0f);
        bnk->slotSize        = j.value("slotSize", 40.0f);
        bnk->borderWidth     = j.value("borderWidth", 2.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        bnk->bgColor          = readColor("bgColor",          bnk->bgColor);
        bnk->borderColor      = readColor("borderColor",      bnk->borderColor);
        bnk->titleColor       = readColor("titleColor",       bnk->titleColor);
        bnk->textColor        = readColor("textColor",        bnk->textColor);
        bnk->goldColor        = readColor("goldColor",        bnk->goldColor);
        bnk->buttonColor      = readColor("buttonColor",      bnk->buttonColor);
        bnk->withdrawBtnColor = readColor("withdrawBtnColor", bnk->withdrawBtnColor);
        bnk->depositBtnColor  = readColor("depositBtnColor",  bnk->depositBtnColor);
        bnk->slotBgColor      = readColor("slotBgColor",      bnk->slotBgColor);
        bnk->dividerColorVal  = readColor("dividerColorVal",  bnk->dividerColorVal);
        node = std::move(bnk);
    }
    else if (type == "teleporter_panel") {
        auto tp = std::make_unique<TeleporterPanel>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        tp->titleOffset = readVec2("titleOffset", tp->titleOffset);
        tp->labelOffset = readVec2("labelOffset", tp->labelOffset);
        tp->rowOffset   = readVec2("rowOffset",   tp->rowOffset);
        tp->goldOffset  = readVec2("goldOffset",  tp->goldOffset);
        tp->title = j.value("title", std::string("Teleporter"));
        tp->titleFontSize = j.value("titleFontSize", 14.0f);
        tp->nameFontSize  = j.value("nameFontSize", 12.0f);
        tp->costFontSize  = j.value("costFontSize", 10.0f);
        tp->labelFontSize = j.value("labelFontSize", 11.0f);
        tp->goldFontSize  = j.value("goldFontSize", 12.0f);
        tp->rowHeight     = j.value("rowHeight", 40.0f);
        auto readColor = [&](const std::string& key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 4)
                return {j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>(), j[key][3].get<float>()};
            return def;
        };
        tp->backgroundColor = readColor("backgroundColor", {0.08f, 0.08f, 0.12f, 0.95f});
        tp->borderColor     = readColor("borderColor",     {0.25f, 0.25f, 0.35f, 1.0f});
        tp->titleBarColor   = readColor("titleBarColor",   {0.12f, 0.12f, 0.18f, 1.0f});
        tp->titleColor      = readColor("titleColor",      {0.9f, 0.9f, 0.85f, 1.0f});
        tp->closeBtnColor   = readColor("closeBtnColor",   {0.3f, 0.15f, 0.15f, 0.9f});
        tp->labelColor      = readColor("labelColor",      {0.7f, 0.7f, 0.65f, 1.0f});
        tp->textColor       = readColor("textColor",       {0.9f, 0.9f, 0.85f, 1.0f});
        tp->goldColor       = readColor("goldColor",       {1.0f, 0.84f, 0.0f, 1.0f});
        tp->disabledColor   = readColor("disabledColor",   {0.4f, 0.4f, 0.4f, 0.5f});
        tp->errorColor      = readColor("errorColor",      {0.8f, 0.2f, 0.2f, 1.0f});
        node = std::move(tp);
    }
    else if (type == "arena_panel") {
        auto w = std::make_unique<ArenaPanel>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        w->titleOffset  = readVec2("titleOffset",  w->titleOffset);
        w->descOffset   = readVec2("descOffset",   w->descOffset);
        w->buttonOffset = readVec2("buttonOffset", w->buttonOffset);
        w->statusOffset = readVec2("statusOffset", w->statusOffset);
        w->titleFontSize = j.value("titleFontSize", 18.0f);
        w->bodyFontSize  = j.value("bodyFontSize", 13.0f);
        w->buttonHeight  = j.value("buttonHeight", 32.0f);
        w->buttonSpacing = j.value("buttonSpacing", 8.0f);
        auto readColor = [&](const std::string& key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 4)
                return {j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>(), j[key][3].get<float>()};
            return def;
        };
        w->backgroundColor = readColor("backgroundColor", {0.08f, 0.08f, 0.12f, 0.95f});
        w->borderColor     = readColor("borderColor",     {0.25f, 0.25f, 0.35f, 1.0f});
        w->titleBarColor   = readColor("titleBarColor",   {0.12f, 0.12f, 0.18f, 1.0f});
        w->titleColor      = readColor("titleColor",      {0.9f, 0.9f, 0.85f, 1.0f});
        w->closeBtnColor   = readColor("closeBtnColor",   {0.3f, 0.15f, 0.15f, 0.9f});
        w->labelColor      = readColor("labelColor",      {0.7f, 0.7f, 0.65f, 1.0f});
        w->buttonColor     = readColor("buttonColor",     {0.18f, 0.22f, 0.35f, 0.9f});
        w->buttonTextColor = readColor("buttonTextColor", {0.9f, 0.9f, 0.85f, 1.0f});
        w->cancelBtnColor  = readColor("cancelBtnColor",  {0.35f, 0.15f, 0.15f, 0.9f});
        w->registeredColor = readColor("registeredColor", {0.3f, 0.8f, 0.3f, 1.0f});
        w->statusColor     = readColor("statusColor",     {0.8f, 0.7f, 0.2f, 1.0f});
        node = std::move(w);
    }
    else if (type == "battlefield_panel") {
        auto w = std::make_unique<BattlefieldPanel>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        w->titleOffset  = readVec2("titleOffset",  w->titleOffset);
        w->descOffset   = readVec2("descOffset",   w->descOffset);
        w->buttonOffset = readVec2("buttonOffset", w->buttonOffset);
        w->statusOffset = readVec2("statusOffset", w->statusOffset);
        w->titleFontSize = j.value("titleFontSize", 18.0f);
        w->bodyFontSize  = j.value("bodyFontSize", 13.0f);
        w->buttonHeight  = j.value("buttonHeight", 36.0f);
        auto readColor = [&](const std::string& key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 4)
                return {j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>(), j[key][3].get<float>()};
            return def;
        };
        w->backgroundColor = readColor("backgroundColor", {0.08f, 0.08f, 0.12f, 0.95f});
        w->borderColor     = readColor("borderColor",     {0.25f, 0.25f, 0.35f, 1.0f});
        w->titleBarColor   = readColor("titleBarColor",   {0.12f, 0.12f, 0.18f, 1.0f});
        w->titleColor      = readColor("titleColor",      {0.9f, 0.9f, 0.85f, 1.0f});
        w->closeBtnColor   = readColor("closeBtnColor",   {0.3f, 0.15f, 0.15f, 0.9f});
        w->labelColor      = readColor("labelColor",      {0.7f, 0.7f, 0.65f, 1.0f});
        w->buttonColor     = readColor("buttonColor",     {0.18f, 0.22f, 0.35f, 0.9f});
        w->buttonTextColor = readColor("buttonTextColor", {0.9f, 0.9f, 0.85f, 1.0f});
        w->cancelBtnColor  = readColor("cancelBtnColor",  {0.35f, 0.15f, 0.15f, 0.9f});
        w->registeredColor = readColor("registeredColor", {0.3f, 0.8f, 0.3f, 1.0f});
        w->timerColor      = readColor("timerColor",      {0.9f, 0.8f, 0.3f, 1.0f});
        w->statusColor     = readColor("statusColor",     {0.8f, 0.7f, 0.2f, 1.0f});
        node = std::move(w);
    }
    else if (type == "leaderboard_panel") {
        auto w = std::make_unique<LeaderboardPanel>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        w->titleOffset    = readVec2("titleOffset",    w->titleOffset);
        w->columnOffset   = readVec2("columnOffset",   w->columnOffset);
        w->listOffset     = readVec2("listOffset",     w->listOffset);
        w->pageInfoOffset = readVec2("pageInfoOffset", w->pageInfoOffset);
        w->titleFontSize     = j.value("titleFontSize", 14.0f);
        w->tabFontSize       = j.value("tabFontSize", 9.0f);
        w->filterFontSize    = j.value("filterFontSize", 10.0f);
        w->headerFontSize    = j.value("headerFontSize", 9.0f);
        w->rowFontSize       = j.value("rowFontSize", 10.0f);
        w->pageFontSize      = j.value("pageFontSize", 11.0f);
        w->closeFontSize     = j.value("closeFontSize", 10.0f);
        w->titleBarHeight    = j.value("titleBarHeight", 28.0f);
        w->catTabHeight      = j.value("catTabHeight", 26.0f);
        w->facBtnHeight      = j.value("facBtnHeight", 24.0f);
        w->rowHeight         = j.value("rowHeight", 22.0f);
        w->headerRowHeight   = j.value("headerRowHeight", 20.0f);
        w->pagBtnHeight      = j.value("pagBtnHeight", 28.0f);
        w->pagBtnWidth       = j.value("pagBtnWidth", 70.0f);
        w->closeCircleRadius = j.value("closeCircleRadius", 10.0f);
        w->borderWidth       = j.value("borderWidth", 2.0f);
        w->contentPadding    = j.value("contentPadding", 6.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        w->bgColor          = readColor("bgColor",          w->bgColor);
        w->textColor        = readColor("textColor",        w->textColor);
        w->titleColor       = readColor("titleColor",       w->titleColor);
        w->borderColor      = readColor("borderColor",      w->borderColor);
        w->dividerColor     = readColor("dividerColor",     w->dividerColor);
        w->headerBgColor    = readColor("headerBgColor",    w->headerBgColor);
        w->rowEvenColor     = readColor("rowEvenColor",     w->rowEvenColor);
        w->rowOddColor      = readColor("rowOddColor",      w->rowOddColor);
        w->activeBgColor    = readColor("activeBgColor",    w->activeBgColor);
        w->closeBgColor     = readColor("closeBgColor",     w->closeBgColor);
        w->rankGoldColor    = readColor("rankGoldColor",    w->rankGoldColor);
        w->rankSilverColor  = readColor("rankSilverColor",  w->rankSilverColor);
        w->rankBronzeColor  = readColor("rankBronzeColor",  w->rankBronzeColor);
        w->btnBgColor       = readColor("btnBgColor",       w->btnBgColor);
        w->btnBorderColor   = readColor("btnBorderColor",   w->btnBorderColor);
        w->goldAccentColor  = readColor("goldAccentColor",  w->goldAccentColor);
        w->closeXColor      = readColor("closeXColor",      w->closeXColor);
        node = std::move(w);
    }
    else if (type == "pet_panel") {
        auto w = std::make_unique<PetPanel>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        w->titleOffset       = readVec2("titleOffset",       w->titleOffset);
        w->activeLabelOffset = readVec2("activeLabelOffset", w->activeLabelOffset);
        w->petInfoOffset     = readVec2("petInfoOffset",     w->petInfoOffset);
        w->listLabelOffset   = readVec2("listLabelOffset",   w->listLabelOffset);
        w->titleFontSize = j.value("titleFontSize", 18.0f);
        w->nameFontSize  = j.value("nameFontSize", 15.0f);
        w->statFontSize  = j.value("statFontSize", 12.0f);
        w->portraitSize  = j.value("portraitSize", 64.0f);
        w->buttonHeight  = j.value("buttonHeight", 32.0f);
        auto readColor = [&](const std::string& key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 4)
                return {j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>(), j[key][3].get<float>()};
            return def;
        };
        w->backgroundColor = readColor("backgroundColor", {0.08f, 0.08f, 0.12f, 0.95f});
        w->borderColor     = readColor("borderColor",     {0.25f, 0.25f, 0.35f, 1.0f});
        w->titleBarColor   = readColor("titleBarColor",   {0.12f, 0.12f, 0.18f, 1.0f});
        w->titleColor      = readColor("titleColor",      {0.9f, 0.9f, 0.85f, 1.0f});
        w->closeBtnColor   = readColor("closeBtnColor",   {0.3f, 0.15f, 0.15f, 0.9f});
        w->labelColor      = readColor("labelColor",      {0.7f, 0.7f, 0.65f, 1.0f});
        w->buttonColor     = readColor("buttonColor",     {0.18f, 0.22f, 0.35f, 0.9f});
        w->buttonTextColor = readColor("buttonTextColor", {0.9f, 0.9f, 0.85f, 1.0f});
        w->unequipBtnColor = readColor("unequipBtnColor", {0.35f, 0.15f, 0.15f, 0.9f});
        w->equippedColor   = readColor("equippedColor",   {0.3f, 0.8f, 0.3f, 1.0f});
        w->xpBarBgColor    = readColor("xpBarBgColor",    {0.15f, 0.15f, 0.2f, 1.0f});
        w->xpBarFillColor  = readColor("xpBarFillColor",  {0.2f, 0.5f, 0.9f, 1.0f});
        w->portraitBgColor = readColor("portraitBgColor",  {0.15f, 0.2f, 0.25f, 0.9f});
        w->selectedBgColor = readColor("selectedBgColor",  {0.2f, 0.25f, 0.4f, 0.8f});
        node = std::move(w);
    }
    else if (type == "crafting_panel") {
        auto w = std::make_unique<CraftingPanel>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        w->titleOffset      = readVec2("titleOffset",      w->titleOffset);
        w->recipeListOffset = readVec2("recipeListOffset", w->recipeListOffset);
        w->detailOffset     = readVec2("detailOffset",     w->detailOffset);
        w->statusOffset     = readVec2("statusOffset",     w->statusOffset);
        w->titleFontSize     = j.value("titleFontSize", 18.0f);
        w->recipeFontSize    = j.value("recipeFontSize", 13.0f);
        w->slotSize          = j.value("slotSize", 36.0f);
        w->resultSlotSize    = j.value("resultSlotSize", 44.0f);
        w->ingredientColumns = j.value("ingredientColumns", 4);
        auto readColor = [&](const std::string& key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 4)
                return {j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>(), j[key][3].get<float>()};
            return def;
        };
        w->backgroundColor     = readColor("backgroundColor",     {0.08f, 0.08f, 0.12f, 0.95f});
        w->borderColor         = readColor("borderColor",         {0.25f, 0.25f, 0.35f, 1.0f});
        w->titleBarColor       = readColor("titleBarColor",       {0.12f, 0.12f, 0.18f, 1.0f});
        w->titleColor          = readColor("titleColor",          {0.9f, 0.9f, 0.85f, 1.0f});
        w->closeBtnColor       = readColor("closeBtnColor",       {0.3f, 0.15f, 0.15f, 0.9f});
        w->labelColor          = readColor("labelColor",          {0.7f, 0.7f, 0.65f, 1.0f});
        w->buttonColor         = readColor("buttonColor",         {0.18f, 0.22f, 0.35f, 0.9f});
        w->buttonDisabledColor = readColor("buttonDisabledColor", {0.15f, 0.15f, 0.2f, 0.6f});
        w->buttonTextColor     = readColor("buttonTextColor",     {0.9f, 0.9f, 0.85f, 1.0f});
        w->selectedColor       = readColor("selectedColor",       {0.2f, 0.25f, 0.4f, 0.8f});
        w->rowColor            = readColor("rowColor",            {0.1f, 0.1f, 0.15f, 0.6f});
        w->hasColor            = readColor("hasColor",            {0.3f, 0.8f, 0.3f, 1.0f});
        w->missingColor        = readColor("missingColor",        {0.8f, 0.3f, 0.3f, 1.0f});
        w->goldColor           = readColor("goldColor",           {1.0f, 0.88f, 0.3f, 1.0f});
        w->slotBgColor         = readColor("slotBgColor",         {0.12f, 0.14f, 0.2f, 0.9f});
        w->statusColor         = readColor("statusColor",         {0.8f, 0.7f, 0.2f, 1.0f});
        node = std::move(w);
    }
    else if (type == "player_context_menu") {
        auto w = std::make_unique<PlayerContextMenu>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        w->nameOffset = readVec2("nameOffset", w->nameOffset);
        w->itemOffset = readVec2("itemOffset", w->itemOffset);
        w->menuFontSize    = j.value("menuFontSize", 13.0f);
        w->itemHeight      = j.value("itemHeight", 28.0f);
        w->menuWidth       = j.value("menuWidth", 140.0f);
        w->headerPadding   = j.value("headerPadding", 12.0f);
        w->borderWidth     = j.value("borderWidth", 1.5f);
        w->separatorMargin = j.value("separatorMargin", 8.0f);
        w->separatorHeight = j.value("separatorHeight", 1.0f);
        w->itemTextPadding = j.value("itemTextPadding", 10.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        w->bgColor           = readColor("bgColor",           w->bgColor);
        w->borderColor       = readColor("borderColor",       w->borderColor);
        w->nameHeaderColor   = readColor("nameHeaderColor",   w->nameHeaderColor);
        w->separatorColor    = readColor("separatorColor",    w->separatorColor);
        w->hoverColor        = readColor("hoverColor",        w->hoverColor);
        w->pressedColor      = readColor("pressedColor",      w->pressedColor);
        w->enabledTextColor  = readColor("enabledTextColor",  w->enabledTextColor);
        w->disabledTextColor = readColor("disabledTextColor", w->disabledTextColor);
        node = std::move(w);
    }
    else if (type == "buff_bar") {
        auto bb = std::make_unique<BuffBar>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        bb->stackBadgeOffset = readVec2("stackBadgeOffset", bb->stackBadgeOffset);
        bb->iconSize   = j.value("iconSize", 24.0f);
        bb->spacing    = j.value("spacing", 3.0f);
        bb->maxVisible = j.value("maxVisible", 12);
        bb->stackFontSize  = j.value("stackFontSize", 8.0f);
        bb->abbrevFontSize = j.value("abbrevFontSize", 7.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        bb->stackTextColor      = readColor("stackTextColor",      bb->stackTextColor);
        bb->stackBadgeBgColor   = readColor("stackBadgeBgColor",   bb->stackBadgeBgColor);
        bb->cooldownOverlayColor = readColor("cooldownOverlayColor", bb->cooldownOverlayColor);
        bb->abbrevTextColor     = readColor("abbrevTextColor",     bb->abbrevTextColor);
        bb->tooltipBgColor      = readColor("tooltipBgColor",      bb->tooltipBgColor);
        bb->tooltipBorderColor  = readColor("tooltipBorderColor",  bb->tooltipBorderColor);
        bb->tooltipTextColor    = readColor("tooltipTextColor",    bb->tooltipTextColor);
        bb->tooltipFontSize  = j.value("tooltipFontSize", 9.0f);
        bb->tooltipPadding   = j.value("tooltipPadding", 6.0f);
        bb->tooltipWidth     = j.value("tooltipWidth", 140.0f);
        bb->iconAtlasKey  = j.value("iconAtlasKey", std::string{});
        bb->iconAtlasCols = j.value("iconAtlasCols", 8);
        bb->iconAtlasRows = j.value("iconAtlasRows", 4);
        node = std::move(bb);
    }
    else if (type == "boss_hp_bar") {
        auto bh = std::make_unique<BossHPBar>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        bh->nameOffset    = readVec2("nameOffset",    bh->nameOffset);
        bh->percentOffset = readVec2("percentOffset", bh->percentOffset);
        bh->bossName   = j.value("bossName", "");
        bh->barHeight  = j.value("barHeight", 20.0f);
        bh->barPadding = j.value("barPadding", 12.0f);
        bh->nameFontSize      = j.value("nameFontSize", 14.0f);
        bh->percentFontSize   = j.value("percentFontSize", 11.0f);
        bh->nameBlockPadding  = j.value("nameBlockPadding", 8.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        bh->nameTextColor = readColor("nameTextColor", bh->nameTextColor);
        bh->barTrackColor = readColor("barTrackColor", bh->barTrackColor);
        bh->hpFillColor   = readColor("hpFillColor",   bh->hpFillColor);
        node = std::move(bh);
    }
    else if (type == "confirm_dialog") {
        auto cd = std::make_unique<ConfirmDialog>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        cd->messageOffset = readVec2("messageOffset", cd->messageOffset);
        cd->buttonOffset  = readVec2("buttonOffset",  cd->buttonOffset);
        cd->message       = j.value("message", "Are you sure?");
        cd->confirmText   = j.value("confirmText", "Confirm");
        cd->cancelText    = j.value("cancelText", "Cancel");
        cd->buttonWidth   = j.value("buttonWidth", 100.0f);
        cd->buttonHeight  = j.value("buttonHeight", 32.0f);
        cd->buttonSpacing = j.value("buttonSpacing", 16.0f);
        cd->messageFontSize = j.value("messageFontSize", 13.0f);
        cd->buttonFontSize  = j.value("buttonFontSize", 13.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        cd->buttonColor      = readColor("buttonColor",      cd->buttonColor);
        cd->buttonHoverColor = readColor("buttonHoverColor", cd->buttonHoverColor);
        node = std::move(cd);
    }
    else if (type == "invite_prompt") {
        auto w = std::make_unique<InvitePromptPanel>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        w->titleOffset   = readVec2("titleOffset",   w->titleOffset);
        w->messageOffset = readVec2("messageOffset", w->messageOffset);
        w->buttonOffset  = readVec2("buttonOffset",  w->buttonOffset);
        w->titleFontSize   = j.value("titleFontSize",   14.0f);
        w->messageFontSize = j.value("messageFontSize", 12.0f);
        w->buttonFontSize  = j.value("buttonFontSize",  12.0f);
        w->panelWidth      = j.value("panelWidth",      260.0f);
        w->panelHeight     = j.value("panelHeight",     120.0f);
        w->buttonWidth     = j.value("buttonWidth",      80.0f);
        w->buttonHeight    = j.value("buttonHeight",     28.0f);
        w->buttonSpacing   = j.value("buttonSpacing",    16.0f);
        w->borderWidth     = j.value("borderWidth",       1.5f);
        w->titlePadTop     = j.value("titlePadTop",      10.0f);
        w->messagePadTop   = j.value("messagePadTop",     8.0f);
        w->buttonPadBottom = j.value("buttonPadBottom",  12.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        w->bgColor             = readColor("bgColor",             w->bgColor);
        w->borderColor         = readColor("borderColor",         w->borderColor);
        w->titleColor          = readColor("titleColor",          w->titleColor);
        w->messageColor        = readColor("messageColor",        w->messageColor);
        w->acceptBtnColor      = readColor("acceptBtnColor",      w->acceptBtnColor);
        w->acceptBtnHoverColor = readColor("acceptBtnHoverColor", w->acceptBtnHoverColor);
        w->acceptBtnTextColor  = readColor("acceptBtnTextColor",  w->acceptBtnTextColor);
        w->declineBtnColor     = readColor("declineBtnColor",     w->declineBtnColor);
        w->declineBtnHoverColor= readColor("declineBtnHoverColor",w->declineBtnHoverColor);
        w->declineBtnTextColor = readColor("declineBtnTextColor", w->declineBtnTextColor);
        node = std::move(w);
    }
    else if (type == "notification_toast") {
        auto nt = std::make_unique<NotificationToast>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        nt->textOffset   = readVec2("textOffset", nt->textOffset);
        nt->toastHeight  = j.value("toastHeight", 28.0f);
        nt->toastSpacing = j.value("toastSpacing", 4.0f);
        nt->fadeInTime   = j.value("fadeInTime", 0.3f);
        nt->fadeOutTime  = j.value("fadeOutTime", 0.5f);
        nt->maxToasts    = j.value("maxToasts", 5);
        nt->textFontSize = j.value("textFontSize", 12.0f);
        nt->accentWidth  = j.value("accentWidth", 4.0f);
        nt->textMargin   = j.value("textMargin", 8.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        nt->toastBgColor = readColor("toastBgColor", nt->toastBgColor);
        node = std::move(nt);
    }
    else if (type == "checkbox") {
        auto cb = std::make_unique<Checkbox>(id);
        cb->checked = j.value("checked", false);
        cb->label = j.value("label", "");
        cb->boxSize = j.value("boxSize", 16.0f);
        cb->spacing = j.value("spacing", 6.0f);
        node = std::move(cb);
    }
    else if (type == "login_screen") {
        auto ls = std::make_unique<LoginScreen>(id);
        ls->serverHost = j.value("serverHost", "127.0.0.1");
        ls->serverPort = j.value("serverPort", 7778);
        ls->rememberMe = j.value("rememberMe", false);
        node = std::move(ls);
    }
    else if (type == "death_overlay") {
        auto dov = std::make_unique<DeathOverlay>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        dov->titleOffset     = readVec2("titleOffset",     dov->titleOffset);
        dov->lossTextOffset  = readVec2("lossTextOffset",  dov->lossTextOffset);
        dov->countdownOffset = readVec2("countdownOffset", dov->countdownOffset);
        dov->buttonOffset    = readVec2("buttonOffset",    dov->buttonOffset);

        dov->titleFontSize     = j.value("titleFontSize", 22.0f);
        dov->bodyFontSize      = j.value("bodyFontSize", 13.0f);
        dov->countdownFontSize = j.value("countdownFontSize", 14.0f);
        dov->buttonFontSize    = j.value("buttonFontSize", 13.0f);

        dov->startYRatio    = j.value("startYRatio", 0.25f);
        dov->buttonWidth    = j.value("buttonWidth", 200.0f);
        dov->buttonHeight   = j.value("buttonHeight", 36.0f);
        dov->buttonSpacing  = j.value("buttonSpacing", 8.0f);

        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        dov->overlayColor      = readColor("overlayColor",      dov->overlayColor);
        dov->titleColor        = readColor("titleColor",        dov->titleColor);
        dov->countdownColor    = readColor("countdownColor",    dov->countdownColor);
        dov->buttonBgColor     = readColor("buttonBgColor",     dov->buttonBgColor);
        dov->buttonBorderColor = readColor("buttonBorderColor", dov->buttonBorderColor);
        dov->buttonTextColor   = readColor("buttonTextColor",   dov->buttonTextColor);

        node = std::move(dov);
    }
    else if (type == "fate_status_bar") {
        auto fsb = std::make_unique<FateStatusBar>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        fsb->portraitOffset = readVec2("portraitOffset", fsb->portraitOffset);
        fsb->levelOffset    = readVec2("levelOffset",    fsb->levelOffset);
        fsb->hpLabelOffset  = readVec2("hpLabelOffset",  fsb->hpLabelOffset);
        fsb->mpLabelOffset  = readVec2("mpLabelOffset",  fsb->mpLabelOffset);
        fsb->menuBtnOffset  = readVec2("menuBtnOffset",  fsb->menuBtnOffset);
        fsb->chatBtnOffset  = readVec2("chatBtnOffset",  fsb->chatBtnOffset);
        fsb->topBarHeight   = j.value("topBarHeight",   40.0f);
        fsb->portraitRadius = j.value("portraitRadius",  20.0f);
        fsb->barHeight      = j.value("barHeight",      22.0f);
        fsb->menuBtnSize    = j.value("menuBtnSize",    21.0f);
        fsb->chatBtnSize    = j.value("chatBtnSize",    21.0f);
        fsb->chatBtnOffsetX = j.value("chatBtnOffsetX",  8.0f);
        fsb->menuBtnGap     = j.value("menuBtnGap",      6.0f);
        fsb->coordOffsetY   = j.value("coordOffsetY",    3.0f);
        fsb->levelFontSize  = j.value("levelFontSize",  26.0f);
        fsb->labelFontSize  = j.value("labelFontSize",  22.0f);
        fsb->numberFontSize = j.value("numberFontSize", 28.0f);
        fsb->coordFontSize  = j.value("coordFontSize",  11.0f);
        fsb->buttonFontSize = j.value("buttonFontSize",  9.0f);
        fsb->menuBtnFontSize = j.value("menuBtnFontSize", 9.0f);
        fsb->chatBtnFontSize = j.value("chatBtnFontSize", 9.0f);
        if (j.contains("hpBarColor") && j["hpBarColor"].is_array() && j["hpBarColor"].size() == 4) {
            auto& c = j["hpBarColor"];
            fsb->hpBarColor = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>()};
        }
        if (j.contains("mpBarColor") && j["mpBarColor"].is_array() && j["mpBarColor"].size() == 4) {
            auto& c = j["mpBarColor"];
            fsb->mpBarColor = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>()};
        }
        if (j.contains("coordColor") && j["coordColor"].is_array() && j["coordColor"].size() == 4) {
            auto& c = j["coordColor"];
            fsb->coordColor = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>()};
        }
        if (j.contains("menuBtnTextColor") && j["menuBtnTextColor"].is_array() && j["menuBtnTextColor"].size() == 4) {
            auto& c = j["menuBtnTextColor"];
            fsb->menuBtnTextColor = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>()};
        }
        if (j.contains("chatBtnTextColor") && j["chatBtnTextColor"].is_array() && j["chatBtnTextColor"].size() == 4) {
            auto& c = j["chatBtnTextColor"];
            fsb->chatBtnTextColor = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>()};
        }
        if (j.contains("menuBtnBgColor") && j["menuBtnBgColor"].is_array() && j["menuBtnBgColor"].size() == 4) {
            auto& c = j["menuBtnBgColor"];
            fsb->menuBtnBgColor = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>()};
        }
        fsb->menuOverlayW     = j.value("menuOverlayW",    140.0f);
        fsb->menuItemH        = j.value("menuItemH",        36.0f);
        fsb->menuItemFontSize = j.value("menuItemFontSize", 12.0f);
        if (j.contains("menuItemTextColor") && j["menuItemTextColor"].is_array() && j["menuItemTextColor"].size() == 4) {
            auto& c = j["menuItemTextColor"];
            fsb->menuItemTextColor = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>()};
        }
        if (j.contains("menuOverlayBgColor") && j["menuOverlayBgColor"].is_array() && j["menuOverlayBgColor"].size() == 4) {
            auto& c = j["menuOverlayBgColor"];
            fsb->menuOverlayBgColor = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>()};
        }
        if (j.contains("menuOverlayBorderColor") && j["menuOverlayBorderColor"].is_array() && j["menuOverlayBorderColor"].size() == 4) {
            auto& c = j["menuOverlayBorderColor"];
            fsb->menuOverlayBorderColor = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>()};
        }
        if (j.contains("menuDividerColor") && j["menuDividerColor"].is_array() && j["menuDividerColor"].size() == 4) {
            auto& c = j["menuDividerColor"];
            fsb->menuDividerColor = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>()};
        }
        fsb->showCoordinates = j.value("showCoordinates", true);
        fsb->showMenuButton  = j.value("showMenuButton",  true);
        fsb->showChatButton  = j.value("showChatButton",  true);
        node = std::move(fsb);
    }
    else if (type == "collection_panel") {
        auto w = std::make_unique<CollectionPanel>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        w->titleOffset = readVec2("titleOffset", w->titleOffset);
        w->tabOffset   = readVec2("tabOffset",   w->tabOffset);
        w->entryOffset = readVec2("entryOffset", w->entryOffset);
        w->titleFontSize     = j.value("titleFontSize", 18.0f);
        w->entryFontSize     = j.value("entryFontSize", 13.0f);
        w->rewardFontSize    = j.value("rewardFontSize", 11.0f);
        w->categoryTabHeight = j.value("categoryTabHeight", 28.0f);
        w->entryHeight       = j.value("entryHeight", 40.0f);
        w->borderWidth       = j.value("borderWidth", 2.0f);
        w->headerHeight      = j.value("headerHeight", 28.0f);
        auto readColor = [&](const std::string& key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 4)
                return {j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>(), j[key][3].get<float>()};
            return def;
        };
        w->completedColor       = readColor("completedColor",       {0.2f, 0.8f, 0.2f, 1.0f});
        w->incompleteColor      = readColor("incompleteColor",      {0.5f, 0.5f, 0.5f, 1.0f});
        w->rewardColor          = readColor("rewardColor",          {0.9f, 0.8f, 0.3f, 1.0f});
        w->progressColor        = readColor("progressColor",        {1.0f, 1.0f, 1.0f, 1.0f});
        w->backgroundColor      = readColor("backgroundColor",      {0.08f, 0.08f, 0.12f, 0.95f});
        w->borderColor          = readColor("borderColor",          {0.25f, 0.25f, 0.35f, 1.0f});
        w->titleBarColor        = readColor("titleBarColor",        {0.12f, 0.12f, 0.18f, 1.0f});
        w->titleColor           = readColor("titleColor",           {0.9f, 0.9f, 0.85f, 1.0f});
        w->closeBtnColor        = readColor("closeBtnColor",        {0.3f, 0.15f, 0.15f, 0.9f});
        w->tabActiveColor       = readColor("tabActiveColor",       {0.20f, 0.18f, 0.10f, 1.0f});
        w->tabInactiveColor     = readColor("tabInactiveColor",     {0.10f, 0.10f, 0.14f, 1.0f});
        w->tabActiveTextColor   = readColor("tabActiveTextColor",   {1.0f, 0.9f, 0.5f, 1.0f});
        w->tabInactiveTextColor = readColor("tabInactiveTextColor", {0.6f, 0.6f, 0.55f, 1.0f});
        node = std::move(w);
    }
    else if (type == "costume_panel") {
        auto w = std::make_unique<CostumePanel>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        w->titleOffset  = readVec2("titleOffset",  w->titleOffset);
        w->toggleOffset = readVec2("toggleOffset", w->toggleOffset);
        w->gridOffset   = readVec2("gridOffset",   w->gridOffset);
        w->infoOffset   = readVec2("infoOffset",   w->infoOffset);
        w->titleFontSize      = j.value("titleFontSize", 18.0f);
        w->bodyFontSize       = j.value("bodyFontSize", 13.0f);
        w->infoFontSize       = j.value("infoFontSize", 11.0f);
        w->gridCols           = j.value("gridCols", 4);
        w->slotSize           = j.value("slotSize", 48.0f);
        w->slotSpacing        = j.value("slotSpacing", 6.0f);
        w->buttonHeight       = j.value("buttonHeight", 32.0f);
        w->buttonSpacing      = j.value("buttonSpacing", 8.0f);
        w->filterTabHeight    = j.value("filterTabHeight", 24.0f);
        w->borderWidth        = j.value("borderWidth", 2.0f);
        w->headerHeight       = j.value("headerHeight", 28.0f);
        w->bottomReserveHeight = j.value("bottomReserveHeight", 60.0f);
        auto readColor = [&](const std::string& key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 4)
                return {j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>(), j[key][3].get<float>()};
            return def;
        };
        w->backgroundColor       = readColor("backgroundColor",       {0.08f, 0.08f, 0.12f, 0.95f});
        w->borderColor           = readColor("borderColor",           {0.25f, 0.25f, 0.35f, 1.0f});
        w->titleBarColor         = readColor("titleBarColor",         {0.12f, 0.12f, 0.18f, 1.0f});
        w->titleColor            = readColor("titleColor",            {0.9f, 0.9f, 0.85f, 1.0f});
        w->closeBtnColor         = readColor("closeBtnColor",         {0.3f, 0.15f, 0.15f, 0.9f});
        w->tabColor              = readColor("tabColor",              {0.14f, 0.14f, 0.20f, 0.9f});
        w->tabActiveColor        = readColor("tabActiveColor",        {0.22f, 0.28f, 0.45f, 0.9f});
        w->tabTextColor          = readColor("tabTextColor",          {0.7f, 0.7f, 0.65f, 1.0f});
        w->tabActiveTextColor    = readColor("tabActiveTextColor",    {0.95f, 0.95f, 0.9f, 1.0f});
        w->slotColor             = readColor("slotColor",             {0.12f, 0.12f, 0.18f, 0.9f});
        w->slotSelectedColor     = readColor("slotSelectedColor",     {0.20f, 0.24f, 0.38f, 0.9f});
        w->equippedIndicatorColor = readColor("equippedIndicatorColor", {0.3f, 0.8f, 0.3f, 1.0f});
        w->nameColor             = readColor("nameColor",             {0.85f, 0.85f, 0.8f, 1.0f});
        w->equipBtnColor         = readColor("equipBtnColor",         {0.18f, 0.22f, 0.35f, 0.9f});
        w->unequipBtnColor       = readColor("unequipBtnColor",       {0.35f, 0.15f, 0.15f, 0.9f});
        w->buttonTextColor       = readColor("buttonTextColor",       {0.9f, 0.9f, 0.85f, 1.0f});
        w->hintColor             = readColor("hintColor",             {0.5f, 0.5f, 0.45f, 0.8f});
        node = std::move(w);
    }
    else if (type == "settings_panel") {
        auto sp = std::make_unique<SettingsPanel>(id);

        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2) {
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            }
            return def;
        };
        sp->titleOffset  = readVec2("titleOffset",  sp->titleOffset);
        sp->logoutOffset = readVec2("logoutOffset", sp->logoutOffset);

        sp->titleFontSize      = j.value("titleFontSize",      sp->titleFontSize);
        sp->sectionFontSize    = j.value("sectionFontSize",    sp->sectionFontSize);
        sp->labelFontSize      = j.value("labelFontSize",      sp->labelFontSize);
        sp->buttonFontSize     = j.value("buttonFontSize",     sp->buttonFontSize);
        sp->logoutButtonWidth  = j.value("logoutButtonWidth",  sp->logoutButtonWidth);
        sp->logoutButtonHeight = j.value("logoutButtonHeight", sp->logoutButtonHeight);
        sp->buttonCornerRadius = j.value("buttonCornerRadius", sp->buttonCornerRadius);
        sp->sectionSpacing     = j.value("sectionSpacing",     sp->sectionSpacing);
        sp->itemSpacing        = j.value("itemSpacing",        sp->itemSpacing);
        sp->borderWidth        = j.value("borderWidth",        sp->borderWidth);

        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        sp->toggleBtnWidth      = j.value("toggleBtnWidth",      sp->toggleBtnWidth);
        sp->toggleBtnHeight     = j.value("toggleBtnHeight",     sp->toggleBtnHeight);
        sp->checkboxSize        = j.value("checkboxSize",        sp->checkboxSize);
        sp->displayOffset       = readVec2("displayOffset",      sp->displayOffset);

        sp->titleColor          = readColor("titleColor",          sp->titleColor);
        sp->sectionColor        = readColor("sectionColor",        sp->sectionColor);
        sp->labelColor          = readColor("labelColor",          sp->labelColor);
        sp->logoutBtnColor      = readColor("logoutBtnColor",      sp->logoutBtnColor);
        sp->logoutBtnHoverColor = readColor("logoutBtnHoverColor", sp->logoutBtnHoverColor);
        sp->logoutTextColor     = readColor("logoutTextColor",     sp->logoutTextColor);
        sp->dividerColor        = readColor("dividerColor",        sp->dividerColor);
        sp->toggleOnColor       = readColor("toggleOnColor",       sp->toggleOnColor);
        sp->toggleOffColor      = readColor("toggleOffColor",      sp->toggleOffColor);
        sp->checkOnColor        = readColor("checkOnColor",        sp->checkOnColor);

        node = std::move(sp);
    }
    else if (type == "fps_counter") {
        auto fc = std::make_unique<FpsCounter>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2) {
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            }
            return def;
        };
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        fc->textOffset  = readVec2("textOffset", fc->textOffset);
        fc->fontSize    = j.value("fontSize", fc->fontSize);
        fc->showMs      = j.value("showMs", fc->showMs);
        fc->textColor   = readColor("textColor", fc->textColor);
        fc->shadowColor = readColor("shadowColor", fc->shadowColor);
        node = std::move(fc);
    }
    else {
        node = std::make_unique<UINode>(id, type);
    }
#else
    node = std::make_unique<UINode>(id, type);
#endif // FATE_HAS_GAME

    // Reflected properties (new system) — auto-deserializes from metadata
    if (node) {
        auto reflectedFields = node->reflectedProperties();
        if (!reflectedFields.empty()) {
            node->deserializeProperties(j);
        }
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

        // Responsive layout fields
        if (a.contains("minSize") && a["minSize"].is_array() && a["minSize"].size() >= 2) {
            anchor.minSize.x = a["minSize"][0].get<float>();
            anchor.minSize.y = a["minSize"][1].get<float>();
        }
        if (a.contains("maxSize") && a["maxSize"].is_array() && a["maxSize"].size() >= 2) {
            anchor.maxSize.x = a["maxSize"][0].get<float>();
            anchor.maxSize.y = a["maxSize"][1].get<float>();
        }
        anchor.useSafeArea = a.value("useSafeArea", false);
        anchor.maxAspectRatio = a.value("maxAspectRatio", 0.0f);

        node->setAnchor(anchor);
    }

    // --- Style name ---
    if (j.contains("style") && j["style"].is_string()) {
        node->setStyleName(j["style"].get<std::string>());
    }

    // --- Inline style overrides (applied after theme in applyThemeStyles) ---
    {
        auto& s = node->resolvedStyle();
        auto readColor = [&](const std::string& key) -> Color {
            if (!j.contains(key)) return Color::clear();
            auto& arr = j[key];
            if (!arr.is_array() || arr.size() < 4) return Color::clear();
            return Color(arr[0].get<int>() / 255.0f, arr[1].get<int>() / 255.0f,
                         arr[2].get<int>() / 255.0f, arr[3].get<int>() / 255.0f);
        };
        Color bg = readColor("backgroundColor");
        if (bg.a > 0.0f) s.backgroundColor = bg;
        Color bc = readColor("borderColor");
        if (bc.a > 0.0f) s.borderColor = bc;
        Color tc = readColor("textColor");
        if (tc.a > 0.0f) s.textColor = tc;
        if (j.contains("borderWidth"))  s.borderWidth = j["borderWidth"].get<float>();
        if (j.contains("fontSize"))     s.fontSize    = j["fontSize"].get<float>();
        if (j.contains("opacity"))      s.opacity     = j["opacity"].get<float>();
        // Rounded rect / gradient / shadow
        if (j.contains("cornerRadius"))    s.cornerRadius  = j["cornerRadius"].get<float>();
        if (j.contains("gradientTop"))     { auto c = readColor("gradientTop");   if (c.a > 0.0f) s.gradientTop   = c; }
        if (j.contains("gradientBottom"))  { auto c = readColor("gradientBottom"); if (c.a > 0.0f) s.gradientBottom = c; }
        if (j.contains("uiShadowOffset")) {
            auto& so = j["uiShadowOffset"];
            s.shadowOffset = {so[0].get<float>(), so[1].get<float>()};
        }
        if (j.contains("shadowBlur"))      s.shadowBlur    = j["shadowBlur"].get<float>();
        if (j.contains("uiShadowColor"))   { auto c = readColor("uiShadowColor"); if (c.a > 0.0f) s.shadowColor = c; }
        // Text effects
        if (j.contains("textStyle"))       s.textStyle     = static_cast<TextStyle>(j["textStyle"].get<int>());
        if (j.contains("textEffects")) {
            auto& te = j["textEffects"];
            if (te.contains("outlineColor"))  { Color c(te["outlineColor"][0].get<int>()/255.0f, te["outlineColor"][1].get<int>()/255.0f, te["outlineColor"][2].get<int>()/255.0f, te["outlineColor"][3].get<int>()/255.0f); s.textEffects.outlineColor = c; }
            if (te.contains("outlineWidth"))  s.textEffects.outlineWidth = te["outlineWidth"].get<float>();
            if (te.contains("shadowOffset")) {
                auto& so = te["shadowOffset"];
                s.textEffects.shadowOffset = {so[0].get<float>(), so[1].get<float>()};
            }
            if (te.contains("shadowColor"))  { Color c(te["shadowColor"][0].get<int>()/255.0f, te["shadowColor"][1].get<int>()/255.0f, te["shadowColor"][2].get<int>()/255.0f, te["shadowColor"][3].get<int>()/255.0f); s.textEffects.shadowColor = c; }
            if (te.contains("glowColor"))    { Color c(te["glowColor"][0].get<int>()/255.0f, te["glowColor"][1].get<int>()/255.0f, te["glowColor"][2].get<int>()/255.0f, te["glowColor"][3].get<int>()/255.0f); s.textEffects.glowColor = c; }
            if (te.contains("glowRadius"))   s.textEffects.glowRadius = te["glowRadius"].get<float>();
        }
        if (j.contains("fontName"))        s.fontName = j["fontName"].get<std::string>();
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
    // Clear stale focus/press if the node was hidden or removed since last frame
    if (focusedNode_ && !focusedNode_->visible()) {
        focusedNode_->onFocusLost();
        focusedNode_ = nullptr;
    }
    if (pressedNode_ && !pressedNode_->visible())  pressedNode_ = nullptr;
    if (hoveredNode_ && !hoveredNode_->visible())   hoveredNode_ = nullptr;

    auto& input = Input::instance();
    Vec2 rawPos = input.mousePosition();
    Vec2 mousePos = {(rawPos.x - inputOffsetX_) * inputScaleX_,
                     (rawPos.y - inputOffsetY_) * inputScaleY_};

    updateHover(mousePos);

    // Handle draggable panel/window movement while mouse is held
    if (pressedNode_ && input.isMouseDown(SDL_BUTTON_LEFT)) {
#ifdef FATE_HAS_GAME
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
#endif // FATE_HAS_GAME

        // Notify pressed widget of drag movement (for item drag cursors, etc.)
        Vec2 localPos = {mousePos.x - pressedNode_->computedRect().x,
                         mousePos.y - pressedNode_->computedRect().y};
        pressedNode_->onDragUpdate(localPos);
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
    // Iterate screens back-to-front. For each, hit-test and try onPress.
    // If onPress returns false (non-interactive container), continue to the
    // next screen so clicks pass through StretchAll root panels to reach
    // interactive widgets in screens underneath.
    UINode* target = nullptr;
    for (int i = static_cast<int>(screenOrder_.size()) - 1; i >= 0; --i) {
        auto it = screens_.find(screenOrder_[i]);
        if (it == screens_.end()) continue;
        UINode* hit = hitTestNode(it->second.get(), mousePos);
        if (hit) {
            Vec2 localPos = {mousePos.x - hit->computedRect().x,
                            mousePos.y - hit->computedRect().y};
            if (hit->onPress(localPos)) {
                target = hit;
                break;
            }
        }
    }

    if (target != focusedNode_) {
        if (focusedNode_) focusedNode_->onFocusLost();
        focusedNode_ = target;
        if (focusedNode_) focusedNode_->onFocusGained();
    }

    pressedNode_ = target;
    pressStartPos_ = mousePos;
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
        // Save inline overrides that were loaded from JSON
        UIStyle inlineOverrides = node->resolvedStyle();
        // Apply theme as base
        node->setResolvedStyle(theme_.getStyle(name));
        // Re-apply inline overrides on top of theme
        auto& s = node->resolvedStyle();
        if (inlineOverrides.backgroundColor.a > 0.0f) s.backgroundColor = inlineOverrides.backgroundColor;
        if (inlineOverrides.borderColor.a > 0.0f)     s.borderColor     = inlineOverrides.borderColor;
        if (inlineOverrides.textColor.r < 1.0f || inlineOverrides.textColor.g < 1.0f ||
            inlineOverrides.textColor.b < 1.0f || inlineOverrides.textColor.a < 1.0f)
            s.textColor = inlineOverrides.textColor;
        if (inlineOverrides.borderWidth > 0.0f)        s.borderWidth    = inlineOverrides.borderWidth;
        if (inlineOverrides.opacity != 1.0f)           s.opacity        = inlineOverrides.opacity;
        // Rounded rect / gradient / shadow inline overrides
        if (inlineOverrides.cornerRadius > 0.0f)       s.cornerRadius   = inlineOverrides.cornerRadius;
        if (inlineOverrides.gradientTop.a > 0.0f)      s.gradientTop    = inlineOverrides.gradientTop;
        if (inlineOverrides.gradientBottom.a > 0.0f)   s.gradientBottom = inlineOverrides.gradientBottom;
        if (inlineOverrides.shadowOffset.x != 0.0f || inlineOverrides.shadowOffset.y != 0.0f)
            s.shadowOffset = inlineOverrides.shadowOffset;
        if (inlineOverrides.shadowBlur > 0.0f)         s.shadowBlur     = inlineOverrides.shadowBlur;
        if (inlineOverrides.shadowColor.a > 0.0f)      s.shadowColor    = inlineOverrides.shadowColor;
        // Text effects inline overrides
        if (inlineOverrides.textStyle != TextStyle::Normal) s.textStyle  = inlineOverrides.textStyle;
        if (inlineOverrides.textStyle != TextStyle::Normal) s.textEffects = inlineOverrides.textEffects;
        if (!inlineOverrides.fontName.empty())          s.fontName       = inlineOverrides.fontName;
    }

    for (size_t i = 0; i < node->childCount(); ++i) {
        applyThemeStyles(node->childAt(i));
    }
}

} // namespace fate
