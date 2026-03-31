#include "engine/ui/ui_serializer.h"
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_anchor.h"
#ifdef FATE_HAS_GAME
#include "engine/ui/widgets/panel.h"
#include "engine/ui/widgets/label.h"
#include "engine/ui/widgets/button.h"
#include "engine/ui/widgets/text_input.h"
#include "engine/ui/widgets/progress_bar.h"
#include "engine/ui/widgets/window.h"
#include "engine/ui/widgets/tab_container.h"
#include "engine/ui/widgets/slot_grid.h"
#include "engine/ui/widgets/slot.h"
#include "engine/ui/widgets/scroll_view.h"
#include "engine/ui/widgets/image_box.h"
#include "engine/ui/widgets/buff_bar.h"
#include "engine/ui/widgets/boss_hp_bar.h"
#include "engine/ui/widgets/confirm_dialog.h"
#include "engine/ui/widgets/notification_toast.h"
#include "engine/ui/widgets/checkbox.h"
#include "engine/ui/widgets/login_screen.h"
#include "engine/ui/widgets/death_overlay.h"
#include "engine/ui/widgets/player_info_block.h"
#include "engine/ui/widgets/skill_arc.h"
#include "engine/ui/widgets/fate_status_bar.h"
#include "engine/ui/widgets/dpad.h"
#include "engine/ui/widgets/menu_button_row.h"
#include "engine/ui/widgets/menu_tab_bar.h"
#include "engine/ui/widgets/chat_ticker.h"
#include "engine/ui/widgets/exp_bar.h"
#include "engine/ui/widgets/target_frame.h"
#include "engine/ui/widgets/left_sidebar.h"
#include "engine/ui/widgets/inventory_panel.h"
#include "engine/ui/widgets/status_panel.h"
#include "engine/ui/widgets/skill_panel.h"
#include "engine/ui/widgets/party_frame.h"
#include "engine/ui/widgets/chat_panel.h"
#include "engine/ui/widgets/character_select_screen.h"
#include "engine/ui/widgets/character_creation_screen.h"
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
#include "engine/ui/widgets/trade_window.h"
#include "engine/ui/widgets/costume_panel.h"
#include "engine/ui/widgets/settings_panel.h"
#include "engine/ui/widgets/loading_panel.h"
#endif // FATE_HAS_GAME
#include "engine/core/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace fate {

// ---------------------------------------------------------------------------
// presetToString
// ---------------------------------------------------------------------------

std::string UISerializer::presetToString(AnchorPreset preset) {
    switch (preset) {
        case AnchorPreset::TopLeft:      return "TopLeft";
        case AnchorPreset::TopCenter:    return "TopCenter";
        case AnchorPreset::TopRight:     return "TopRight";
        case AnchorPreset::CenterLeft:   return "CenterLeft";
        case AnchorPreset::Center:       return "Center";
        case AnchorPreset::CenterRight:  return "CenterRight";
        case AnchorPreset::BottomLeft:   return "BottomLeft";
        case AnchorPreset::BottomCenter: return "BottomCenter";
        case AnchorPreset::BottomRight:  return "BottomRight";
        case AnchorPreset::StretchX:     return "StretchX";
        case AnchorPreset::StretchY:     return "StretchY";
        case AnchorPreset::StretchAll:   return "StretchAll";
        default:                         return "TopLeft";
    }
}

// ---------------------------------------------------------------------------
// serializeNode
// ---------------------------------------------------------------------------

