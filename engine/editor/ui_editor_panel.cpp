#include "engine/editor/ui_editor_panel.h"
#include "engine/ui/ui_serializer.h"
#include "engine/core/logger.h"
#include "engine/ui/widgets/panel.h"
#include "engine/ui/widgets/label.h"
#include "engine/ui/widgets/button.h"
#include "engine/ui/widgets/text_input.h"
#include "engine/ui/widgets/progress_bar.h"
#include "engine/ui/widgets/window.h"
#include "engine/ui/widgets/tab_container.h"
#include "engine/ui/widgets/slot.h"
#include "engine/ui/widgets/slot_grid.h"
#include "engine/ui/widgets/scroll_view.h"
#include <imgui.h>
#include <cstdio>

namespace fate {

// ============================================================================
// Public
// ============================================================================

void UIEditorPanel::draw(UIManager& uiMgr) {
    drawHierarchy(uiMgr);
    drawInspector(uiMgr);
}

// ============================================================================
// Hierarchy tree window
// ============================================================================

void UIEditorPanel::drawHierarchy(UIManager& uiMgr) {
    if (!showHierarchy) return;
    if (!ImGui::Begin("UI Hierarchy", &showHierarchy)) {
        ImGui::End();
        return;
    }

    auto ids = uiMgr.screenIds();
    if (ids.empty()) {
        ImGui::TextDisabled("No UI screens loaded");
    }

    for (auto& id : ids) {
        auto* root = uiMgr.getScreen(id);
        if (!root) continue;

        if (ImGui::TreeNodeEx(id.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            drawNodeTree(root, id);
            ImGui::TreePop();
        }
    }

    ImGui::End();
}

void UIEditorPanel::drawNodeTree(UINode* node, const std::string& screenId) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (node == selectedNode_) flags |= ImGuiTreeNodeFlags_Selected;
    if (node->childCount() == 0) flags |= ImGuiTreeNodeFlags_Leaf;

    char label[256];
    snprintf(label, sizeof(label), "%s: %s", node->type().c_str(), node->id().c_str());

    // Use pointer as stable ID for ImGui
    ImGui::PushID(node);
    bool opened = ImGui::TreeNodeEx(label, flags);

    if (ImGui::IsItemClicked()) {
        selectedNode_ = node;
        selectedScreenId_ = screenId;
    }

    // Right-click context menu
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Add Panel Child")) {
            std::string childId = "new_panel_" + std::to_string(node->childCount());
            node->addChild(std::make_unique<Panel>(childId));
        }
        if (ImGui::MenuItem("Add Label Child")) {
            std::string childId = "new_label_" + std::to_string(node->childCount());
            node->addChild(std::make_unique<Label>(childId));
        }
        if (ImGui::MenuItem("Add Button Child")) {
            std::string childId = "new_button_" + std::to_string(node->childCount());
            node->addChild(std::make_unique<Button>(childId));
        }
        if (ImGui::MenuItem("Delete") && node->parent()) {
            if (node == selectedNode_) selectedNode_ = nullptr;
            node->parent()->removeChild(node->id());
            ImGui::EndPopup();
            if (opened) ImGui::TreePop();
            ImGui::PopID();
            return;
        }
        ImGui::EndPopup();
    }

    if (opened) {
        for (size_t i = 0; i < node->childCount(); i++) {
            drawNodeTree(node->childAt(i), screenId);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

// ============================================================================
// Inspector window
// ============================================================================

void UIEditorPanel::drawInspector(UIManager& uiMgr) {
    if (!showInspector) return;
    if (!ImGui::Begin("UI Inspector", &showInspector)) {
        ImGui::End();
        return;
    }

    if (!selectedNode_) {
        ImGui::TextDisabled("No node selected");
        ImGui::End();
        return;
    }

    // --- Common properties ---
    ImGui::SeparatorText("Node");

    // ID (read-only display)
    ImGui::Text("ID: %s", selectedNode_->id().c_str());

    // Type (read-only)
    ImGui::Text("Type: %s", selectedNode_->type().c_str());

    // Visible
    bool visible = selectedNode_->visible();
    if (ImGui::Checkbox("Visible", &visible)) {
        selectedNode_->setVisible(visible);
    }

    // Enabled
    bool enabled = selectedNode_->enabled();
    if (ImGui::Checkbox("Enabled", &enabled)) {
        selectedNode_->setEnabled(enabled);
    }

    // Z-Order
    int zOrder = selectedNode_->zOrder();
    if (ImGui::DragInt("Z-Order", &zOrder, 1.0f, -100, 100)) {
        selectedNode_->setZOrder(zOrder);
    }

    // --- Anchor ---
    drawAnchorEditor(selectedNode_);

    // --- Style ---
    drawStyleEditor(selectedNode_, uiMgr);

    // --- Widget-specific properties ---
    ImGui::SeparatorText("Widget Properties");

    if (auto* panel = dynamic_cast<Panel*>(selectedNode_)) {
        char titleBuf[256] = {};
        snprintf(titleBuf, sizeof(titleBuf), "%s", panel->title.c_str());
        if (ImGui::InputText("Title##panel", titleBuf, sizeof(titleBuf))) {
            panel->title = titleBuf;
        }
        ImGui::Checkbox("Draggable", &panel->draggable);
        ImGui::Checkbox("Closeable##panel", &panel->closeable);
    }
    else if (auto* label = dynamic_cast<Label*>(selectedNode_)) {
        char textBuf[1024] = {};
        snprintf(textBuf, sizeof(textBuf), "%s", label->text.c_str());
        if (ImGui::InputText("Text##label", textBuf, sizeof(textBuf))) {
            label->text = textBuf;
        }
        const char* alignNames[] = {"Left", "Center", "Right"};
        int alignIdx = static_cast<int>(label->align);
        if (ImGui::Combo("Align", &alignIdx, alignNames, 3)) {
            label->align = static_cast<TextAlign>(alignIdx);
        }
        ImGui::Checkbox("Word Wrap", &label->wordWrap);
    }
    else if (auto* button = dynamic_cast<Button*>(selectedNode_)) {
        char textBuf[256] = {};
        snprintf(textBuf, sizeof(textBuf), "%s", button->text.c_str());
        if (ImGui::InputText("Text##btn", textBuf, sizeof(textBuf))) {
            button->text = textBuf;
        }
        char iconBuf[256] = {};
        snprintf(iconBuf, sizeof(iconBuf), "%s", button->icon.c_str());
        if (ImGui::InputText("Icon##btn", iconBuf, sizeof(iconBuf))) {
            button->icon = iconBuf;
        }
    }
    else if (auto* textInput = dynamic_cast<TextInput*>(selectedNode_)) {
        char placeholderBuf[256] = {};
        snprintf(placeholderBuf, sizeof(placeholderBuf), "%s", textInput->placeholder.c_str());
        if (ImGui::InputText("Placeholder", placeholderBuf, sizeof(placeholderBuf))) {
            textInput->placeholder = placeholderBuf;
        }
        ImGui::DragInt("Max Length", &textInput->maxLength, 1.0f, 0, 1000);
    }
    else if (auto* bar = dynamic_cast<ProgressBar*>(selectedNode_)) {
        ImGui::DragFloat("Value", &bar->value, 0.5f, 0.0f, bar->maxValue);
        ImGui::DragFloat("Max Value", &bar->maxValue, 1.0f, 0.1f, 10000.0f);
        ImGui::ColorEdit4("Fill Color", &bar->fillColor.r);
        ImGui::Checkbox("Show Text", &bar->showText);
        const char* dirNames[] = {"LeftToRight", "RightToLeft", "BottomToTop", "TopToBottom"};
        int dirIdx = static_cast<int>(bar->direction);
        if (ImGui::Combo("Direction", &dirIdx, dirNames, 4)) {
            bar->direction = static_cast<BarDirection>(dirIdx);
        }
    }
    else if (auto* window = dynamic_cast<Window*>(selectedNode_)) {
        char titleBuf[256] = {};
        snprintf(titleBuf, sizeof(titleBuf), "%s", window->title.c_str());
        if (ImGui::InputText("Title##win", titleBuf, sizeof(titleBuf))) {
            window->title = titleBuf;
        }
        ImGui::Checkbox("Closeable##win", &window->closeable);
        ImGui::Checkbox("Resizable", &window->resizable);
        ImGui::Checkbox("Minimizable", &window->minimizable);
        ImGui::DragFloat("Title Bar Height", &window->titleBarHeight, 1.0f, 16.0f, 64.0f);
    }
    else if (auto* tabs = dynamic_cast<TabContainer*>(selectedNode_)) {
        int activeTab = tabs->activeTab;
        int maxTab = static_cast<int>(tabs->tabLabels_.size()) - 1;
        if (maxTab < 0) maxTab = 0;
        if (ImGui::DragInt("Active Tab", &activeTab, 1.0f, 0, maxTab)) {
            tabs->activeTab = activeTab;
        }
        ImGui::DragFloat("Tab Height", &tabs->tabHeight, 1.0f, 16.0f, 64.0f);
        // Show tab labels read-only
        for (size_t i = 0; i < tabs->tabLabels_.size(); i++) {
            ImGui::Text("  Tab %zu: %s", i, tabs->tabLabels_[i].c_str());
        }
    }
    else if (auto* slot = dynamic_cast<Slot*>(selectedNode_)) {
        char itemBuf[256] = {};
        snprintf(itemBuf, sizeof(itemBuf), "%s", slot->itemId.c_str());
        if (ImGui::InputText("Item ID", itemBuf, sizeof(itemBuf))) {
            slot->itemId = itemBuf;
        }
        ImGui::DragInt("Quantity", &slot->quantity, 1.0f, 0, 9999);
        char slotTypeBuf[128] = {};
        snprintf(slotTypeBuf, sizeof(slotTypeBuf), "%s", slot->slotType.c_str());
        if (ImGui::InputText("Slot Type", slotTypeBuf, sizeof(slotTypeBuf))) {
            slot->slotType = slotTypeBuf;
        }
    }
    else if (auto* grid = dynamic_cast<SlotGrid*>(selectedNode_)) {
        ImGui::DragInt("Columns", &grid->columns, 1.0f, 1, 20);
        ImGui::DragInt("Rows", &grid->rows, 1.0f, 1, 20);
        ImGui::DragFloat("Slot Size", &grid->slotSize, 1.0f, 16.0f, 128.0f);
        ImGui::DragFloat("Slot Padding", &grid->slotPadding, 0.5f, 0.0f, 16.0f);
    }
    else if (auto* scroll = dynamic_cast<ScrollView*>(selectedNode_)) {
        ImGui::DragFloat("Scroll Offset", &scroll->scrollOffset, 1.0f);
        ImGui::DragFloat("Content Height", &scroll->contentHeight, 1.0f, 0.0f, 10000.0f);
        ImGui::DragFloat("Scroll Speed", &scroll->scrollSpeed, 1.0f, 1.0f, 200.0f);
    }
    else {
        ImGui::TextDisabled("(no widget-specific properties)");
    }

    // --- Save ---
    ImGui::Separator();
    if (ImGui::Button("Save Screen") && !selectedScreenId_.empty()) {
        std::string path = "assets/ui/screens/" + selectedScreenId_ + ".json";
        UISerializer::saveToFile(path, selectedScreenId_, uiMgr.getScreen(selectedScreenId_));
        LOG_INFO("UI", "Saved screen: %s", path.c_str());
    }

    ImGui::End();
}

// ============================================================================
// Anchor sub-editor
// ============================================================================

void UIEditorPanel::drawAnchorEditor(UINode* node) {
    ImGui::SeparatorText("Anchor");

    UIAnchor& anchor = node->anchor();

    const char* presetNames[] = {
        "TopLeft", "TopCenter", "TopRight",
        "CenterLeft", "Center", "CenterRight",
        "BottomLeft", "BottomCenter", "BottomRight",
        "StretchX", "StretchY", "StretchAll"
    };
    int presetIdx = static_cast<int>(anchor.preset);
    if (ImGui::Combo("Preset", &presetIdx, presetNames, 12)) {
        anchor.preset = static_cast<AnchorPreset>(presetIdx);
    }

    ImGui::DragFloat2("Offset", &anchor.offset.x, 1.0f);
    ImGui::DragFloat2("Size", &anchor.size.x, 1.0f, 0.0f, 4096.0f);

    // Margin: top, right, bottom, left stored as Vec4 xyzw
    if (ImGui::TreeNode("Margin")) {
        ImGui::DragFloat("Top##margin", &anchor.margin.x, 0.5f);
        ImGui::DragFloat("Right##margin", &anchor.margin.y, 0.5f);
        ImGui::DragFloat("Bottom##margin", &anchor.margin.z, 0.5f);
        ImGui::DragFloat("Left##margin", &anchor.margin.w, 0.5f);
        ImGui::TreePop();
    }

    // Padding: top, right, bottom, left
    if (ImGui::TreeNode("Padding")) {
        ImGui::DragFloat("Top##padding", &anchor.padding.x, 0.5f);
        ImGui::DragFloat("Right##padding", &anchor.padding.y, 0.5f);
        ImGui::DragFloat("Bottom##padding", &anchor.padding.z, 0.5f);
        ImGui::DragFloat("Left##padding", &anchor.padding.w, 0.5f);
        ImGui::TreePop();
    }
}

// ============================================================================
// Style sub-editor
// ============================================================================

void UIEditorPanel::drawStyleEditor(UINode* node, UIManager& uiMgr) {
    ImGui::SeparatorText("Style");

    // Style name combo
    auto names = uiMgr.theme().styleNames();

    // Find current index (-1 if none)
    int currentIdx = -1;
    for (int i = 0; i < static_cast<int>(names.size()); i++) {
        if (names[i] == node->styleName()) {
            currentIdx = i;
            break;
        }
    }

    // Build null-terminated string list for ImGui combo
    std::string comboItems;
    for (auto& n : names) {
        comboItems += n;
        comboItems += '\0';
    }
    comboItems += '\0';

    int comboIdx = currentIdx;
    if (ImGui::Combo("Style Name", &comboIdx, comboItems.c_str())) {
        if (comboIdx >= 0 && comboIdx < static_cast<int>(names.size())) {
            node->setStyleName(names[comboIdx]);
            node->setResolvedStyle(uiMgr.theme().getStyle(names[comboIdx]));
        }
    }

    // Direct style overrides
    UIStyle& style = node->resolvedStyle();
    ImGui::ColorEdit4("Background Color", &style.backgroundColor.r);
    ImGui::ColorEdit4("Border Color", &style.borderColor.r);
    ImGui::DragFloat("Border Width", &style.borderWidth, 0.5f, 0.0f, 10.0f);
    ImGui::ColorEdit4("Text Color", &style.textColor.r);
    ImGui::DragFloat("Font Size", &style.fontSize, 0.5f, 6.0f, 72.0f);
    ImGui::DragFloat("Opacity", &style.opacity, 0.01f, 0.0f, 1.0f);
    ImGui::ColorEdit4("Hover Color", &style.hoverColor.r);
    ImGui::ColorEdit4("Pressed Color", &style.pressedColor.r);
    ImGui::ColorEdit4("Disabled Color", &style.disabledColor.r);
}

} // namespace fate
