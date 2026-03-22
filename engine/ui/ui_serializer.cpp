#include "engine/ui/ui_serializer.h"
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_anchor.h"
#include "engine/ui/widgets/panel.h"
#include "engine/ui/widgets/label.h"
#include "engine/ui/widgets/button.h"
#include "engine/ui/widgets/text_input.h"
#include "engine/ui/widgets/progress_bar.h"
#include "engine/ui/widgets/window.h"
#include "engine/ui/widgets/tab_container.h"
#include "engine/ui/widgets/slot_grid.h"
#include "engine/ui/widgets/slot.h"
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

        j["anchor"] = std::move(a);
    }

    // --- Style ---
    if (!node->styleName().empty()) {
        j["style"] = node->styleName();
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
    else if (type == "text_input") {
        if (auto* w = dynamic_cast<const TextInput*>(node)) {
            if (!w->text.empty())        j["text"]        = w->text;
            if (!w->placeholder.empty()) j["placeholder"] = w->placeholder;
            if (w->maxLength != 0)       j["maxLength"]   = w->maxLength;
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