nlohmann::json UISerializer::serializeNode(const UINode* node) {
    nlohmann::json j;

    j["id"]   = node->id();
    j["type"] = node->type();

    // --- Anchor ---
    {
        nlohmann::json a;
        const UIAnchor& anchor = node->anchor();
        a["preset"] = presetToString(anchor.preset);
        a["offset"] = { anchor.offset.x, anchor.offset.y };
        a["size"]   = { anchor.size.x,   anchor.size.y   };
        if (anchor.offsetPercent.x != 0.0f || anchor.offsetPercent.y != 0.0f)
            a["offsetPercent"] = { anchor.offsetPercent.x, anchor.offsetPercent.y };
        if (anchor.sizePercent.x != 0.0f || anchor.sizePercent.y != 0.0f)
            a["sizePercent"] = { anchor.sizePercent.x, anchor.sizePercent.y };

        // margin: only include if non-zero
        if (anchor.margin.x != 0.0f || anchor.margin.y != 0.0f ||
            anchor.margin.z != 0.0f || anchor.margin.w != 0.0f) {
            a["margin"] = { anchor.margin.x, anchor.margin.y,
                            anchor.margin.z, anchor.margin.w };
        }

        // padding: only include if non-zero
        if (anchor.padding.x != 0.0f || anchor.padding.y != 0.0f ||
            anchor.padding.z != 0.0f || anchor.padding.w != 0.0f) {
            a["padding"] = { anchor.padding.x, anchor.padding.y,
                             anchor.padding.z, anchor.padding.w };
        }

        // Responsive layout fields (only serialize non-default values)
        if (anchor.minSize.x > 0.0f || anchor.minSize.y > 0.0f)
            a["minSize"] = {anchor.minSize.x, anchor.minSize.y};
        if (anchor.maxSize.x > 0.0f || anchor.maxSize.y > 0.0f)
            a["maxSize"] = {anchor.maxSize.x, anchor.maxSize.y};
        if (anchor.useSafeArea)
            a["useSafeArea"] = true;
        if (anchor.maxAspectRatio > 0.0f)
            a["maxAspectRatio"] = anchor.maxAspectRatio;

        j["anchor"] = std::move(a);
    }

    // --- Style ---
    if (!node->styleName().empty()) {
        j["style"] = node->styleName();
    }

    // --- Inline style overrides (saved when non-default) ---
    {
        const auto& s = node->resolvedStyle();
        auto colorArr = [](const Color& c) { return nlohmann::json::array({
            static_cast<int>(c.r * 255.0f + 0.5f), static_cast<int>(c.g * 255.0f + 0.5f),
            static_cast<int>(c.b * 255.0f + 0.5f), static_cast<int>(c.a * 255.0f + 0.5f)
        }); };
        if (s.backgroundColor.a > 0.0f)  j["backgroundColor"] = colorArr(s.backgroundColor);
        if (s.borderColor.a > 0.0f)       j["borderColor"]      = colorArr(s.borderColor);
        if (s.borderWidth > 0.0f)         j["borderWidth"]      = s.borderWidth;
        if (s.textColor.r < 1.0f || s.textColor.g < 1.0f || s.textColor.b < 1.0f || s.textColor.a < 1.0f)
            j["textColor"] = colorArr(s.textColor);
        if (s.fontSize != 14.0f)          j["fontSize"]         = s.fontSize;
        if (s.opacity != 1.0f)            j["opacity"]          = s.opacity;
        // Rounded rect
        if (s.cornerRadius > 0.0f)        j["cornerRadius"]  = s.cornerRadius;
        if (s.gradientTop.a > 0.0f)       j["gradientTop"]   = colorArr(s.gradientTop);
        if (s.gradientBottom.a > 0.0f)    j["gradientBottom"] = colorArr(s.gradientBottom);
        if (s.shadowOffset.x != 0.0f || s.shadowOffset.y != 0.0f)
                                           j["uiShadowOffset"]  = nlohmann::json::array({s.shadowOffset.x, s.shadowOffset.y});
        if (s.shadowBlur > 0.0f)          j["shadowBlur"]    = s.shadowBlur;
        if (s.shadowColor.a > 0.0f)       j["uiShadowColor"]   = colorArr(s.shadowColor);
        // Text effects
        if (s.textStyle != TextStyle::Normal) j["textStyle"] = static_cast<int>(s.textStyle);
        if (s.textStyle != TextStyle::Normal) {
            auto& te = s.textEffects;
            nlohmann::json tej;
            tej["outlineColor"]     = colorArr(te.outlineColor);
            tej["outlineWidth"]     = te.outlineWidth;
            tej["shadowOffset"]     = nlohmann::json::array({te.shadowOffset.x, te.shadowOffset.y});
            tej["shadowColor"]      = colorArr(te.shadowColor);
            tej["glowColor"]        = colorArr(te.glowColor);
            tej["glowRadius"]       = te.glowRadius;
            j["textEffects"]        = tej;
        }
        // fontName (was missing from inline serialization)
        if (!s.fontName.empty())          j["fontName"] = s.fontName;
    }

    // --- zOrder ---
    if (node->zOrder() != 0) {
        j["zOrder"] = node->zOrder();
    }

    // --- Visible ---
    if (!node->visible()) {
        j["visible"] = false;
    }

    // --- Widget-specific properties ---
    // Reflected properties (new system) — auto-serializes from metadata
    auto reflectedFields = node->reflectedProperties();
    if (!reflectedFields.empty()) {
        node->serializeProperties(j);
    } else {
#ifdef FATE_HAS_GAME
    // Legacy widget-specific serialization (dynamic_cast chain)
    const std::string& type = node->type();

    if (type == "panel") {
        if (auto* w = dynamic_cast<const Panel*>(node)) {
            if (!w->title.empty())  j["title"]     = w->title;
            if (w->draggable)       j["draggable"]  = w->draggable;
            if (w->closeable)       j["closeable"]  = w->closeable;
        }
    }
    else if (type == "label") {
        if (auto* w = dynamic_cast<const Label*>(node)) {
            if (!w->text.empty())   j["text"] = w->text;
            if (w->wordWrap)        j["wordWrap"] = w->wordWrap;
            switch (w->align) {
                case TextAlign::Center: j["align"] = "center"; break;
                case TextAlign::Right:  j["align"] = "right";  break;
                default:                j["align"] = "left";   break;
            }
        }
    }
    else if (type == "button") {
        if (auto* w = dynamic_cast<const Button*>(node)) {
            if (!w->text.empty())   j["text"] = w->text;
            if (!w->icon.empty())   j["icon"] = w->icon;
        }
    }
    else if (type == "image_box") {
        if (auto* w = dynamic_cast<const ImageBox*>(node)) {
            if (!w->textureKey.empty()) j["textureKey"] = w->textureKey;
            j["fitMode"] = (w->fitMode == ImageFitMode::Stretch) ? "stretch" : "fit";
            if (w->tint.r != 1.0f || w->tint.g != 1.0f || w->tint.b != 1.0f || w->tint.a != 1.0f) {
                j["tint"] = { w->tint.r, w->tint.g, w->tint.b, w->tint.a };
            }
            if (w->sourceRect.x != 0.0f || w->sourceRect.y != 0.0f ||
                w->sourceRect.w != 1.0f || w->sourceRect.h != 1.0f) {
                j["sourceRect"] = { w->sourceRect.x, w->sourceRect.y,
                                    w->sourceRect.w, w->sourceRect.h };
            }
        }
    }
    else if (type == "text_input") {
        if (auto* w = dynamic_cast<const TextInput*>(node)) {
            if (!w->text.empty())        j["text"]        = w->text;
            if (!w->placeholder.empty()) j["placeholder"] = w->placeholder;
            if (w->maxLength != 0)       j["maxLength"]   = w->maxLength;
            if (w->masked)               j["masked"]      = w->masked;
        }
    }
    else if (type == "progress_bar") {
        if (auto* w = dynamic_cast<const ProgressBar*>(node)) {
            j["value"]    = w->value;
            j["maxValue"] = w->maxValue;
            j["fillColor"] = { w->fillColor.r, w->fillColor.g,
                               w->fillColor.b, w->fillColor.a };
            if (w->showText) j["showText"] = w->showText;
            switch (w->direction) {
                case BarDirection::RightToLeft:  j["direction"] = "right_to_left";  break;
                case BarDirection::BottomToTop:  j["direction"] = "bottom_to_top";  break;
                case BarDirection::TopToBottom:  j["direction"] = "top_to_bottom";  break;
                default:                         j["direction"] = "left_to_right";  break;
            }
        }
    }
    else if (type == "window") {
        if (auto* w = dynamic_cast<const Window*>(node)) {
            if (!w->title.empty())  j["title"]          = w->title;
            j["closeable"]          = w->closeable;
            if (w->resizable)       j["resizable"]      = w->resizable;
            if (w->minimizable)     j["minimizable"]    = w->minimizable;
            if (w->titleBarHeight != 28.0f)
                j["titleBarHeight"] = w->titleBarHeight;
        }
    }
    else if (type == "tab_container") {
        if (auto* w = dynamic_cast<const TabContainer*>(node)) {
            if (!w->tabLabels_.empty()) {
                j["tabs"] = w->tabLabels_;
            }
            if (w->activeTab != 0)    j["activeTab"]  = w->activeTab;
            if (w->tabHeight != 30.0f) j["tabHeight"] = w->tabHeight;
        }
    }
    else if (type == "slot_grid") {
        if (auto* w = dynamic_cast<const SlotGrid*>(node)) {
            j["columns"]    = w->columns;
            j["rows"]       = w->rows;
            j["slotSize"]   = w->slotSize;
            j["slotPadding"] = w->slotPadding;
            if (!w->acceptsDragType.empty())
                j["acceptsDrag"] = w->acceptsDragType;
        }
        // slot_grid children are auto-generated — skip the children serialization below
        return j;
    }
    else if (type == "slot") {
        if (auto* w = dynamic_cast<const Slot*>(node)) {
            if (!w->itemId.empty())         j["itemId"]      = w->itemId;
            if (w->quantity != 0)           j["quantity"]    = w->quantity;
            if (!w->icon.empty())           j["icon"]        = w->icon;
            if (!w->slotType.empty())       j["slotType"]    = w->slotType;
            if (!w->acceptsDragType.empty()) j["acceptsDrag"] = w->acceptsDragType;
        }
    }
    else if (type == "player_info_block") {
        if (auto* w = dynamic_cast<const PlayerInfoBlock*>(node)) {
            j["portraitOffset"] = {w->portraitOffset.x, w->portraitOffset.y};
            j["barOffset"]      = {w->barOffset.x, w->barOffset.y};
            j["levelOffset"]    = {w->levelOffset.x, w->levelOffset.y};
            j["goldOffset"]     = {w->goldOffset.x, w->goldOffset.y};
            j["portraitSize"] = w->portraitSize;
            j["barWidth"]     = w->barWidth;
            j["barHeight"]    = w->barHeight;
            j["barSpacing"]   = w->barSpacing;
            j["overlayFontSize"] = w->overlayFontSize;
            j["goldFontSize"]    = w->goldFontSize;
            j["portraitFillColor"]   = {w->portraitFillColor.r, w->portraitFillColor.g, w->portraitFillColor.b, w->portraitFillColor.a};
            j["portraitBorderColor"] = {w->portraitBorderColor.r, w->portraitBorderColor.g, w->portraitBorderColor.b, w->portraitBorderColor.a};
            j["barBgColor"]          = {w->barBgColor.r, w->barBgColor.g, w->barBgColor.b, w->barBgColor.a};
            j["barBorderColor"]      = {w->barBorderColor.r, w->barBorderColor.g, w->barBorderColor.b, w->barBorderColor.a};
            j["hpFillColor"]         = {w->hpFillColor.r, w->hpFillColor.g, w->hpFillColor.b, w->hpFillColor.a};
            j["mpFillColor"]         = {w->mpFillColor.r, w->mpFillColor.g, w->mpFillColor.b, w->mpFillColor.a};
            j["textShadowColor"]     = {w->textShadowColor.r, w->textShadowColor.g, w->textShadowColor.b, w->textShadowColor.a};
            j["goldTextColor"]       = {w->goldTextColor.r, w->goldTextColor.g, w->goldTextColor.b, w->goldTextColor.a};
        }
    }
    else if (type == "skill_arc") {
        if (auto* w = dynamic_cast<const SkillArc*>(node)) {
            j["attackButtonSize"] = w->attackButtonSize;
            j["pickUpButtonSize"] = w->pickUpButtonSize;
            j["slotSize"]         = w->slotSize;
            j["arcRadius"]        = w->arcRadius;
            j["slotCount"]        = w->slotCount;
            j["startAngleDeg"]    = w->startAngleDeg;
            j["endAngleDeg"]      = w->endAngleDeg;
            j["skillArcOffset"]   = {w->skillArcOffset.x, w->skillArcOffset.y};
            j["attackOffset"]     = {w->attackOffset.x, w->attackOffset.y};
            j["pickUpOffset"]     = {w->pickUpOffset.x, w->pickUpOffset.y};
            j["slotArcRadius"]    = w->slotArcRadius;
            j["slotArcStartDeg"]  = w->slotArcStartDeg;
            j["slotArcEndDeg"]    = w->slotArcEndDeg;
            j["slotArcOffset"]    = {w->slotArcOffset.x, w->slotArcOffset.y};
        }
    }
    else if (type == "fate_status_bar") {
        if (auto* w = dynamic_cast<const FateStatusBar*>(node)) {
            j["portraitOffset"] = {w->portraitOffset.x, w->portraitOffset.y};
            j["levelOffset"]   = {w->levelOffset.x, w->levelOffset.y};
            j["hpLabelOffset"] = {w->hpLabelOffset.x, w->hpLabelOffset.y};
            j["mpLabelOffset"] = {w->mpLabelOffset.x, w->mpLabelOffset.y};
            j["menuBtnOffset"] = {w->menuBtnOffset.x, w->menuBtnOffset.y};
            j["chatBtnOffset"] = {w->chatBtnOffset.x, w->chatBtnOffset.y};
            j["topBarHeight"]   = w->topBarHeight;
            j["portraitRadius"] = w->portraitRadius;
            j["barHeight"]      = w->barHeight;
            j["menuBtnSize"]    = w->menuBtnSize;
            j["chatBtnSize"]    = w->chatBtnSize;
            j["chatBtnOffsetX"] = w->chatBtnOffsetX;
            j["menuBtnGap"]     = w->menuBtnGap;
            j["coordOffsetY"]   = w->coordOffsetY;
            j["levelFontSize"]  = w->levelFontSize;
            j["labelFontSize"]  = w->labelFontSize;
            j["numberFontSize"] = w->numberFontSize;
            j["coordFontSize"]  = w->coordFontSize;
            j["buttonFontSize"] = w->buttonFontSize;
            j["menuBtnFontSize"] = w->menuBtnFontSize;
            j["chatBtnFontSize"] = w->chatBtnFontSize;
            j["hpBarColor"] = {w->hpBarColor.r, w->hpBarColor.g, w->hpBarColor.b, w->hpBarColor.a};
            j["mpBarColor"] = {w->mpBarColor.r, w->mpBarColor.g, w->mpBarColor.b, w->mpBarColor.a};
            j["coordColor"] = {w->coordColor.r, w->coordColor.g, w->coordColor.b, w->coordColor.a};
            j["menuBtnTextColor"] = {w->menuBtnTextColor.r, w->menuBtnTextColor.g, w->menuBtnTextColor.b, w->menuBtnTextColor.a};
            j["chatBtnTextColor"] = {w->chatBtnTextColor.r, w->chatBtnTextColor.g, w->chatBtnTextColor.b, w->chatBtnTextColor.a};
            j["menuBtnBgColor"] = {w->menuBtnBgColor.r, w->menuBtnBgColor.g, w->menuBtnBgColor.b, w->menuBtnBgColor.a};
            j["menuOverlayW"]    = w->menuOverlayW;
            j["menuItemH"]       = w->menuItemH;
            j["menuItemFontSize"] = w->menuItemFontSize;
            j["menuItemTextColor"] = {w->menuItemTextColor.r, w->menuItemTextColor.g, w->menuItemTextColor.b, w->menuItemTextColor.a};
            j["menuOverlayBgColor"] = {w->menuOverlayBgColor.r, w->menuOverlayBgColor.g, w->menuOverlayBgColor.b, w->menuOverlayBgColor.a};
            j["menuOverlayBorderColor"] = {w->menuOverlayBorderColor.r, w->menuOverlayBorderColor.g, w->menuOverlayBorderColor.b, w->menuOverlayBorderColor.a};
            j["menuDividerColor"] = {w->menuDividerColor.r, w->menuDividerColor.g, w->menuDividerColor.b, w->menuDividerColor.a};
            if (!w->showCoordinates) j["showCoordinates"] = false;
            if (!w->showMenuButton)  j["showMenuButton"]  = false;
            if (!w->showChatButton)  j["showChatButton"]  = false;
        }
    }
    else if (type == "dpad") {
        if (auto* w = dynamic_cast<const DPad*>(node)) {
            j["dpadSize"]       = w->dpadSize;
            j["deadZoneRadius"] = w->deadZoneRadius;
            j["opacity"]        = w->opacity;
        }
    }
    else if (type == "menu_button_row") {
        if (auto* w = dynamic_cast<const MenuButtonRow*>(node)) {
            j["buttonSize"] = w->buttonSize;
            j["spacing"]    = w->spacing;
            if (!w->labels.empty())
                j["labels"] = nlohmann::json(w->labels);
        }
    }
    else if (type == "menu_tab_bar") {
        if (auto* w = dynamic_cast<const MenuTabBar*>(node)) {
            j["activeTab"] = w->activeTab;
            j["tabSize"]   = w->tabSize;
            j["arrowSize"] = w->arrowSize;
            j["tabFontSize"]   = w->tabFontSize;
            j["arrowFontSize"] = w->arrowFontSize;
            j["borderWidth"]   = w->borderWidth;
            j["activeTabBg"]       = {w->activeTabBg.r, w->activeTabBg.g, w->activeTabBg.b, w->activeTabBg.a};
            j["inactiveTabBg"]     = {w->inactiveTabBg.r, w->inactiveTabBg.g, w->inactiveTabBg.b, w->inactiveTabBg.a};
            j["arrowBg"]           = {w->arrowBg.r, w->arrowBg.g, w->arrowBg.b, w->arrowBg.a};
            j["borderColor"]       = {w->borderColor.r, w->borderColor.g, w->borderColor.b, w->borderColor.a};
            j["activeTextColor"]   = {w->activeTextColor.r, w->activeTextColor.g, w->activeTextColor.b, w->activeTextColor.a};
            j["inactiveTextColor"] = {w->inactiveTextColor.r, w->inactiveTextColor.g, w->inactiveTextColor.b, w->inactiveTextColor.a};
            j["arrowTextColor"]    = {w->arrowTextColor.r, w->arrowTextColor.g, w->arrowTextColor.b, w->arrowTextColor.a};
            j["highlightColor"]    = {w->highlightColor.r, w->highlightColor.g, w->highlightColor.b, w->highlightColor.a};
            if (!w->tabLabels.empty())
                j["tabLabels"] = nlohmann::json(w->tabLabels);
        }
    }
    else if (type == "chat_ticker") {
        if (auto* w = dynamic_cast<const ChatTicker*>(node)) {
            j["scrollSpeed"] = w->scrollSpeed;
        }
    }
    else if (type == "exp_bar") {
        if (auto* w = dynamic_cast<const EXPBar*>(node)) {
            j["textOffset"] = {w->textOffset.x, w->textOffset.y};
            j["xp"]        = w->xp;
            j["xpToLevel"] = w->xpToLevel;
            j["fontSize"]    = w->fontSize;
            j["fillColor"]   = {w->fillColor.r, w->fillColor.g, w->fillColor.b, w->fillColor.a};
            j["shadowColor"] = {w->shadowColor.r, w->shadowColor.g, w->shadowColor.b, w->shadowColor.a};
        }
    }
    else if (type == "target_frame") {
        if (auto* w = dynamic_cast<const TargetFrame*>(node)) {
            j["nameOffset"]   = {w->nameOffset.x, w->nameOffset.y};
            j["hpBarOffset"]  = {w->hpBarOffset.x, w->hpBarOffset.y};
            j["hpTextOffset"] = {w->hpTextOffset.x, w->hpTextOffset.y};
            if (!w->targetName.empty())
                j["targetName"] = w->targetName;
            j["nameFontSize"] = w->nameFontSize;
            j["hpFontSize"]   = w->hpFontSize;
            j["barPadding"]   = w->barPadding;
            j["barHeight"]    = w->barHeight;
            j["nameTopPad"]   = w->nameTopPad;
            j["hpBarBgColor"] = {w->hpBarBgColor.r, w->hpBarBgColor.g, w->hpBarBgColor.b, w->hpBarBgColor.a};
            j["hpFillColor"]  = {w->hpFillColor.r, w->hpFillColor.g, w->hpFillColor.b, w->hpFillColor.a};
        }
    }
    else if (type == "left_sidebar") {
        if (auto* w = dynamic_cast<const LeftSidebar*>(node)) {
            j["buttonSize"] = w->buttonSize;
            j["spacing"]    = w->spacing;
            if (!w->panelLabels.empty())
                j["labels"] = nlohmann::json(w->panelLabels);
            if (!w->activePanel.empty())
                j["activePanel"] = w->activePanel;
        }
    }
    else if (type == "inventory_panel") {
        if (auto* w = dynamic_cast<const InventoryPanel*>(node)) {
            j["gridColumns"]    = w->gridColumns;
            j["gridRows"]       = w->gridRows;
            j["slotSize"]       = w->slotSize;
            j["equipSlotSize"]  = w->equipSlotSize;
            j["dollWidthRatio"] = w->dollWidthRatio;
            j["contentPadding"] = w->contentPadding;
            j["currencyHeight"] = w->currencyHeight;
            j["platOffsetX"]    = w->platOffsetX;
            j["platOffsetY"]    = w->platOffsetY;
            j["gridPadding"]    = w->gridPadding;
            j["dollCenterY"]    = w->dollCenterY;
            j["characterScale"] = w->characterScale;
            nlohmann::json slots = nlohmann::json::array();
            for (int i = 0; i < InventoryPanel::NUM_EQUIP_SLOTS; ++i) {
                slots.push_back({
                    {"offsetX", w->equipLayout[i].offsetX},
                    {"offsetY", w->equipLayout[i].offsetY},
                    {"sizeMul", w->equipLayout[i].sizeMul}
                });
            }
            j["equipLayout"] = slots;

            // Slot appearance
            j["slotFilledBgColor"]    = {w->slotFilledBgColor.r, w->slotFilledBgColor.g, w->slotFilledBgColor.b, w->slotFilledBgColor.a};
            j["slotEmptyBgColor"]     = {w->slotEmptyBgColor.r, w->slotEmptyBgColor.g, w->slotEmptyBgColor.b, w->slotEmptyBgColor.a};
            j["slotEmptyBorderColor"] = {w->slotEmptyBorderColor.r, w->slotEmptyBorderColor.g, w->slotEmptyBorderColor.b, w->slotEmptyBorderColor.a};
            j["slotBorderWidth"]      = w->slotBorderWidth;

            // Paper doll inset
            j["dollInsetBgColor"]     = {w->dollInsetBgColor.r, w->dollInsetBgColor.g, w->dollInsetBgColor.b, w->dollInsetBgColor.a};
            j["dollInsetBorderColor"] = {w->dollInsetBorderColor.r, w->dollInsetBorderColor.g, w->dollInsetBorderColor.b, w->dollInsetBorderColor.a};
            j["dollInsetBorderW"]     = w->dollInsetBorderW;
            j["equipLabelGap"]        = w->equipLabelGap;

            // Font sizes
            j["itemFontSize"]          = w->itemFontSize;
            j["quantityFontSize"]      = w->quantityFontSize;
            j["currencyFontSize"]      = w->currencyFontSize;
            j["currencyLabelFontSize"] = w->currencyLabelFontSize;
            j["equipLabelFontSize"]    = w->equipLabelFontSize;

            // Colors
            j["quantityColor"]   = {w->quantityColor.r, w->quantityColor.g, w->quantityColor.b, w->quantityColor.a};
            j["itemTextColor"]   = {w->itemTextColor.r, w->itemTextColor.g, w->itemTextColor.b, w->itemTextColor.a};
            j["equipLabelColor"] = {w->equipLabelColor.r, w->equipLabelColor.g, w->equipLabelColor.b, w->equipLabelColor.a};
            j["goldLabelColor"]  = {w->goldLabelColor.r, w->goldLabelColor.g, w->goldLabelColor.b, w->goldLabelColor.a};
            j["goldValueColor"]  = {w->goldValueColor.r, w->goldValueColor.g, w->goldValueColor.b, w->goldValueColor.a};
            j["platLabelColor"]  = {w->platLabelColor.r, w->platLabelColor.g, w->platLabelColor.b, w->platLabelColor.a};
            j["platValueColor"]  = {w->platValueColor.r, w->platValueColor.g, w->platValueColor.b, w->platValueColor.a};

            // Tooltip layout
            j["tooltipWidth"]       = w->tooltipWidth;
            j["tooltipPadding"]     = w->tooltipPadding;
            j["tooltipOffset"]      = w->tooltipOffset;
            j["tooltipShadowOffset"]= w->tooltipShadowOffset;
            j["tooltipLineSpacing"] = w->tooltipLineSpacing;
            j["tooltipBorderWidth"] = w->tooltipBorderWidth;
            j["tooltipSepHeight"]   = w->tooltipSepHeight;

            // Tooltip font sizes
            j["tooltipNameFontSize"]  = w->tooltipNameFontSize;
            j["tooltipStatFontSize"]  = w->tooltipStatFontSize;
            j["tooltipLevelFontSize"] = w->tooltipLevelFontSize;

            // Tooltip colors
            j["tooltipBgColor"]      = {w->tooltipBgColor.r, w->tooltipBgColor.g, w->tooltipBgColor.b, w->tooltipBgColor.a};
            j["tooltipBorderColor"]  = {w->tooltipBorderColor.r, w->tooltipBorderColor.g, w->tooltipBorderColor.b, w->tooltipBorderColor.a};
            j["tooltipShadowColor"]  = {w->tooltipShadowColor.r, w->tooltipShadowColor.g, w->tooltipShadowColor.b, w->tooltipShadowColor.a};
            j["tooltipStatColor"]    = {w->tooltipStatColor.r, w->tooltipStatColor.g, w->tooltipStatColor.b, w->tooltipStatColor.a};
            j["tooltipSepColor"]     = {w->tooltipSepColor.r, w->tooltipSepColor.g, w->tooltipSepColor.b, w->tooltipSepColor.a};
            j["tooltipLevelColor"]   = {w->tooltipLevelColor.r, w->tooltipLevelColor.g, w->tooltipLevelColor.b, w->tooltipLevelColor.a};

            // Rarity colors
            j["rarityCommonColor"]    = {w->rarityCommonColor.r, w->rarityCommonColor.g, w->rarityCommonColor.b, w->rarityCommonColor.a};
            j["rarityUncommonColor"]  = {w->rarityUncommonColor.r, w->rarityUncommonColor.g, w->rarityUncommonColor.b, w->rarityUncommonColor.a};
            j["rarityRareColor"]      = {w->rarityRareColor.r, w->rarityRareColor.g, w->rarityRareColor.b, w->rarityRareColor.a};
            j["rarityEpicColor"]      = {w->rarityEpicColor.r, w->rarityEpicColor.g, w->rarityEpicColor.b, w->rarityEpicColor.a};
            j["rarityLegendaryColor"] = {w->rarityLegendaryColor.r, w->rarityLegendaryColor.g, w->rarityLegendaryColor.b, w->rarityLegendaryColor.a};

            // Close button
            j["closeBtnRadius"]   = w->closeBtnRadius;
            j["closeBtnOffset"]   = w->closeBtnOffset;
            j["closeBtnBorderW"]  = w->closeBtnBorderW;
            j["closeBtnFontSize"] = w->closeBtnFontSize;
            j["closeBtnBgColor"]     = {w->closeBtnBgColor.r, w->closeBtnBgColor.g, w->closeBtnBgColor.b, w->closeBtnBgColor.a};
            j["closeBtnBorderColor"] = {w->closeBtnBorderColor.r, w->closeBtnBorderColor.g, w->closeBtnBorderColor.b, w->closeBtnBorderColor.a};
            j["closeBtnTextColor"]   = {w->closeBtnTextColor.r, w->closeBtnTextColor.g, w->closeBtnTextColor.b, w->closeBtnTextColor.a};

            // Context menu
            j["ctxMenuWidth"]      = w->ctxMenuWidth;
            j["ctxMenuItemHeight"] = w->ctxMenuItemHeight;
            j["ctxMenuPadding"]    = w->ctxMenuPadding;
            j["ctxMenuBorderW"]    = w->ctxMenuBorderW;
            j["ctxMenuFontSize"]   = w->ctxMenuFontSize;
            j["ctxMenuTextPadX"]   = w->ctxMenuTextPadX;
            j["ctxMenuBgColor"]       = {w->ctxMenuBgColor.r, w->ctxMenuBgColor.g, w->ctxMenuBgColor.b, w->ctxMenuBgColor.a};
            j["ctxMenuBorderColor"]   = {w->ctxMenuBorderColor.r, w->ctxMenuBorderColor.g, w->ctxMenuBorderColor.b, w->ctxMenuBorderColor.a};
            j["ctxMenuTextColor"]     = {w->ctxMenuTextColor.r, w->ctxMenuTextColor.g, w->ctxMenuTextColor.b, w->ctxMenuTextColor.a};
            j["ctxMenuDisabledColor"] = {w->ctxMenuDisabledColor.r, w->ctxMenuDisabledColor.g, w->ctxMenuDisabledColor.b, w->ctxMenuDisabledColor.a};

            // Panel colors
            j["panelBgColor"]     = {w->panelBgColor.r, w->panelBgColor.g, w->panelBgColor.b, w->panelBgColor.a};
            j["panelBorderColor"] = {w->panelBorderColor.r, w->panelBorderColor.g, w->panelBorderColor.b, w->panelBorderColor.a};

            // Icon atlas
            if (!w->iconAtlasKey.empty()) j["iconAtlasKey"] = w->iconAtlasKey;
            j["iconAtlasCols"] = w->iconAtlasCols;
            j["iconAtlasRows"] = w->iconAtlasRows;
        }
    }
    else if (type == "status_panel") {
        if (auto* w = dynamic_cast<const StatusPanel*>(node)) {
            j["titleOffset"]    = {w->titleOffset.x, w->titleOffset.y};
            j["nameOffset"]     = {w->nameOffset.x, w->nameOffset.y};
            j["levelOffset"]    = {w->levelOffset.x, w->levelOffset.y};
            j["statGridOffset"] = {w->statGridOffset.x, w->statGridOffset.y};
            j["factionOffset"]  = {w->factionOffset.x, w->factionOffset.y};

            j["titleFontSize"]     = w->titleFontSize;
            j["nameFontSize"]      = w->nameFontSize;
            j["levelFontSize"]     = w->levelFontSize;
            j["statLabelFontSize"] = w->statLabelFontSize;
            j["statValueFontSize"] = w->statValueFontSize;
            j["factionFontSize"]   = w->factionFontSize;

            j["titleColor"]     = {w->titleColor.r, w->titleColor.g, w->titleColor.b, w->titleColor.a};
            j["nameColor"]      = {w->nameColor.r, w->nameColor.g, w->nameColor.b, w->nameColor.a};
            j["levelColor"]     = {w->levelColor.r, w->levelColor.g, w->levelColor.b, w->levelColor.a};
            j["statLabelColor"] = {w->statLabelColor.r, w->statLabelColor.g, w->statLabelColor.b, w->statLabelColor.a};
            j["factionColor"]   = {w->factionColor.r, w->factionColor.g, w->factionColor.b, w->factionColor.a};
        }
    }
    else if (type == "skill_panel") {
        if (auto* w = dynamic_cast<const SkillPanel*>(node)) {
            j["titleOffset"]        = {w->titleOffset.x, w->titleOffset.y};
            j["tabOffset"]          = {w->tabOffset.x, w->tabOffset.y};
            j["pointsBadgeOffset"]  = {w->pointsBadgeOffset.x, w->pointsBadgeOffset.y};
            j["skillsHeaderOffset"] = {w->skillsHeaderOffset.x, w->skillsHeaderOffset.y};
            j["activeSetPage"]    = w->activeSetPage;
            j["splitRatio"]       = w->splitRatio;
            j["gridColumns"]      = w->gridColumns;
            j["circleRadiusMul"]  = w->circleRadiusMul;
            j["dotSize"]          = w->dotSize;
            j["dotSpacing"]       = w->dotSpacing;
            j["headerHeight"]     = w->headerHeight;
            j["borderWidth"]      = w->borderWidth;
            j["contentPadding"]   = w->contentPadding;
            j["gridMargin"]       = w->gridMargin;
            j["dividerWidth"]     = w->dividerWidth;
            j["ringWidthNormal"]    = w->ringWidthNormal;
            j["ringWidthSelected"]  = w->ringWidthSelected;
            j["tabRadius"]        = w->tabRadius;
            j["tabSpacingMul"]    = w->tabSpacingMul;
            j["wheelStartDeg"]    = w->wheelStartDeg;
            j["wheelEndDeg"]      = w->wheelEndDeg;
            j["wheelSlotSizeMul"] = w->wheelSlotSizeMul;
            j["closeBtnRadius"]   = w->closeBtnRadius;
            j["closeBtnOffset"]   = w->closeBtnOffset;
            j["closeBtnBorderW"]  = w->closeBtnBorderW;
            j["closeBtnFontSize"] = w->closeBtnFontSize;
            j["ptsBadgeRadius"]   = w->ptsBadgeRadius;
            j["ptsFontSize"]      = w->ptsFontSize;
            j["slotNameFontSize"] = w->slotNameFontSize;
            j["titleFontSize"]    = w->titleFontSize;
            j["headerFontSize"]   = w->headerFontSize;
            j["nameFontSize"]     = w->nameFontSize;
            j["tabFontSize"]      = w->tabFontSize;
            j["pointsFontSize"]   = w->pointsFontSize;
            j["titleColor"]       = {w->titleColor.r, w->titleColor.g, w->titleColor.b, w->titleColor.a};
            j["headerColor"]      = {w->headerColor.r, w->headerColor.g, w->headerColor.b, w->headerColor.a};
            j["skillBgUnlocked"]  = {w->skillBgUnlocked.r, w->skillBgUnlocked.g, w->skillBgUnlocked.b, w->skillBgUnlocked.a};
            j["skillBgLocked"]    = {w->skillBgLocked.r, w->skillBgLocked.g, w->skillBgLocked.b, w->skillBgLocked.a};
            j["ringSelected"]     = {w->ringSelected.r, w->ringSelected.g, w->ringSelected.b, w->ringSelected.a};
            j["ringNormal"]       = {w->ringNormal.r, w->ringNormal.g, w->ringNormal.b, w->ringNormal.a};
            j["dotActivated"]     = {w->dotActivated.r, w->dotActivated.g, w->dotActivated.b, w->dotActivated.a};
            j["dotUnlocked"]      = {w->dotUnlocked.r, w->dotUnlocked.g, w->dotUnlocked.b, w->dotUnlocked.a};
            j["dotLocked"]        = {w->dotLocked.r, w->dotLocked.g, w->dotLocked.b, w->dotLocked.a};
            j["nameUnlocked"]     = {w->nameUnlocked.r, w->nameUnlocked.g, w->nameUnlocked.b, w->nameUnlocked.a};
            j["nameLocked"]       = {w->nameLocked.r, w->nameLocked.g, w->nameLocked.b, w->nameLocked.a};
            j["pointsBadge"]      = {w->pointsBadge.r, w->pointsBadge.g, w->pointsBadge.b, w->pointsBadge.a};
            j["pointsEmpty"]      = {w->pointsEmpty.r, w->pointsEmpty.g, w->pointsEmpty.b, w->pointsEmpty.a};
            j["dividerColor"]     = {w->dividerColor.r, w->dividerColor.g, w->dividerColor.b, w->dividerColor.a};
            j["ptsBadgeRingColor"] = {w->ptsBadgeRingColor.r, w->ptsBadgeRingColor.g, w->ptsBadgeRingColor.b, w->ptsBadgeRingColor.a};
            j["ptsTextColor"]     = {w->ptsTextColor.r, w->ptsTextColor.g, w->ptsTextColor.b, w->ptsTextColor.a};
            j["tabBgActive"]      = {w->tabBgActive.r, w->tabBgActive.g, w->tabBgActive.b, w->tabBgActive.a};
            j["tabBgInactive"]    = {w->tabBgInactive.r, w->tabBgInactive.g, w->tabBgInactive.b, w->tabBgInactive.a};
            j["tabRingActive"]    = {w->tabRingActive.r, w->tabRingActive.g, w->tabRingActive.b, w->tabRingActive.a};
            j["tabRingInactive"]  = {w->tabRingInactive.r, w->tabRingInactive.g, w->tabRingInactive.b, w->tabRingInactive.a};
            j["tabTextActive"]    = {w->tabTextActive.r, w->tabTextActive.g, w->tabTextActive.b, w->tabTextActive.a};
            j["tabTextInactive"]  = {w->tabTextInactive.r, w->tabTextInactive.g, w->tabTextInactive.b, w->tabTextInactive.a};
            j["closeBtnBgColor"]     = {w->closeBtnBgColor.r, w->closeBtnBgColor.g, w->closeBtnBgColor.b, w->closeBtnBgColor.a};
            j["closeBtnBorderColor"] = {w->closeBtnBorderColor.r, w->closeBtnBorderColor.g, w->closeBtnBorderColor.b, w->closeBtnBorderColor.a};
            j["closeBtnTextColor"]   = {w->closeBtnTextColor.r, w->closeBtnTextColor.g, w->closeBtnTextColor.b, w->closeBtnTextColor.a};
        }
    }
    else if (type == "buff_bar") {
        if (auto* w = dynamic_cast<const BuffBar*>(node)) {
            j["stackBadgeOffset"] = {w->stackBadgeOffset.x, w->stackBadgeOffset.y};
            j["iconSize"]   = w->iconSize;
            j["spacing"]    = w->spacing;
            j["maxVisible"] = w->maxVisible;
            j["stackFontSize"] = w->stackFontSize;
            j["abbrevFontSize"] = w->abbrevFontSize;
            j["stackTextColor"]      = {w->stackTextColor.r, w->stackTextColor.g, w->stackTextColor.b, w->stackTextColor.a};
            j["stackBadgeBgColor"]   = {w->stackBadgeBgColor.r, w->stackBadgeBgColor.g, w->stackBadgeBgColor.b, w->stackBadgeBgColor.a};
            j["cooldownOverlayColor"] = {w->cooldownOverlayColor.r, w->cooldownOverlayColor.g, w->cooldownOverlayColor.b, w->cooldownOverlayColor.a};
            j["abbrevTextColor"]     = {w->abbrevTextColor.r, w->abbrevTextColor.g, w->abbrevTextColor.b, w->abbrevTextColor.a};
            j["tooltipBgColor"]      = {w->tooltipBgColor.r, w->tooltipBgColor.g, w->tooltipBgColor.b, w->tooltipBgColor.a};
            j["tooltipBorderColor"]  = {w->tooltipBorderColor.r, w->tooltipBorderColor.g, w->tooltipBorderColor.b, w->tooltipBorderColor.a};
            j["tooltipTextColor"]    = {w->tooltipTextColor.r, w->tooltipTextColor.g, w->tooltipTextColor.b, w->tooltipTextColor.a};
            j["tooltipFontSize"]  = w->tooltipFontSize;
            j["tooltipPadding"]   = w->tooltipPadding;
            j["tooltipWidth"]     = w->tooltipWidth;
            if (!w->iconAtlasKey.empty()) j["iconAtlasKey"] = w->iconAtlasKey;
            j["iconAtlasCols"] = w->iconAtlasCols;
            j["iconAtlasRows"] = w->iconAtlasRows;
        }
    }
    else if (type == "boss_hp_bar") {
        if (auto* w = dynamic_cast<const BossHPBar*>(node)) {
            j["nameOffset"]    = {w->nameOffset.x, w->nameOffset.y};
            j["percentOffset"] = {w->percentOffset.x, w->percentOffset.y};
            if (!w->bossName.empty()) j["bossName"] = w->bossName;
            j["barHeight"]  = w->barHeight;
            j["barPadding"] = w->barPadding;
            j["nameFontSize"]      = w->nameFontSize;
            j["percentFontSize"]   = w->percentFontSize;
            j["nameBlockPadding"]  = w->nameBlockPadding;
            j["nameTextColor"]  = {w->nameTextColor.r, w->nameTextColor.g, w->nameTextColor.b, w->nameTextColor.a};
            j["barTrackColor"]  = {w->barTrackColor.r, w->barTrackColor.g, w->barTrackColor.b, w->barTrackColor.a};
            j["hpFillColor"]    = {w->hpFillColor.r, w->hpFillColor.g, w->hpFillColor.b, w->hpFillColor.a};
        }
    }
    else if (type == "confirm_dialog") {
        if (auto* w = dynamic_cast<const ConfirmDialog*>(node)) {
            j["messageOffset"] = {w->messageOffset.x, w->messageOffset.y};
            j["buttonOffset"]  = {w->buttonOffset.x, w->buttonOffset.y};
            j["message"]       = w->message;
            j["confirmText"]   = w->confirmText;
            j["cancelText"]    = w->cancelText;
            j["buttonWidth"]   = w->buttonWidth;
            j["buttonHeight"]  = w->buttonHeight;
            j["buttonSpacing"] = w->buttonSpacing;
            j["messageFontSize"] = w->messageFontSize;
            j["buttonFontSize"]  = w->buttonFontSize;
            j["buttonColor"]      = {w->buttonColor.r, w->buttonColor.g, w->buttonColor.b, w->buttonColor.a};
            j["buttonHoverColor"] = {w->buttonHoverColor.r, w->buttonHoverColor.g, w->buttonHoverColor.b, w->buttonHoverColor.a};
        }
    }
    else if (type == "notification_toast") {
        if (auto* w = dynamic_cast<const NotificationToast*>(node)) {
            j["textOffset"]   = {w->textOffset.x, w->textOffset.y};
            j["toastHeight"]  = w->toastHeight;
            j["toastSpacing"] = w->toastSpacing;
            j["fadeInTime"]   = w->fadeInTime;
            j["fadeOutTime"]  = w->fadeOutTime;
            j["maxToasts"]    = w->maxToasts;
            j["textFontSize"] = w->textFontSize;
            j["accentWidth"]  = w->accentWidth;
            j["textMargin"]   = w->textMargin;
            j["toastBgColor"] = {w->toastBgColor.r, w->toastBgColor.g, w->toastBgColor.b, w->toastBgColor.a};
        }
    }
    else if (type == "checkbox") {
        if (auto* w = dynamic_cast<const Checkbox*>(node)) {
            if (w->checked) j["checked"] = w->checked;
            if (!w->label.empty()) j["label"] = w->label;
            if (w->boxSize != 16.0f) j["boxSize"] = w->boxSize;
            if (w->spacing != 6.0f) j["spacing"] = w->spacing;
        }
    }
    else if (type == "login_screen") {
        if (auto* w = dynamic_cast<const LoginScreen*>(node)) {
            j["serverHost"]  = w->serverHost;
            j["serverPort"]  = w->serverPort;
            j["rememberMe"]  = w->rememberMe;
        }
    }
    else if (type == "death_overlay") {
        if (auto* w = dynamic_cast<const DeathOverlay*>(node)) {
            j["titleOffset"]     = {w->titleOffset.x, w->titleOffset.y};
            j["lossTextOffset"]  = {w->lossTextOffset.x, w->lossTextOffset.y};
            j["countdownOffset"] = {w->countdownOffset.x, w->countdownOffset.y};
            j["buttonOffset"]    = {w->buttonOffset.x, w->buttonOffset.y};

            j["titleFontSize"]     = w->titleFontSize;
            j["bodyFontSize"]      = w->bodyFontSize;
            j["countdownFontSize"] = w->countdownFontSize;
            j["buttonFontSize"]    = w->buttonFontSize;

            j["startYRatio"]    = w->startYRatio;
            j["buttonWidth"]    = w->buttonWidth;
            j["buttonHeight"]   = w->buttonHeight;
            j["buttonSpacing"]  = w->buttonSpacing;

            auto c2a = [](const Color& c) { return nlohmann::json::array({c.r, c.g, c.b, c.a}); };
            j["overlayColor"]      = c2a(w->overlayColor);
            j["titleColor"]        = c2a(w->titleColor);
            j["countdownColor"]    = c2a(w->countdownColor);
            j["buttonBgColor"]     = c2a(w->buttonBgColor);
            j["buttonBorderColor"] = c2a(w->buttonBorderColor);
            j["buttonTextColor"]   = c2a(w->buttonTextColor);
        }
    }
    else if (type == "party_frame") {
        if (auto* w = dynamic_cast<const PartyFrame*>(node)) {
            j["nameOffset"]     = {w->nameOffset.x, w->nameOffset.y};
            j["levelOffset"]    = {w->levelOffset.x, w->levelOffset.y};
            j["portraitOffset"] = {w->portraitOffset.x, w->portraitOffset.y};
            j["barOffset"]      = {w->barOffset.x, w->barOffset.y};
            j["cardWidth"]   = w->cardWidth;
            j["cardHeight"]  = w->cardHeight;
            j["cardSpacing"] = w->cardSpacing;
            j["nameFontSize"]    = w->nameFontSize;
            j["levelFontSize"]   = w->levelFontSize;
            j["portraitRadius"]  = w->portraitRadius;
            j["hpBarHeight"]     = w->hpBarHeight;
            j["mpBarHeight"]     = w->mpBarHeight;
            j["borderWidth"]     = w->borderWidth;
            j["cardBgColor"]       = {w->cardBgColor.r, w->cardBgColor.g, w->cardBgColor.b, w->cardBgColor.a};
            j["cardBorderColor"]   = {w->cardBorderColor.r, w->cardBorderColor.g, w->cardBorderColor.b, w->cardBorderColor.a};
            j["portraitFillColor"] = {w->portraitFillColor.r, w->portraitFillColor.g, w->portraitFillColor.b, w->portraitFillColor.a};
            j["portraitRimColor"]  = {w->portraitRimColor.r, w->portraitRimColor.g, w->portraitRimColor.b, w->portraitRimColor.a};
            j["crownColor"]        = {w->crownColor.r, w->crownColor.g, w->crownColor.b, w->crownColor.a};
            j["nameColor"]         = {w->nameColor.r, w->nameColor.g, w->nameColor.b, w->nameColor.a};
            j["levelColor"]        = {w->levelColor.r, w->levelColor.g, w->levelColor.b, w->levelColor.a};
            j["hpBarBgColor"]      = {w->hpBarBgColor.r, w->hpBarBgColor.g, w->hpBarBgColor.b, w->hpBarBgColor.a};
            j["hpFillColor"]       = {w->hpFillColor.r, w->hpFillColor.g, w->hpFillColor.b, w->hpFillColor.a};
            j["mpBarBgColor"]      = {w->mpBarBgColor.r, w->mpBarBgColor.g, w->mpBarBgColor.b, w->mpBarBgColor.a};
            j["mpFillColor"]       = {w->mpFillColor.r, w->mpFillColor.g, w->mpFillColor.b, w->mpFillColor.a};
        }
    }
    else if (type == "chat_panel") {
        if (auto* w = dynamic_cast<const ChatPanel*>(node)) {
            j["messageOffset"] = {w->messageOffset.x, w->messageOffset.y};
            j["inputOffset"]   = {w->inputOffset.x, w->inputOffset.y};
            j["channelOffset"] = {w->channelOffset.x, w->channelOffset.y};
            j["chatIdleLines"]    = w->chatIdleLines;
            j["fullPanelWidth"]   = w->fullPanelWidth;
            j["fullPanelHeight"]  = w->fullPanelHeight;
            j["inputBarHeight"]   = w->inputBarHeight;
            j["inputBarWidth"]    = w->inputBarWidth;
            j["channelBtnWidth"]  = w->channelBtnWidth;
            j["channelBtnHeight"] = w->channelBtnHeight;
            j["closeBtnSize"]     = w->closeBtnSize;
            j["messageFontSize"]       = w->messageFontSize;
            j["inputFontSize"]         = w->inputFontSize;
            j["channelLabelFontSize"]  = w->channelLabelFontSize;
            j["messageLineSpacing"]    = w->messageLineSpacing;
            j["messageShadowOffset"]   = w->messageShadowOffset;

            // Channel colors
            j["colorAll"]      = {w->colorAll.r, w->colorAll.g, w->colorAll.b, w->colorAll.a};
            j["colorMap"]      = {w->colorMap.r, w->colorMap.g, w->colorMap.b, w->colorMap.a};
            j["colorGlobal"]   = {w->colorGlobal.r, w->colorGlobal.g, w->colorGlobal.b, w->colorGlobal.a};
            j["colorTrade"]    = {w->colorTrade.r, w->colorTrade.g, w->colorTrade.b, w->colorTrade.a};
            j["colorParty"]    = {w->colorParty.r, w->colorParty.g, w->colorParty.b, w->colorParty.a};
            j["colorGuild"]    = {w->colorGuild.r, w->colorGuild.g, w->colorGuild.b, w->colorGuild.a};
            j["colorPrivate"]  = {w->colorPrivate.r, w->colorPrivate.g, w->colorPrivate.b, w->colorPrivate.a};

            // Faction colors
            j["factionNoneColor"]   = {w->factionNoneColor.r, w->factionNoneColor.g, w->factionNoneColor.b, w->factionNoneColor.a};
            j["factionXyrosColor"]  = {w->factionXyrosColor.r, w->factionXyrosColor.g, w->factionXyrosColor.b, w->factionXyrosColor.a};
            j["factionFenorColor"]  = {w->factionFenorColor.r, w->factionFenorColor.g, w->factionFenorColor.b, w->factionFenorColor.a};
            j["factionZethosColor"] = {w->factionZethosColor.r, w->factionZethosColor.g, w->factionZethosColor.b, w->factionZethosColor.a};
            j["factionSolisColor"]  = {w->factionSolisColor.r, w->factionSolisColor.g, w->factionSolisColor.b, w->factionSolisColor.a};

            // Message colors
            j["messageTextColor"]   = {w->messageTextColor.r, w->messageTextColor.g, w->messageTextColor.b, w->messageTextColor.a};
            j["messageShadowColor"] = {w->messageShadowColor.r, w->messageShadowColor.g, w->messageShadowColor.b, w->messageShadowColor.a};

            // Close button colors
            j["closeBtnBgColor"]     = {w->closeBtnBgColor.r, w->closeBtnBgColor.g, w->closeBtnBgColor.b, w->closeBtnBgColor.a};
            j["closeBtnBorderColor"] = {w->closeBtnBorderColor.r, w->closeBtnBorderColor.g, w->closeBtnBorderColor.b, w->closeBtnBorderColor.a};
            j["closeBtnIconColor"]   = {w->closeBtnIconColor.r, w->closeBtnIconColor.g, w->closeBtnIconColor.b, w->closeBtnIconColor.a};

            // Input bar colors
            j["inputBarBgColor"]       = {w->inputBarBgColor.r, w->inputBarBgColor.g, w->inputBarBgColor.b, w->inputBarBgColor.a};
            j["inputFieldBgColor"]     = {w->inputFieldBgColor.r, w->inputFieldBgColor.g, w->inputFieldBgColor.b, w->inputFieldBgColor.a};
            j["inputBorderColor"]      = {w->inputBorderColor.r, w->inputBorderColor.g, w->inputBorderColor.b, w->inputBorderColor.a};
            j["inputBorderFocusColor"] = {w->inputBorderFocusColor.r, w->inputBorderFocusColor.g, w->inputBorderFocusColor.b, w->inputBorderFocusColor.a};
            j["channelBtnBgColor"]     = {w->channelBtnBgColor.r, w->channelBtnBgColor.g, w->channelBtnBgColor.b, w->channelBtnBgColor.a};
            j["placeholderColor"]      = {w->placeholderColor.r, w->placeholderColor.g, w->placeholderColor.b, w->placeholderColor.a};
        }
    }
    else if (type == "scroll_view") {
        if (auto* w = dynamic_cast<const ScrollView*>(node)) {
            j["scrollSpeed"]   = w->scrollSpeed;
            j["contentHeight"] = w->contentHeight;
        }
    }
    else if (type == "character_select_screen") {
        if (auto* w = dynamic_cast<const CharacterSelectScreen*>(node)) {
            j["selectedSlot"]     = w->selectedSlot;
            // Layout
            j["slotCircleSize"]    = w->slotCircleSize;
            j["entryButtonWidth"]  = w->entryButtonWidth;
            j["slotSpacing"]       = w->slotSpacing;
            j["slotBottomMargin"]  = w->slotBottomMargin;
            j["selectedRingWidth"] = w->selectedRingWidth;
            j["normalRingWidth"]   = w->normalRingWidth;
            j["displayWidthRatio"]  = w->displayWidthRatio;
            j["displayHeightRatio"] = w->displayHeightRatio;
            j["displayTopRatio"]    = w->displayTopRatio;
            j["displayBorderWidth"] = w->displayBorderWidth;
            j["nameBgHeight"]      = w->nameBgHeight;
            j["nameBgWidthRatio"]  = w->nameBgWidthRatio;
            j["nameTextY"]         = w->nameTextY;
            j["classTextY"]        = w->classTextY;
            j["levelTextY"]        = w->levelTextY;
            j["previewScale"]      = w->previewScale;
            j["previewCenterYRatio"] = w->previewCenterYRatio;
            j["entryBtnBorderWidth"] = w->entryBtnBorderWidth;
            j["swapDeleteScale"]   = w->swapDeleteScale;
            j["swapDeleteMargin"]  = w->swapDeleteMargin;
            j["swapBtnRingWidth"]  = w->swapBtnRingWidth;
            j["deleteBtnRingWidth"] = w->deleteBtnRingWidth;
            // Dialog layout
            j["dialogWidth"]          = w->dialogWidth;
            j["dialogHeight"]         = w->dialogHeight;
            j["dialogBorderWidth"]    = w->dialogBorderWidth;
            j["dialogInputHeight"]    = w->dialogInputHeight;
            j["dialogInputPadding"]   = w->dialogInputPadding;
            j["dialogInputBorderWidth"] = w->dialogInputBorderWidth;
            j["dialogBtnWidth"]       = w->dialogBtnWidth;
            j["dialogBtnHeight"]      = w->dialogBtnHeight;
            j["dialogBtnMargin"]      = w->dialogBtnMargin;
            // Font sizes
            j["nameFontSize"]         = w->nameFontSize;
            j["classFontSize"]        = w->classFontSize;
            j["levelFontSize"]        = w->levelFontSize;
            j["emptyPromptFontSize"]  = w->emptyPromptFontSize;
            j["plusFontSize"]          = w->plusFontSize;
            j["slotLevelFontSize"]    = w->slotLevelFontSize;
            j["entryFontSize"]        = w->entryFontSize;
            j["swapFontSize"]         = w->swapFontSize;
            j["deleteFontSize"]       = w->deleteFontSize;
            j["dialogTitleFontSize"]  = w->dialogTitleFontSize;
            j["dialogPromptFontSize"] = w->dialogPromptFontSize;
            j["dialogRefNameFontSize"] = w->dialogRefNameFontSize;
            j["dialogInputFontSize"]  = w->dialogInputFontSize;
            j["dialogBtnFontSize"]    = w->dialogBtnFontSize;
            // Colors
            auto c2a = [](const Color& c) { return nlohmann::json::array({c.r, c.g, c.b, c.a}); };
            j["backgroundColor"]    = c2a(w->backgroundColor);
            j["displayBgColor"]     = c2a(w->displayBgColor);
            j["displayBorderColor"] = c2a(w->displayBorderColor);
            j["nameBgColor"]        = c2a(w->nameBgColor);
            j["nameColor"]          = c2a(w->nameColor);
            j["classColor"]         = c2a(w->classColor);
            j["levelColor"]         = c2a(w->levelColor);
            j["emptyPromptColor"]   = c2a(w->emptyPromptColor);
            j["emptySlotColor"]     = c2a(w->emptySlotColor);
            j["filledSlotColor"]    = c2a(w->filledSlotColor);
            j["selectedRingColor"]  = c2a(w->selectedRingColor);
            j["emptyRingColor"]     = c2a(w->emptyRingColor);
            j["plusColor"]           = c2a(w->plusColor);
            j["slotLevelColor"]     = c2a(w->slotLevelColor);
            j["entryBtnColor"]      = c2a(w->entryBtnColor);
            j["entryBtnBorderColor"] = c2a(w->entryBtnBorderColor);
            j["swapBtnColor"]       = c2a(w->swapBtnColor);
            j["swapBtnRingColor"]   = c2a(w->swapBtnRingColor);
            j["deleteBtnColor"]     = c2a(w->deleteBtnColor);
            j["deleteBtnRingColor"] = c2a(w->deleteBtnRingColor);
            j["dialogOverlayColor"] = c2a(w->dialogOverlayColor);
            j["dialogBgColor"]      = c2a(w->dialogBgColor);
            j["dialogBorderColor"]  = c2a(w->dialogBorderColor);
            j["dialogTitleColor"]   = c2a(w->dialogTitleColor);
            j["dialogPromptColor"]  = c2a(w->dialogPromptColor);
            j["dialogRefNameColor"] = c2a(w->dialogRefNameColor);
            j["dialogInputBgColor"] = c2a(w->dialogInputBgColor);
            j["dialogInputBorderColor"] = c2a(w->dialogInputBorderColor);
            j["dialogConfirmColor"] = c2a(w->dialogConfirmColor);
            j["dialogConfirmDisabledColor"] = c2a(w->dialogConfirmDisabledColor);
            j["dialogConfirmDisabledTextColor"] = c2a(w->dialogConfirmDisabledTextColor);
            j["dialogCancelColor"]  = c2a(w->dialogCancelColor);
        }
    }
    else if (type == "character_creation_screen") {
        if (auto* w = dynamic_cast<const CharacterCreationScreen*>(node)) {
            j["leftPanelRatio"]      = w->leftPanelRatio;
            j["previewScale"]        = w->previewScale;
            j["headerY"]             = w->headerY;
            j["genderRowY"]          = w->genderRowY;
            j["genderBtnWidth"]      = w->genderBtnWidth;
            j["genderBtnHeight"]     = w->genderBtnHeight;
            j["hairstyleRowY"]       = w->hairstyleRowY;
            j["hairstyleBtnSize"]    = w->hairstyleBtnSize;
            j["classRowY"]           = w->classRowY;
            j["classBtnSize"]        = w->classBtnSize;
            j["factionRowY"]         = w->factionRowY;
            j["factionRadius"]       = w->factionRadius;
            j["genderGap"]           = w->genderGap;
            j["genderBorderWidth"]   = w->genderBorderWidth;
            j["genderSelBorderWidth"] = w->genderSelBorderWidth;
            j["hairstyleGap"]        = w->hairstyleGap;
            j["hairstyleRingWidth"]  = w->hairstyleRingWidth;
            j["hairstyleSelRingWidth"] = w->hairstyleSelRingWidth;
            j["hairstyleLabelGap"]   = w->hairstyleLabelGap;
            j["classGap"]            = w->classGap;
            j["classRingWidth"]      = w->classRingWidth;
            j["classSelRingWidth"]   = w->classSelRingWidth;
            j["classNameGap"]        = w->classNameGap;
            j["classDescGap"]        = w->classDescGap;
            j["classDescPadX"]       = w->classDescPadX;
            j["factionGap"]          = w->factionGap;
            j["factionSelScale"]     = w->factionSelScale;
            j["factionSelRingWidth"] = w->factionSelRingWidth;
            j["factionNameGap"]      = w->factionNameGap;
            j["backBtnRadius"]       = w->backBtnRadius;
            j["backBtnOffsetX"]      = w->backBtnOffsetX;
            j["backBtnOffsetY"]      = w->backBtnOffsetY;
            j["backBtnRingWidth"]    = w->backBtnRingWidth;
            j["nameFieldY"]          = w->nameFieldY;
            j["nameFieldHeight"]     = w->nameFieldHeight;
            j["nameFieldPadX"]       = w->nameFieldPadX;
            j["nameFieldBorderWidth"] = w->nameFieldBorderWidth;
            j["nameFieldLabelGap"]   = w->nameFieldLabelGap;
            j["nameFieldTextPad"]    = w->nameFieldTextPad;
            j["nameFieldCursorWidth"] = w->nameFieldCursorWidth;
            j["nameFieldCursorPad"]  = w->nameFieldCursorPad;
            j["nextBtnHeight"]       = w->nextBtnHeight;
            j["nextBtnBottomMargin"] = w->nextBtnBottomMargin;
            j["nextBtnPadX"]         = w->nextBtnPadX;
            j["nextBtnBorderWidth"]  = w->nextBtnBorderWidth;
            j["statusGap"]           = w->statusGap;
            j["dividerWidth"]        = w->dividerWidth;
            j["headerFontSize"]         = w->headerFontSize;
            j["classFontSize"]          = w->classFontSize;
            j["classInitialFontSize"]   = w->classInitialFontSize;
            j["descFontSize"]           = w->descFontSize;
            j["buttonFontSize"]         = w->buttonFontSize;
            j["labelFontSize"]          = w->labelFontSize;
            j["nameLabelFontSize"]      = w->nameLabelFontSize;
            j["nameInputFontSize"]      = w->nameInputFontSize;
            j["nextBtnFontSize"]        = w->nextBtnFontSize;
            j["statusFontSize"]         = w->statusFontSize;
            j["factionInitialFontSize"] = w->factionInitialFontSize;
            j["factionNameFontSize"]    = w->factionNameFontSize;
            j["backBtnFontSize"]        = w->backBtnFontSize;
            auto c2a = [](const Color& c) { return nlohmann::json::array({c.r, c.g, c.b, c.a}); };
            j["backgroundColor"]           = c2a(w->backgroundColor);
            j["leftPanelColor"]            = c2a(w->leftPanelColor);
            j["headerColor"]               = c2a(w->headerColor);
            j["selectedColor"]             = c2a(w->selectedColor);
            j["selectedBgColor"]           = c2a(w->selectedBgColor);
            j["unselectedBgColor"]         = c2a(w->unselectedBgColor);
            j["unselectedBorderColor"]     = c2a(w->unselectedBorderColor);
            j["unselectedTextColor"]       = c2a(w->unselectedTextColor);
            j["labelColor"]                = c2a(w->labelColor);
            j["descColor"]                 = c2a(w->descColor);
            j["nameFieldBgColor"]          = c2a(w->nameFieldBgColor);
            j["nameFieldFocusBgColor"]     = c2a(w->nameFieldFocusBgColor);
            j["nameFieldBorderColor"]      = c2a(w->nameFieldBorderColor);
            j["nameFieldFocusBorderColor"] = c2a(w->nameFieldFocusBorderColor);
            j["nameLabelColor"]            = c2a(w->nameLabelColor);
            j["placeholderColor"]          = c2a(w->placeholderColor);
            j["nextBtnColor"]              = c2a(w->nextBtnColor);
            j["nextBtnBorderColor"]        = c2a(w->nextBtnBorderColor);
            j["backBtnColor"]              = c2a(w->backBtnColor);
            j["backBtnBorderColor"]        = c2a(w->backBtnBorderColor);
            j["errorColor"]                = c2a(w->errorColor);
            j["successColor"]              = c2a(w->successColor);
        }
    }
    else if (type == "guild_panel") {
        if (auto* w = dynamic_cast<const GuildPanel*>(node)) {
            j["titleOffset"]     = {w->titleOffset.x, w->titleOffset.y};
            j["emblemOffset"]    = {w->emblemOffset.x, w->emblemOffset.y};
            j["guildInfoOffset"] = {w->guildInfoOffset.x, w->guildInfoOffset.y};
            j["rosterOffset"]    = {w->rosterOffset.x, w->rosterOffset.y};
            j["titleFontSize"]        = w->titleFontSize;
            j["guildNameFontSize"]    = w->guildNameFontSize;
            j["infoFontSize"]         = w->infoFontSize;
            j["rosterHeaderFontSize"] = w->rosterHeaderFontSize;
            j["rosterRowFontSize"]    = w->rosterRowFontSize;
            j["headerHeight"]       = w->headerHeight;
            j["emblemSize"]         = w->emblemSize;
            j["closeRadius"]        = w->closeRadius;
            j["rosterHeaderHeight"] = w->rosterHeaderHeight;
            j["rosterRowHeight"]    = w->rosterRowHeight;
            j["borderWidth"]        = w->borderWidth;
            auto c2a = [](const Color& c) { return nlohmann::json::array({c.r, c.g, c.b, c.a}); };
            j["bgColor"]               = c2a(w->bgColor);
            j["borderColor"]           = c2a(w->borderColor);
            j["titleColor"]            = c2a(w->titleColor);
            j["closeBgColor"]          = c2a(w->closeBgColor);
            j["closeBorderColor"]      = c2a(w->closeBorderColor);
            j["dividerColor"]          = c2a(w->dividerColor);
            j["guildNameColor"]        = c2a(w->guildNameColor);
            j["infoColor"]             = c2a(w->infoColor);
            j["rosterHeaderBgColor"]   = c2a(w->rosterHeaderBgColor);
            j["rosterHeaderTextColor"] = c2a(w->rosterHeaderTextColor);
            j["rosterRowTextColor"]    = c2a(w->rosterRowTextColor);
            j["onlineColor"]           = c2a(w->onlineColor);
            j["offlineColor"]          = c2a(w->offlineColor);
        }
    }
    else if (type == "npc_dialogue_panel") {
        if (auto* w = dynamic_cast<const NpcDialoguePanel*>(node)) {
            j["titleOffset"]  = {w->titleOffset.x, w->titleOffset.y};
            j["textOffset"]   = {w->textOffset.x, w->textOffset.y};
            j["buttonOffset"] = {w->buttonOffset.x, w->buttonOffset.y};
            j["titleFontSize"]      = w->titleFontSize;
            j["bodyFontSize"]       = w->bodyFontSize;
            j["buttonFontSize"]     = w->buttonFontSize;
            j["closeFontSize"]      = w->closeFontSize;
            j["questNameFontSize"]  = w->questNameFontSize;
            j["questStatusFontSize"] = w->questStatusFontSize;
            j["titleBarHeight"]     = w->titleBarHeight;
            j["buttonHeight"]       = w->buttonHeight;
            j["buttonMargin"]       = w->buttonMargin;
            j["buttonGap"]          = w->buttonGap;
            j["questRowHeight"]     = w->questRowHeight;
            j["closeCircleRadius"]  = w->closeCircleRadius;
            j["borderWidth"]        = w->borderWidth;
            auto c2a = [](const Color& c) { return nlohmann::json::array({c.r, c.g, c.b, c.a}); };
            j["bgColor"]            = c2a(w->bgColor);
            j["textColor"]          = c2a(w->textColor);
            j["titleColor"]         = c2a(w->titleColor);
            j["goldColor"]          = c2a(w->goldColor);
            j["buttonBgColor"]      = c2a(w->buttonBgColor);
            j["buttonBorderColor"]  = c2a(w->buttonBorderColor);
            j["closeBgColor"]       = c2a(w->closeBgColor);
            j["dividerColor"]       = c2a(w->dividerColor);
        }
    }
    else if (type == "shop_panel") {
        if (auto* w = dynamic_cast<const ShopPanel*>(node)) {
            j["titleOffset"]     = {w->titleOffset.x, w->titleOffset.y};
            j["shopListOffset"]  = {w->shopListOffset.x, w->shopListOffset.y};
            j["inventoryOffset"] = {w->inventoryOffset.x, w->inventoryOffset.y};
            j["goldOffset"]      = {w->goldOffset.x, w->goldOffset.y};
            j["titleFontSize"]   = w->titleFontSize;
            j["headerFontSize"]  = w->headerFontSize;
            j["itemFontSize"]    = w->itemFontSize;
            j["priceFontSize"]   = w->priceFontSize;
            j["goldFontSize"]    = w->goldFontSize;
            j["quantityFontSize"]= w->quantityFontSize;
            j["headerHeight"]    = w->headerHeight;
            j["rowHeight"]       = w->rowHeight;
            j["slotSize"]        = w->slotSize;
            j["goldBarHeight"]   = w->goldBarHeight;
            j["buyBtnWidth"]     = w->buyBtnWidth;
            j["buyBtnHeight"]    = w->buyBtnHeight;
            j["gridPadding"]     = w->gridPadding;
            auto c2a = [](const Color& c) { return nlohmann::json::array({c.r, c.g, c.b, c.a}); };
            j["bgColor"]             = c2a(w->bgColor);
            j["borderColor"]         = c2a(w->borderColor);
            j["titleColor"]          = c2a(w->titleColor);
            j["headerBgColor"]       = c2a(w->headerBgColor);
            j["textColor"]           = c2a(w->textColor);
            j["goldColor"]           = c2a(w->goldColor);
            j["buyBtnColor"]         = c2a(w->buyBtnColor);
            j["buyBtnDisabledColor"] = c2a(w->buyBtnDisabledColor);
            j["slotBgColor"]         = c2a(w->slotBgColor);
            j["dividerColor"]        = c2a(w->dividerColor);
            j["errorColor"]          = c2a(w->errorColor);
            // Slot appearance
            j["slotFilledBgColor"]    = c2a(w->slotFilledBgColor);
            j["slotEmptyBgColor"]     = c2a(w->slotEmptyBgColor);
            j["slotEmptyBorderColor"] = c2a(w->slotEmptyBorderColor);
            j["slotBorderWidth"]      = w->slotBorderWidth;
            j["itemTextColor"]        = c2a(w->itemTextColor);
            j["quantityColor"]        = c2a(w->quantityColor);
            // Rarity colors
            j["rarityCommonColor"]    = c2a(w->rarityCommonColor);
            j["rarityUncommonColor"]  = c2a(w->rarityUncommonColor);
            j["rarityRareColor"]      = c2a(w->rarityRareColor);
            j["rarityEpicColor"]      = c2a(w->rarityEpicColor);
            j["rarityLegendaryColor"] = c2a(w->rarityLegendaryColor);
            // Tooltip
            j["tooltipWidth"]        = w->tooltipWidth;
            j["tooltipPadding"]      = w->tooltipPadding;
            j["tooltipOffset"]       = w->tooltipOffset;
            j["tooltipShadowOffset"] = w->tooltipShadowOffset;
            j["tooltipLineSpacing"]  = w->tooltipLineSpacing;
            j["tooltipBorderWidth"]  = w->tooltipBorderWidth;
            j["tooltipSepHeight"]    = w->tooltipSepHeight;
            j["tooltipNameFontSize"]  = w->tooltipNameFontSize;
            j["tooltipStatFontSize"]  = w->tooltipStatFontSize;
            j["tooltipLevelFontSize"] = w->tooltipLevelFontSize;
            j["tooltipBgColor"]      = c2a(w->tooltipBgColor);
            j["tooltipBorderColor"]  = c2a(w->tooltipBorderColor);
            j["tooltipShadowColor"]  = c2a(w->tooltipShadowColor);
            j["tooltipStatColor"]    = c2a(w->tooltipStatColor);
            j["tooltipSepColor"]     = c2a(w->tooltipSepColor);
            j["tooltipLevelColor"]   = c2a(w->tooltipLevelColor);
            // Icon atlas
            if (!w->iconAtlasKey.empty()) j["iconAtlasKey"] = w->iconAtlasKey;
            j["iconAtlasCols"] = w->iconAtlasCols;
            j["iconAtlasRows"] = w->iconAtlasRows;
        }
    }
    else if (type == "bank_panel") {
        if (auto* w = dynamic_cast<const BankPanel*>(node)) {
            j["titleOffset"]     = {w->titleOffset.x, w->titleOffset.y};
            j["bankListOffset"]  = {w->bankListOffset.x, w->bankListOffset.y};
            j["inventoryOffset"] = {w->inventoryOffset.x, w->inventoryOffset.y};
            j["goldOffset"]      = {w->goldOffset.x, w->goldOffset.y};
            j["titleFontSize"]    = w->titleFontSize;
            j["headerFontSize"]   = w->headerFontSize;
            j["bodyFontSize"]     = w->bodyFontSize;
            j["smallFontSize"]    = w->smallFontSize;
            j["headerHeight"]     = w->headerHeight;
            j["bottomBarHeight"]  = w->bottomBarHeight;
            j["rowHeight"]        = w->rowHeight;
            j["slotSize"]         = w->slotSize;
            j["borderWidth"]      = w->borderWidth;
            auto c2a = [](const Color& c) { return nlohmann::json::array({c.r, c.g, c.b, c.a}); };
            j["bgColor"]           = c2a(w->bgColor);
            j["borderColor"]       = c2a(w->borderColor);
            j["titleColor"]        = c2a(w->titleColor);
            j["textColor"]         = c2a(w->textColor);
            j["goldColor"]         = c2a(w->goldColor);
            j["buttonColor"]       = c2a(w->buttonColor);
            j["withdrawBtnColor"]  = c2a(w->withdrawBtnColor);
            j["depositBtnColor"]   = c2a(w->depositBtnColor);
            j["slotBgColor"]       = c2a(w->slotBgColor);
            j["dividerColorVal"]   = c2a(w->dividerColorVal);
        }
    }
    else if (type == "teleporter_panel") {
        if (auto* w = dynamic_cast<const TeleporterPanel*>(node)) {
            j["titleOffset"] = {w->titleOffset.x, w->titleOffset.y};
            j["labelOffset"] = {w->labelOffset.x, w->labelOffset.y};
            j["rowOffset"]   = {w->rowOffset.x, w->rowOffset.y};
            j["goldOffset"]  = {w->goldOffset.x, w->goldOffset.y};
            if (!w->title.empty() && w->title != "Teleporter")
                j["title"] = w->title;
            j["titleFontSize"] = w->titleFontSize;
            j["nameFontSize"]  = w->nameFontSize;
            j["costFontSize"]  = w->costFontSize;
            j["labelFontSize"] = w->labelFontSize;
            j["goldFontSize"]  = w->goldFontSize;
            j["rowHeight"]     = w->rowHeight;
            auto c2a = [](const Color& c) { return nlohmann::json::array({c.r, c.g, c.b, c.a}); };
            j["backgroundColor"] = c2a(w->backgroundColor);
            j["borderColor"]     = c2a(w->borderColor);
            j["titleBarColor"]   = c2a(w->titleBarColor);
            j["titleColor"]      = c2a(w->titleColor);
            j["closeBtnColor"]   = c2a(w->closeBtnColor);
            j["labelColor"]      = c2a(w->labelColor);
            j["textColor"]       = c2a(w->textColor);
            j["goldColor"]       = c2a(w->goldColor);
            j["disabledColor"]   = c2a(w->disabledColor);
            j["errorColor"]      = c2a(w->errorColor);
        }
    }
    else if (type == "arena_panel") {
        if (auto* w = dynamic_cast<const ArenaPanel*>(node)) {
            j["titleOffset"]  = {w->titleOffset.x, w->titleOffset.y};
            j["descOffset"]   = {w->descOffset.x, w->descOffset.y};
            j["buttonOffset"] = {w->buttonOffset.x, w->buttonOffset.y};
            j["statusOffset"] = {w->statusOffset.x, w->statusOffset.y};
            j["titleFontSize"]  = w->titleFontSize;
            j["bodyFontSize"]   = w->bodyFontSize;
            j["buttonHeight"]   = w->buttonHeight;
            j["buttonSpacing"]  = w->buttonSpacing;
            auto c2a = [](const Color& c) { return nlohmann::json::array({c.r, c.g, c.b, c.a}); };
            j["backgroundColor"] = c2a(w->backgroundColor);
            j["borderColor"]     = c2a(w->borderColor);
            j["titleBarColor"]   = c2a(w->titleBarColor);
            j["titleColor"]      = c2a(w->titleColor);
            j["closeBtnColor"]   = c2a(w->closeBtnColor);
            j["labelColor"]      = c2a(w->labelColor);
            j["buttonColor"]     = c2a(w->buttonColor);
            j["buttonTextColor"] = c2a(w->buttonTextColor);
            j["cancelBtnColor"]  = c2a(w->cancelBtnColor);
            j["registeredColor"] = c2a(w->registeredColor);
            j["statusColor"]     = c2a(w->statusColor);
        }
    }
    else if (type == "battlefield_panel") {
        if (auto* w = dynamic_cast<const BattlefieldPanel*>(node)) {
            j["titleOffset"]  = {w->titleOffset.x, w->titleOffset.y};
            j["descOffset"]   = {w->descOffset.x, w->descOffset.y};
            j["buttonOffset"] = {w->buttonOffset.x, w->buttonOffset.y};
            j["statusOffset"] = {w->statusOffset.x, w->statusOffset.y};
            j["titleFontSize"] = w->titleFontSize;
            j["bodyFontSize"]  = w->bodyFontSize;
            j["buttonHeight"]  = w->buttonHeight;
            auto c2a = [](const Color& c) { return nlohmann::json::array({c.r, c.g, c.b, c.a}); };
            j["backgroundColor"] = c2a(w->backgroundColor);
            j["borderColor"]     = c2a(w->borderColor);
            j["titleBarColor"]   = c2a(w->titleBarColor);
            j["titleColor"]      = c2a(w->titleColor);
            j["closeBtnColor"]   = c2a(w->closeBtnColor);
            j["labelColor"]      = c2a(w->labelColor);
            j["buttonColor"]     = c2a(w->buttonColor);
            j["buttonTextColor"] = c2a(w->buttonTextColor);
            j["cancelBtnColor"]  = c2a(w->cancelBtnColor);
            j["registeredColor"] = c2a(w->registeredColor);
            j["timerColor"]      = c2a(w->timerColor);
            j["statusColor"]     = c2a(w->statusColor);
        }
    }
    else if (type == "leaderboard_panel") {
        if (auto* w = dynamic_cast<const LeaderboardPanel*>(node)) {
            j["titleOffset"]    = {w->titleOffset.x, w->titleOffset.y};
            j["columnOffset"]   = {w->columnOffset.x, w->columnOffset.y};
            j["listOffset"]     = {w->listOffset.x, w->listOffset.y};
            j["pageInfoOffset"] = {w->pageInfoOffset.x, w->pageInfoOffset.y};
            j["titleFontSize"]  = w->titleFontSize;
            j["tabFontSize"]    = w->tabFontSize;
            j["filterFontSize"] = w->filterFontSize;
            j["headerFontSize"] = w->headerFontSize;
            j["rowFontSize"]    = w->rowFontSize;
            j["pageFontSize"]   = w->pageFontSize;
            j["closeFontSize"]  = w->closeFontSize;
            j["titleBarHeight"]     = w->titleBarHeight;
            j["catTabHeight"]       = w->catTabHeight;
            j["facBtnHeight"]       = w->facBtnHeight;
            j["rowHeight"]          = w->rowHeight;
            j["headerRowHeight"]    = w->headerRowHeight;
            j["pagBtnHeight"]       = w->pagBtnHeight;
            j["pagBtnWidth"]        = w->pagBtnWidth;
            j["closeCircleRadius"]  = w->closeCircleRadius;
            j["borderWidth"]        = w->borderWidth;
            j["contentPadding"]     = w->contentPadding;
            auto c2a = [](const Color& c) { return nlohmann::json::array({c.r, c.g, c.b, c.a}); };
            j["bgColor"]          = c2a(w->bgColor);
            j["textColor"]        = c2a(w->textColor);
            j["titleColor"]       = c2a(w->titleColor);
            j["borderColor"]      = c2a(w->borderColor);
            j["dividerColor"]     = c2a(w->dividerColor);
            j["headerBgColor"]    = c2a(w->headerBgColor);
            j["rowEvenColor"]     = c2a(w->rowEvenColor);
            j["rowOddColor"]      = c2a(w->rowOddColor);
            j["activeBgColor"]    = c2a(w->activeBgColor);
            j["closeBgColor"]     = c2a(w->closeBgColor);
            j["rankGoldColor"]    = c2a(w->rankGoldColor);
            j["rankSilverColor"]  = c2a(w->rankSilverColor);
            j["rankBronzeColor"]  = c2a(w->rankBronzeColor);
            j["btnBgColor"]       = c2a(w->btnBgColor);
            j["btnBorderColor"]   = c2a(w->btnBorderColor);
            j["goldAccentColor"]  = c2a(w->goldAccentColor);
            j["closeXColor"]      = c2a(w->closeXColor);
        }
    }
    else if (type == "pet_panel") {
        if (auto* w = dynamic_cast<const PetPanel*>(node)) {
            j["titleOffset"]       = {w->titleOffset.x, w->titleOffset.y};
            j["activeLabelOffset"] = {w->activeLabelOffset.x, w->activeLabelOffset.y};
            j["petInfoOffset"]     = {w->petInfoOffset.x, w->petInfoOffset.y};
            j["listLabelOffset"]   = {w->listLabelOffset.x, w->listLabelOffset.y};
            j["titleFontSize"] = w->titleFontSize;
            j["nameFontSize"]  = w->nameFontSize;
            j["statFontSize"]  = w->statFontSize;
            j["portraitSize"]  = w->portraitSize;
            j["buttonHeight"]  = w->buttonHeight;
            auto c2a = [](const Color& c) { return nlohmann::json::array({c.r, c.g, c.b, c.a}); };
            j["backgroundColor"]  = c2a(w->backgroundColor);
            j["borderColor"]      = c2a(w->borderColor);
            j["titleBarColor"]    = c2a(w->titleBarColor);
            j["titleColor"]       = c2a(w->titleColor);
            j["closeBtnColor"]    = c2a(w->closeBtnColor);
            j["labelColor"]       = c2a(w->labelColor);
            j["buttonColor"]      = c2a(w->buttonColor);
            j["buttonTextColor"]  = c2a(w->buttonTextColor);
            j["unequipBtnColor"]  = c2a(w->unequipBtnColor);
            j["equippedColor"]    = c2a(w->equippedColor);
            j["xpBarBgColor"]     = c2a(w->xpBarBgColor);
            j["xpBarFillColor"]   = c2a(w->xpBarFillColor);
            j["portraitBgColor"]  = c2a(w->portraitBgColor);
            j["selectedBgColor"]  = c2a(w->selectedBgColor);
        }
    }
    else if (type == "crafting_panel") {
        if (auto* w = dynamic_cast<const CraftingPanel*>(node)) {
            j["titleOffset"]      = {w->titleOffset.x, w->titleOffset.y};
            j["recipeListOffset"] = {w->recipeListOffset.x, w->recipeListOffset.y};
            j["detailOffset"]     = {w->detailOffset.x, w->detailOffset.y};
            j["statusOffset"]     = {w->statusOffset.x, w->statusOffset.y};
            j["titleFontSize"]     = w->titleFontSize;
            j["recipeFontSize"]    = w->recipeFontSize;
            j["slotSize"]          = w->slotSize;
            j["resultSlotSize"]    = w->resultSlotSize;
            j["ingredientColumns"] = w->ingredientColumns;
            auto c2a = [](const Color& c) { return nlohmann::json::array({c.r, c.g, c.b, c.a}); };
            j["backgroundColor"]      = c2a(w->backgroundColor);
            j["borderColor"]          = c2a(w->borderColor);
            j["titleBarColor"]        = c2a(w->titleBarColor);
            j["titleColor"]           = c2a(w->titleColor);
            j["closeBtnColor"]        = c2a(w->closeBtnColor);
            j["labelColor"]           = c2a(w->labelColor);
            j["buttonColor"]          = c2a(w->buttonColor);
            j["buttonDisabledColor"]  = c2a(w->buttonDisabledColor);
            j["buttonTextColor"]      = c2a(w->buttonTextColor);
            j["selectedColor"]        = c2a(w->selectedColor);
            j["rowColor"]             = c2a(w->rowColor);
            j["hasColor"]             = c2a(w->hasColor);
            j["missingColor"]         = c2a(w->missingColor);
            j["goldColor"]            = c2a(w->goldColor);
            j["slotBgColor"]          = c2a(w->slotBgColor);
            j["statusColor"]          = c2a(w->statusColor);
        }
    }
    else if (type == "collection_panel") {
        if (auto* w = dynamic_cast<const CollectionPanel*>(node)) {
            j["titleOffset"] = {w->titleOffset.x, w->titleOffset.y};
            j["tabOffset"]   = {w->tabOffset.x, w->tabOffset.y};
            j["entryOffset"] = {w->entryOffset.x, w->entryOffset.y};
            j["titleFontSize"]     = w->titleFontSize;
            j["entryFontSize"]     = w->entryFontSize;
            j["rewardFontSize"]    = w->rewardFontSize;
            j["categoryTabHeight"] = w->categoryTabHeight;
            j["entryHeight"]       = w->entryHeight;
            j["borderWidth"]       = w->borderWidth;
            j["headerHeight"]      = w->headerHeight;
            auto c2a = [](const Color& c) { return nlohmann::json::array({c.r, c.g, c.b, c.a}); };
            j["completedColor"]       = c2a(w->completedColor);
            j["incompleteColor"]      = c2a(w->incompleteColor);
            j["rewardColor"]          = c2a(w->rewardColor);
            j["progressColor"]        = c2a(w->progressColor);
            j["backgroundColor"]      = c2a(w->backgroundColor);
            j["borderColor"]          = c2a(w->borderColor);
            j["titleBarColor"]        = c2a(w->titleBarColor);
            j["titleColor"]           = c2a(w->titleColor);
            j["closeBtnColor"]        = c2a(w->closeBtnColor);
            j["tabActiveColor"]       = c2a(w->tabActiveColor);
            j["tabInactiveColor"]     = c2a(w->tabInactiveColor);
            j["tabActiveTextColor"]   = c2a(w->tabActiveTextColor);
            j["tabInactiveTextColor"] = c2a(w->tabInactiveTextColor);
        }
    }
    else if (type == "player_context_menu") {
        if (auto* w = dynamic_cast<const PlayerContextMenu*>(node)) {
            j["nameOffset"] = {w->nameOffset.x, w->nameOffset.y};
            j["itemOffset"] = {w->itemOffset.x, w->itemOffset.y};
            j["menuFontSize"] = w->menuFontSize;
            j["itemHeight"]   = w->itemHeight;
            j["menuWidth"]    = w->menuWidth;
            j["bgColor"]              = {w->bgColor.r, w->bgColor.g, w->bgColor.b, w->bgColor.a};
            j["borderColor"]          = {w->borderColor.r, w->borderColor.g, w->borderColor.b, w->borderColor.a};
            j["nameHeaderColor"]      = {w->nameHeaderColor.r, w->nameHeaderColor.g, w->nameHeaderColor.b, w->nameHeaderColor.a};
            j["separatorColor"]       = {w->separatorColor.r, w->separatorColor.g, w->separatorColor.b, w->separatorColor.a};
            j["pressedColor"]         = {w->pressedColor.r, w->pressedColor.g, w->pressedColor.b, w->pressedColor.a};
            j["enabledTextColor"]     = {w->enabledTextColor.r, w->enabledTextColor.g, w->enabledTextColor.b, w->enabledTextColor.a};
            j["disabledTextColor"]    = {w->disabledTextColor.r, w->disabledTextColor.g, w->disabledTextColor.b, w->disabledTextColor.a};
        }
    }
    else if (type == "loading_panel") {
        if (auto* w = dynamic_cast<const LoadingPanel*>(node)) {
            j["barHeight"]    = w->barHeight;
            j["barPadX"]      = w->barPadX;
            j["barBottomY"]   = w->barBottomY;
            j["nameFontSize"] = w->nameFontSize;
            j["pctFontSize"]  = w->pctFontSize;
            j["shadowOffset"] = w->shadowOffset;
            j["bgColor"]      = {w->bgColor.r, w->bgColor.g, w->bgColor.b, w->bgColor.a};
            j["barBgColor"]   = {w->barBgColor.r, w->barBgColor.g, w->barBgColor.b, w->barBgColor.a};
            j["barFillColor"] = {w->barFillColor.r, w->barFillColor.g, w->barFillColor.b, w->barFillColor.a};
            j["nameColor"]    = {w->nameColor.r, w->nameColor.g, w->nameColor.b, w->nameColor.a};
            j["pctColor"]     = {w->pctColor.r, w->pctColor.g, w->pctColor.b, w->pctColor.a};
            j["shadowColor"]  = {w->shadowColor.r, w->shadowColor.g, w->shadowColor.b, w->shadowColor.a};
        }
    }
    else if (type == "trade_window") {
        if (auto* w = dynamic_cast<const TradeWindow*>(node)) {
            j["titleOffset"]     = {w->titleOffset.x, w->titleOffset.y};
            j["myOfferOffset"]   = {w->myOfferOffset.x, w->myOfferOffset.y};
            j["theirOfferOffset"] = {w->theirOfferOffset.x, w->theirOfferOffset.y};
            j["buttonOffset"]    = {w->buttonOffset.x, w->buttonOffset.y};
            j["titleFontSize"]   = w->titleFontSize;
            j["labelFontSize"]   = w->labelFontSize;
            j["bodyFontSize"]    = w->bodyFontSize;
            j["smallFontSize"]   = w->smallFontSize;
            j["headerHeight"]    = w->headerHeight;
            j["buttonRowHeight"] = w->buttonRowHeight;
            j["padding"]         = w->padding;
            j["borderWidth"]     = w->borderWidth;
            auto c2a = [](const Color& c) { return nlohmann::json::array({c.r, c.g, c.b, c.a}); };
            j["bgColor"]         = c2a(w->bgColor);
            j["borderColor"]     = c2a(w->borderColor);
            j["titleColor"]      = c2a(w->titleColor);
            j["labelColor"]      = c2a(w->labelColor);
            j["goldColor"]       = c2a(w->goldColor);
            j["acceptBtnColor"]  = c2a(w->acceptBtnColor);
            j["cancelBtnColor"]  = c2a(w->cancelBtnColor);
            j["dividerColor"]    = c2a(w->dividerColor);
        }
    }
    else if (type == "costume_panel") {
        if (auto* w = dynamic_cast<const CostumePanel*>(node)) {
            j["titleOffset"]  = {w->titleOffset.x, w->titleOffset.y};
            j["toggleOffset"] = {w->toggleOffset.x, w->toggleOffset.y};
            j["gridOffset"]   = {w->gridOffset.x, w->gridOffset.y};
            j["infoOffset"]   = {w->infoOffset.x, w->infoOffset.y};
            j["titleFontSize"]      = w->titleFontSize;
            j["bodyFontSize"]       = w->bodyFontSize;
            j["infoFontSize"]       = w->infoFontSize;
            j["gridCols"]           = w->gridCols;
            j["slotSize"]           = w->slotSize;
            j["slotSpacing"]        = w->slotSpacing;
            j["buttonHeight"]       = w->buttonHeight;
            j["buttonSpacing"]      = w->buttonSpacing;
            j["filterTabHeight"]    = w->filterTabHeight;
            j["borderWidth"]        = w->borderWidth;
            j["headerHeight"]       = w->headerHeight;
            j["bottomReserveHeight"] = w->bottomReserveHeight;
            auto c2a = [](const Color& c) { return nlohmann::json::array({c.r, c.g, c.b, c.a}); };
            j["backgroundColor"]       = c2a(w->backgroundColor);
            j["borderColor"]           = c2a(w->borderColor);
            j["titleBarColor"]         = c2a(w->titleBarColor);
            j["titleColor"]            = c2a(w->titleColor);
            j["closeBtnColor"]         = c2a(w->closeBtnColor);
            j["tabColor"]              = c2a(w->tabColor);
            j["tabActiveColor"]        = c2a(w->tabActiveColor);
            j["tabTextColor"]          = c2a(w->tabTextColor);
            j["tabActiveTextColor"]    = c2a(w->tabActiveTextColor);
            j["slotColor"]             = c2a(w->slotColor);
            j["slotSelectedColor"]     = c2a(w->slotSelectedColor);
            j["equippedIndicatorColor"] = c2a(w->equippedIndicatorColor);
            j["nameColor"]             = c2a(w->nameColor);
            j["equipBtnColor"]         = c2a(w->equipBtnColor);
            j["unequipBtnColor"]       = c2a(w->unequipBtnColor);
            j["buttonTextColor"]       = c2a(w->buttonTextColor);
            j["hintColor"]             = c2a(w->hintColor);
        }
    }
    else if (type == "settings_panel") {
        if (auto* w = dynamic_cast<const SettingsPanel*>(node)) {
            j["titleOffset"]  = {w->titleOffset.x, w->titleOffset.y};
            j["logoutOffset"] = {w->logoutOffset.x, w->logoutOffset.y};

            j["titleFontSize"]      = w->titleFontSize;
            j["sectionFontSize"]    = w->sectionFontSize;
            j["labelFontSize"]      = w->labelFontSize;
            j["buttonFontSize"]     = w->buttonFontSize;
            j["logoutButtonWidth"]  = w->logoutButtonWidth;
            j["logoutButtonHeight"] = w->logoutButtonHeight;
            j["buttonCornerRadius"] = w->buttonCornerRadius;
            j["sectionSpacing"]     = w->sectionSpacing;
            j["itemSpacing"]        = w->itemSpacing;
            j["borderWidth"]        = w->borderWidth;

            j["titleColor"]          = {w->titleColor.r, w->titleColor.g, w->titleColor.b, w->titleColor.a};
            j["sectionColor"]        = {w->sectionColor.r, w->sectionColor.g, w->sectionColor.b, w->sectionColor.a};
            j["labelColor"]          = {w->labelColor.r, w->labelColor.g, w->labelColor.b, w->labelColor.a};
            j["logoutBtnColor"]      = {w->logoutBtnColor.r, w->logoutBtnColor.g, w->logoutBtnColor.b, w->logoutBtnColor.a};
            j["logoutBtnHoverColor"] = {w->logoutBtnHoverColor.r, w->logoutBtnHoverColor.g, w->logoutBtnHoverColor.b, w->logoutBtnHoverColor.a};
            j["logoutTextColor"]     = {w->logoutTextColor.r, w->logoutTextColor.g, w->logoutTextColor.b, w->logoutTextColor.a};
            j["dividerColor"]        = {w->dividerColor.r, w->dividerColor.g, w->dividerColor.b, w->dividerColor.a};
        }
    }
#endif // FATE_HAS_GAME
    } // end legacy serialization fallback

    // --- Event bindings ---
    if (!node->eventBindings.empty()) {
        nlohmann::json evts = nlohmann::json::object();
        for (auto& [k, v] : node->eventBindings) {
            evts[k] = v;
        }
        j["events"] = std::move(evts);
    }

    // --- Data bindings ---
    if (!node->dataBindings.empty()) {
        nlohmann::json binds = nlohmann::json::object();
        for (auto& [k, v] : node->dataBindings) {
            binds[k] = v;
        }
        j["bind"] = std::move(binds);
    }

    // --- Custom properties ---
    if (!node->properties.empty()) {
        nlohmann::json props = nlohmann::json::object();
        for (auto& [k, v] : node->properties) {
            props[k] = v;
        }
        j["properties"] = std::move(props);
    }

    // --- Children ---
    if (node->childCount() > 0) {
        nlohmann::json children = nlohmann::json::array();
        for (size_t i = 0; i < node->childCount(); ++i) {
            children.push_back(serializeNode(node->childAt(i)));
        }
        j["children"] = std::move(children);
    }

    return j;
}

// ---------------------------------------------------------------------------
// serializeScreen
// ---------------------------------------------------------------------------

std::string UISerializer::serializeScreen(const std::string& screenId, const UINode* root) {
    if (!root) {
        LOG_WARN("UI", "UISerializer::serializeScreen: null root for screen '%s'", screenId.c_str());
        return {};
    }

    nlohmann::json doc;
    doc["screen"] = screenId;
    doc["root"]   = serializeNode(root);
    return doc.dump(2);
}

// ---------------------------------------------------------------------------
// saveToFile
// ---------------------------------------------------------------------------

bool UISerializer::saveToFile(const std::string& filepath, const std::string& screenId, const UINode* root) {
    std::string json = serializeScreen(screenId, root);
    if (json.empty()) {
        return false;
    }

    std::ofstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR("UI", "UISerializer::saveToFile: failed to open '%s' for writing", filepath.c_str());
        return false;
    }

    file << json;
    if (!file.good()) {
        LOG_ERROR("UI", "UISerializer::saveToFile: write error for '%s'", filepath.c_str());
        return false;
    }

    LOG_INFO("UI", "UISerializer: saved screen '%s' to '%s'", screenId.c_str(), filepath.c_str());
    return true;
}

} // namespace fate
