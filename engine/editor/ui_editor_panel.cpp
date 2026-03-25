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
#include "engine/ui/widgets/image_box.h"
#include "engine/ui/widgets/buff_bar.h"
#include "engine/ui/widgets/boss_hp_bar.h"
#include "engine/ui/widgets/confirm_dialog.h"
#include "engine/ui/widgets/notification_toast.h"
#include "engine/ui/widgets/checkbox.h"
#include "engine/ui/widgets/login_screen.h"
#include "engine/ui/widgets/party_frame.h"
#include "engine/ui/widgets/chat_panel.h"
#include "engine/ui/widgets/death_overlay.h"
#include "engine/ui/widgets/fate_status_bar.h"
#include "engine/ui/widgets/character_select_screen.h"
#include "engine/ui/widgets/character_creation_screen.h"
#include "engine/ui/widgets/guild_panel.h"
#include "engine/ui/widgets/npc_dialogue_panel.h"
#include "engine/ui/widgets/shop_panel.h"
#include "engine/ui/widgets/bank_panel.h"
#include "engine/ui/widgets/teleporter_panel.h"
#include "engine/ui/widgets/trade_window.h"
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
// Hierarchy helpers — type badge colors & icons (Unity-style)
// ============================================================================

namespace {

struct TypeBadge { ImVec4 color; const char* icon; };

TypeBadge badgeForType(const std::string& type) {
    if (type == "panel")              return {{0.30f, 0.55f, 0.80f, 1.0f}, "PNL"};
    if (type == "label")              return {{0.65f, 0.65f, 0.45f, 1.0f}, "LBL"};
    if (type == "button")             return {{0.45f, 0.70f, 0.45f, 1.0f}, "BTN"};
    if (type == "text_input")         return {{0.55f, 0.55f, 0.75f, 1.0f}, "TXT"};
    if (type == "progress_bar")       return {{0.70f, 0.50f, 0.30f, 1.0f}, "BAR"};
    if (type == "image_box")          return {{0.50f, 0.70f, 0.50f, 1.0f}, "IMG"};
    if (type == "slot" || type == "slot_grid") return {{0.60f, 0.50f, 0.65f, 1.0f}, "SLT"};
    if (type == "scroll_view")        return {{0.45f, 0.60f, 0.70f, 1.0f}, "SCR"};
    if (type == "window")             return {{0.40f, 0.60f, 0.80f, 1.0f}, "WIN"};
    if (type == "tab_container")      return {{0.55f, 0.55f, 0.70f, 1.0f}, "TAB"};
    if (type == "checkbox")           return {{0.50f, 0.70f, 0.55f, 1.0f}, "CHK"};
    if (type == "skill_arc")          return {{0.75f, 0.55f, 0.40f, 1.0f}, "ARC"};
    if (type == "dpad")               return {{0.65f, 0.55f, 0.35f, 1.0f}, "PAD"};
    if (type == "player_info_block")  return {{0.40f, 0.65f, 0.65f, 1.0f}, "PLR"};
    if (type == "target_frame")       return {{0.75f, 0.40f, 0.40f, 1.0f}, "TGT"};
    if (type == "exp_bar")            return {{0.60f, 0.60f, 0.30f, 1.0f}, "EXP"};
    if (type == "buff_bar")           return {{0.50f, 0.65f, 0.50f, 1.0f}, "BUF"};
    if (type == "boss_hp_bar")        return {{0.80f, 0.35f, 0.35f, 1.0f}, "BOS"};
    if (type == "fate_status_bar")    return {{0.50f, 0.65f, 0.75f, 1.0f}, "STS"};
    if (type == "chat_panel")         return {{0.55f, 0.60f, 0.45f, 1.0f}, "CHT"};
    if (type == "chat_ticker")        return {{0.50f, 0.55f, 0.45f, 1.0f}, "TKR"};
    if (type == "inventory_panel")    return {{0.60f, 0.50f, 0.65f, 1.0f}, "INV"};
    if (type == "skill_panel")        return {{0.70f, 0.50f, 0.40f, 1.0f}, "SKL"};
    if (type == "status_panel")       return {{0.40f, 0.65f, 0.65f, 1.0f}, "STA"};
    if (type == "shop_panel")         return {{0.65f, 0.60f, 0.35f, 1.0f}, "SHP"};
    if (type == "bank_panel")         return {{0.55f, 0.60f, 0.40f, 1.0f}, "BNK"};
    if (type == "trade_window")       return {{0.60f, 0.55f, 0.45f, 1.0f}, "TRD"};
    if (type == "guild_panel")        return {{0.45f, 0.55f, 0.70f, 1.0f}, "GLD"};
    if (type == "party_frame")        return {{0.45f, 0.65f, 0.55f, 1.0f}, "PTY"};
    if (type == "npc_dialogue_panel") return {{0.55f, 0.55f, 0.60f, 1.0f}, "NPC"};
    if (type == "teleporter_panel")   return {{0.50f, 0.60f, 0.70f, 1.0f}, "TEL"};
    if (type == "confirm_dialog")     return {{0.70f, 0.50f, 0.50f, 1.0f}, "DLG"};
    if (type == "notification_toast") return {{0.65f, 0.55f, 0.40f, 1.0f}, "TST"};
    if (type == "login_screen")       return {{0.50f, 0.60f, 0.70f, 1.0f}, "LOG"};
    if (type == "death_overlay")      return {{0.75f, 0.35f, 0.35f, 1.0f}, "DTH"};
    if (type == "left_sidebar")       return {{0.45f, 0.55f, 0.65f, 1.0f}, "BAR"};
    if (type == "menu_button_row")    return {{0.50f, 0.55f, 0.55f, 1.0f}, "MNU"};
    if (type == "character_select_screen")   return {{0.50f, 0.60f, 0.70f, 1.0f}, "SEL"};
    if (type == "character_creation_screen") return {{0.50f, 0.60f, 0.70f, 1.0f}, "CRT"};
    return {{0.50f, 0.50f, 0.50f, 1.0f}, "???"};
}

// Draw alternating row background across the full window width
void drawRowBackground(int rowIdx, bool isSelected) {
    if (isSelected) {
        ImVec2 min = ImGui::GetCursorScreenPos();
        min.y -= ImGui::GetStyle().ItemSpacing.y * 0.5f;
        float h = ImGui::GetTextLineHeightWithSpacing();
        float w = ImGui::GetWindowWidth();
        ImGui::GetWindowDrawList()->AddRectFilled(
            min, ImVec2(min.x + w, min.y + h),
            IM_COL32(51, 102, 170, 80));
    } else if (rowIdx % 2 == 1) {
        ImVec2 min = ImGui::GetCursorScreenPos();
        min.y -= ImGui::GetStyle().ItemSpacing.y * 0.5f;
        float h = ImGui::GetTextLineHeightWithSpacing();
        float w = ImGui::GetWindowWidth();
        ImGui::GetWindowDrawList()->AddRectFilled(
            min, ImVec2(min.x + w, min.y + h),
            IM_COL32(255, 255, 255, 8));
    }
}

// Draw the small colored type badge
void drawTypeBadge(const TypeBadge& badge) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float sz = ImGui::GetTextLineHeight();
    float pad = 1.0f;
    ImU32 bg = ImGui::ColorConvertFloat4ToU32(
        ImVec4(badge.color.x * 0.4f, badge.color.y * 0.4f, badge.color.z * 0.4f, 1.0f));
    ImU32 fg = ImGui::ColorConvertFloat4ToU32(badge.color);
    dl->AddRectFilled(ImVec2(pos.x, pos.y + pad),
                      ImVec2(pos.x + sz, pos.y + sz - pad), bg, 3.0f);
    float textW = ImGui::CalcTextSize(badge.icon).x;
    dl->AddText(ImVec2(pos.x + (sz - textW) * 0.5f, pos.y + pad), fg, badge.icon);
    ImGui::Dummy(ImVec2(sz + 4.0f, sz));
    ImGui::SameLine();
}

} // anonymous namespace

