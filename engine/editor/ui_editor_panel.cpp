#include "engine/editor/ui_editor_panel.h"
#include "engine/editor/undo.h"
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
#include "engine/ui/widgets/player_info_block.h"
#include "engine/ui/widgets/skill_arc.h"
#include "engine/ui/widgets/dpad.h"
#include "engine/ui/widgets/menu_button_row.h"
#include "engine/ui/widgets/chat_ticker.h"
#include "engine/ui/widgets/exp_bar.h"
#include "engine/ui/widgets/target_frame.h"
#include "engine/ui/widgets/left_sidebar.h"
#include "engine/ui/widgets/inventory_panel.h"
#include "engine/ui/widgets/status_panel.h"
#include "engine/ui/widgets/skill_panel.h"
#include <imgui.h>
#include <cstdio>

namespace fate {

// ============================================================================
// Undo helpers
// ============================================================================

void UIEditorPanel::checkUndoCapture(UIManager& uiMgr) {
    if (ImGui::IsItemActivated() && !selectedScreenId_.empty()) {
        auto* root = uiMgr.getScreen(selectedScreenId_);
        if (root) {
            pendingSnapshot_ = UISerializer::serializeScreen(selectedScreenId_, root);
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && !pendingSnapshot_.empty()) {
        auto* root = uiMgr.getScreen(selectedScreenId_);
        if (root) {
            std::string newSnapshot = UISerializer::serializeScreen(selectedScreenId_, root);
            if (newSnapshot != pendingSnapshot_) {
                auto cmd = std::make_unique<UIPropertyCommand>();
                cmd->screenId = selectedScreenId_;
                cmd->oldJson = std::move(pendingSnapshot_);
                cmd->newJson = newSnapshot;
                cmd->nodeId = selectedNodeId_;
                cmd->desc = "UI Property";
                cmd->uiMgr = &uiMgr;
                UndoSystem::instance().push(std::move(cmd));
            }
        }
        pendingSnapshot_.clear();
    }
}

void UIEditorPanel::revalidateSelection(UIManager& uiMgr) {
    if (!selectedNode_ || selectedScreenId_.empty() || selectedNodeId_.empty()) return;
    auto* root = uiMgr.getScreen(selectedScreenId_);
    if (!root) { selectedNode_ = nullptr; return; }
    auto* found = root->findById(selectedNodeId_);
    selectedNode_ = found; // may be nullptr if node was removed
}

// ============================================================================
// Viewport widget drag (called from editor mouse handling)
// ============================================================================

bool UIEditorPanel::handleViewportClick(const Vec2& viewportLocalPos) {
    if (!selectedNode_) return false;
    const Rect& r = selectedNode_->computedRect();
    if (r.contains(viewportLocalPos)) {
        isDraggingWidget_ = true;
        dragStartMousePos_ = viewportLocalPos;
        dragStartOffset_ = selectedNode_->anchor().offset;
        return true;
    }
    return false;
}

void UIEditorPanel::handleViewportDrag(const Vec2& viewportLocalPos) {
    if (!isDraggingWidget_ || !selectedNode_) return;
    Vec2 delta = {viewportLocalPos.x - dragStartMousePos_.x,
                  viewportLocalPos.y - dragStartMousePos_.y};
    selectedNode_->anchor().offset = {dragStartOffset_.x + delta.x,
                                      dragStartOffset_.y + delta.y};
}

void UIEditorPanel::handleViewportRelease(UIManager* uiMgr) {
    if (!isDraggingWidget_ || !selectedNode_ || !uiMgr) {
        isDraggingWidget_ = false;
        return;
    }
    // Push undo command if offset changed
    Vec2 newOffset = selectedNode_->anchor().offset;
    if (newOffset.x != dragStartOffset_.x || newOffset.y != dragStartOffset_.y) {
        // Take a snapshot with old offset, then current
        selectedNode_->anchor().offset = dragStartOffset_;
        std::string oldJson = UISerializer::serializeScreen(selectedScreenId_,
            uiMgr->getScreen(selectedScreenId_));
        selectedNode_->anchor().offset = newOffset;
        std::string newJson = UISerializer::serializeScreen(selectedScreenId_,
            uiMgr->getScreen(selectedScreenId_));
        auto cmd = std::make_unique<UIPropertyCommand>();
        cmd->screenId = selectedScreenId_;
        cmd->oldJson = std::move(oldJson);
        cmd->newJson = std::move(newJson);
        cmd->nodeId = selectedNodeId_;
        cmd->desc = "Move UI Widget";
        cmd->uiMgr = uiMgr;
        UndoSystem::instance().push(std::move(cmd));
    }
    isDraggingWidget_ = false;
}

// ============================================================================
// Public
// ============================================================================

void UIEditorPanel::draw(UIManager& uiMgr) {
    // Re-resolve selection each frame (handles undo/redo screen reloads)
    revalidateSelection(uiMgr);

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

    ImGui::PushID(node);
    bool opened = ImGui::TreeNodeEx(label, flags);

    if (ImGui::IsItemClicked()) {
        selectedNode_ = node;
        selectedScreenId_ = screenId;
        selectedNodeId_ = node->id();
    }

    // Right-click context menu
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Add Panel Child")) {
            std::string childId = "new_panel_" + std::to_string(nextChildId_++);
            node->addChild(std::make_unique<Panel>(childId));
        }
        if (ImGui::MenuItem("Add Label Child")) {
            std::string childId = "new_label_" + std::to_string(nextChildId_++);
            node->addChild(std::make_unique<Label>(childId));
        }
        if (ImGui::MenuItem("Add Button Child")) {
            std::string childId = "new_button_" + std::to_string(nextChildId_++);
            node->addChild(std::make_unique<Button>(childId));
        }
        if (ImGui::MenuItem("Delete") && node->parent()) {
            if (node == selectedNode_) { selectedNode_ = nullptr; selectedNodeId_.clear(); }
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

    ImGui::Text("ID: %s", selectedNode_->id().c_str());
    ImGui::Text("Type: %s", selectedNode_->type().c_str());

    bool visible = selectedNode_->visible();
    if (ImGui::Checkbox("Visible", &visible)) {
        selectedNode_->setVisible(visible);
    }
    checkUndoCapture(uiMgr);

    bool enabled = selectedNode_->enabled();
    if (ImGui::Checkbox("Enabled", &enabled)) {
        selectedNode_->setEnabled(enabled);
    }
    checkUndoCapture(uiMgr);

    int zOrder = selectedNode_->zOrder();
    if (ImGui::DragInt("Z-Order", &zOrder, 1.0f, -100, 100)) {
        selectedNode_->setZOrder(zOrder);
    }
    checkUndoCapture(uiMgr);

    // --- Anchor ---
    drawAnchorEditor(selectedNode_, uiMgr);

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
        checkUndoCapture(uiMgr);
        ImGui::Checkbox("Draggable", &panel->draggable); checkUndoCapture(uiMgr);
        ImGui::Checkbox("Closeable##panel", &panel->closeable); checkUndoCapture(uiMgr);
    }
    else if (auto* lbl = dynamic_cast<Label*>(selectedNode_)) {
        char textBuf[1024] = {};
        snprintf(textBuf, sizeof(textBuf), "%s", lbl->text.c_str());
        if (ImGui::InputText("Text##label", textBuf, sizeof(textBuf))) {
            lbl->text = textBuf;
        }
        checkUndoCapture(uiMgr);
        const char* alignNames[] = {"Left", "Center", "Right"};
        int alignIdx = static_cast<int>(lbl->align);
        if (ImGui::Combo("Align", &alignIdx, alignNames, 3)) {
            lbl->align = static_cast<TextAlign>(alignIdx);
        }
        checkUndoCapture(uiMgr);
        ImGui::Checkbox("Word Wrap", &lbl->wordWrap); checkUndoCapture(uiMgr);
    }
    else if (auto* button = dynamic_cast<Button*>(selectedNode_)) {
        char textBuf[256] = {};
        snprintf(textBuf, sizeof(textBuf), "%s", button->text.c_str());
        if (ImGui::InputText("Text##btn", textBuf, sizeof(textBuf))) {
            button->text = textBuf;
        }
        checkUndoCapture(uiMgr);
        char iconBuf[256] = {};
        snprintf(iconBuf, sizeof(iconBuf), "%s", button->icon.c_str());
        if (ImGui::InputText("Icon##btn", iconBuf, sizeof(iconBuf))) {
            button->icon = iconBuf;
        }
        checkUndoCapture(uiMgr);
    }
    else if (auto* textInput = dynamic_cast<TextInput*>(selectedNode_)) {
        char placeholderBuf[256] = {};
        snprintf(placeholderBuf, sizeof(placeholderBuf), "%s", textInput->placeholder.c_str());
        if (ImGui::InputText("Placeholder", placeholderBuf, sizeof(placeholderBuf))) {
            textInput->placeholder = placeholderBuf;
        }
        checkUndoCapture(uiMgr);
        ImGui::DragInt("Max Length", &textInput->maxLength, 1.0f, 0, 1000); checkUndoCapture(uiMgr);
    }
    else if (auto* bar = dynamic_cast<ProgressBar*>(selectedNode_)) {
        ImGui::DragFloat("Value", &bar->value, 0.5f, 0.0f, bar->maxValue); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Max Value", &bar->maxValue, 1.0f, 0.1f, 10000.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Fill Color", &bar->fillColor.r); checkUndoCapture(uiMgr);
        ImGui::Checkbox("Show Text", &bar->showText); checkUndoCapture(uiMgr);
        const char* dirNames[] = {"LeftToRight", "RightToLeft", "BottomToTop", "TopToBottom"};
        int dirIdx = static_cast<int>(bar->direction);
        if (ImGui::Combo("Direction", &dirIdx, dirNames, 4)) {
            bar->direction = static_cast<BarDirection>(dirIdx);
        }
        checkUndoCapture(uiMgr);
    }
    else if (auto* window = dynamic_cast<Window*>(selectedNode_)) {
        char titleBuf[256] = {};
        snprintf(titleBuf, sizeof(titleBuf), "%s", window->title.c_str());
        if (ImGui::InputText("Title##win", titleBuf, sizeof(titleBuf))) {
            window->title = titleBuf;
        }
        checkUndoCapture(uiMgr);
        ImGui::Checkbox("Closeable##win", &window->closeable); checkUndoCapture(uiMgr);
        ImGui::Checkbox("Resizable", &window->resizable); checkUndoCapture(uiMgr);
        ImGui::Checkbox("Minimizable", &window->minimizable); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Title Bar Height", &window->titleBarHeight, 1.0f, 16.0f, 64.0f); checkUndoCapture(uiMgr);
    }
    else if (auto* tabs = dynamic_cast<TabContainer*>(selectedNode_)) {
        int activeTab = tabs->activeTab;
        int maxTab = static_cast<int>(tabs->tabLabels_.size()) - 1;
        if (maxTab < 0) maxTab = 0;
        if (ImGui::DragInt("Active Tab", &activeTab, 1.0f, 0, maxTab)) {
            tabs->activeTab = activeTab;
        }
        checkUndoCapture(uiMgr);
        ImGui::DragFloat("Tab Height", &tabs->tabHeight, 1.0f, 16.0f, 64.0f); checkUndoCapture(uiMgr);
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
        checkUndoCapture(uiMgr);
        ImGui::DragInt("Quantity", &slot->quantity, 1.0f, 0, 9999); checkUndoCapture(uiMgr);
        char slotTypeBuf[128] = {};
        snprintf(slotTypeBuf, sizeof(slotTypeBuf), "%s", slot->slotType.c_str());
        if (ImGui::InputText("Slot Type", slotTypeBuf, sizeof(slotTypeBuf))) {
            slot->slotType = slotTypeBuf;
        }
        checkUndoCapture(uiMgr);
    }
    else if (auto* grid = dynamic_cast<SlotGrid*>(selectedNode_)) {
        ImGui::DragInt("Columns", &grid->columns, 1.0f, 1, 20); checkUndoCapture(uiMgr);
        ImGui::DragInt("Rows", &grid->rows, 1.0f, 1, 20); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Slot Size", &grid->slotSize, 1.0f, 16.0f, 128.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Slot Padding", &grid->slotPadding, 0.5f, 0.0f, 16.0f); checkUndoCapture(uiMgr);
    }
    else if (auto* scroll = dynamic_cast<ScrollView*>(selectedNode_)) {
        ImGui::DragFloat("Scroll Offset", &scroll->scrollOffset, 1.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Content Height", &scroll->contentHeight, 1.0f, 0.0f, 10000.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Scroll Speed", &scroll->scrollSpeed, 1.0f, 1.0f, 200.0f); checkUndoCapture(uiMgr);
    }
    else if (auto* pib = dynamic_cast<PlayerInfoBlock*>(selectedNode_)) {
        ImGui::SeparatorText("PlayerInfoBlock");
        ImGui::DragFloat("Portrait Size", &pib->portraitSize, 1.0f, 16.0f, 128.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Bar Width", &pib->barWidth, 1.0f, 40.0f, 400.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Bar Height", &pib->barHeight, 1.0f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Bar Spacing", &pib->barSpacing, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("HP: %.0f / %.0f", pib->hp, pib->maxHp);
        ImGui::Text("MP: %.0f / %.0f", pib->mp, pib->maxMp);
        ImGui::Text("Level: %d", pib->level);
        ImGui::Text("Name: %s", pib->playerName.c_str());
        ImGui::Text("Gold: %s", pib->goldText.c_str());
    }
    else if (auto* arc = dynamic_cast<SkillArc*>(selectedNode_)) {
        ImGui::SeparatorText("SkillArc");
        ImGui::DragFloat("Attack Button Size", &arc->attackButtonSize, 1.0f, 40.0f, 200.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Slot Size", &arc->slotSize, 1.0f, 20.0f, 128.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Arc Radius", &arc->arcRadius, 1.0f, 30.0f, 200.0f); checkUndoCapture(uiMgr);
        ImGui::DragInt("Slot Count", &arc->slotCount, 1.0f, 1, 8); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Start Angle", &arc->startAngleDeg, 1.0f, 0.0f, 360.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("End Angle", &arc->endAngleDeg, 1.0f, 0.0f, 360.0f); checkUndoCapture(uiMgr);
    }
    else if (auto* dp = dynamic_cast<DPad*>(selectedNode_)) {
        ImGui::SeparatorText("DPad");
        ImGui::DragFloat("DPad Size", &dp->dpadSize, 1.0f, 60.0f, 400.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Dead Zone Radius", &dp->deadZoneRadius, 0.5f, 0.0f, 50.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Opacity##dpad", &dp->opacity, 0.01f, 0.0f, 1.0f); checkUndoCapture(uiMgr);
    }
    else if (auto* mbr = dynamic_cast<MenuButtonRow*>(selectedNode_)) {
        ImGui::SeparatorText("MenuButtonRow");
        ImGui::DragFloat("Button Size##mbr", &mbr->buttonSize, 1.0f, 16.0f, 80.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Spacing##mbr", &mbr->spacing, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
        ImGui::Separator();
        for (size_t i = 0; i < mbr->labels.size(); i++) {
            ImGui::Text("  Label %zu: %s", i, mbr->labels[i].c_str());
        }
    }
    else if (auto* ticker = dynamic_cast<ChatTicker*>(selectedNode_)) {
        ImGui::SeparatorText("ChatTicker");
        ImGui::DragFloat("Scroll Speed##ticker", &ticker->scrollSpeed, 1.0f, 0.0f, 200.0f); checkUndoCapture(uiMgr);
    }
    else if (auto* expBar = dynamic_cast<EXPBar*>(selectedNode_)) {
        ImGui::SeparatorText("EXPBar");
        ImGui::Text("XP: %.0f / %.0f", expBar->xp, expBar->xpToLevel);
        if (expBar->xpToLevel > 0.0f) {
            float pct = (expBar->xp / expBar->xpToLevel) * 100.0f;
            ImGui::Text("Progress: %.1f%%", pct);
        }
    }
    else if (auto* tf = dynamic_cast<TargetFrame*>(selectedNode_)) {
        ImGui::SeparatorText("TargetFrame");
        ImGui::Text("Target: %s", tf->targetName.c_str());
        ImGui::Text("HP: %.0f / %.0f", tf->hp, tf->maxHp);
    }
    else if (auto* lsb = dynamic_cast<LeftSidebar*>(selectedNode_)) {
        ImGui::SeparatorText("LeftSidebar");
        ImGui::DragFloat("Button Size##lsb", &lsb->buttonSize, 1.0f, 16.0f, 80.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Spacing##lsb", &lsb->spacing, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Active Panel: %s", lsb->activePanel.c_str());
        for (size_t i = 0; i < lsb->panelLabels.size(); i++) {
            ImGui::Text("  Panel %zu: %s", i, lsb->panelLabels[i].c_str());
        }
    }
    else if (auto* inv = dynamic_cast<InventoryPanel*>(selectedNode_)) {
        ImGui::SeparatorText("InventoryPanel");
        ImGui::DragInt("Grid Columns", &inv->gridColumns, 1.0f, 1, 10); checkUndoCapture(uiMgr);
        ImGui::DragInt("Grid Rows", &inv->gridRows, 1.0f, 1, 10); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Slot Size##inv", &inv->slotSize, 1.0f, 16.0f, 128.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Equip Slot Size", &inv->equipSlotSize, 1.0f, 16.0f, 128.0f); checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Gold: %d", inv->gold);
        ImGui::Text("Platinum: %d", inv->platinum);
        ImGui::Text("Armor: %d", inv->armorValue);
    }
    else if (auto* sp = dynamic_cast<StatusPanel*>(selectedNode_)) {
        ImGui::SeparatorText("StatusPanel");
        ImGui::Text("Name: %s", sp->playerName.c_str());
        ImGui::Text("Class: %s", sp->className.c_str());
        ImGui::Text("Faction: %s", sp->factionName.c_str());
        ImGui::Text("Level: %d", sp->level);
        ImGui::Text("XP: %.0f / %.0f", sp->xp, sp->xpToLevel);
        ImGui::Separator();
        ImGui::Text("STR: %d  INT: %d  DEX: %d", sp->str, sp->intl, sp->dex);
        ImGui::Text("CON: %d  WIS: %d  ARM: %d", sp->con, sp->wis, sp->arm);
        ImGui::Text("HIT: %d  CRI: %d  SPD: %d", sp->hit, sp->cri, sp->spd);
    }
    else if (auto* skp = dynamic_cast<SkillPanel*>(selectedNode_)) {
        ImGui::SeparatorText("SkillPanel");
        ImGui::DragInt("Active Set Page", &skp->activeSetPage, 1.0f, 0, 4); checkUndoCapture(uiMgr);
        ImGui::Text("Remaining Points: %d", skp->remainingPoints);
        ImGui::Text("Selected Skill: %d", skp->selectedSkillIndex);
        ImGui::Separator();
        for (size_t i = 0; i < skp->classSkills.size(); i++) {
            auto& sk = skp->classSkills[i];
            ImGui::Text("  [%zu] %s Lv%d/%d %s", i, sk.name.c_str(),
                         sk.currentLevel, sk.maxLevel, sk.unlocked ? "" : "(locked)");
        }
    }
    else {
        ImGui::TextDisabled("(no widget-specific properties)");
    }

    // --- Save ---
    ImGui::Separator();
    if (ImGui::Button("Save Screen") && !selectedScreenId_.empty()) {
        std::string relPath = "assets/ui/screens/" + selectedScreenId_ + ".json";
        UISerializer::saveToFile(relPath, selectedScreenId_, uiMgr.getScreen(selectedScreenId_));
        LOG_INFO("UI", "Saved screen: %s", relPath.c_str());
        if (!sourceDir_.empty()) {
            std::string srcPath = sourceDir_ + "/" + relPath;
            UISerializer::saveToFile(srcPath, selectedScreenId_, uiMgr.getScreen(selectedScreenId_));
            LOG_INFO("UI", "Saved screen (source): %s", srcPath.c_str());
        }
    }

    ImGui::End();
}

// ============================================================================
// Anchor sub-editor
// ============================================================================

void UIEditorPanel::drawAnchorEditor(UINode* node, UIManager& uiMgr) {
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
    checkUndoCapture(uiMgr);

    ImGui::DragFloat2("Offset", &anchor.offset.x, 1.0f); checkUndoCapture(uiMgr);
    ImGui::DragFloat2("Size", &anchor.size.x, 1.0f, 0.0f, 4096.0f); checkUndoCapture(uiMgr);

    if (ImGui::TreeNode("Margin")) {
        ImGui::DragFloat("Top##margin", &anchor.margin.x, 0.5f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Right##margin", &anchor.margin.y, 0.5f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Bottom##margin", &anchor.margin.z, 0.5f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Left##margin", &anchor.margin.w, 0.5f); checkUndoCapture(uiMgr);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Padding")) {
        ImGui::DragFloat("Top##padding", &anchor.padding.x, 0.5f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Right##padding", &anchor.padding.y, 0.5f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Bottom##padding", &anchor.padding.z, 0.5f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Left##padding", &anchor.padding.w, 0.5f); checkUndoCapture(uiMgr);
        ImGui::TreePop();
    }
}

// ============================================================================
// Style sub-editor
// ============================================================================

void UIEditorPanel::drawStyleEditor(UINode* node, UIManager& uiMgr) {
    ImGui::SeparatorText("Style");

    auto names = uiMgr.theme().styleNames();

    int currentIdx = -1;
    for (int i = 0; i < static_cast<int>(names.size()); i++) {
        if (names[i] == node->styleName()) {
            currentIdx = i;
            break;
        }
    }

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
    checkUndoCapture(uiMgr);

    UIStyle& style = node->resolvedStyle();
    ImGui::ColorEdit4("Background Color", &style.backgroundColor.r); checkUndoCapture(uiMgr);
    ImGui::ColorEdit4("Border Color", &style.borderColor.r); checkUndoCapture(uiMgr);
    ImGui::DragFloat("Border Width", &style.borderWidth, 0.5f, 0.0f, 10.0f); checkUndoCapture(uiMgr);
    ImGui::ColorEdit4("Text Color", &style.textColor.r); checkUndoCapture(uiMgr);
    ImGui::DragFloat("Font Size", &style.fontSize, 0.5f, 6.0f, 72.0f); checkUndoCapture(uiMgr);
    ImGui::DragFloat("Opacity", &style.opacity, 0.01f, 0.0f, 1.0f); checkUndoCapture(uiMgr);
    ImGui::ColorEdit4("Hover Color", &style.hoverColor.r); checkUndoCapture(uiMgr);
    ImGui::ColorEdit4("Pressed Color", &style.pressedColor.r); checkUndoCapture(uiMgr);
    ImGui::ColorEdit4("Disabled Color", &style.disabledColor.r); checkUndoCapture(uiMgr);
}

} // namespace fate
