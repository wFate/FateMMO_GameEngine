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
    float scale = screenHeight / UI_REFERENCE_HEIGHT;
    Rect screenRect{0.0f, 0.0f, screenWidth, screenHeight};
    for (const auto& id : screenOrder_) {
        auto it = screens_.find(id);
        if (it != screens_.end() && it->second->visible()) {
            it->second->computeLayout(screenRect, scale);
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
        pib->portraitSize = j.value("portraitSize", 48.0f);
        pib->barWidth     = j.value("barWidth", 120.0f);
        pib->barHeight    = j.value("barHeight", 16.0f);
        pib->barSpacing   = j.value("barSpacing", 2.0f);
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
    else if (type == "menu_tab_bar") {
        auto mtb = std::make_unique<MenuTabBar>(id);
        mtb->activeTab = j.value("activeTab", 0);
        mtb->tabSize   = j.value("tabSize", 50.0f);
        mtb->arrowSize = j.value("arrowSize", 28.0f);
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
        ip->quantityColor   = readColor("quantityColor",   ip->quantityColor);
        ip->itemTextColor   = readColor("itemTextColor",   ip->itemTextColor);
        ip->equipLabelColor = readColor("equipLabelColor", ip->equipLabelColor);
        ip->goldLabelColor  = readColor("goldLabelColor",  ip->goldLabelColor);
        ip->goldValueColor  = readColor("goldValueColor",  ip->goldValueColor);
        ip->platLabelColor  = readColor("platLabelColor",  ip->platLabelColor);
        ip->platValueColor  = readColor("platValueColor",  ip->platValueColor);

        node = std::move(ip);
    }
    else if (type == "status_panel") {
        auto sp = std::make_unique<StatusPanel>(id);

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
        node = std::move(skp);
    }
    else if (type == "character_select_screen") {
        auto css = std::make_unique<CharacterSelectScreen>(id);
        css->slotCircleSize   = j.value("slotCircleSize", 52.0f);
        css->entryButtonWidth = j.value("entryButtonWidth", 120.0f);
        node = std::move(css);
    }
    else if (type == "character_creation_screen") {
        auto ccs = std::make_unique<CharacterCreationScreen>(id);
        node = std::move(ccs);
    }
    else if (type == "chat_panel") {
        auto cp = std::make_unique<ChatPanel>(id);
        cp->chatIdleLines = j.value("chatIdleLines", 3);
        node = std::move(cp);
    }
    else if (type == "trade_window") {
        auto tw = std::make_unique<TradeWindow>(id);
        node = std::move(tw);
    }
    else if (type == "party_frame") {
        auto pf = std::make_unique<PartyFrame>(id);
        pf->cardWidth   = j.value("cardWidth",   170.0f);
        pf->cardHeight  = j.value("cardHeight",   48.0f);
        pf->cardSpacing = j.value("cardSpacing",   4.0f);
        node = std::move(pf);
    }
    else if (type == "guild_panel") {
        auto gp = std::make_unique<GuildPanel>(id);
        node = std::move(gp);
    }
    else if (type == "npc_dialogue_panel") {
        node = std::make_unique<NpcDialoguePanel>(id);
    }
    else if (type == "shop_panel") {
        node = std::make_unique<ShopPanel>(id);
    }
    else if (type == "bank_panel") {
        node = std::make_unique<BankPanel>(id);
    }
    else if (type == "teleporter_panel") {
        auto tp = std::make_unique<TeleporterPanel>(id);
        tp->title = j.value("title", std::string("Teleporter"));
        node = std::move(tp);
    }
    else if (type == "buff_bar") {
        auto bb = std::make_unique<BuffBar>(id);
        bb->iconSize   = j.value("iconSize", 24.0f);
        bb->spacing    = j.value("spacing", 3.0f);
        bb->maxVisible = j.value("maxVisible", 12);
        node = std::move(bb);
    }
    else if (type == "boss_hp_bar") {
        auto bh = std::make_unique<BossHPBar>(id);
        bh->bossName   = j.value("bossName", "");
        bh->barHeight  = j.value("barHeight", 20.0f);
        bh->barPadding = j.value("barPadding", 12.0f);
        node = std::move(bh);
    }
    else if (type == "confirm_dialog") {
        auto cd = std::make_unique<ConfirmDialog>(id);
        cd->message       = j.value("message", "Are you sure?");
        cd->confirmText   = j.value("confirmText", "Confirm");
        cd->cancelText    = j.value("cancelText", "Cancel");
        cd->buttonWidth   = j.value("buttonWidth", 100.0f);
        cd->buttonHeight  = j.value("buttonHeight", 32.0f);
        cd->buttonSpacing = j.value("buttonSpacing", 16.0f);
        node = std::move(cd);
    }
    else if (type == "notification_toast") {
        auto nt = std::make_unique<NotificationToast>(id);
        nt->toastHeight  = j.value("toastHeight", 28.0f);
        nt->toastSpacing = j.value("toastSpacing", 4.0f);
        nt->fadeInTime   = j.value("fadeInTime", 0.3f);
        nt->fadeOutTime  = j.value("fadeOutTime", 0.5f);
        nt->maxToasts    = j.value("maxToasts", 5);
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
        node = std::move(ls);
    }
    else if (type == "death_overlay") {
        node = std::make_unique<DeathOverlay>(id);
    }
    else if (type == "fate_status_bar") {
        auto fsb = std::make_unique<FateStatusBar>(id);
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
        fsb->showCoordinates = j.value("showCoordinates", true);
        fsb->showMenuButton  = j.value("showMenuButton",  true);
        fsb->showChatButton  = j.value("showChatButton",  true);
        node = std::move(fsb);
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
        node->setResolvedStyle(theme_.getStyle(name));
    }

    for (size_t i = 0; i < node->childCount(); ++i) {
        applyThemeStyles(node->childAt(i));
    }
}

} // namespace fate