// ============================================================================
// Hierarchy tree window
// ============================================================================

void UIEditorPanel::drawHierarchy(UIManager& uiMgr) {
    if (!showHierarchy) return;
    if (!ImGui::Begin("UI Hierarchy", &showHierarchy)) {
        ImGui::End();
        return;
    }

    hierarchyRowIdx_ = 0;

    auto ids = uiMgr.screenIds();
    if (ids.empty()) {
        ImGui::TextDisabled("No UI screens loaded");
    }

    for (auto& id : ids) {
        auto* root = uiMgr.getScreen(id);
        if (!root) continue;

        // Screen header — distinct from child nodes
        drawRowBackground(hierarchyRowIdx_++, false);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.75f, 0.45f, 1.0f));
        bool opened = ImGui::TreeNodeEx(id.c_str(), ImGuiTreeNodeFlags_DefaultOpen
            | ImGuiTreeNodeFlags_SpanAvailWidth);
        ImGui::PopStyleColor();

        if (opened) {
            drawNodeTree(root, id);
            ImGui::TreePop();
        }
    }

    // Escape to deselect
    if (selectedNode_ && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        selectedNode_ = nullptr;
        selectedNodeId_.clear();
        selectedScreenId_.clear();
    }

    ImGui::End();
}

void UIEditorPanel::drawNodeTree(UINode* node, const std::string& screenId) {
    bool isSelected = (node == selectedNode_);
    drawRowBackground(hierarchyRowIdx_++, isSelected);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
    if (node->childCount() == 0) flags |= ImGuiTreeNodeFlags_Leaf;

    ImGui::PushID(node);

    // Visibility toggle (eye icon)
    bool vis = node->visibleSelf();
    ImGui::PushStyleColor(ImGuiCol_Text, vis
        ? ImVec4(0.7f, 0.7f, 0.7f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    char eyeLabel[32];
    snprintf(eyeLabel, sizeof(eyeLabel), "%s##eye", vis ? "o" : "-");
    if (ImGui::SmallButton(eyeLabel)) {
        node->setVisible(!vis);
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();

    // Type badge
    TypeBadge badge = badgeForType(node->type());
    drawTypeBadge(badge);

    // Dim hidden nodes
    if (!vis) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

    // Show just the ID as label — type is conveyed by the badge
    bool opened = ImGui::TreeNodeEx(node->id().c_str(), flags);

    if (!vis) ImGui::PopStyleColor();

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
        ImGui::Separator();
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
        ImGui::DragFloat("PickUp Button Size", &arc->pickUpButtonSize, 1.0f, 20.0f, 200.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Slot Size", &arc->slotSize, 1.0f, 20.0f, 128.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Arc Radius", &arc->arcRadius, 1.0f, 30.0f, 400.0f); checkUndoCapture(uiMgr);
        ImGui::DragInt("Slot Count", &arc->slotCount, 1.0f, 1, 8); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Start Angle", &arc->startAngleDeg, 1.0f, 0.0f, 360.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("End Angle", &arc->endAngleDeg, 1.0f, 0.0f, 360.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat2("Skill Arc Offset", &arc->skillArcOffset.x, 1.0f, -300.0f, 300.0f); checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Button Positions");
        ImGui::DragFloat2("Attack Offset", &arc->attackOffset.x, 1.0f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat2("PickUp Offset", &arc->pickUpOffset.x, 1.0f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("SlotArc (Page Selector)");
        ImGui::DragFloat("SlotArc Radius", &arc->slotArcRadius, 1.0f, 10.0f, 200.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("SlotArc Start Angle", &arc->slotArcStartDeg, 1.0f, 0.0f, 360.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("SlotArc End Angle", &arc->slotArcEndDeg, 1.0f, 0.0f, 360.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat2("SlotArc Offset", &arc->slotArcOffset.x, 1.0f, -300.0f, 300.0f); checkUndoCapture(uiMgr);
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
    else if (auto* img = dynamic_cast<ImageBox*>(selectedNode_)) {
        ImGui::SeparatorText("ImageBox");
        char texBuf[256] = {};
        snprintf(texBuf, sizeof(texBuf), "%s", img->textureKey.c_str());
        if (ImGui::InputText("Texture Key", texBuf, sizeof(texBuf))) {
            img->textureKey = texBuf;
        }
        checkUndoCapture(uiMgr);
        const char* fitNames[] = {"Stretch", "Fit"};
        int fitIdx = static_cast<int>(img->fitMode);
        if (ImGui::Combo("Fit Mode", &fitIdx, fitNames, 2)) {
            img->fitMode = static_cast<ImageFitMode>(fitIdx);
        }
        checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Tint", &img->tint.r); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Source X (UV)", &img->sourceRect.x, 0.01f, 0.0f, 1.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Source Y (UV)", &img->sourceRect.y, 0.01f, 0.0f, 1.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Source W (UV)", &img->sourceRect.w, 0.01f, 0.0f, 1.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Source H (UV)", &img->sourceRect.h, 0.01f, 0.0f, 1.0f); checkUndoCapture(uiMgr);
    }
    else if (auto* bb = dynamic_cast<BuffBar*>(selectedNode_)) {
        ImGui::SeparatorText("BuffBar");
        ImGui::DragFloat("Icon Size##bb", &bb->iconSize, 1.0f, 8.0f, 64.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Spacing##bb", &bb->spacing, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
        ImGui::DragInt("Max Visible", &bb->maxVisible, 1.0f, 1, 30); checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Active Buffs: %zu", bb->buffs.size());
    }
    else if (auto* bhp = dynamic_cast<BossHPBar*>(selectedNode_)) {
        ImGui::SeparatorText("BossHPBar");
        ImGui::DragFloat("Bar Height##bhp", &bhp->barHeight, 1.0f, 8.0f, 60.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Bar Padding##bhp", &bhp->barPadding, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Boss: %s", bhp->bossName.c_str());
        ImGui::Text("HP: %d / %d", bhp->currentHP, bhp->maxHP);
    }
    else if (auto* cd = dynamic_cast<ConfirmDialog*>(selectedNode_)) {
        ImGui::SeparatorText("ConfirmDialog");
        char msgBuf[512] = {};
        snprintf(msgBuf, sizeof(msgBuf), "%s", cd->message.c_str());
        if (ImGui::InputText("Message##cd", msgBuf, sizeof(msgBuf))) {
            cd->message = msgBuf;
        }
        checkUndoCapture(uiMgr);
        char confBuf[128] = {};
        snprintf(confBuf, sizeof(confBuf), "%s", cd->confirmText.c_str());
        if (ImGui::InputText("Confirm Text", confBuf, sizeof(confBuf))) {
            cd->confirmText = confBuf;
        }
        checkUndoCapture(uiMgr);
        char canBuf[128] = {};
        snprintf(canBuf, sizeof(canBuf), "%s", cd->cancelText.c_str());
        if (ImGui::InputText("Cancel Text", canBuf, sizeof(canBuf))) {
            cd->cancelText = canBuf;
        }
        checkUndoCapture(uiMgr);
        ImGui::DragFloat("Button Width##cd", &cd->buttonWidth, 1.0f, 40.0f, 300.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Button Height##cd", &cd->buttonHeight, 1.0f, 16.0f, 80.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Button Spacing##cd", &cd->buttonSpacing, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
    }
    else if (auto* nt = dynamic_cast<NotificationToast*>(selectedNode_)) {
        ImGui::SeparatorText("NotificationToast");
        ImGui::DragFloat("Toast Height", &nt->toastHeight, 1.0f, 16.0f, 60.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Toast Spacing", &nt->toastSpacing, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Fade In Time", &nt->fadeInTime, 0.05f, 0.0f, 2.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Fade Out Time", &nt->fadeOutTime, 0.05f, 0.0f, 2.0f); checkUndoCapture(uiMgr);
        ImGui::DragInt("Max Toasts", &nt->maxToasts, 1.0f, 1, 20); checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Active Toasts: %zu", nt->toasts().size());
    }
    else if (auto* cb = dynamic_cast<Checkbox*>(selectedNode_)) {
        ImGui::SeparatorText("Checkbox");
        char lblBuf[256] = {};
        snprintf(lblBuf, sizeof(lblBuf), "%s", cb->label.c_str());
        if (ImGui::InputText("Label##cb", lblBuf, sizeof(lblBuf))) {
            cb->label = lblBuf;
        }
        checkUndoCapture(uiMgr);
        ImGui::Checkbox("Checked##cb", &cb->checked); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Box Size", &cb->boxSize, 1.0f, 8.0f, 40.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Spacing##cb", &cb->spacing, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
    }
    else if (auto* ls = dynamic_cast<LoginScreen*>(selectedNode_)) {
        ImGui::SeparatorText("LoginScreen");
        char hostBuf[256] = {};
        snprintf(hostBuf, sizeof(hostBuf), "%s", ls->serverHost.c_str());
        if (ImGui::InputText("Server Host", hostBuf, sizeof(hostBuf))) {
            ls->serverHost = hostBuf;
        }
        checkUndoCapture(uiMgr);
        ImGui::DragInt("Server Port", &ls->serverPort, 1.0f, 1, 65535); checkUndoCapture(uiMgr);
        ImGui::Separator();
        const char* modeNames[] = {"Login", "Register"};
        ImGui::Text("Mode: %s", modeNames[static_cast<int>(ls->mode)]);
        ImGui::Text("Status: %s", ls->statusMessage.c_str());
    }
    else if (auto* pf = dynamic_cast<PartyFrame*>(selectedNode_)) {
        ImGui::SeparatorText("PartyFrame");
        ImGui::DragFloat("Card Width", &pf->cardWidth, 1.0f, 60.0f, 400.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Card Height", &pf->cardHeight, 1.0f, 20.0f, 120.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Card Spacing", &pf->cardSpacing, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Members: %zu", pf->members.size());
        for (size_t i = 0; i < pf->members.size(); i++) {
            auto& m = pf->members[i];
            ImGui::Text("  [%zu] %s Lv%d HP:%.0f/%.0f%s", i, m.name.c_str(),
                         m.level, m.hp, m.maxHp, m.isLeader ? " (Leader)" : "");
        }
    }
    else if (auto* cp = dynamic_cast<ChatPanel*>(selectedNode_)) {
        ImGui::SeparatorText("ChatPanel");
        ImGui::DragInt("Idle Lines", &cp->chatIdleLines, 1.0f, 0, 5); checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Active Tab: %d", cp->activeTab);
        ImGui::Text("Mode: %s", cp->fullPanelMode_ ? "Full Panel" : "Idle Overlay");
        ImGui::Text("In Party: %s", cp->isInParty ? "Yes" : "No");
        ImGui::Text("In Guild: %s", cp->isInGuild ? "Yes" : "No");
    }
    else if (auto* fsb = dynamic_cast<FateStatusBar*>(selectedNode_)) {
        ImGui::SeparatorText("FateStatusBar — Layout");
        ImGui::DragFloat("Top Bar Height", &fsb->topBarHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Portrait Radius", &fsb->portraitRadius, 1.0f, 5.0f, 60.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("HP/MP Bar Height", &fsb->barHeight, 1.0f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("HP Bar Color", &fsb->hpBarColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("MP Bar Color", &fsb->mpBarColor.r); checkUndoCapture(uiMgr);

        ImGui::SeparatorText("Menu Button");
        ImGui::Checkbox("Show Menu Button", &fsb->showMenuButton); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Menu Btn Radius", &fsb->menuBtnSize, 1.0f, 5.0f, 60.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Menu Btn Gap", &fsb->menuBtnGap, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);

        ImGui::SeparatorText("Chat Button");
        ImGui::Checkbox("Show Chat Button", &fsb->showChatButton); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Chat Btn Radius", &fsb->chatBtnSize, 1.0f, 5.0f, 60.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Chat Btn Offset X", &fsb->chatBtnOffsetX, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);

        ImGui::SeparatorText("Coordinates");
        ImGui::Checkbox("Show Coordinates", &fsb->showCoordinates); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Coord Font Size", &fsb->coordFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Coord Offset Y", &fsb->coordOffsetY, 0.5f, -20.0f, 40.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Coord Color", &fsb->coordColor.r); checkUndoCapture(uiMgr);

        ImGui::SeparatorText("Font Sizes");
        ImGui::DragFloat("Level Font", &fsb->levelFontSize, 0.5f, 6.0f, 50.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Label Font (HP/MP)", &fsb->labelFontSize, 0.5f, 6.0f, 50.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Number Font", &fsb->numberFontSize, 0.5f, 6.0f, 50.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Button Font", &fsb->buttonFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
    }
    else if (auto* dov = dynamic_cast<DeathOverlay*>(selectedNode_)) {
        ImGui::SeparatorText("DeathOverlay");
        ImGui::Checkbox("Active##dov", &dov->active); checkUndoCapture(uiMgr);
        int xpL = dov->xpLost;
        if (ImGui::DragInt("XP Lost", &xpL, 1.0f, 0, 999999)) { dov->xpLost = xpL; }
        checkUndoCapture(uiMgr);
        int honL = dov->honorLost;
        if (ImGui::DragInt("Honor Lost", &honL, 1.0f, 0, 999999)) { dov->honorLost = honL; }
        checkUndoCapture(uiMgr);
        ImGui::DragFloat("Countdown##dov", &dov->countdown, 0.1f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
        ImGui::Checkbox("Respawn Pending", &dov->respawnPending); checkUndoCapture(uiMgr);
    }
    else if (auto* css = dynamic_cast<CharacterSelectScreen*>(selectedNode_)) {
        ImGui::SeparatorText("CharacterSelectScreen");
        ImGui::DragFloat("Slot Circle Size", &css->slotCircleSize, 1.0f, 20.0f, 120.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Entry Button Width", &css->entryButtonWidth, 1.0f, 60.0f, 300.0f); checkUndoCapture(uiMgr);
        ImGui::DragInt("Selected Slot##css", &css->selectedSlot, 1.0f, 0, 6); checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Slots: %zu", css->slots.size());
        for (size_t i = 0; i < css->slots.size(); i++) {
            auto& s = css->slots[i];
            if (s.empty)
                ImGui::Text("  [%zu] (empty)", i);
            else
                ImGui::Text("  [%zu] %s %s Lv%d", i, s.name.c_str(), s.className.c_str(), s.level);
        }
    }
    else if (auto* ccs = dynamic_cast<CharacterCreationScreen*>(selectedNode_)) {
        ImGui::SeparatorText("CharacterCreationScreen");
        const char* classNames[] = {"Warrior", "Magician", "Archer"};
        if (ImGui::Combo("Class##ccs", &ccs->selectedClass, classNames, 3)) {}
        checkUndoCapture(uiMgr);
        const char* factionNames[] = {"Xyros", "Fenor", "Zethos", "Solis"};
        if (ImGui::Combo("Faction##ccs", &ccs->selectedFaction, factionNames, 4)) {}
        checkUndoCapture(uiMgr);
        char nameBuf[32] = {};
        snprintf(nameBuf, sizeof(nameBuf), "%s", ccs->characterName.c_str());
        if (ImGui::InputText("Character Name##ccs", nameBuf, sizeof(nameBuf))) {
            ccs->characterName = nameBuf;
        }
        checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Status: %s", ccs->statusMessage.c_str());
    }
    else if (auto* gp = dynamic_cast<GuildPanel*>(selectedNode_)) {
        ImGui::SeparatorText("GuildPanel");
        char gnameBuf[128] = {};
        snprintf(gnameBuf, sizeof(gnameBuf), "%s", gp->guildName.c_str());
        if (ImGui::InputText("Guild Name", gnameBuf, sizeof(gnameBuf))) {
            gp->guildName = gnameBuf;
        }
        checkUndoCapture(uiMgr);
        ImGui::DragInt("Guild Level", &gp->guildLevel, 1.0f, 1, 50); checkUndoCapture(uiMgr);
        ImGui::DragInt("Member Count", &gp->memberCount, 1.0f, 0, 100); checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Members loaded: %zu", gp->members.size());
        for (size_t i = 0; i < gp->members.size(); i++) {
            auto& m = gp->members[i];
            ImGui::Text("  %s Lv%d %s%s", m.name.c_str(), m.level,
                         m.rank.c_str(), m.online ? " (on)" : "");
        }
    }
    else if (auto* ndp = dynamic_cast<NpcDialoguePanel*>(selectedNode_)) {
        ImGui::SeparatorText("NpcDialoguePanel");
        char npcBuf[128] = {};
        snprintf(npcBuf, sizeof(npcBuf), "%s", ndp->npcName.c_str());
        if (ImGui::InputText("NPC Name", npcBuf, sizeof(npcBuf))) {
            ndp->npcName = npcBuf;
        }
        checkUndoCapture(uiMgr);
        char greetBuf[512] = {};
        snprintf(greetBuf, sizeof(greetBuf), "%s", ndp->greeting.c_str());
        if (ImGui::InputText("Greeting", greetBuf, sizeof(greetBuf))) {
            ndp->greeting = greetBuf;
        }
        checkUndoCapture(uiMgr);
        ImGui::Checkbox("Has Shop##ndp", &ndp->hasShop); checkUndoCapture(uiMgr);
        ImGui::Checkbox("Has Bank##ndp", &ndp->hasBank); checkUndoCapture(uiMgr);
        ImGui::Checkbox("Has Teleporter##ndp", &ndp->hasTeleporter); checkUndoCapture(uiMgr);
        ImGui::Checkbox("Has Guild##ndp", &ndp->hasGuild); checkUndoCapture(uiMgr);
        ImGui::Checkbox("Has Dungeon##ndp", &ndp->hasDungeon); checkUndoCapture(uiMgr);
        ImGui::Checkbox("Story Mode##ndp", &ndp->isStoryMode); checkUndoCapture(uiMgr);
        if (ndp->isStoryMode) {
            char storyBuf[512] = {};
            snprintf(storyBuf, sizeof(storyBuf), "%s", ndp->storyText.c_str());
            if (ImGui::InputText("Story Text", storyBuf, sizeof(storyBuf))) {
                ndp->storyText = storyBuf;
            }
            checkUndoCapture(uiMgr);
        }
        ImGui::Separator();
        ImGui::Text("Quests: %zu", ndp->quests.size());
    }
    else if (auto* sp2 = dynamic_cast<ShopPanel*>(selectedNode_)) {
        ImGui::SeparatorText("ShopPanel");
        char shopBuf[128] = {};
        snprintf(shopBuf, sizeof(shopBuf), "%s", sp2->shopName.c_str());
        if (ImGui::InputText("Shop Name", shopBuf, sizeof(shopBuf))) {
            sp2->shopName = shopBuf;
        }
        checkUndoCapture(uiMgr);
        int64_t pg = sp2->playerGold;
        int pgInt = static_cast<int>(pg);
        if (ImGui::DragInt("Player Gold##shop", &pgInt, 1.0f, 0, 999999)) {
            sp2->playerGold = pgInt;
        }
        checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Shop Items: %zu", sp2->shopItems.size());
        ImGui::Text("NPC: %u", sp2->npcId);
    }
    else if (auto* bp = dynamic_cast<BankPanel*>(selectedNode_)) {
        ImGui::SeparatorText("BankPanel");
        int64_t bg = bp->bankGold;
        int bgInt = static_cast<int>(bg);
        if (ImGui::DragInt("Bank Gold", &bgInt, 1.0f, 0, 999999)) {
            bp->bankGold = bgInt;
        }
        checkUndoCapture(uiMgr);
        int64_t pg2 = bp->playerGold;
        int pgInt2 = static_cast<int>(pg2);
        if (ImGui::DragInt("Player Gold##bank", &pgInt2, 1.0f, 0, 999999)) {
            bp->playerGold = pgInt2;
        }
        checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Bank Items: %zu", bp->bankItems.size());
        ImGui::Text("NPC: %u", bp->npcId);
    }
    else if (auto* tp = dynamic_cast<TeleporterPanel*>(selectedNode_)) {
        ImGui::SeparatorText("TeleporterPanel");
        char titleBuf[128] = {};
        snprintf(titleBuf, sizeof(titleBuf), "%s", tp->title.c_str());
        if (ImGui::InputText("Title##tp", titleBuf, sizeof(titleBuf))) {
            tp->title = titleBuf;
        }
        checkUndoCapture(uiMgr);
        int64_t pg3 = tp->playerGold;
        int pgInt3 = static_cast<int>(pg3);
        if (ImGui::DragInt("Player Gold##tp", &pgInt3, 1.0f, 0, 999999)) {
            tp->playerGold = pgInt3;
        }
        checkUndoCapture(uiMgr);
        int plvl = tp->playerLevel;
        if (ImGui::DragInt("Player Level##tp", &plvl, 1.0f, 1, 999)) {
            tp->playerLevel = static_cast<uint16_t>(plvl);
        }
        checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Destinations: %zu", tp->destinations.size());
        for (size_t i = 0; i < tp->destinations.size(); i++) {
            auto& d = tp->destinations[i];
            ImGui::Text("  [%zu] %s (%s) %lld gold Lv%d", i, d.name.c_str(),
                         d.sceneId.c_str(), (long long)d.cost, d.requiredLevel);
        }
    }
    else if (auto* tw = dynamic_cast<TradeWindow*>(selectedNode_)) {
        ImGui::SeparatorText("TradeWindow");
        char pnameBuf[128] = {};
        snprintf(pnameBuf, sizeof(pnameBuf), "%s", tw->partnerName.c_str());
        if (ImGui::InputText("Partner Name", pnameBuf, sizeof(pnameBuf))) {
            tw->partnerName = pnameBuf;
        }
        checkUndoCapture(uiMgr);
        ImGui::DragInt("My Gold##tw", &tw->myGold, 1.0f, 0, 999999); checkUndoCapture(uiMgr);
        ImGui::DragInt("Their Gold##tw", &tw->theirGold, 1.0f, 0, 999999); checkUndoCapture(uiMgr);
        ImGui::Checkbox("My Locked##tw", &tw->myLocked); checkUndoCapture(uiMgr);
        ImGui::Checkbox("Their Locked##tw", &tw->theirLocked); checkUndoCapture(uiMgr);
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
