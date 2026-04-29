// The UI editor panel authors gameplay HUD/widgets — it depends on the
// `engine/ui/widgets/*` family which is only compiled when FATE_HAS_GAME=1.
// In demo builds this whole file compiles to nothing; the corresponding
// header (engine/editor/ui_editor_panel.h) is also fenced out of editor.h
// (see editor.h:25-29 forward-decl shim).
#ifdef FATE_HAS_GAME
#include "engine/editor/ui_editor_panel.h"
#include "engine/editor/undo.h"
#include "engine/editor/property_inspector.h"
#include "engine/ui/ui_serializer.h"
#include "engine/ui/ui_widget_registry.h"
#include "engine/render/font_registry.h"
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
#include "engine/ui/widgets/menu_tab_bar.h"
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
#include "engine/ui/widgets/divider.h"
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
#include "engine/ui/widgets/arena_panel.h"
#include "engine/ui/widgets/battlefield_panel.h"
#include "engine/ui/widgets/pet_panel.h"
#include "engine/ui/widgets/crafting_panel.h"
#include "engine/ui/widgets/player_context_menu.h"
#include "engine/ui/widgets/trade_window.h"
#include "engine/ui/widgets/collection_panel.h"
#include "engine/ui/widgets/costume_panel.h"
#include "engine/ui/widgets/settings_panel.h"
#include "engine/ui/widgets/fps_counter.h"
#include "engine/ui/widgets/leaderboard_panel.h"
#include "engine/ui/widgets/invite_prompt_panel.h"
#include "engine/ui/widgets/market_panel.h"
#include "engine/ui/widgets/emoticon_panel.h"
#include "engine/ui/widgets/quantity_selector.h"
#include "engine/ui/widgets/bag_view_panel.h"
#include "engine/ui/widgets/loading_panel.h"
#include "engine/ui/widgets/tooltip.h"
#include "engine/ui/widgets/chat_idle_overlay.h"
#include "engine/ui/widgets/bounty_panel.h"
#include "engine/ui/widgets/friends_panel.h"
#include "engine/ui/widgets/gauntlet_hud.h"
#include "engine/ui/widgets/gauntlet_result_modal.h"
#include <imgui.h>
#include <cstdio>
#include <algorithm>

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
    if (ImGui::IsItemDeactivatedAfterEdit() && !selectedScreenId_.empty()) {
        // For single-frame edits (Checkbox, etc.), IsItemActivated fires
        // AFTER the value has already changed, making pendingSnapshot_ hold
        // the post-edit state.  Fall back to the pre-frame snapshot which
        // was captured before any widgets ran.
        std::string& baseline = !pendingSnapshot_.empty() ? pendingSnapshot_ : preFrameSnapshot_;
        if (!baseline.empty()) {
            auto* root = uiMgr.getScreen(selectedScreenId_);
            if (root) {
                std::string newSnapshot = UISerializer::serializeScreen(selectedScreenId_, root);
                if (newSnapshot != baseline) {
                    auto cmd = std::make_unique<UIPropertyCommand>();
                    cmd->screenId = selectedScreenId_;
                    cmd->oldJson = baseline;
                    cmd->newJson = newSnapshot;
                    cmd->nodeId = selectedNodeId_;
                    cmd->desc = "UI Property";
                    cmd->uiMgr = &uiMgr;
                    UndoSystem::instance().push(std::move(cmd));
                }
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
    // Push lightweight move undo (patches offset in-place, no screen replacement)
    Vec2 newOffset = selectedNode_->anchor().offset;
    if (newOffset.x != dragStartOffset_.x || newOffset.y != dragStartOffset_.y) {
        auto cmd = std::make_unique<UIWidgetMoveCommand>();
        cmd->screenId = selectedScreenId_;
        cmd->nodeId = selectedNodeId_;
        cmd->oldOffset = dragStartOffset_;
        cmd->newOffset = newOffset;
        cmd->uiMgr = uiMgr;
        UndoSystem::instance().push(std::move(cmd));
    }
    isDraggingWidget_ = false;
}

// ============================================================================
// Public
// ============================================================================

void UIEditorPanel::draw(UIManager& uiMgr) {
    static bool validatedOnce = false;
    if (!validatedOnce) {
        validateWidgetCoverage();
        validatedOnce = true;
    }

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
    if (type == "divider")            return {{0.55f, 0.55f, 0.55f, 1.0f}, "DIV"};
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
    if (type == "chat_idle_overlay")  return {{0.45f, 0.55f, 0.50f, 1.0f}, "IDL"};
    if (type == "emoticon_panel")     return {{0.55f, 0.55f, 0.50f, 1.0f}, "EMO"};
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
    if (type == "arena_panel")        return {{0.70f, 0.40f, 0.40f, 1.0f}, "ARN"};
    if (type == "battlefield_panel")  return {{0.65f, 0.35f, 0.45f, 1.0f}, "BTL"};
    if (type == "crafting_panel")         return {{0.55f, 0.50f, 0.65f, 1.0f}, "CRF"};
    if (type == "player_context_menu")  return {{0.60f, 0.55f, 0.45f, 1.0f}, "CTX"};
    if (type == "confirm_dialog")       return {{0.70f, 0.50f, 0.50f, 1.0f}, "DLG"};
    if (type == "quantity_selector")  return {{0.70f, 0.55f, 0.50f, 1.0f}, "QTY"};
    if (type == "bag_view_panel")    return {{0.50f, 0.60f, 0.55f, 1.0f}, "BAG"};
    if (type == "notification_toast") return {{0.65f, 0.55f, 0.40f, 1.0f}, "TST"};
    if (type == "login_screen")       return {{0.50f, 0.60f, 0.70f, 1.0f}, "LOG"};
    if (type == "death_overlay")      return {{0.75f, 0.35f, 0.35f, 1.0f}, "DTH"};
    if (type == "left_sidebar")       return {{0.45f, 0.55f, 0.65f, 1.0f}, "BAR"};
    if (type == "menu_button_row")    return {{0.50f, 0.55f, 0.55f, 1.0f}, "MNU"};
    if (type == "character_select_screen")   return {{0.50f, 0.60f, 0.70f, 1.0f}, "SEL"};
    if (type == "character_creation_screen") return {{0.50f, 0.60f, 0.70f, 1.0f}, "CRT"};
    if (type == "costume_panel")       return {{0.55f, 0.35f, 0.60f, 1.0f}, "COS"};
    if (type == "settings_panel")      return {{0.55f, 0.45f, 0.25f, 1.0f}, "SET"};
    if (type == "invite_prompt")       return {{0.35f, 0.65f, 0.35f, 1.0f}, "INV"};
    if (type == "fps_counter")         return {{0.70f, 0.70f, 0.30f, 1.0f}, "FPS"};
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
        clearSelection();
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
#ifdef FATE_HAS_GAME
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
        // Divider presets — one-click insert of a pre-configured separator.
        // Each branch mutates a fresh Divider before handing it to the parent.
        if (ImGui::BeginMenu("Add Divider Preset")) {
            auto spawn = [&](const char* label, auto configure) {
                if (ImGui::MenuItem(label)) {
                    std::string childId = "new_divider_" + std::to_string(nextChildId_++);
                    auto d = std::make_unique<Divider>(childId);
                    configure(*d);
                    node->addChild(std::move(d));
                }
            };

            spawn("Thin Line", [](Divider& d) {
                d.thickness = 1.0f;
                d.color = Color{0.55f, 0.55f, 0.55f, 1.0f};
            });
            spawn("Double Line", [](Divider& d) {
                d.thickness    = 1.0f;
                d.doubleStroke = true;
                d.strokeGap    = 3.0f;
                d.color        = Color{0.60f, 0.55f, 0.45f, 1.0f};
            });
            spawn("Gradient Fade", [](Divider& d) {
                d.thickness = 2.0f;
                d.fadeEnds  = 0.30f;
                d.color     = Color{0.75f, 0.70f, 0.55f, 1.0f};
            });
            spawn("Scroll (Ornament)", [](Divider& d) {
                d.thickness    = 1.0f;
                d.doubleStroke = true;
                d.strokeGap    = 4.0f;
                d.fadeEnds     = 0.20f;
                d.ornamentSize = 24.0f;
                // ornamentImage left blank — user assigns their own sprite.
                d.color        = Color{0.70f, 0.60f, 0.40f, 1.0f};
            });
            spawn("Zebra Band", [](Divider& d) {
                d.thickness = 20.0f;
                d.color     = Color{0.60f, 0.45f, 0.40f, 0.35f};
            });
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Add Divider Child")) {
            std::string childId = "new_divider_" + std::to_string(nextChildId_++);
            node->addChild(std::make_unique<Divider>(childId));
        }
#endif // FATE_HAS_GAME
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

    // Capture a pre-frame snapshot before any widgets run.
    // This is the undo baseline for single-frame edits (Checkbox, etc.)
    // where IsItemActivated fires after the value has already changed.
    if (!selectedScreenId_.empty() && pendingSnapshot_.empty()) {
        auto* root = uiMgr.getScreen(selectedScreenId_);
        if (root) {
            preFrameSnapshot_ = UISerializer::serializeScreen(selectedScreenId_, root);
        }
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

    // Reflected properties (new system) — auto-generates inspector from metadata
    auto reflectedFields = selectedNode_->reflectedProperties();
    if (!reflectedFields.empty()) {
        drawPropertyInspector(selectedNode_, reflectedFields,
                              [&]() { checkUndoCapture(uiMgr); });
    }
#ifdef FATE_HAS_GAME
    else
    // Legacy widget-specific properties (dynamic_cast chain)
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
        ImGui::Checkbox("Masked (Password)", &textInput->masked); checkUndoCapture(uiMgr);
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
        char iconBuf[256] = {};
        snprintf(iconBuf, sizeof(iconBuf), "%s", slot->icon.c_str());
        if (ImGui::InputText("Icon", iconBuf, sizeof(iconBuf))) {
            slot->icon = iconBuf;
        }
        checkUndoCapture(uiMgr);
        char dragBuf[128] = {};
        snprintf(dragBuf, sizeof(dragBuf), "%s", slot->acceptsDragType.c_str());
        if (ImGui::InputText("Accepts Drag", dragBuf, sizeof(dragBuf))) {
            slot->acceptsDragType = dragBuf;
        }
        checkUndoCapture(uiMgr);
    }
    else if (auto* grid = dynamic_cast<SlotGrid*>(selectedNode_)) {
        ImGui::DragInt("Columns", &grid->columns, 1.0f, 1, 20); checkUndoCapture(uiMgr);
        ImGui::DragInt("Rows", &grid->rows, 1.0f, 1, 20); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Slot Size", &grid->slotSize, 1.0f, 16.0f, 128.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Slot Padding", &grid->slotPadding, 0.5f, 0.0f, 16.0f); checkUndoCapture(uiMgr);
        char dragBuf[128] = {};
        snprintf(dragBuf, sizeof(dragBuf), "%s", grid->acceptsDragType.c_str());
        if (ImGui::InputText("Accepts Drag##grid", dragBuf, sizeof(dragBuf))) {
            grid->acceptsDragType = dragBuf;
        }
        checkUndoCapture(uiMgr);
    }
    else if (auto* scroll = dynamic_cast<ScrollView*>(selectedNode_)) {
        ImGui::DragFloat("Scroll Offset", &scroll->scrollOffset, 1.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Content Height", &scroll->contentHeight, 1.0f, 0.0f, 10000.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Scroll Speed", &scroll->scrollSpeed, 1.0f, 1.0f, 200.0f); checkUndoCapture(uiMgr);
    }
    else if (auto* pib = dynamic_cast<PlayerInfoBlock*>(selectedNode_)) {
        ImGui::SeparatorText("PlayerInfoBlock");
        if (ImGui::TreeNodeEx("Position Offsets##pib", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Portrait##pibo", &pib->portraitOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Bars##pibo", &pib->barOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Level##pibo", &pib->levelOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Gold##pibo", &pib->goldOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Font Sizes##pib", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Overlay##pibf", &pib->overlayFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Gold##pibf", &pib->goldFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##pib", 0)) {
            ImGui::ColorEdit4("Portrait Fill##pibc", &pib->portraitFillColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Portrait Border##pibc", &pib->portraitBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bar BG##pibc", &pib->barBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bar Border##pibc", &pib->barBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("HP Fill##pibc", &pib->hpFillColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shield T1 (0-100%)##pibc", &pib->shieldTier1Color.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shield T2 (100-200%)##pibc", &pib->shieldTier2Color.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shield T3 (200-300%)##pibc", &pib->shieldTier3Color.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("MP Fill##pibc", &pib->mpFillColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text Shadow##pibc", &pib->textShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gold Text##pibc", &pib->goldTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
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

        // --- Scroll Gesture ---
        ImGui::Separator();
        ImGui::Text("Scroll Gesture");
        ImGui::Checkbox("Scroll Enabled##arc", &arc->scrollEnabled); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Tap Deadzone (deg)##arc",       &arc->tapAngleDeadzoneDeg,           0.1f, 0.0f, 30.0f);   checkUndoCapture(uiMgr);
        ImGui::DragFloat("Page Commit Angle (deg)##arc",  &arc->pageCommitAngleDeg,            0.5f, 5.0f, 60.0f);   checkUndoCapture(uiMgr);
        ImGui::DragFloat("Flick Velocity (deg/s)##arc",   &arc->flickAngularVelocityDegPerSec, 1.0f, 10.0f, 500.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Snap Duration (ms)##arc",       &arc->snapAnimationMs,               1.0f, 0.0f, 1000.0f); checkUndoCapture(uiMgr);

        // --- Font ---
        ImGui::Separator();
        ImGui::Text("Font");
        {
            auto names = FontRegistry::instance().fontNames();
            int current = 0;
            for (int i = 0; i < static_cast<int>(names.size()); ++i) {
                if (names[i] == arc->fontName) { current = i + 1; break; }
            }
            auto getter = [](void* data, int idx, const char** out) -> bool {
                auto* v = static_cast<std::vector<std::string>*>(data);
                if (idx == 0) { *out = "(default)"; return true; }
                if (idx - 1 < static_cast<int>(v->size())) { *out = (*v)[idx - 1].c_str(); return true; }
                return false;
            };
            if (ImGui::Combo("Font Name##arc", &current, getter, &names, static_cast<int>(names.size()) + 1)) {
                arc->fontName = (current == 0) ? "" : names[current - 1];
            }
        }
        checkUndoCapture(uiMgr);

        // --- Font Sizes ---
        ImGui::Separator();
        ImGui::Text("Font Sizes");
        ImGui::DragFloat("LV Font Size", &arc->lvFontSize, 0.5f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Cooldown Font Size", &arc->cooldownFontSize, 0.5f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Action Font Size", &arc->actionFontSize, 0.5f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("PickUp Font Size", &arc->pickUpFontSize, 0.5f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Page Font Size", &arc->pageFontSize, 0.5f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Page Dot Radius", &arc->pageDotRadius, 0.5f, 4.0f, 40.0f); checkUndoCapture(uiMgr);

        // --- Text Colors ---
        ImGui::SeparatorText("Text Colors");
        ImGui::ColorEdit4("LV Text##arc", &arc->lvTextColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Cooldown Text##arc", &arc->cooldownTextColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Action Text##arc", &arc->actionTextColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("PickUp Text##arc", &arc->pickUpTextColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Page Active Text##arc", &arc->pageActiveTextColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Page Inactive Text##arc", &arc->pageInactiveTextColor.r); checkUndoCapture(uiMgr);

        // --- Button Background Colors ---
        ImGui::SeparatorText("Button Background Colors");
        ImGui::ColorEdit4("Attack BG##arc", &arc->attackBgColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("PickUp BG##arc", &arc->pickUpBgColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Page Active BG##arc", &arc->pageActiveBgColor.r); checkUndoCapture(uiMgr);
        ImGui::SliderFloat("Metallic Opacity##arc", &arc->metallicOpacity, 0.0f, 1.0f, "%.2f"); checkUndoCapture(uiMgr);

        // --- Text Position Offsets ---
        ImGui::SeparatorText("Text Offsets");
        ImGui::DragFloat2("Action Text Offset##arc", &arc->actionTextOffset.x, 0.5f, -50.0f, 50.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat2("PickUp Text Offset##arc", &arc->pickUpTextOffset.x, 0.5f, -50.0f, 50.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat2("LV Text Offset##arc", &arc->lvTextOffset.x, 0.5f, -50.0f, 50.0f); checkUndoCapture(uiMgr);

        // --- Slot/Overlay Colors ---
        ImGui::SeparatorText("Slot Colors");
        ImGui::ColorEdit4("Filled Slot BG##arc", &arc->filledSlotBgColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Filled Slot Border##arc", &arc->filledSlotBorderColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Empty Slot BG##arc", &arc->emptySlotBgColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Empty Slot Border##arc", &arc->emptySlotBorderColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Empty Slot Plus##arc", &arc->emptySlotPlusColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Cooldown Overlay##arc", &arc->cooldownOverlayColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Page Inactive BG##arc", &arc->pageInactiveBgColor.r); checkUndoCapture(uiMgr);

        ImGui::SeparatorText("SkillArc — Chrome");
        ImGui::Checkbox("hudUseChrome", &arc->hudUseChrome_); checkUndoCapture(uiMgr);
        if (ImGui::TreeNodeEx("Chrome — Buttons##sa", 0)) {
            ImGui::ColorEdit4("chromeAttackBgColor",          &arc->chromeAttackBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeAttackGradientTopColor", &arc->chromeAttackGradientTopColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeAttackBorderColor",      &arc->chromeAttackBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromePickUpBgColor",          &arc->chromePickUpBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromePickUpGradientTopColor", &arc->chromePickUpGradientTopColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromePickUpBorderColor",      &arc->chromePickUpBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome — Slots##sa", 0)) {
            ImGui::ColorEdit4("chromeFilledSlotBgColor",          &arc->chromeFilledSlotBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeFilledSlotBorderColor",      &arc->chromeFilledSlotBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeFilledSlotGradientTopColor", &arc->chromeFilledSlotGradientTopColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeEmptySlotBgColor",           &arc->chromeEmptySlotBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeEmptySlotBorderColor",       &arc->chromeEmptySlotBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome — Pages##sa", 0)) {
            ImGui::ColorEdit4("chromePageActiveBgColor",   &arc->chromePageActiveBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromePageInactiveBgColor", &arc->chromePageInactiveBgColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
    }
    else if (auto* dp = dynamic_cast<DPad*>(selectedNode_)) {
        ImGui::SeparatorText("DPad — Layout");
        ImGui::DragFloat("DPad Size", &dp->dpadSize, 1.0f, 60.0f, 400.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Dead Zone Radius", &dp->deadZoneRadius, 0.5f, 0.0f, 50.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Opacity##dpad", &dp->opacity, 0.01f, 0.0f, 1.0f); checkUndoCapture(uiMgr);
        ImGui::Checkbox("Show Bounds##dpad", &dp->showBounds); checkUndoCapture(uiMgr);

        ImGui::SeparatorText("DPad — Colors");
        ImGui::ColorEdit4("Background##dpad", &dp->bgColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Border Color##dpad", &dp->borderColor.r); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Border Width##dpad", &dp->borderWidth, 0.5f, 0.0f, 10.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Arm Color##dpad", &dp->armColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Active Color##dpad", &dp->activeColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Center Color##dpad", &dp->centerColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Arrow Color##dpad", &dp->arrowColor.r); checkUndoCapture(uiMgr);

        ImGui::SeparatorText("DPad — Sizing Ratios");
        ImGui::DragFloat("Arm Width", &dp->armWidthRatio, 0.01f, 0.05f, 1.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Arm Height", &dp->armHeightRatio, 0.01f, 0.1f, 1.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Center Size", &dp->centerSizeRatio, 0.01f, 0.05f, 0.5f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Arrow Dist", &dp->arrowDistRatio, 0.01f, 0.1f, 0.5f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Arrow Width", &dp->arrowWidthRatio, 0.005f, 0.02f, 0.3f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Arrow Height", &dp->arrowHeightRatio, 0.005f, 0.02f, 0.3f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Active Length", &dp->activeLenRatio, 0.01f, 0.1f, 1.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Active Offset", &dp->activeOffsetRatio, 0.01f, 0.0f, 0.5f); checkUndoCapture(uiMgr);

        ImGui::SeparatorText("DPad — Chrome");
        ImGui::Checkbox("hudUseChrome", &dp->hudUseChrome_); checkUndoCapture(uiMgr);
        if (ImGui::TreeNodeEx("Chrome##dp", 0)) {
            ImGui::DragFloat("chromeDiscCornerRadius",   &dp->chromeDiscCornerRadius,   0.5f, 0.0f, 256.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("chromeDiscShadowOffset",  &dp->chromeDiscShadowOffset.x, 0.5f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("chromeDiscShadowBlur",     &dp->chromeDiscShadowBlur,     0.5f, 0.0f, 64.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeDiscBgColor",       &dp->chromeDiscBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeDiscBorderColor",   &dp->chromeDiscBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeDiscShadowColor",   &dp->chromeDiscShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeArmColor",          &dp->chromeArmColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeActiveColor",       &dp->chromeActiveColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeArrowColor",        &dp->chromeArrowColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
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
    else if (auto* mtb = dynamic_cast<MenuTabBar*>(selectedNode_)) {
        ImGui::SeparatorText("MenuTabBar — Layout");
        ImGui::DragInt("Active Tab", &mtb->activeTab, 1.0f, 0, static_cast<int>(mtb->tabLabels.size()) - 1); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Tab Size", &mtb->tabSize, 1.0f, 20.0f, 120.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Arrow Size", &mtb->arrowSize, 1.0f, 16.0f, 60.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Border Width", &mtb->borderWidth, 0.5f, 0.0f, 10.0f); checkUndoCapture(uiMgr);

        ImGui::SeparatorText("Font Sizes");
        ImGui::DragFloat("Tab Font", &mtb->tabFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Arrow Font", &mtb->arrowFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);

        ImGui::SeparatorText("Colors");
        ImGui::ColorEdit4("Active Tab Bg", &mtb->activeTabBg.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Inactive Tab Bg", &mtb->inactiveTabBg.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Arrow Bg", &mtb->arrowBg.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Border Color", &mtb->borderColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Active Text", &mtb->activeTextColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Inactive Text", &mtb->inactiveTextColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Arrow Text", &mtb->arrowTextColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Highlight", &mtb->highlightColor.r); checkUndoCapture(uiMgr);

        ImGui::SeparatorText("Tab Labels");
        for (size_t i = 0; i < mtb->tabLabels.size(); i++) {
            ImGui::Text("  Tab %zu: %s", i, mtb->tabLabels[i].c_str());
        }

        ImGui::SeparatorText("MenuTabBar — Chrome");
        ImGui::Checkbox("panelUseChrome", &mtb->panelUseChrome_); checkUndoCapture(uiMgr);
        if (ImGui::TreeNodeEx("Chrome##mtb", 0)) {
            ImGui::DragFloat("chromeStripCornerRadius",   &mtb->chromeStripCornerRadius,   0.5f, 0.0f, 64.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("chromeStripShadowBlur",     &mtb->chromeStripShadowBlur,     0.5f, 0.0f, 64.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("chromeStripShadowOffset",  &mtb->chromeStripShadowOffset.x, 0.5f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeStripBgColor",          &mtb->chromeStripBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeStripBorderColor",      &mtb->chromeStripBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeStripShadowColor",      &mtb->chromeStripShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeActiveTabBgColor",      &mtb->chromeActiveTabBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeActiveTabBorderColor",  &mtb->chromeActiveTabBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeInactiveTabBgColor",    &mtb->chromeInactiveTabBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeArrowBgColor",          &mtb->chromeArrowBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeArrowTextColor",        &mtb->chromeArrowTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
    }
    else if (auto* ticker = dynamic_cast<ChatTicker*>(selectedNode_)) {
        ImGui::SeparatorText("ChatTicker");
        ImGui::DragFloat("Scroll Speed##ticker", &ticker->scrollSpeed, 1.0f, 0.0f, 200.0f); checkUndoCapture(uiMgr);

        ImGui::Separator();
        if (ImGui::TreeNode("Chrome ChatTicker##chatticker_cp")) {
            ImGui::Checkbox  ("Use Chrome##chatticker_cp",                 &ticker->hudUseChrome_);                     checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Border Width##chatticker_cp",               &ticker->chromePanelBorderWidth, 0.1f);     checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Corner Radius##chatticker_cp",              &ticker->chromePanelCornerRadius, 0.5f);    checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##chatticker_cp",              &ticker->chromePanelShadowOffset.x, 0.1f);  checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Shadow Blur##chatticker_cp",                &ticker->chromePanelShadowBlur, 0.5f);      checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bg Color##chatticker_cp",                   &ticker->chromePanelBgColor.r);             checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##chatticker_cp",               &ticker->chromePanelGradientTop.r);         checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##chatticker_cp",            &ticker->chromePanelGradientBottom.r);      checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##chatticker_cp",               &ticker->chromePanelBorderColor.r);         checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow Color##chatticker_cp",               &ticker->chromePanelShadowColor.r);         checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
    }
    else if (auto* idle = dynamic_cast<ChatIdleOverlay*>(selectedNode_)) {
        ImGui::SeparatorText("ChatIdleOverlay");
        if (ImGui::TreeNodeEx("Layout##idle", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragInt("Max Lines", &idle->maxLines, 1.0f, 0, 10); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Font Size##idle", &idle->fontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Line Spacing##idle", &idle->lineSpacing, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Offset##idle", &idle->shadowOffset, 0.5f, 0.0f, 10.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Padding##idle", &idle->padding, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Background##idle", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("BG Width##idle", &idle->bgWidth, 1.0f, 0.0f, 2000.0f, "%.0f (0=widget)"); checkUndoCapture(uiMgr);
            ImGui::DragFloat("BG Height##idle", &idle->bgHeight, 1.0f, 0.0f, 500.0f, "%.0f (0=auto)"); checkUndoCapture(uiMgr);
            ImGui::DragFloat("BG Corner Radius##idle", &idle->bgCornerRadius, 0.5f, 0.0f, 50.0f, "%.1f"); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("BG Offset##idle", &idle->bgOffset.x, 0.5f, -500.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("BG Color##idle", &idle->bgColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Text Colors##idle", 0)) {
            ImGui::ColorEdit4("Text##idle", &idle->textColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##idle", &idle->shadowColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Separator();
        if (ImGui::TreeNode("Chrome ChatIdleOverlay##chatidle_cp")) {
            ImGui::Checkbox  ("Use Chrome##chatidle_cp",                  &idle->hudUseChrome_);                     checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Border Width##chatidle_cp",                &idle->chromePanelBorderWidth, 0.1f);     checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Corner Radius##chatidle_cp",               &idle->chromePanelCornerRadius, 0.5f);    checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##chatidle_cp",               &idle->chromePanelShadowOffset.x, 0.1f);  checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Shadow Blur##chatidle_cp",                 &idle->chromePanelShadowBlur, 0.5f);      checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bg Color##chatidle_cp",                    &idle->chromePanelBgColor.r);             checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##chatidle_cp",                &idle->chromePanelGradientTop.r);         checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##chatidle_cp",             &idle->chromePanelGradientBottom.r);      checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##chatidle_cp",                &idle->chromePanelBorderColor.r);         checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow Color##chatidle_cp",                &idle->chromePanelShadowColor.r);         checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
    }
    else if (auto* expBar = dynamic_cast<EXPBar*>(selectedNode_)) {
        ImGui::SeparatorText("EXPBar");
        if (ImGui::TreeNodeEx("Position Offsets##eb", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Text##ebo", &expBar->textOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Font Sizes##eb", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Text##ebf", &expBar->fontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##eb", 0)) {
            ImGui::ColorEdit4("Fill##ebc", &expBar->fillColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##ebc", &expBar->shadowColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Chrome ExpBar##expbar_cp")) {
            ImGui::Checkbox  ("Use Chrome##expbar_cp",        &expBar->hudUseChrome_);                checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bg Color##expbar_cp",          &expBar->chromeBgColor.r);              checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##expbar_cp",      &expBar->chromeBorderColor.r);          checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Border Width##expbar_cp",      &expBar->chromeBorderWidth, 0.1f);      checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Corner Radius##expbar_cp",     &expBar->chromeCornerRadius, 0.5f);     checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##expbar_cp",     &expBar->chromeShadowOffset.x, 0.1f);   checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Shadow Blur##expbar_cp",       &expBar->chromeShadowBlur, 0.5f);       checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow Color##expbar_cp",      &expBar->chromeShadowColor.r);          checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Text("XP: %.0f / %.0f", expBar->xp, expBar->xpToLevel);
        if (expBar->xpToLevel > 0.0f) {
            float pct = (expBar->xp / expBar->xpToLevel) * 100.0f;
            ImGui::Text("Progress: %.1f%%", pct);
        }
    }
    else if (auto* tf = dynamic_cast<TargetFrame*>(selectedNode_)) {
        ImGui::SeparatorText("TargetFrame");
        if (ImGui::TreeNodeEx("Panel##tf", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit4("BG Color##tfp", &tf->panelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##tfp", &tf->panelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##tfp", &tf->panelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##tfp", &tf->panelCornerRadius, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Name##tf", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Offset##tfn", &tf->nameOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Font Size##tfn", &tf->nameFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text Color##tfn", &tf->nameTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("BG Color##tfn", &tf->nameBgColor.r); checkUndoCapture(uiMgr);
            ImGui::DragFloat("BG Padding##tfn", &tf->nameBgPadding, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Top Pad##tfn", &tf->nameTopPad, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Level##tf", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Offset##tflv", &tf->levelOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Font Size##tflv", &tf->levelFontSize, 0.5f, 0.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text Color##tflv", &tf->levelTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("HP Bar##tf", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Offset##tfhb", &tf->hpBarOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Padding##tfhb", &tf->barPadding, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Height##tfhb", &tf->barHeight, 0.5f, 2.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##tfhb", &tf->barCornerRadius, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Track BG##tfhb", &tf->hpBarBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Fill##tfhb", &tf->hpFillColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##tfhb", &tf->barBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##tfhb", &tf->barBorderWidth, 0.25f, 0.0f, 4.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("HP Text##tf", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Offset##tfht", &tf->hpTextOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Font Size##tfht", &tf->hpFontSize, 0.5f, 0.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text Color##tfht", &tf->hpTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::Text("Target: %s  Lv.%d", tf->targetName.c_str(), tf->targetLevel);
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
        ImGui::Separator();
        if (ImGui::TreeNode("Chrome LeftSidebar##sidebar_cp")) {
            ImGui::Checkbox  ("Use Chrome##sidebar_cp",                  &lsb->hudUseChrome_);                     checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Border Width##sidebar_cp",                &lsb->chromePanelBorderWidth, 0.1f);     checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Corner Radius##sidebar_cp",               &lsb->chromePanelCornerRadius, 0.5f);    checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##sidebar_cp",               &lsb->chromePanelShadowOffset.x, 0.1f);  checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Shadow Blur##sidebar_cp",                 &lsb->chromePanelShadowBlur, 0.5f);      checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bg Color##sidebar_cp",                    &lsb->chromePanelBgColor.r);             checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##sidebar_cp",                &lsb->chromePanelGradientTop.r);         checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##sidebar_cp",             &lsb->chromePanelGradientBottom.r);      checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##sidebar_cp",                &lsb->chromePanelBorderColor.r);         checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow Color##sidebar_cp",                &lsb->chromePanelShadowColor.r);         checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Color##sidebar_cp",                 &lsb->chromeTitleColor.r);               checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
    }
    else if (auto* inv = dynamic_cast<InventoryPanel*>(selectedNode_)) {
        ImGui::SeparatorText("InventoryPanel");

        if (ImGui::TreeNodeEx("Layout", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Doll Width Ratio", &inv->dollWidthRatio, 0.01f, 0.1f, 0.9f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Content Padding", &inv->contentPadding, 0.5f, 0.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Currency Height", &inv->currencyHeight, 0.5f, 10.0f, 80.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Gold Offset X", &inv->goldOffsetX, 0.5f, -200.0f, 400.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Gold Offset Y", &inv->goldOffsetY, 0.5f, -50.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Opal Offset X", &inv->opalOffsetX, 0.5f, -200.0f, 400.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Opal Offset Y", &inv->opalOffsetY, 0.5f, -50.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Grid Padding", &inv->gridPadding, 0.5f, 0.0f, 16.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Doll Area Offset", &inv->dollAreaOffset.x, 0.5f, -500.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Grid Area Offset", &inv->gridAreaOffset.x, 0.5f, -500.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Currency Offset", &inv->currencyOffset.x, 0.5f, -500.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Paper Doll", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Center Y", &inv->dollCenterY, 0.01f, 0.1f, 0.9f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Character Scale", &inv->characterScale, 0.1f, 1.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Grid", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragInt("Columns", &inv->gridColumns, 1.0f, 1, 10); checkUndoCapture(uiMgr);
            ImGui::DragInt("Rows", &inv->gridRows, 1.0f, 1, 10); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Size##inv", &inv->slotSize, 1.0f, 16.0f, 128.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Slot Appearance##inv", 0)) {
            ImGui::ColorEdit4("Filled Bg##slot", &inv->slotFilledBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Empty Bg##slot", &inv->slotEmptyBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Empty Border##slot", &inv->slotEmptyBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##slot", &inv->slotBorderWidth, 0.25f, 0.0f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Paper Doll Inset##inv", 0)) {
            ImGui::ColorEdit4("Inset Bg##doll", &inv->dollInsetBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Inset Border##doll", &inv->dollInsetBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Inset Border Width##doll", &inv->dollInsetBorderW, 0.25f, 0.0f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Label Gap##doll", &inv->equipLabelGap, 0.25f, 0.0f, 10.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Equipment Slots", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Equip Slot Size", &inv->equipSlotSize, 0.5f, 16.0f, 128.0f); checkUndoCapture(uiMgr);
            for (int i = 0; i < InventoryPanel::NUM_EQUIP_SLOTS; ++i) {
                // Guard: if anything earlier in this frame triggered an undo/redo
                // screen reload (destroying the widget tree), `inv` and
                // `selectedNode_` now point to freed memory.  Stop reading
                // equipSlots[i] before we touch heap-freed std::string.
                if (selectedNode_ != inv) break;
                // Show equipped item name in the tree label
                char label[128];
                if (!inv->equipSlots[i].displayName.empty()) {
                    snprintf(label, sizeof(label), "%s: %s##eq%d",
                             InventoryPanel::equipSlotNames[i],
                             inv->equipSlots[i].displayName.c_str(), i);
                } else {
                    snprintf(label, sizeof(label), "%s: (empty)##eq%d",
                             InventoryPanel::equipSlotNames[i], i);
                }
                if (ImGui::TreeNode(label)) {
                    ImGui::DragFloat("Offset X", &inv->equipLayout[i].offsetX, 0.05f, -5.0f, 5.0f); checkUndoCapture(uiMgr);
                    if (selectedNode_ != inv) { ImGui::TreePop(); break; }
                    ImGui::DragFloat("Offset Y", &inv->equipLayout[i].offsetY, 0.05f, -5.0f, 5.0f); checkUndoCapture(uiMgr);
                    if (selectedNode_ != inv) { ImGui::TreePop(); break; }
                    ImGui::DragFloat("Size Mul", &inv->equipLayout[i].sizeMul, 0.05f, 0.5f, 3.0f); checkUndoCapture(uiMgr);
                    if (selectedNode_ != inv) { ImGui::TreePop(); break; }
                    if (!inv->equipSlots[i].itemId.empty()) {
                        ImGui::Separator();
                        ImGui::Text("Item: %s", inv->equipSlots[i].displayName.c_str());
                        ImGui::Text("Rarity: %s", inv->equipSlots[i].rarity.c_str());
                        ImGui::Text("Type: %s", inv->equipSlots[i].itemType.c_str());
                        if (inv->equipSlots[i].enchantLevel > 0)
                            ImGui::Text("Enchant: +%d", inv->equipSlots[i].enchantLevel);
                        for (auto& line : inv->equipSlots[i].statLines)
                            ImGui::Text("  %s", line.c_str());
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Font Sizes", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Item Letter", &inv->itemFontSize, 0.5f, 4.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Quantity Badge", &inv->quantityFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Currency Value", &inv->currencyFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Currency Label", &inv->currencyLabelFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Equip Label", &inv->equipLabelFontSize, 0.5f, 4.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Colors##inv", 0)) {
            ImGui::ColorEdit4("Quantity Badge##c", &inv->quantityColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Item Text##c", &inv->itemTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Equip Label##c", &inv->equipLabelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gold Label##c", &inv->goldLabelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gold Value##c", &inv->goldValueColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Opal Label##c", &inv->opalLabelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Opal Value##c", &inv->opalValueColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Close Button##inv", 0)) {
            ImGui::DragFloat("Radius##cb", &inv->closeBtnRadius, 0.5f, 4.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Offset##cb", &inv->closeBtnOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##cb", &inv->closeBtnBorderW, 0.25f, 0.0f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Font Size##cb", &inv->closeBtnFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bg Color##cb", &inv->closeBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##cb", &inv->closeBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text Color##cb", &inv->closeBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Context Menu##inv", 0)) {
            ImGui::DragFloat("Width##ctx", &inv->ctxMenuWidth, 1.0f, 60.0f, 400.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Item Height##ctx", &inv->ctxMenuItemHeight, 0.5f, 12.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Padding##ctx", &inv->ctxMenuPadding, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##ctx", &inv->ctxMenuBorderW, 0.25f, 0.0f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Font Size##ctx", &inv->ctxMenuFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Text Pad X##ctx", &inv->ctxMenuTextPadX, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bg Color##ctx", &inv->ctxMenuBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##ctx", &inv->ctxMenuBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text Color##ctx", &inv->ctxMenuTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Disabled Text##ctx", &inv->ctxMenuDisabledColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Panel Colors##inv", 0)) {
            ImGui::ColorEdit4("Background##invpan", &inv->panelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##invpan", &inv->panelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##invpan", &inv->panelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel (Checkpoint 3)##invpc", &inv->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Chrome path reads Chrome Panel groups below;");
        ImGui::TextDisabled("legacy fields above are ignored when chrome is on.");
        ImGui::Separator();

        if (ImGui::TreeNodeEx("Chrome Panel Layout##inv", 0)) {
            ImGui::DragFloat("Border Width##invcpl", &inv->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##invcpl", &inv->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##invcpl", &inv->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##invcpl", &inv->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Panel Colors##inv", 0)) {
            ImGui::ColorEdit4("Background##invcpc", &inv->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##invcpc", &inv->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##invcpc", &inv->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##invcpc", &inv->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##invcpc", &inv->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Close Button##inv", 0)) {
            ImGui::ColorEdit4("Bg##invccb", &inv->chromeCloseBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##invccb", &inv->chromeCloseBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##invccb", &inv->chromeCloseBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Visibility##inv", 0)) {
            ImGui::Checkbox("Equip Area Visible", &inv->equipAreaVisible); checkUndoCapture(uiMgr);
            ImGui::Checkbox("Grid Area Visible", &inv->gridAreaVisible); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Skill Loadout Strip##inv", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Strip Visible##inv", &inv->stripVisible); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Strip Height##inv", &inv->stripHeight, 1.0f, 30.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Strip Padding##inv", &inv->stripPadding, 1.0f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Strip BG##inv", &inv->stripBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Strip Border##inv", &inv->stripBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Strip Chrome##inv", 0)) {
            auto& s = inv->strip_;
            ImGui::DragFloat2("Origin##strip", &s.origin.x, 1.0f, -500.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Size##strip", &s.slotSize, 1.0f, 16.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Gap##strip", &s.slotGap, 1.0f, 0.0f, 50.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Page Sel Offset##strip", &s.pageSelectorOffset.x, 1.0f, -300.0f, 300.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Page Dot Radius##strip", &s.pageDotRadius, 0.5f, 1.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Page Dot Gap##strip", &s.pageDotGap, 0.5f, 0.0f, 50.0f); checkUndoCapture(uiMgr);
            int align = static_cast<int>(s.pageSelectorAlignment);
            if (ImGui::Combo("Page Sel Alignment##strip", &align, "Left\0Right\0\0")) {
                s.pageSelectorAlignment = static_cast<SkillLoadoutStrip::PageAlign>(align);
                checkUndoCapture(uiMgr);
            }
            ImGui::Checkbox("Use Chrome##strip", &s.useChrome); checkUndoCapture(uiMgr);
            ImGui::Checkbox("Slot Is Circle##strip", &s.slotIsCircle); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Slot BG##strip",            &s.slotBgColor.r);           checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Slot BG Active##strip",     &s.slotBgColorActive.r);     checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Slot Border##strip",        &s.slotBorderColor.r);       checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Slot Border Active##strip", &s.slotBorderColorActive.r); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Border Thick##strip",   &s.slotBorderThickness, 0.1f, 0.0f, 10.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##strip",       &s.cornerRadius,        0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Page Dot##strip",           &s.pageDotColor.r);          checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Page Dot Active##strip",    &s.pageDotColorActive.r);    checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Stack Count##strip",        &s.stackCountColor.r);       checkUndoCapture(uiMgr);
            ImGui::DragFloat("Stack Count Font##strip",    &s.stackCountFontSize, 0.5f, 6.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Drop Highlight##strip",     &s.dropHighlightColor.r);    checkUndoCapture(uiMgr);
            ImGui::DragFloat("Drop High Thick##strip",     &s.dropHighlightThickness, 0.1f, 0.0f, 10.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Cooldown Overlay##strip",   &s.cooldownOverlayColor.r);  checkUndoCapture(uiMgr);
            ImGui::DragFloat("Cooldown Alpha##strip",      &s.cooldownOverlayAlpha, 0.05f, 0.0f, 1.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Empty Slot Icon##strip",    &s.emptySlotIconColor.r);    checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Checkbox("Use Chrome (Checkpoint 2)##invtt", &inv->tooltipUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Chrome path reads Chrome Tooltip groups below;");
        ImGui::TextDisabled("legacy fields above are ignored when chrome is on.");
        ImGui::Separator();

        if (ImGui::TreeNodeEx("Tooltip Layout##inv", 0)) {
            ImGui::Checkbox("Auto Width##tt", &inv->tooltipAutoWidth); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Min Width##tt", &inv->tooltipWidth, 1.0f, 40.0f, 500.0f); checkUndoCapture(uiMgr);
            if (inv->tooltipAutoWidth) {
                ImGui::DragFloat("Max Width##tt", &inv->tooltipMaxWidth, 1.0f, 100.0f, 800.0f); checkUndoCapture(uiMgr);
            }
            ImGui::DragFloat("Padding##tt", &inv->tooltipPadding, 0.5f, 0.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Offset##tt", &inv->tooltipOffset, 0.5f, 0.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Offset##tt", &inv->tooltipShadowOffset, 0.5f, 0.0f, 16.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Line Spacing##tt", &inv->tooltipLineSpacing, 0.5f, 0.0f, 16.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##tt", &inv->tooltipBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Separator Height##tt", &inv->tooltipSepHeight, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Tooltip Fonts##inv", 0)) {
            ImGui::DragFloat("Name Font##tt", &inv->tooltipNameFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Stat Font##tt", &inv->tooltipStatFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Level Font##tt", &inv->tooltipLevelFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Tooltip Colors##inv", 0)) {
            ImGui::ColorEdit4("Background##ttc", &inv->tooltipBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##ttc", &inv->tooltipBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##ttc", &inv->tooltipShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Stat Text##ttc", &inv->tooltipStatColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Separator##ttc", &inv->tooltipSepColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Level Req (Unmet)##ttc", &inv->tooltipLevelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Level Req (Met)##ttc", &inv->tooltipLevelMetColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Rarity Colors##inv", 0)) {
            ImGui::ColorEdit4("Common##rar", &inv->rarityCommonColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Uncommon##rar", &inv->rarityUncommonColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Rare##rar", &inv->rarityRareColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Epic##rar", &inv->rarityEpicColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Legendary##rar", &inv->rarityLegendaryColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        // ----- Chrome (Checkpoint 2) tooltip fields — parallel to legacy -----
        if (ImGui::TreeNodeEx("Chrome Tooltip Layout##inv", 0)) {
            ImGui::DragFloat("Padding##cttl", &inv->chromeTooltipPadding, 0.5f, 0.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Offset##cttl", &inv->chromeTooltipOffset, 0.5f, 0.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Line Spacing##cttl", &inv->chromeTooltipLineSpacing, 0.5f, 0.0f, 16.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##cttl", &inv->chromeTooltipBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Separator Height##cttl", &inv->chromeTooltipSepHeight, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Separator Offset##cttl", &inv->chromeTooltipSepOffset.x, 0.5f, -100.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##cttl", &inv->chromeTooltipCornerRadius, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##cttl", &inv->chromeTooltipShadowOffset.x, 0.5f, -20.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##cttl", &inv->chromeTooltipShadowBlur, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Tooltip Fonts##inv", 0)) {
            ImGui::DragFloat("Name Font##cttf", &inv->chromeTooltipNameFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Stat Font##cttf", &inv->chromeTooltipStatFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Level Font##cttf", &inv->chromeTooltipLevelFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Tooltip Colors##inv", 0)) {
            ImGui::ColorEdit4("Background##cttc", &inv->chromeTooltipBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##cttc", &inv->chromeTooltipGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##cttc", &inv->chromeTooltipGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##cttc", &inv->chromeTooltipBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##cttc", &inv->chromeTooltipShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Stat Text##cttc", &inv->chromeTooltipStatColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Separator##cttc", &inv->chromeTooltipSepColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Level Req (Unmet)##cttc", &inv->chromeTooltipLevelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Level Req (Met)##cttc", &inv->chromeTooltipLevelMetColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Rarity Colors##inv", 0)) {
            ImGui::ColorEdit4("Common##crar", &inv->chromeRarityCommonColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Uncommon##crar", &inv->chromeRarityUncommonColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Rare##crar", &inv->chromeRarityRareColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Epic##crar", &inv->chromeRarityEpicColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Legendary##crar", &inv->chromeRarityLegendaryColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Icon Atlas##inv", 0)) {
            char atlasKeyBuf[256] = {};
            snprintf(atlasKeyBuf, sizeof(atlasKeyBuf), "%s", inv->iconAtlasKey.c_str());
            if (ImGui::InputText("Atlas Key##inv_icon", atlasKeyBuf, sizeof(atlasKeyBuf))) {
                inv->iconAtlasKey = atlasKeyBuf;
            }
            checkUndoCapture(uiMgr);
            ImGui::DragInt("Atlas Cols##inv_icon", &inv->iconAtlasCols, 1.0f, 1, 32); checkUndoCapture(uiMgr);
            ImGui::DragInt("Atlas Rows##inv_icon", &inv->iconAtlasRows, 1.0f, 1, 32); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        // Bag overlay panel
        if (ImGui::TreeNode("Bag Overlay Panel")) {
            ImGui::ColorEdit4("BG Color##bagpanel",      &inv->bagPanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##bagpanel",  &inv->bagPanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##bagpanel",   &inv->bagPanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Padding##bagpanel",        &inv->bagPanelPadding, 0.5f, 0.0f, 64.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Title Height##bagpanel",   &inv->bagTitleHeight, 0.5f, 0.0f, 64.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Panel Offset##bagpanel",  &inv->bagPanelOffset.x, 0.5f, -500.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Text("Gold: %lld", (long long)inv->gold);
        ImGui::Text("Opals: %lld", (long long)inv->opals);
        ImGui::Text("Armor: %d", inv->armorValue);
        ImGui::Separator();
        ImGui::Text("Context Menu: %s (Slot: %d)", inv->contextMenuOpen ? "Open" : "Closed", inv->contextMenuSlot);
    }
    else if (auto* sp = dynamic_cast<StatusPanel*>(selectedNode_)) {
        ImGui::SeparatorText("StatusPanel");

        char titleBuf[128];
        snprintf(titleBuf, sizeof(titleBuf), "%s", sp->title.c_str());
        if (ImGui::InputText("Title Text##sp", titleBuf, sizeof(titleBuf))) {
            sp->title = titleBuf;
            checkUndoCapture(uiMgr);
        }

        if (ImGui::TreeNodeEx("Panel Background##sp", 0)) {
            ImGui::ColorEdit4("Background##spbg", &sp->backgroundColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##spbg", &sp->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##spbg", &sp->borderWidth, 0.25f, 0.0f, 10.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Close Button##sp", 0)) {
            ImGui::DragFloat("Radius##spcb", &sp->closeBtnRadius, 0.5f, 4.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Offset##spcb", &sp->closeBtnOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##spcb", &sp->closeBtnBorderW, 0.25f, 0.0f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Font Size##spcb", &sp->closeBtnFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bg##spcb", &sp->closeBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##spcb", &sp->closeBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##spcb", &sp->closeBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Position Offsets##sp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##spo",    &sp->titleOffset.x,    0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Name##spo",     &sp->nameOffset.x,     0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Level##spo",    &sp->levelOffset.x,    0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Stat Grid##spo",    &sp->statGridOffset.x,    0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Faction##spo",      &sp->factionOffset.x,     0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Char Display##spo", &sp->charDisplayOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Diamond##spo",      &sp->diamondOffset.x,     0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Char Sprite##spo",  &sp->charSpriteOffset.x,  0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Font Sizes##sp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title", &sp->titleFontSize, 0.5f, 4.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name", &sp->nameFontSize, 0.5f, 4.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Level", &sp->levelFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Stat Label", &sp->statLabelFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Stat Value", &sp->statValueFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Faction", &sp->factionFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        {
            char factionBuf[128];
            snprintf(factionBuf, sizeof(factionBuf), "%s", sp->noFactionText.c_str());
            if (ImGui::InputText("No-Faction Text##sp", factionBuf, sizeof(factionBuf))) {
                sp->noFactionText = factionBuf;
                checkUndoCapture(uiMgr);
            }
        }

        if (ImGui::TreeNodeEx("Faction Banner##sp", 0)) {
            ImGui::DragFloat("Height##spfb", &sp->factionBannerHeight, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Padding##spfb", &sp->factionBannerPadding, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Gap##spfb", &sp->factionBannerGap, 0.5f, -20.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##spfb", &sp->factionBannerBorderW, 0.25f, 0.0f, 10.0f); checkUndoCapture(uiMgr);
            ImGui::DragInt("Border Segments##spfb", &sp->factionBannerBorderSegments, 0.1f, 3, 32); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##spfb", &sp->factionBannerBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##spfb", &sp->factionBannerBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Diamond Icon##sp", 0)) {
            ImGui::DragFloat("Size##spd", &sp->diamondSize, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragInt("Segments##spd", &sp->diamondSegments, 0.1f, 3, 32); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##spd", &sp->diamondBorderWidth, 0.25f, 0.0f, 10.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Fill##spd", &sp->diamondFillColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##spd", &sp->diamondBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("XP Bar##sp", 0)) {
            ImGui::DragFloat("Height##spxp", &sp->xpBarHeight, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##spxp", &sp->xpBarBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Fill##spxp", &sp->xpBarFillColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##spxp", &sp->xpBarBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Character Display##sp", 0)) {
            ImGui::ColorEdit4("Background##spcd", &sp->charDisplayBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##spcd", &sp->charDisplayBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Char Scale##spcd", &sp->characterScale, 0.1f, 0.5f, 10.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Stat Grid Background##sp", 0)) {
            ImGui::ColorEdit4("Background##spsg", &sp->statGridBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##spsg", &sp->statGridBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Colors##sp", 0)) {
            ImGui::ColorEdit4("Title##spc", &sp->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Name##spc", &sp->nameColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Level##spc", &sp->levelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Stat Label##spc", &sp->statLabelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Faction##spc", &sp->factionColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel##sppc", &sp->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        if (ImGui::TreeNodeEx("Chrome Panel##sp", 0)) {
            ImGui::DragFloat("Border Width##spcp", &sp->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##spcp", &sp->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##spcp", &sp->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##spcp", &sp->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##spcp", &sp->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##spcp", &sp->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##spcp", &sp->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##spcp", &sp->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##spcp", &sp->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##spcp", &sp->chromeTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Bg##spcp", &sp->chromeCloseBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Border##spcp", &sp->chromeCloseBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Text##spcp", &sp->chromeCloseBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::Separator();
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

        if (ImGui::TreeNodeEx("Position Offsets##skp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##skpo", &skp->titleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Tabs##skpo", &skp->tabOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Points Badge##skpo", &skp->pointsBadgeOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Skills Header##skpo", &skp->skillsHeaderOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Skill Name##skpo", &skp->nameOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Layout##skp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Split Ratio", &skp->splitRatio, 0.01f, 0.1f, 0.9f); checkUndoCapture(uiMgr);
            ImGui::DragInt("Grid Columns", &skp->gridColumns, 0.1f, 1, 8); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Circle Radius Mul", &skp->circleRadiusMul, 0.01f, 0.05f, 0.5f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dot Size", &skp->dotSize, 0.1f, 1.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dot Spacing", &skp->dotSpacing, 0.1f, 2.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Dot Offset", &skp->dotOffset.x, 0.5f, -50.0f, 50.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Header Height", &skp->headerHeight, 0.5f, 10.0f, 80.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width", &skp->borderWidth, 0.25f, 0.0f, 10.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Content Padding", &skp->contentPadding, 0.25f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Grid Margin", &skp->gridMargin, 0.25f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Divider Width", &skp->dividerWidth, 0.25f, 0.0f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Ring Width Normal", &skp->ringWidthNormal, 0.25f, 0.5f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Ring Width Selected", &skp->ringWidthSelected, 0.25f, 0.5f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Tab Circles##skp", 0)) {
            ImGui::DragFloat("Tab Radius", &skp->tabRadius, 0.5f, 4.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Tab Spacing Mul", &skp->tabSpacingMul, 0.1f, 1.0f, 5.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Tab Bg Active", &skp->tabBgActive.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Tab Bg Inactive", &skp->tabBgInactive.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Tab Ring Active", &skp->tabRingActive.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Tab Ring Inactive", &skp->tabRingInactive.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Tab Text Active", &skp->tabTextActive.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Tab Text Inactive", &skp->tabTextInactive.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Wheel Arc##skp", 0)) {
            ImGui::DragFloat("Start Angle", &skp->wheelStartDeg, 1.0f, 0.0f, 360.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("End Angle", &skp->wheelEndDeg, 1.0f, 0.0f, 360.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Size Mul", &skp->wheelSlotSizeMul, 0.01f, 0.1f, 1.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Name Font", &skp->slotNameFontSize, 0.5f, 4.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Close Button##skp", 0)) {
            ImGui::DragFloat("Radius##skcb", &skp->closeBtnRadius, 0.5f, 4.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Offset##skcb", &skp->closeBtnOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##skcb", &skp->closeBtnBorderW, 0.25f, 0.0f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Font Size##skcb", &skp->closeBtnFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bg Color##skcb", &skp->closeBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##skcb", &skp->closeBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text Color##skcb", &skp->closeBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Points Badge##skp", 0)) {
            ImGui::DragFloat("Badge Radius", &skp->ptsBadgeRadius, 0.5f, 4.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Pts Font Size", &skp->ptsFontSize, 0.5f, 4.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Badge Ring", &skp->ptsBadgeRingColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Pts Text", &skp->ptsTextColor.r); checkUndoCapture(uiMgr);
            {
                char lblBuf[64] = {};
                snprintf(lblBuf, sizeof(lblBuf), "%s", skp->ptsLabelText.c_str());
                if (ImGui::InputText("Label Text", lblBuf, sizeof(lblBuf)))
                    skp->ptsLabelText = lblBuf;
                checkUndoCapture(uiMgr);
            }
            ImGui::ColorEdit4("Label Color", &skp->ptsLabelColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Font Sizes##skp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title##skpf", &skp->titleFontSize, 0.1f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Header##skpf", &skp->headerFontSize, 0.1f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Skill Name##skpf", &skp->nameFontSize, 0.1f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Tab##skpf", &skp->tabFontSize, 0.1f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Points##skpf", &skp->pointsFontSize, 0.1f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Skill Colors##skp")) {
            ImGui::ColorEdit4("Panel BG", &skp->panelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Panel Border", &skp->panelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("BG Unlocked", &skp->skillBgUnlocked.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("BG Locked", &skp->skillBgLocked.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Ring Selected", &skp->ringSelected.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Ring Normal", &skp->ringNormal.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Divider", &skp->dividerColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Dot Colors##skp")) {
            ImGui::ColorEdit4("Activated", &skp->dotActivated.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Unlocked", &skp->dotUnlocked.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Locked", &skp->dotLocked.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Text Colors##skp")) {
            ImGui::ColorEdit4("Title", &skp->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Header", &skp->headerColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Name Unlocked", &skp->nameUnlocked.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Name Locked", &skp->nameLocked.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Points Badge", &skp->pointsBadge.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Points Empty", &skp->pointsEmpty.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Runtime State")) {
            ImGui::InputInt("Active Set Page", &skp->activeSetPage);
            if (skp->activeSetPage < 0) skp->activeSetPage = 0;
            if (skp->activeSetPage > 3) skp->activeSetPage = 3;
            ImGui::Text("Remaining Points: %d", skp->remainingPoints);
            ImGui::Text("Selected Skill: %d", skp->selectedSkillIndex);
            ImGui::Text("%zu skills loaded", skp->classSkills.size());
            if (!skp->classSkills.empty() && ImGui::BeginTable("##skills", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 150))) {
                ImGui::TableSetupColumn("#",    ImGuiTableColumnFlags_WidthFixed, 20);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_WidthFixed, 50);
                ImGui::TableSetupColumn("Lv",   ImGuiTableColumnFlags_WidthFixed, 30);
                ImGui::TableHeadersRow();
                for (size_t i = 0; i < skp->classSkills.size(); i++) {
                    auto& sk = skp->classSkills[i];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("%zu", i);
                    ImGui::TableNextColumn(); ImGui::Text("%s", sk.name.c_str());
                    ImGui::TableNextColumn(); ImGui::Text("%d/%d", sk.currentLevel, sk.maxLevel);
                    ImGui::TableNextColumn(); ImGui::Text("%d", sk.levelRequired);
                }
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Confirm Dialog##skp")) {
            ImGui::DragFloat("Dialog W##skcd", &skp->confirmDialogW, 1.0f, 50.0f, 600.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dialog H##skcd", &skp->confirmDialogH, 1.0f, 30.0f, 400.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button W##skcd", &skp->confirmBtnW, 1.0f, 20.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button H##skcd", &skp->confirmBtnH, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Font Size##skcd", &skp->confirmFontSize, 0.5f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Font Size##skcd", &skp->confirmBtnFontSz, 0.5f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("BG##skcd", &skp->confirmBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##skcd", &skp->confirmBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##skcd", &skp->confirmTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Accept BG##skcd", &skp->confirmAcceptBg.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Decline BG##skcd", &skp->confirmDeclineBg.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Btn Text##skcd", &skp->confirmBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Tooltip##skp")) {
            ImGui::DragFloat2("Offset##sktt", &skp->tooltipOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            if (ImGui::DragFloat("Width##sktt", &skp->tooltipW, 1.0f, 80.0f, 400.0f)) {
                skp->tooltipW = (std::max)(skp->tooltipW, 80.0f);
            }
            checkUndoCapture(uiMgr);
            if (ImGui::DragFloat("Padding##sktt", &skp->tooltipPadding, 0.5f, 0.0f, 30.0f)) {
                skp->tooltipPadding = (std::max)(skp->tooltipPadding, 0.0f);
                float minTooltipWidth = (std::max)(80.0f, skp->tooltipPadding * 2.0f + 8.0f);
                skp->tooltipW = (std::max)(skp->tooltipW, minTooltipWidth);
            }
            checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border W##sktt", &skp->tooltipBorderW, 0.25f, 0.0f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Line Spacing##sktt", &skp->tooltipLineSpacing, 0.25f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Desc Gap##sktt", &skp->tooltipDescGap, 0.25f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::SeparatorText("Font Sizes");
            ImGui::DragFloat("Title Font##sktt", &skp->tooltipTitleFontSize, 0.5f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Body Font##sktt", &skp->tooltipBodyFontSize, 0.5f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Rank Font##sktt", &skp->tooltipRankFontSize, 0.5f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Req Font##sktt", &skp->tooltipReqFontSize, 0.5f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Desc Font##sktt", &skp->tooltipDescFontSize, 0.5f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::SeparatorText("Colors");
            ImGui::ColorEdit4("BG##sktt", &skp->tooltipBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##sktt", &skp->tooltipBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##sktt", &skp->tooltipTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Rank##sktt", &skp->tooltipRankColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##sktt", &skp->tooltipTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Req Level##sktt", &skp->tooltipReqColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Desc##sktt", &skp->tooltipDescColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Checkbox("Use Chrome (Checkpoint B sweep)##skptt", &skp->tooltipUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Chrome path reads Chrome Tooltip groups below;");
        ImGui::TextDisabled("legacy fields above are ignored when chrome is on.");
        ImGui::Separator();

        if (ImGui::TreeNode("Chrome Tooltip Layout##skp")) {
            ImGui::DragFloat("Width##skcttl", &skp->chromeTooltipW, 1.0f, 80.0f, 400.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Padding##skcttl", &skp->chromeTooltipPadding, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##skcttl", &skp->chromeTooltipBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Line Spacing##skcttl", &skp->chromeTooltipLineSpacing, 0.25f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Desc Gap##skcttl", &skp->chromeTooltipDescGap, 0.25f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##skcttl", &skp->chromeTooltipCornerRadius, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##skcttl", &skp->chromeTooltipShadowOffset.x, 0.5f, -20.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##skcttl", &skp->chromeTooltipShadowBlur, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Chrome Tooltip Fonts##skp")) {
            ImGui::DragFloat("Title##skcttf", &skp->chromeTooltipTitleFontSize, 0.5f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Body##skcttf", &skp->chromeTooltipBodyFontSize, 0.5f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Rank##skcttf", &skp->chromeTooltipRankFontSize, 0.5f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Req##skcttf", &skp->chromeTooltipReqFontSize, 0.5f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Desc##skcttf", &skp->chromeTooltipDescFontSize, 0.5f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Chrome Tooltip Colors##skp")) {
            ImGui::ColorEdit4("Background##skcttc", &skp->chromeTooltipBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##skcttc", &skp->chromeTooltipGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##skcttc", &skp->chromeTooltipGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##skcttc", &skp->chromeTooltipBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##skcttc", &skp->chromeTooltipShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##skcttc", &skp->chromeTooltipTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Rank##skcttc", &skp->chromeTooltipRankColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##skcttc", &skp->chromeTooltipTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Req Level##skcttc", &skp->chromeTooltipReqColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Desc##skcttc", &skp->chromeTooltipDescColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel##skppc", &skp->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        if (ImGui::TreeNode("Chrome Panel##skp")) {
            ImGui::DragFloat("Border Width##skpcp", &skp->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##skpcp", &skp->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##skpcp", &skp->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##skpcp", &skp->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##skpcp", &skp->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##skpcp", &skp->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##skpcp", &skp->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##skpcp", &skp->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##skpcp", &skp->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##skpcp", &skp->chromeTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Bg##skpcp", &skp->chromeCloseBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Border##skpcp", &skp->chromeCloseBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Text##skpcp", &skp->chromeCloseBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
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
        if (ImGui::TreeNodeEx("Position Offsets##bb", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Stack Badge##bbo", &bb->stackBadgeOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Font Sizes##bb", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Stack##bbf", &bb->stackFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Abbreviation##bbf", &bb->abbrevFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Tooltip##bbf", &bb->tooltipFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##bb", 0)) {
            ImGui::ColorEdit4("Stack Text##bbc", &bb->stackTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Stack Badge BG##bbc", &bb->stackBadgeBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Cooldown Overlay##bbc", &bb->cooldownOverlayColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Abbrev Text##bbc", &bb->abbrevTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Tooltip##bb", 0)) {
            ImGui::ColorEdit4("BG##bbt", &bb->tooltipBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##bbt", &bb->tooltipBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##bbt", &bb->tooltipTextColor.r); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Padding##bbt", &bb->tooltipPadding, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Width##bbt", &bb->tooltipWidth, 1.0f, 50.0f, 400.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Checkbox("Use Chrome (Checkpoint B sweep)##bbtt", &bb->tooltipUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Chrome path reads Chrome Tooltip groups below;");
        ImGui::TextDisabled("legacy fields above are ignored when chrome is on.");
        ImGui::Separator();

        if (ImGui::TreeNodeEx("Chrome Tooltip Layout##bb", 0)) {
            ImGui::DragFloat("Padding##bbcttl", &bb->chromeTooltipPadding, 0.5f, 0.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Line Spacing##bbcttl", &bb->chromeTooltipLineSpacing, 0.25f, 0.0f, 16.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##bbcttl", &bb->chromeTooltipBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##bbcttl", &bb->chromeTooltipCornerRadius, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##bbcttl", &bb->chromeTooltipShadowOffset.x, 0.5f, -20.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##bbcttl", &bb->chromeTooltipShadowBlur, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Tooltip Fonts##bb", 0)) {
            ImGui::DragFloat("Tooltip##bbcttf", &bb->chromeTooltipFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Tooltip Colors##bb", 0)) {
            ImGui::ColorEdit4("Background##bbcttc", &bb->chromeTooltipBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##bbcttc", &bb->chromeTooltipGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##bbcttc", &bb->chromeTooltipGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##bbcttc", &bb->chromeTooltipBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##bbcttc", &bb->chromeTooltipShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##bbcttc", &bb->chromeTooltipTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Chrome BuffBar##buffbar_cp")) {
            ImGui::Checkbox  ("Use Chrome##buffbar_cp",        &bb->hudUseChrome_);                checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bg Color##buffbar_cp",          &bb->chromeBgColor.r);              checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##buffbar_cp",      &bb->chromeBorderColor.r);          checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Border Width##buffbar_cp",      &bb->chromeBorderWidth, 0.1f);      checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Corner Radius##buffbar_cp",     &bb->chromeCornerRadius, 0.5f);     checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##buffbar_cp",     &bb->chromeShadowOffset.x, 0.1f);   checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Shadow Blur##buffbar_cp",       &bb->chromeShadowBlur, 0.5f);       checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow Color##buffbar_cp",      &bb->chromeShadowColor.r);          checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::DragFloat("Icon Size##bb", &bb->iconSize, 1.0f, 8.0f, 64.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Spacing##bb", &bb->spacing, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
        ImGui::DragInt("Max Visible", &bb->maxVisible, 1.0f, 1, 30); checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Icon Atlas");
        // Show current atlas path and allow editing
        static char atlasPathBuf[256] = {};
        if (atlasPathBuf[0] == '\0' && !bb->iconAtlasKey.empty()) {
            strncpy(atlasPathBuf, bb->iconAtlasKey.c_str(), sizeof(atlasPathBuf) - 1);
        }
        if (ImGui::InputText("Atlas Path##bb", atlasPathBuf, sizeof(atlasPathBuf))) {
            bb->iconAtlasKey = atlasPathBuf;
            checkUndoCapture(uiMgr);
        }
        ImGui::DragInt("Atlas Cols##bb", &bb->iconAtlasCols, 1.0f, 1, 16); checkUndoCapture(uiMgr);
        ImGui::DragInt("Atlas Rows##bb", &bb->iconAtlasRows, 1.0f, 1, 16); checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Active Buffs: %zu", bb->buffs.size());
    }
    else if (auto* bhp = dynamic_cast<BossHPBar*>(selectedNode_)) {
        ImGui::SeparatorText("BossHPBar");
        if (ImGui::TreeNodeEx("Position Offsets##bh", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Name##bho", &bhp->nameOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Percent##bho", &bhp->percentOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Font Sizes##bh", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Name##bhf", &bhp->nameFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Percent##bhf", &bhp->percentFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout##bh", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Name Padding##bhl", &bhp->nameBlockPadding, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##bh", 0)) {
            ImGui::ColorEdit4("Name Text##bhc", &bhp->nameTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bar Track##bhc", &bhp->barTrackColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("HP Fill##bhc", &bhp->hpFillColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Chrome BossHpBar##bosshp_cp")) {
            ImGui::Checkbox  ("Use Chrome##bosshp_cp",        &bhp->hudUseChrome_);                checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bg Color##bosshp_cp",          &bhp->chromeBgColor.r);              checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##bosshp_cp",      &bhp->chromeBorderColor.r);          checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Border Width##bosshp_cp",      &bhp->chromeBorderWidth, 0.1f);      checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Corner Radius##bosshp_cp",     &bhp->chromeCornerRadius, 0.5f);     checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##bosshp_cp",     &bhp->chromeShadowOffset.x, 0.1f);   checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Shadow Blur##bosshp_cp",       &bhp->chromeShadowBlur, 0.5f);       checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow Color##bosshp_cp",      &bhp->chromeShadowColor.r);          checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::DragFloat("Bar Height##bhp", &bhp->barHeight, 1.0f, 8.0f, 60.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Bar Padding##bhp", &bhp->barPadding, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Boss: %s", bhp->bossName.c_str());
        ImGui::Text("HP: %d / %d", bhp->currentHP, bhp->maxHP);
    }
    else if (auto* cd = dynamic_cast<ConfirmDialog*>(selectedNode_)) {
        ImGui::SeparatorText("ConfirmDialog");
        if (ImGui::TreeNodeEx("Position Offsets##cd", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Message##cdo", &cd->messageOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Buttons##cdo", &cd->buttonOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Font Sizes##cd", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Message##cdf", &cd->messageFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button##cdf", &cd->buttonFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##cd", 0)) {
            ImGui::ColorEdit4("Background##cfm", &cd->bgColor.r);     checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##cfm",     &cd->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##cfm", &cd->borderWidth, 0.1f, 0.0f, 10.0f); checkUndoCapture(uiMgr);
            ImGui::Separator();
            ImGui::ColorEdit4("Button##cdc", &cd->buttonColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Button Hover##cdc", &cd->buttonHoverColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
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
    else if (auto* qs = dynamic_cast<QuantitySelector*>(selectedNode_)) {
        ImGui::SeparatorText("QuantitySelector");
        if (ImGui::TreeNodeEx("Layout##qs", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Button Width##qs", &qs->buttonWidth, 1.0f, 40.0f, 300.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button Height##qs", &qs->buttonHeight, 1.0f, 16.0f, 80.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button Spacing##qs", &qs->buttonSpacing, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Arrow Btn Size##qs", &qs->arrowButtonSize, 1.0f, 16.0f, 80.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Font Sizes##qs", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title##qsf", &qs->titleFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Quantity##qsf", &qs->quantityFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button##qsf", &qs->buttonFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##qs", 0)) {
            ImGui::ColorEdit4("Button##qsc", &qs->buttonColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Button Hover##qsc", &qs->buttonHoverColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Arrow##qsc", &qs->arrowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Arrow Hover##qsc", &qs->arrowHoverColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Max Button##qs", 0)) {
            ImGui::DragFloat("Width##qsm", &qs->maxButtonWidth, 1.0f, 20.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Font Size##qsm", &qs->maxButtonFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Color##qsm", &qs->maxButtonColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Hover Color##qsm", &qs->maxButtonHoverColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text Color##qsm", &qs->maxButtonTextColor.r); checkUndoCapture(uiMgr);
            char maxBuf[128] = {};
            snprintf(maxBuf, sizeof(maxBuf), "%s", qs->maxButtonText.c_str());
            if (ImGui::InputText("Text##qsm", maxBuf, sizeof(maxBuf))) {
                qs->maxButtonText = maxBuf;
            }
            checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        char confBuf[128] = {};
        snprintf(confBuf, sizeof(confBuf), "%s", qs->confirmText.c_str());
        if (ImGui::InputText("Confirm Text##qs", confBuf, sizeof(confBuf))) {
            qs->confirmText = confBuf;
        }
        checkUndoCapture(uiMgr);
        char canBuf[128] = {};
        snprintf(canBuf, sizeof(canBuf), "%s", qs->cancelText.c_str());
        if (ImGui::InputText("Cancel Text##qs", canBuf, sizeof(canBuf))) {
            qs->cancelText = canBuf;
        }
        checkUndoCapture(uiMgr);
    }
    else if (auto* bv = dynamic_cast<BagViewPanel*>(selectedNode_)) {
        ImGui::SeparatorText("BagViewPanel");
        if (ImGui::TreeNodeEx("Panel##bvp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("BG Width##bvpp", &bv->bgWidth, 1.0f, 0.0f, 1000.0f, "%.0f (0=full)"); checkUndoCapture(uiMgr);
            ImGui::DragFloat("BG Height##bvpp", &bv->bgHeight, 1.0f, 0.0f, 1000.0f, "%.0f (0=full)"); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("BG Offset##bvpp", &bv->bgOffset.x, 0.5f, -500.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##bvpp", &bv->panelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##bvpp", &bv->panelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##bvpp", &bv->panelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Text##bvpp", &bv->titleTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout##bvp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Slot Size##bvp", &bv->slotSize, 1.0f, 16.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Padding##bvp", &bv->slotPadding, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragInt("Grid Columns##bvp", &bv->gridColumns, 1, 1, 10); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Content Padding##bvp", &bv->contentPadding, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Title Height##bvp", &bv->titleHeight, 0.5f, 0.0f, 64.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Title Pad Top##bvp", &bv->titlePadTop, 0.5f, 0.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Border Width##bvp", &bv->slotBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Font Sizes##bvp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title##bvpf", &bv->titleFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Text##bvpf", &bv->slotFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Slot Colors##bvp", 0)) {
            ImGui::ColorEdit4("Slot BG##bvpc", &bv->slotBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Slot Border##bvpc", &bv->slotBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Empty Slot##bvpc", &bv->emptySlotColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Item Text##bvpc", &bv->itemTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Enchant Text##bvpc", &bv->enchantTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Quantity Text##bvpc", &bv->quantityTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Close Button##bvp", 0)) {
            ImGui::DragFloat("Radius##bvpcb", &bv->closeBtnRadius, 0.5f, 4.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Offset##bvpcb", &bv->closeBtnOffset, 0.5f, 0.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Font Size##bvpcb", &bv->closeBtnFontSize, 0.5f, 6.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##bvpcb", &bv->closeBtnBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("BG Color##bvpcb", &bv->closeBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##bvpcb", &bv->closeBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text Color##bvpcb", &bv->closeBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel##bvppc", &bv->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        if (ImGui::TreeNodeEx("Chrome Panel##bvp", 0)) {
            ImGui::DragFloat("Border Width##bvpcp", &bv->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##bvpcp", &bv->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##bvpcp", &bv->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##bvpcp", &bv->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##bvpcp", &bv->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##bvpcp", &bv->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##bvpcp", &bv->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##bvpcp", &bv->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##bvpcp", &bv->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Text##bvpcp", &bv->chromeTitleTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Bg##bvpcp", &bv->chromeCloseBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Border##bvpcp", &bv->chromeCloseBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Text##bvpcp", &bv->chromeCloseBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
    }
    else if (auto* ip = dynamic_cast<InvitePromptPanel*>(selectedNode_)) {
        ImGui::SeparatorText("InvitePromptPanel");
        if (ImGui::TreeNodeEx("Position Offsets##ip", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##ipo", &ip->titleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Message##ipo", &ip->messageOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Buttons##ipo", &ip->buttonOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Font Sizes##ip", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title##ipf", &ip->titleFontSize, 0.5f, 6.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Message##ipf", &ip->messageFontSize, 0.5f, 6.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button##ipf", &ip->buttonFontSize, 0.5f, 6.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout##ip", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Panel Width##ipl", &ip->panelWidth, 1.0f, 100.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Panel Height##ipl", &ip->panelHeight, 1.0f, 60.0f, 300.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Width##ipl", &ip->buttonWidth, 1.0f, 40.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Height##ipl", &ip->buttonHeight, 1.0f, 16.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Spacing##ipl", &ip->buttonSpacing, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##ipl", &ip->borderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Title Pad Top##ipl", &ip->titlePadTop, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Msg Pad Top##ipl", &ip->messagePadTop, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Pad Bot##ipl", &ip->buttonPadBottom, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##ip", 0)) {
            ImGui::ColorEdit4("Background##ipc", &ip->bgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##ipc", &ip->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##ipc", &ip->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Message##ipc", &ip->messageColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Accept Btn##ipc", &ip->acceptBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Accept Hover##ipc", &ip->acceptBtnHoverColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Accept Text##ipc", &ip->acceptBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Decline Btn##ipc", &ip->declineBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Decline Hover##ipc", &ip->declineBtnHoverColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Decline Text##ipc", &ip->declineBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::Text("Busy: %s", ip->isBusy() ? "Yes" : "No");
    }
    else if (auto* nt = dynamic_cast<NotificationToast*>(selectedNode_)) {
        ImGui::SeparatorText("NotificationToast");
        if (ImGui::TreeNodeEx("Position Offsets##nt", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Text##nto", &nt->textOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Font Sizes##nt", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Text##ntf", &nt->textFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout##nt", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Accent Width##ntl", &nt->accentWidth, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Text Margin##ntl", &nt->textMargin, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##nt", 0)) {
            ImGui::ColorEdit4("Toast BG##ntc", &nt->toastBgColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Chrome NotificationToast##notif_cp")) {
            ImGui::Checkbox  ("Use Chrome##notif_cp",        &nt->hudUseChrome_);                checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bg Color##notif_cp",          &nt->chromeBgColor.r);              checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##notif_cp",      &nt->chromeBorderColor.r);          checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Border Width##notif_cp",      &nt->chromeBorderWidth, 0.1f);      checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Corner Radius##notif_cp",     &nt->chromeCornerRadius, 0.5f);     checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##notif_cp",     &nt->chromeShadowOffset.x, 0.1f);   checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Shadow Blur##notif_cp",       &nt->chromeShadowBlur, 0.5f);       checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow Color##notif_cp",      &nt->chromeShadowColor.r);          checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
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
    else if (auto* div = dynamic_cast<Divider*>(selectedNode_)) {
        ImGui::SeparatorText("Divider");
        const char* orientNames[] = {"Horizontal", "Vertical"};
        int oIdx = static_cast<int>(div->orientation);
        if (ImGui::Combo("Orientation##div", &oIdx, orientNames, 2)) {
            div->orientation = static_cast<DividerOrientation>(oIdx);
        }
        checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Color##div", &div->color.r); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Thickness##div", &div->thickness, 0.1f, 0.0f, 40.0f, "%.1f px");
        checkUndoCapture(uiMgr);
        ImGui::DragFloat("Length (px)##div", &div->length, 0.5f, 0.0f, 4000.0f, "%.0f");
        checkUndoCapture(uiMgr);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Absolute length. 0 = stretch to parent. Overridden when Length %% > 0.");
        ImGui::DragFloat("Length %##div", &div->lengthPercent, 0.005f, 0.0f, 1.0f, "%.2f");
        checkUndoCapture(uiMgr);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Fraction of parent extent (0-1). Overrides Length when > 0.");
        ImGui::DragFloat("Edge Padding##div", &div->padding, 0.5f, 0.0f, 200.0f, "%.1f px");
        checkUndoCapture(uiMgr);
        ImGui::DragFloat("Fade Ends##div", &div->fadeEnds, 0.005f, 0.0f, 0.5f, "%.2f");
        checkUndoCapture(uiMgr);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Alpha fade fraction at each end. 0 = crisp, 0.2 = soft, 0.5 = all-fade.");

        char ornBuf[192] = {};
        snprintf(ornBuf, sizeof(ornBuf), "%s", div->ornamentImage.c_str());
        if (ImGui::InputText("Ornament Image##div", ornBuf, sizeof(ornBuf))) {
            div->ornamentImage = ornBuf;
        }
        checkUndoCapture(uiMgr);
        ImGui::DragFloat("Ornament Size##div", &div->ornamentSize, 0.5f, 0.0f, 256.0f, "%.1f px");
        checkUndoCapture(uiMgr);

        ImGui::Checkbox("Double Stroke##div", &div->doubleStroke); checkUndoCapture(uiMgr);
        if (div->doubleStroke) {
            ImGui::DragFloat("Stroke Gap##div", &div->strokeGap, 0.1f, 0.0f, 40.0f, "%.1f px");
            checkUndoCapture(uiMgr);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Space between the two parallel strokes (px).");
        }
    }
    // login_screen: reflected properties drive the auto-inspector above. This
    // branch only adds the S128 Tier E chrome rows (not in FATE_REFLECT).
    else if (auto* ls = dynamic_cast<LoginScreen*>(selectedNode_)) {
        ImGui::SeparatorText("LoginScreen Chrome (S128 Tier E)");
        if (ImGui::TreeNode("Chrome LoginScreen##login_cp")) {
            if (ImGui::TreeNode("Layout##login_l")) {
                ImGui::Checkbox  ("Use Chrome##login_l",            &ls->loginUseChrome_);              checkUndoCapture(uiMgr);
                ImGui::DragFloat ("Border Width##login_l",          &ls->chromePanelBorderWidth, 0.1f); checkUndoCapture(uiMgr);
                ImGui::DragFloat ("Corner Radius##login_l",         &ls->chromePanelCornerRadius, 0.5f); checkUndoCapture(uiMgr);
                ImGui::DragFloat2("Shadow Offset##login_l",         &ls->chromePanelShadowOffset.x, 0.1f); checkUndoCapture(uiMgr);
                ImGui::DragFloat ("Shadow Blur##login_l",           &ls->chromePanelShadowBlur, 0.5f);  checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Bg Color##login_l",              &ls->chromePanelBgColor.r);          checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Gradient Top##login_l",          &ls->chromePanelGradientTop.r);      checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Gradient Bottom##login_l",       &ls->chromePanelGradientBottom.r);   checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Border Color##login_l",          &ls->chromePanelBorderColor.r);      checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Shadow Color##login_l",          &ls->chromePanelShadowColor.r);      checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Title Bar Color##login_l",       &ls->chromeTitleBarColor.r);         checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Title Color##login_l",           &ls->chromeTitleColor.r);            checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Backdrop Tint##login_l",         &ls->chromeBackdropTintColor.r);     checkUndoCapture(uiMgr);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Form##login_cp")) {
                ImGui::ColorEdit4("Form Field Bg##login_f",         &ls->chromeFormFieldBgColor.r);      checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Form Field Border##login_f",     &ls->chromeFormFieldBorderColor.r);  checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Button Row Bg##login_f",         &ls->chromeButtonRowBgColor.r);      checkUndoCapture(uiMgr);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Buttons##login_cp")) {
                ImGui::ColorEdit4("Button Bg##login_b",             &ls->chromeButtonBgColor.r);         checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Button Border##login_b",         &ls->chromeButtonBorderColor.r);     checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Button Text##login_b",           &ls->chromeButtonTextColor.r);       checkUndoCapture(uiMgr);
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
    }
    else if (auto* pf = dynamic_cast<PartyFrame*>(selectedNode_)) {
        ImGui::SeparatorText("PartyFrame");
        if (ImGui::TreeNodeEx("Position Offsets##pf", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Name##pfo", &pf->nameOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Level##pfo", &pf->levelOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Portrait##pfo", &pf->portraitOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Bars##pfo", &pf->barOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Font Sizes##pf", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Name##pff", &pf->nameFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Level##pff", &pf->levelFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout##pf", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Portrait Radius##pfl", &pf->portraitRadius, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("HP Bar Height##pfl", &pf->hpBarHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("MP Bar Height##pfl", &pf->mpBarHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##pfl", &pf->borderWidth, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Portrait Pad L##pfl", &pf->portraitPadLeft, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Portrait Rim##pfl", &pf->portraitRimWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Crown Size##pfl", &pf->crownSize, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Text Gap##pfl", &pf->textGapAfterPortrait, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Text Pad R##pfl", &pf->textPadRight, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name Pad Top##pfl", &pf->namePadTop, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Lvl Pad R##pfl", &pf->levelPadRight, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Bar Offset Y##pfl", &pf->barOffsetY, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Bar Gap##pfl", &pf->barGap, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##pf", 0)) {
            ImGui::ColorEdit4("Card BG##pfc", &pf->cardBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Card Border##pfc", &pf->cardBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Portrait Fill##pfc", &pf->portraitFillColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Portrait Rim##pfc", &pf->portraitRimColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Crown##pfc", &pf->crownColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Name##pfc", &pf->nameColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Level##pfc", &pf->levelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("HP Bar BG##pfc", &pf->hpBarBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("HP Fill##pfc", &pf->hpFillColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("MP Bar BG##pfc", &pf->mpBarBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("MP Fill##pfc", &pf->mpFillColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
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

        if (ImGui::TreeNodeEx("Position Offsets##chp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Messages##chpo", &cp->messageOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Input##chpo", &cp->inputOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Channel##chpo", &cp->channelOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Layout##cp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Full Panel Width", &cp->fullPanelWidth, 1.0f, 100.0f, 2000.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Full Panel Height", &cp->fullPanelHeight, 1.0f, 100.0f, 900.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Input Bar Height", &cp->inputBarHeight, 0.5f, 16.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Input Bar Width", &cp->inputBarWidth, 1.0f, 0.0f, 2000.0f, "%.0f (0=full)"); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Channel Btn Width", &cp->channelBtnWidth, 1.0f, 24.0f, 120.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Channel Btn Height", &cp->channelBtnHeight, 0.5f, 0.0f, 60.0f, "%.1f (0=bar)"); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Close Btn Size", &cp->closeBtnSize, 0.5f, 10.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Emoticon & Dice Buttons##cp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Emoticon Btn Size", &cp->emoticonBtnSize, 0.5f, 0.0f, 60.0f, "%.1f (0=bar)"); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dice Btn Size", &cp->diceBtnSize, 0.5f, 0.0f, 60.0f, "%.1f (0=bar)"); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button Spacing", &cp->buttonSpacing, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Keyboard Height Ratio", &cp->keyboardHeightRatio, 0.01f, 0.0f, 0.6f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Emote Btn BG##cpb", &cp->emoticonBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Emote Btn Active##cpb", &cp->emoticonBtnActiveColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Dice Btn BG##cpb", &cp->diceBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Dice Btn Text##cpb", &cp->diceBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Font Sizes##cp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Message Font", &cp->messageFontSize, 0.5f, 7.0f, 24.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Input Font", &cp->inputFontSize, 0.5f, 7.0f, 24.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Channel Label Font", &cp->channelLabelFontSize, 0.5f, 5.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Line Spacing", &cp->messageLineSpacing, 0.5f, 0.0f, 16.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Offset", &cp->messageShadowOffset, 0.5f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Channel Colors##cp", 0)) {
            ImGui::ColorEdit4("All##ch", &cp->colorAll.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Map##ch", &cp->colorMap.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Global##ch", &cp->colorGlobal.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Trade##ch", &cp->colorTrade.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Party##ch", &cp->colorParty.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Guild##ch", &cp->colorGuild.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Private/System##ch", &cp->colorPrivate.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Faction Colors##cp", 0)) {
            ImGui::ColorEdit4("None##fac", &cp->factionNoneColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Xyros##fac", &cp->factionXyrosColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Fenor##fac", &cp->factionFenorColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Zethos##fac", &cp->factionZethosColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Solis##fac", &cp->factionSolisColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Message Colors##cp", 0)) {
            ImGui::ColorEdit4("Text##msg", &cp->messageTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##msg", &cp->messageShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("System Message Colors##cp", 0)) {
            ImGui::ColorEdit4("Default [System]##sys", &cp->colorSystemDefault.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Loot]##sys", &cp->colorSystemLoot.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Trade]##sys", &cp->colorSystemTrade.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Market]##sys", &cp->colorSystemMarket.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Bounty]##sys", &cp->colorSystemBounty.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Gauntlet]##sys", &cp->colorSystemGauntlet.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Guild]##sys", &cp->colorSystemGuild.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Social]##sys", &cp->colorSystemSocial.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Party]##sys", &cp->colorSystemParty.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Item]##sys", &cp->colorSystemItem.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Quest]##sys", &cp->colorSystemQuest.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Enchant]##sys", &cp->colorSystemEnchant.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Repair]##sys", &cp->colorSystemRepair.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Extract]##sys", &cp->colorSystemExtract.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Craft]##sys", &cp->colorSystemCraft.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Socket]##sys", &cp->colorSystemSocket.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Pet]##sys", &cp->colorSystemPet.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Arena]##sys", &cp->colorSystemArena.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Battlefield]##sys", &cp->colorSystemBattlefield.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Boss]##sys", &cp->colorSystemBoss.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("[Event]##sys", &cp->colorSystemEvent.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("System Message Font Sizes##cp", 0)) {
            ImGui::Text("0 = use default messageFontSize");
            ImGui::DragFloat("Default [System]##sfs", &cp->fontSizeSystemDefault, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Loot]##sfs", &cp->fontSizeSystemLoot, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Trade]##sfs", &cp->fontSizeSystemTrade, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Market]##sfs", &cp->fontSizeSystemMarket, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Bounty]##sfs", &cp->fontSizeSystemBounty, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Gauntlet]##sfs", &cp->fontSizeSystemGauntlet, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Guild]##sfs", &cp->fontSizeSystemGuild, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Social]##sfs", &cp->fontSizeSystemSocial, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Party]##sfs", &cp->fontSizeSystemParty, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Item]##sfs", &cp->fontSizeSystemItem, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Quest]##sfs", &cp->fontSizeSystemQuest, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Enchant]##sfs", &cp->fontSizeSystemEnchant, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Repair]##sfs", &cp->fontSizeSystemRepair, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Extract]##sfs", &cp->fontSizeSystemExtract, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Craft]##sfs", &cp->fontSizeSystemCraft, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Socket]##sfs", &cp->fontSizeSystemSocket, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Pet]##sfs", &cp->fontSizeSystemPet, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Arena]##sfs", &cp->fontSizeSystemArena, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Battlefield]##sfs", &cp->fontSizeSystemBattlefield, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Boss]##sfs", &cp->fontSizeSystemBoss, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("[Event]##sfs", &cp->fontSizeSystemEvent, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Close Button##cp", 0)) {
            ImGui::ColorEdit4("Background##cls", &cp->closeBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##cls", &cp->closeBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Icon##cls", &cp->closeBtnIconColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Input Bar Colors##cp", 0)) {
            ImGui::ColorEdit4("Bar Background##inp", &cp->inputBarBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Field Background##inp", &cp->inputFieldBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##inp", &cp->inputBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border (Focused)##inp", &cp->inputBorderFocusColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Channel Btn##inp", &cp->channelBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Placeholder##inp", &cp->placeholderColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Chrome ChatPanel##chat_cp")) {
            ImGui::Checkbox  ("Use Chrome##chat_cp",                  &cp->hudUseChrome_);                     checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Border Width##chat_cp",                &cp->chromePanelBorderWidth, 0.1f);     checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Corner Radius##chat_cp",               &cp->chromePanelCornerRadius, 0.5f);    checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##chat_cp",               &cp->chromePanelShadowOffset.x, 0.1f);  checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Shadow Blur##chat_cp",                 &cp->chromePanelShadowBlur, 0.5f);      checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bg Color##chat_cp",                    &cp->chromePanelBgColor.r);             checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##chat_cp",                &cp->chromePanelGradientTop.r);         checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##chat_cp",             &cp->chromePanelGradientBottom.r);      checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##chat_cp",                &cp->chromePanelBorderColor.r);         checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow Color##chat_cp",                &cp->chromePanelShadowColor.r);         checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar Color##chat_cp",             &cp->chromeTitleBarColor.r);            checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Color##chat_cp",                 &cp->chromeTitleColor.r);               checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Text("Active Tab: %d", cp->activeTab);
        ImGui::Text("Mode: %s", cp->fullPanelMode_ ? "Full Panel" : "Idle Overlay");
        ImGui::Text("In Party: %s", cp->isInParty ? "Yes" : "No");
        ImGui::Text("In Guild: %s", cp->isInGuild ? "Yes" : "No");
    }
    else if (auto* ep = dynamic_cast<EmoticonPanel*>(selectedNode_)) {
        ImGui::SeparatorText("EmoticonPanel");

        if (ImGui::TreeNodeEx("Layout##ep", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragInt("Slot Count", &ep->slotCount, 1.0f, 1, 32); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Size", &ep->slotSize, 0.5f, 16.0f, 80.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Padding", &ep->slotPadding, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Strip Height", &ep->stripHeight, 0.5f, 24.0f, 120.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Radius", &ep->borderRadius, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Scroll Offset", &ep->scrollOffset, 1.0f, 0.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Colors##ep", 0)) {
            ImGui::ColorEdit4("Background##ep", &ep->bgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##ep", &ep->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Slot BG##ep", &ep->slotBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Slot Hover##ep", &ep->slotHoverColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
    }
    else if (auto* fsb = dynamic_cast<FateStatusBar*>(selectedNode_)) {
        ImGui::SeparatorText("FateStatusBar — Layout");
        if (ImGui::TreeNodeEx("Position Offsets##fsb", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Portrait##fsbo", &fsb->portraitOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Level##fsbo", &fsb->levelOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("HP Label##fsbo", &fsb->hpLabelOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("HP Bar##fsbo", &fsb->hpBarOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("HP Text##fsbo", &fsb->hpTextOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("MP Label##fsbo", &fsb->mpLabelOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("MP Bar##fsbo", &fsb->mpBarOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("MP Text##fsbo", &fsb->mpTextOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Menu Btn##fsbo", &fsb->menuBtnOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Chat Btn##fsbo", &fsb->chatBtnOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::DragFloat("Top Bar Height", &fsb->topBarHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
        ImGui::Checkbox("Show Portrait", &fsb->showPortrait); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Portrait Radius", &fsb->portraitRadius, 1.0f, 5.0f, 60.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Portrait Fill", &fsb->portraitFillColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Portrait Border", &fsb->portraitBorderColor.r); checkUndoCapture(uiMgr);
        ImGui::DragFloat("HP/MP Bar Height", &fsb->barHeight, 1.0f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("HP Bar Width (0=auto)", &fsb->hpBarWidth, 1.0f, 0.0f, 400.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("HP Bar Height (0=shared)", &fsb->hpBarHeight, 1.0f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("MP Bar Width (0=auto)", &fsb->mpBarWidth, 1.0f, 0.0f, 400.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("MP Bar Height (0=shared)", &fsb->mpBarHeight, 1.0f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Bar Corner Radius", &fsb->barCornerRadius, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("HP Bar Color", &fsb->hpBarColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("HP Bar Track", &fsb->hpBarBgColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("MP Bar Color", &fsb->mpBarColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("MP Bar Track", &fsb->mpBarBgColor.r); checkUndoCapture(uiMgr);
        if (ImGui::TreeNodeEx("Shield Overlay##fsb", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit4("Tier 1 (0-100% HP)##shld", &fsb->shieldTier1Color.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Tier 2 (100-200% HP)##shld", &fsb->shieldTier2Color.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Tier 3 (200-300% HP)##shld", &fsb->shieldTier3Color.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::ColorEdit4("Strip Background", &fsb->stripBgColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Strip Border", &fsb->stripBorderColor.r); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Strip Border Width", &fsb->stripBorderWidth, 0.5f, 0.0f, 10.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Strip Corner Radius", &fsb->stripCornerRadius, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);

        ImGui::SeparatorText("Menu Button");
        ImGui::Checkbox("Show Menu Button", &fsb->showMenuButton); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Menu Btn Radius", &fsb->menuBtnSize, 1.0f, 5.0f, 60.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Menu Btn Gap", &fsb->menuBtnGap, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Menu Btn Font", &fsb->menuBtnFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Menu Btn Text", &fsb->menuBtnTextColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Menu Btn Bg", &fsb->menuBtnBgColor.r); checkUndoCapture(uiMgr);

        ImGui::SeparatorText("Menu Overlay");
        ImGui::DragFloat("Overlay Width", &fsb->menuOverlayW, 1.0f, 60.0f, 300.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Item Height", &fsb->menuItemH, 1.0f, 16.0f, 80.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Item Font Size", &fsb->menuItemFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Item Text Color", &fsb->menuItemTextColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Overlay Bg", &fsb->menuOverlayBgColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Overlay Border", &fsb->menuOverlayBorderColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Divider Color", &fsb->menuDividerColor.r); checkUndoCapture(uiMgr);

        ImGui::SeparatorText("Chat Button");
        ImGui::Checkbox("Show Chat Button", &fsb->showChatButton); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Chat Btn Radius", &fsb->chatBtnSize, 1.0f, 5.0f, 60.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Chat Btn Offset X", &fsb->chatBtnOffsetX, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Chat Btn Font", &fsb->chatBtnFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Chat Btn Text", &fsb->chatBtnTextColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Chat Btn Bg", &fsb->chatBtnBgColor.r); checkUndoCapture(uiMgr);

        ImGui::Separator();
        ImGui::Checkbox("Use Chrome (S125b)##fsbpc", &fsb->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        if (ImGui::TreeNodeEx("Chrome Menu Button##fsb", 0)) {
            ImGui::ColorEdit4("Bg##fsbcmb", &fsb->chromeMenuBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##fsbcmb", &fsb->chromeMenuBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##fsbcmb", &fsb->chromeMenuBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Chat Button##fsb", 0)) {
            ImGui::ColorEdit4("Bg##fsbccb", &fsb->chromeChatBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##fsbccb", &fsb->chromeChatBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##fsbccb", &fsb->chromeChatBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Menu Overlay##fsb", 0)) {
            ImGui::ColorEdit4("Bg##fsbcmo", &fsb->chromeMenuOverlayBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##fsbcmo", &fsb->chromeMenuOverlayBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Item Text##fsbcmo", &fsb->chromeMenuItemTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Divider##fsbcmo", &fsb->chromeMenuDividerColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("S130 Chrome — Strip")) {
            ImGui::ColorEdit4("chromeStripBgColor",        &fsb->chromeStripBgColor.r);        checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeStripGradientTop",    &fsb->chromeStripGradientTop.r);    checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeStripGradientBottom", &fsb->chromeStripGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeStripBorderColor",    &fsb->chromeStripBorderColor.r);    checkUndoCapture(uiMgr);
            ImGui::DragFloat ("chromeStripBorderWidth",    &fsb->chromeStripBorderWidth, 0.1f, 0.0f, 10.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("S130 Chrome — Portrait")) {
            ImGui::ColorEdit4("chromePortraitFillColor",   &fsb->chromePortraitFillColor.r);   checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromePortraitBorderColor", &fsb->chromePortraitBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("S130 Chrome — EXP Arc (incl. legacy promotions)")) {
            // Legacy schema-gap promotions (read by both legacy and chrome modes)
            ImGui::ColorEdit4("expArcFillColor (legacy)",  &fsb->expArcFillColor.r);           checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("expArcTrackColor (legacy)", &fsb->expArcTrackColor.r);          checkUndoCapture(uiMgr);
            // Chrome-only
            ImGui::ColorEdit4("chromeExpArcFillColor",     &fsb->chromeExpArcFillColor.r);     checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeExpArcTrackColor",    &fsb->chromeExpArcTrackColor.r);    checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("S130 Chrome — HP/MP Bar Tracks")) {
            ImGui::ColorEdit4("chromeHpBarBgColor", &fsb->chromeHpBarBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeMpBarBgColor", &fsb->chromeMpBarBgColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("S130 Chrome — HUD Text")) {
            ImGui::ColorEdit4("chromeHpLabelColor",  &fsb->chromeHpLabelColor.r);  checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeHpNumberColor", &fsb->chromeHpNumberColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeMpLabelColor",  &fsb->chromeMpLabelColor.r);  checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeMpNumberColor", &fsb->chromeMpNumberColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeLevelColor",    &fsb->chromeLevelColor.r);    checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeCoordColor",    &fsb->chromeCoordColor.r);    checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::SeparatorText("Coordinates");
        ImGui::Checkbox("Show Coordinates", &fsb->showCoordinates); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Coord Font Size", &fsb->coordFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat2("Coord Offset", &fsb->coordOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Coord Color", &fsb->coordColor.r); checkUndoCapture(uiMgr);
        {
            char fmtBuf[64];
            snprintf(fmtBuf, sizeof(fmtBuf), "%s", fsb->coordFormat.c_str());
            if (ImGui::InputText("Coord Format", fmtBuf, sizeof(fmtBuf))) {
                fsb->coordFormat = fmtBuf; checkUndoCapture(uiMgr);
            }
        }
        {
            int styleIdx = static_cast<int>(fsb->coordStyle) - 1;
            if (ImGui::Combo("Coord Style", &styleIdx, kTextStyleNames, kTextStyleCount)) {
                fsb->coordStyle = static_cast<TextStyle>(styleIdx + 1); checkUndoCapture(uiMgr);
            }
        }

        ImGui::SeparatorText("Font Sizes");
        ImGui::DragFloat("Level Font", &fsb->levelFontSize, 0.5f, 6.0f, 50.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Level Color", &fsb->levelColor.r); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Label Font (shared)", &fsb->labelFontSize, 0.5f, 6.0f, 50.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Number Font (shared)", &fsb->numberFontSize, 0.5f, 6.0f, 50.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("HP Label Font (0=shared)", &fsb->hpLabelFontSize, 0.5f, 0.0f, 50.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("HP Label Color", &fsb->hpLabelColor.r); checkUndoCapture(uiMgr);
        ImGui::DragFloat("HP Number Font (0=shared)", &fsb->hpNumberFontSize, 0.5f, 0.0f, 50.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("HP Number Color", &fsb->hpNumberColor.r); checkUndoCapture(uiMgr);
        ImGui::DragFloat("MP Label Font (0=shared)", &fsb->mpLabelFontSize, 0.5f, 0.0f, 50.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("MP Label Color", &fsb->mpLabelColor.r); checkUndoCapture(uiMgr);
        ImGui::DragFloat("MP Number Font (0=shared)", &fsb->mpNumberFontSize, 0.5f, 0.0f, 50.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("MP Number Color", &fsb->mpNumberColor.r); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Button Font", &fsb->buttonFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);

        ImGui::SeparatorText("Font Names");
        {
            auto names = FontRegistry::instance().fontNames();
            auto fontCombo = [&](const char* label, std::string& fontName) {
                int current = 0;
                std::vector<const char*> items;
                items.push_back("(default)");
                for (int i = 0; i < (int)names.size(); ++i) {
                    items.push_back(names[i].c_str());
                    if (names[i] == fontName) current = i + 1;
                }
                if (ImGui::Combo(label, &current, items.data(), (int)items.size())) {
                    fontName = (current == 0) ? "" : names[current - 1];
                    checkUndoCapture(uiMgr);
                }
            };
            fontCombo("Level Font##fsbfn",     fsb->levelFontName);
            fontCombo("HP Label Font##fsbfn",  fsb->hpLabelFontName);
            fontCombo("HP Number Font##fsbfn", fsb->hpNumberFontName);
            fontCombo("MP Label Font##fsbfn",  fsb->mpLabelFontName);
            fontCombo("MP Number Font##fsbfn", fsb->mpNumberFontName);
            fontCombo("Coord Font##fsbfn",     fsb->coordFontName);
            fontCombo("Menu Btn Font##fsbfn",  fsb->menuBtnFontName);
            fontCombo("Chat Btn Font##fsbfn",  fsb->chatBtnFontName);
            fontCombo("Menu Item Font##fsbfn", fsb->menuItemFontName);
        }
    }
    else if (auto* dov = dynamic_cast<DeathOverlay*>(selectedNode_)) {
        ImGui::SeparatorText("DeathOverlay");

        if (ImGui::TreeNodeEx("Position Offsets##do", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##doo", &dov->titleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Loss Text##doo", &dov->lossTextOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Countdown##doo", &dov->countdownOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Buttons##doo", &dov->buttonOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Font Sizes##do", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title##dof", &dov->titleFontSize, 0.5f, 6.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Body##dof", &dov->bodyFontSize, 0.5f, 6.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Countdown##dof", &dov->countdownFontSize, 0.5f, 6.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button##dof", &dov->buttonFontSize, 0.5f, 6.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Layout##do", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Start Y Ratio", &dov->startYRatio, 0.01f, 0.0f, 1.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button Width", &dov->buttonWidth, 1.0f, 50.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button Height", &dov->buttonHeight, 1.0f, 16.0f, 80.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button Spacing", &dov->buttonSpacing, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Colors##do", 0)) {
            ImGui::ColorEdit4("Overlay BG##doc", &dov->overlayColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##doc", &dov->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Countdown##doc", &dov->countdownColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Button BG##doc", &dov->buttonBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Button Border##doc", &dov->buttonBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Button Text##doc", &dov->buttonTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Checkbox("Active##dov", &dov->active); checkUndoCapture(uiMgr);
        int xpL = dov->xpLost;
        if (ImGui::DragInt("XP Lost", &xpL, 1.0f, 0, 999999)) { dov->xpLost = xpL; }
        checkUndoCapture(uiMgr);
        int honL = dov->honorLost;
        if (ImGui::DragInt("Honor Lost", &honL, 1.0f, 0, 999999)) { dov->honorLost = honL; }
        checkUndoCapture(uiMgr);
        ImGui::DragFloat("Countdown##dov", &dov->countdown, 0.1f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
        ImGui::Checkbox("Respawn Pending", &dov->respawnPending); checkUndoCapture(uiMgr);

        // S128 Tier E chrome — driven by UIManager::hudUseChrome_ (in-game).
        ImGui::SeparatorText("DeathOverlay Chrome (S128 Tier E)");
        if (ImGui::TreeNode("Chrome DeathOverlay##deathoverlay_cp")) {
            if (ImGui::TreeNode("Layout##deathoverlay_l")) {
                ImGui::Checkbox  ("Use Chrome##deathoverlay_l",            &dov->hudUseChrome_);                 checkUndoCapture(uiMgr);
                ImGui::DragFloat ("Border Width##deathoverlay_l",          &dov->chromePanelBorderWidth, 0.1f);  checkUndoCapture(uiMgr);
                ImGui::DragFloat ("Corner Radius##deathoverlay_l",         &dov->chromePanelCornerRadius, 0.5f); checkUndoCapture(uiMgr);
                ImGui::DragFloat2("Shadow Offset##deathoverlay_l",         &dov->chromePanelShadowOffset.x, 0.1f); checkUndoCapture(uiMgr);
                ImGui::DragFloat ("Shadow Blur##deathoverlay_l",           &dov->chromePanelShadowBlur, 0.5f);   checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Bg Color##deathoverlay_l",              &dov->chromePanelBgColor.r);          checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Gradient Top##deathoverlay_l",          &dov->chromePanelGradientTop.r);      checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Gradient Bottom##deathoverlay_l",       &dov->chromePanelGradientBottom.r);   checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Border Color##deathoverlay_l",          &dov->chromePanelBorderColor.r);      checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Shadow Color##deathoverlay_l",          &dov->chromePanelShadowColor.r);      checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Title Bar Color##deathoverlay_l",       &dov->chromeTitleBarColor.r);         checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Title Color##deathoverlay_l",           &dov->chromeTitleColor.r);            checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Backdrop Tint##deathoverlay_l",         &dov->chromeBackdropTintColor.r);     checkUndoCapture(uiMgr);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Form##deathoverlay_cp")) {
                ImGui::ColorEdit4("Form Field Bg##deathoverlay_f",         &dov->chromeFormFieldBgColor.r);      checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Form Field Border##deathoverlay_f",     &dov->chromeFormFieldBorderColor.r);  checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Button Row Bg##deathoverlay_f",         &dov->chromeButtonRowBgColor.r);      checkUndoCapture(uiMgr);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Buttons##deathoverlay_cp")) {
                ImGui::ColorEdit4("Button Bg##deathoverlay_b",             &dov->chromeButtonBgColor.r);         checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Button Border##deathoverlay_b",         &dov->chromeButtonBorderColor.r);     checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Button Text##deathoverlay_b",           &dov->chromeButtonTextColor.r);       checkUndoCapture(uiMgr);
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
    }
    else if (auto* css = dynamic_cast<CharacterSelectScreen*>(selectedNode_)) {
        ImGui::SeparatorText("CharacterSelectScreen");
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
        if (ImGui::TreeNodeEx("Layout##css", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Slot Circle Size##css", &css->slotCircleSize, 1.0f, 20.0f, 120.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Entry Button Width##css", &css->entryButtonWidth, 1.0f, 60.0f, 300.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Spacing##css", &css->slotSpacing, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Bottom Margin##css", &css->slotBottomMargin, 0.5f, 0.0f, 80.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Selected Ring Width##css", &css->selectedRingWidth, 0.1f, 0.5f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Normal Ring Width##css", &css->normalRingWidth, 0.1f, 0.5f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Display Width Ratio##css", &css->displayWidthRatio, 0.01f, 0.2f, 0.9f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Display Height Ratio##css", &css->displayHeightRatio, 0.01f, 0.2f, 0.9f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Display Top Ratio##css", &css->displayTopRatio, 0.01f, 0.0f, 0.5f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Display Border Width##css", &css->displayBorderWidth, 0.1f, 0.0f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name Bg Height##css", &css->nameBgHeight, 0.5f, 16.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name Bg Width Ratio##css", &css->nameBgWidthRatio, 0.01f, 0.3f, 1.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name Text Y##css", &css->nameTextY, 0.5f, 0.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Class Text Y##css", &css->classTextY, 0.5f, 0.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Level Text Y##css", &css->levelTextY, 0.5f, 0.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Preview Scale##css", &css->previewScale, 0.1f, 1.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Preview Center Y##css", &css->previewCenterYRatio, 0.01f, 0.1f, 0.9f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Entry Btn Border##css", &css->entryBtnBorderWidth, 0.1f, 0.0f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Delete Scale##css", &css->deleteScale, 0.01f, 0.3f, 1.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Delete Margin##css", &css->deleteMargin, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Delete Ring Width##css", &css->deleteBtnRingWidth, 0.1f, 0.0f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Dialog Layout##css", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Dialog Offset X##css", &css->dialogOffsetX, 0.5f, -300.0f, 300.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dialog Offset Y##css", &css->dialogOffsetY, 0.5f, -300.0f, 300.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dialog Width##css", &css->dialogWidth, 1.0f, 200.0f, 600.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dialog Height##css", &css->dialogHeight, 1.0f, 150.0f, 400.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dialog Border##css", &css->dialogBorderWidth, 0.1f, 0.0f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dialog Input Height##css", &css->dialogInputHeight, 0.5f, 16.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dialog Input Padding##css", &css->dialogInputPadding, 1.0f, 20.0f, 120.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dialog Input Border##css", &css->dialogInputBorderWidth, 0.1f, 0.0f, 4.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dialog Btn Width##css", &css->dialogBtnWidth, 1.0f, 50.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dialog Btn Height##css", &css->dialogBtnHeight, 0.5f, 20.0f, 50.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dialog Btn Margin##css", &css->dialogBtnMargin, 0.5f, 5.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Ref Name Offset X##css", &css->dialogRefNameOffsetX, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Ref Name Offset Y##css", &css->dialogRefNameOffsetY, 0.5f, 0.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Fonts##css", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Name Font##css", &css->nameFontSize, 0.5f, 8.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Class Font##css", &css->classFontSize, 0.5f, 6.0f, 24.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Level Font##css", &css->levelFontSize, 0.5f, 6.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Empty Prompt Font##css", &css->emptyPromptFontSize, 0.5f, 8.0f, 24.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Plus Font##css", &css->plusFontSize, 0.5f, 10.0f, 36.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Level Font##css", &css->slotLevelFontSize, 0.5f, 6.0f, 18.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Entry Font##css", &css->entryFontSize, 0.5f, 8.0f, 24.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Delete Font##css", &css->deleteFontSize, 0.5f, 6.0f, 18.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dialog Title Font##css", &css->dialogTitleFontSize, 0.5f, 8.0f, 24.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dialog Prompt Font##css", &css->dialogPromptFontSize, 0.5f, 6.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dialog Ref Name Font##css", &css->dialogRefNameFontSize, 0.5f, 8.0f, 24.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dialog Input Font##css", &css->dialogInputFontSize, 0.5f, 6.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Dialog Btn Font##css", &css->dialogBtnFontSize, 0.5f, 6.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##css", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit4("Background##css", &css->backgroundColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Display Bg##css", &css->displayBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Display Border##css", &css->displayBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Name Bg##css", &css->nameBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Name Text##css", &css->nameColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Class Text##css", &css->classColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Level Text##css", &css->levelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Empty Prompt##css", &css->emptyPromptColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Empty Slot##css", &css->emptySlotColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Filled Slot##css", &css->filledSlotColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Selected Ring##css", &css->selectedRingColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Empty Ring##css", &css->emptyRingColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Plus##css", &css->plusColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Slot Level##css", &css->slotLevelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Entry Btn##css", &css->entryBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Entry Btn Border##css", &css->entryBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Delete Btn##css", &css->deleteBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Delete Ring##css", &css->deleteBtnRingColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Dialog Colors##css", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit4("Overlay##css", &css->dialogOverlayColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Dialog Bg##css", &css->dialogBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Dialog Border##css_d", &css->dialogBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Dialog Title##css", &css->dialogTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Dialog Prompt##css", &css->dialogPromptColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Dialog Ref Name##css", &css->dialogRefNameColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Dialog Input Bg##css", &css->dialogInputBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Dialog Input Border##css", &css->dialogInputBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Confirm Active##css", &css->dialogConfirmColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Confirm Disabled##css", &css->dialogConfirmDisabledColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Confirm Disabled Text##css", &css->dialogConfirmDisabledTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Cancel##css", &css->dialogCancelColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Back Button##css", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Width##backbtn", &css->backBtnWidth, 1.0f, 40.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Height##backbtn", &css->backBtnHeight, 0.5f, 20.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Margin X##backbtn", &css->backBtnMarginX, 0.5f, 0.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Margin Y##backbtn", &css->backBtnMarginY, 0.5f, 0.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##backbtn", &css->backBtnBorderWidth, 0.1f, 0.0f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Font Size##backbtn", &css->backBtnFontSize, 0.5f, 6.0f, 24.0f); checkUndoCapture(uiMgr);
            char backTextBuf[64] = {};
            snprintf(backTextBuf, sizeof(backTextBuf), "%s", css->backBtnText.c_str());
            if (ImGui::InputText("Label##backbtn", backTextBuf, sizeof(backTextBuf))) {
                css->backBtnText = backTextBuf;
            }
            checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Text Offset##backbtn", &css->backBtnTextOffset.x, 0.5f, -20.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Button Color##backbtn", &css->backBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##backbtn", &css->backBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text Color##backbtn", &css->backBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        // S128 Tier E chrome (panel + form + button overrides + backdrop)
        ImGui::SeparatorText("CharacterSelectScreen Chrome (S128 Tier E)");
        if (ImGui::TreeNode("Chrome CharacterSelectScreen##charsel_cp")) {
            if (ImGui::TreeNode("Layout##charsel_l")) {
                ImGui::Checkbox  ("Use Chrome##charsel_l",            &css->loginUseChrome_);              checkUndoCapture(uiMgr);
                ImGui::DragFloat ("Border Width##charsel_l",          &css->chromePanelBorderWidth, 0.1f); checkUndoCapture(uiMgr);
                ImGui::DragFloat ("Corner Radius##charsel_l",         &css->chromePanelCornerRadius, 0.5f); checkUndoCapture(uiMgr);
                ImGui::DragFloat2("Shadow Offset##charsel_l",         &css->chromePanelShadowOffset.x, 0.1f); checkUndoCapture(uiMgr);
                ImGui::DragFloat ("Shadow Blur##charsel_l",           &css->chromePanelShadowBlur, 0.5f);  checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Bg Color##charsel_l",              &css->chromePanelBgColor.r);          checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Gradient Top##charsel_l",          &css->chromePanelGradientTop.r);      checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Gradient Bottom##charsel_l",       &css->chromePanelGradientBottom.r);   checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Border Color##charsel_l",          &css->chromePanelBorderColor.r);      checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Shadow Color##charsel_l",          &css->chromePanelShadowColor.r);      checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Title Bar Color##charsel_l",       &css->chromeTitleBarColor.r);         checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Title Color##charsel_l",           &css->chromeTitleColor.r);            checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Backdrop Tint##charsel_l",         &css->chromeBackdropTintColor.r);     checkUndoCapture(uiMgr);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Form##charsel_cp")) {
                ImGui::ColorEdit4("Form Field Bg##charsel_f",         &css->chromeFormFieldBgColor.r);      checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Form Field Border##charsel_f",     &css->chromeFormFieldBorderColor.r);  checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Button Row Bg##charsel_f",         &css->chromeButtonRowBgColor.r);      checkUndoCapture(uiMgr);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Buttons##charsel_cp")) {
                ImGui::ColorEdit4("Button Bg##charsel_b",             &css->chromeButtonBgColor.r);         checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Button Border##charsel_b",         &css->chromeButtonBorderColor.r);     checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Button Text##charsel_b",           &css->chromeButtonTextColor.r);       checkUndoCapture(uiMgr);
                ImGui::TreePop();
            }
            ImGui::TreePop();
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
        ImGui::Text("Status: %s", ccs->statusMessage.c_str());
        if (ImGui::TreeNodeEx("Layout##ccs", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Left Panel Ratio##ccs", &ccs->leftPanelRatio, 0.01f, 0.2f, 0.8f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Preview Scale##ccs", &ccs->previewScale, 0.1f, 1.0f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Header Y##ccs", &ccs->headerY, 0.5f, 0.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Divider Width##ccs", &ccs->dividerWidth, 0.1f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::Separator();
            ImGui::DragFloat("Gender Row Y##ccs", &ccs->genderRowY, 0.5f, 20.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Gender Btn Width##ccs", &ccs->genderBtnWidth, 0.5f, 30.0f, 120.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Gender Btn Height##ccs", &ccs->genderBtnHeight, 0.5f, 16.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Gender Gap##ccs", &ccs->genderGap, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Gender Border##ccs", &ccs->genderBorderWidth, 0.1f, 0.5f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Gender Sel Border##ccs", &ccs->genderSelBorderWidth, 0.1f, 0.5f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::Separator();
            ImGui::DragFloat("Hairstyle Row Y##ccs", &ccs->hairstyleRowY, 0.5f, 40.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Hairstyle Btn Size##ccs", &ccs->hairstyleBtnSize, 0.5f, 16.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Hairstyle Gap##ccs", &ccs->hairstyleGap, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Hairstyle Ring##ccs", &ccs->hairstyleRingWidth, 0.1f, 0.5f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Hairstyle Sel Ring##ccs", &ccs->hairstyleSelRingWidth, 0.1f, 0.5f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Hairstyle Label Gap##ccs", &ccs->hairstyleLabelGap, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::Separator();
            ImGui::DragFloat("Class Row Y##ccs", &ccs->classRowY, 0.5f, 60.0f, 300.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Class Btn Size##ccs", &ccs->classBtnSize, 0.5f, 24.0f, 80.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Class Gap##ccs", &ccs->classGap, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Class Ring##ccs", &ccs->classRingWidth, 0.1f, 0.5f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Class Sel Ring##ccs", &ccs->classSelRingWidth, 0.1f, 0.5f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Class Name Gap##ccs", &ccs->classNameGap, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Class Desc Gap##ccs", &ccs->classDescGap, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Class Desc Pad X##ccs", &ccs->classDescPadX, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::Separator();
            ImGui::DragFloat("Faction Row Y##ccs", &ccs->factionRowY, 0.5f, 100.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Faction Radius##ccs", &ccs->factionRadius, 0.5f, 10.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Faction Gap##ccs", &ccs->factionGap, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Faction Sel Scale##ccs", &ccs->factionSelScale, 0.01f, 1.0f, 2.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Faction Sel Ring##ccs", &ccs->factionSelRingWidth, 0.1f, 0.5f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Faction Name Gap##ccs", &ccs->factionNameGap, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::Separator();
            ImGui::DragFloat("Back Btn Radius##ccs", &ccs->backBtnRadius, 0.5f, 8.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Back Btn Offset X##ccs", &ccs->backBtnOffsetX, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Back Btn Offset Y##ccs", &ccs->backBtnOffsetY, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Back Btn Ring##ccs", &ccs->backBtnRingWidth, 0.1f, 0.5f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::Separator();
            ImGui::DragFloat("Name Field Y##ccs", &ccs->nameFieldY, 0.5f, 150.0f, 600.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name Field Height##ccs", &ccs->nameFieldHeight, 0.5f, 24.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name Field Pad X##ccs", &ccs->nameFieldPadX, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name Field Border##ccs", &ccs->nameFieldBorderWidth, 0.1f, 0.5f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name Field Label Gap##ccs", &ccs->nameFieldLabelGap, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name Field Text Pad##ccs", &ccs->nameFieldTextPad, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name Cursor Width##ccs", &ccs->nameFieldCursorWidth, 0.1f, 0.5f, 4.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name Cursor Pad##ccs", &ccs->nameFieldCursorPad, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::Separator();
            ImGui::DragFloat("Next Btn Height##ccs", &ccs->nextBtnHeight, 0.5f, 24.0f, 64.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Next Btn Bottom Margin##ccs", &ccs->nextBtnBottomMargin, 0.5f, 20.0f, 120.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Next Btn Pad X##ccs", &ccs->nextBtnPadX, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Next Btn Border##ccs", &ccs->nextBtnBorderWidth, 0.1f, 0.5f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Status Gap##ccs", &ccs->statusGap, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Fonts##ccs", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Header Font##ccs", &ccs->headerFontSize, 0.5f, 8.0f, 36.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Class Font##ccs", &ccs->classFontSize, 0.5f, 8.0f, 36.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Class Initial Font##ccs", &ccs->classInitialFontSize, 0.5f, 8.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Desc Font##ccs", &ccs->descFontSize, 0.5f, 6.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button Font##ccs", &ccs->buttonFontSize, 0.5f, 6.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Label Font##ccs", &ccs->labelFontSize, 0.5f, 6.0f, 18.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name Label Font##ccs", &ccs->nameLabelFontSize, 0.5f, 6.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name Input Font##ccs", &ccs->nameInputFontSize, 0.5f, 8.0f, 24.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Next Btn Font##ccs", &ccs->nextBtnFontSize, 0.5f, 8.0f, 24.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Status Font##ccs", &ccs->statusFontSize, 0.5f, 6.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Faction Initial Font##ccs", &ccs->factionInitialFontSize, 0.5f, 6.0f, 18.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Faction Name Font##ccs", &ccs->factionNameFontSize, 0.5f, 6.0f, 18.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Back Btn Font##ccs", &ccs->backBtnFontSize, 0.5f, 8.0f, 24.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##ccs", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit4("Background##ccs", &ccs->backgroundColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Left Panel##ccs", &ccs->leftPanelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Header Text##ccs", &ccs->headerColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Selected##ccs", &ccs->selectedColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Selected Bg##ccs", &ccs->selectedBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Unselected Bg##ccs", &ccs->unselectedBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Unselected Border##ccs", &ccs->unselectedBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Unselected Text##ccs", &ccs->unselectedTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Label##ccs", &ccs->labelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Description##ccs", &ccs->descColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Name Field Bg##ccs", &ccs->nameFieldBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Name Field Focus Bg##ccs", &ccs->nameFieldFocusBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Name Field Border##ccs", &ccs->nameFieldBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Name Field Focus Border##ccs", &ccs->nameFieldFocusBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Name Label##ccs", &ccs->nameLabelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Placeholder##ccs", &ccs->placeholderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Next Btn##ccs", &ccs->nextBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Next Btn Border##ccs", &ccs->nextBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Back Btn##ccs", &ccs->backBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Back Btn Border##ccs", &ccs->backBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Error##ccs", &ccs->errorColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Success##ccs", &ccs->successColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        // S128 Tier E chrome (panel + form + button overrides + backdrop)
        ImGui::SeparatorText("CharacterCreationScreen Chrome (S128 Tier E)");
        if (ImGui::TreeNode("Chrome CharacterCreationScreen##charcreate_cp")) {
            if (ImGui::TreeNode("Layout##charcreate_l")) {
                ImGui::Checkbox  ("Use Chrome##charcreate_l",            &ccs->loginUseChrome_);              checkUndoCapture(uiMgr);
                ImGui::DragFloat ("Border Width##charcreate_l",          &ccs->chromePanelBorderWidth, 0.1f); checkUndoCapture(uiMgr);
                ImGui::DragFloat ("Corner Radius##charcreate_l",         &ccs->chromePanelCornerRadius, 0.5f); checkUndoCapture(uiMgr);
                ImGui::DragFloat2("Shadow Offset##charcreate_l",         &ccs->chromePanelShadowOffset.x, 0.1f); checkUndoCapture(uiMgr);
                ImGui::DragFloat ("Shadow Blur##charcreate_l",           &ccs->chromePanelShadowBlur, 0.5f);  checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Bg Color##charcreate_l",              &ccs->chromePanelBgColor.r);          checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Gradient Top##charcreate_l",          &ccs->chromePanelGradientTop.r);      checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Gradient Bottom##charcreate_l",       &ccs->chromePanelGradientBottom.r);   checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Border Color##charcreate_l",          &ccs->chromePanelBorderColor.r);      checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Shadow Color##charcreate_l",          &ccs->chromePanelShadowColor.r);      checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Title Bar Color##charcreate_l",       &ccs->chromeTitleBarColor.r);         checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Title Color##charcreate_l",           &ccs->chromeTitleColor.r);            checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Backdrop Tint##charcreate_l",         &ccs->chromeBackdropTintColor.r);     checkUndoCapture(uiMgr);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Form##charcreate_cp")) {
                ImGui::ColorEdit4("Form Field Bg##charcreate_f",         &ccs->chromeFormFieldBgColor.r);      checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Form Field Border##charcreate_f",     &ccs->chromeFormFieldBorderColor.r);  checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Button Row Bg##charcreate_f",         &ccs->chromeButtonRowBgColor.r);      checkUndoCapture(uiMgr);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Buttons##charcreate_cp")) {
                ImGui::ColorEdit4("Button Bg##charcreate_b",             &ccs->chromeButtonBgColor.r);         checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Button Border##charcreate_b",         &ccs->chromeButtonBorderColor.r);     checkUndoCapture(uiMgr);
                ImGui::ColorEdit4("Button Text##charcreate_b",           &ccs->chromeButtonTextColor.r);       checkUndoCapture(uiMgr);
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
    }
    else if (auto* gp = dynamic_cast<GuildPanel*>(selectedNode_)) {
        ImGui::SeparatorText("GuildPanel");
        if (ImGui::TreeNodeEx("Position Offsets##gp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##gpo", &gp->titleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Emblem##gpo", &gp->emblemOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Guild Info##gpo", &gp->guildInfoOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Roster##gpo", &gp->rosterOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Font Sizes##gp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title##gpf", &gp->titleFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Guild Name##gpf", &gp->guildNameFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Info##gpf", &gp->infoFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Roster Header##gpf", &gp->rosterHeaderFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Roster Row##gpf", &gp->rosterRowFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout##gp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Header Height##gpl", &gp->headerHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Emblem Size##gpl", &gp->emblemSize, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Close Radius##gpl", &gp->closeRadius, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Roster Header Height##gpl", &gp->rosterHeaderHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Roster Row Height##gpl", &gp->rosterRowHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##gpl", &gp->borderWidth, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##gp", 0)) {
            ImGui::ColorEdit4("Background##gpc", &gp->bgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##gpc", &gp->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##gpc", &gp->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close BG##gpc", &gp->closeBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Border##gpc", &gp->closeBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Divider##gpc", &gp->dividerColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Guild Name##gpc", &gp->guildNameColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Info##gpc", &gp->infoColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Roster Header BG##gpc", &gp->rosterHeaderBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Roster Header Text##gpc", &gp->rosterHeaderTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Roster Row Text##gpc", &gp->rosterRowTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Online##gpc", &gp->onlineColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Offline##gpc", &gp->offlineColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel##gppc", &gp->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        if (ImGui::TreeNodeEx("Chrome Panel##gp", 0)) {
            ImGui::DragFloat("Border Width##gpcp", &gp->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##gpcp", &gp->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##gpcp", &gp->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##gpcp", &gp->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##gpcp", &gp->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##gpcp", &gp->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##gpcp", &gp->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##gpcp", &gp->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##gpcp", &gp->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##gpcp", &gp->chromeTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Bg##gpcp", &gp->chromeCloseBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Border##gpcp", &gp->chromeCloseBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Text##gpcp", &gp->chromeCloseBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
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
        if (ImGui::TreeNodeEx("Position Offsets##ndp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##ndpo", &ndp->titleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Text##ndpo", &ndp->textOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Buttons##ndpo", &ndp->buttonOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Close##ndpo", &ndp->closeOffset.x, 0.5f, -1000.0f, 1000.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Font Sizes##ndp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title##ndpf", &ndp->titleFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Body##ndpf", &ndp->bodyFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button##ndpf", &ndp->buttonFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Close##ndpf", &ndp->closeFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Quest Name##ndpf", &ndp->questNameFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Quest Status##ndpf", &ndp->questStatusFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout##ndp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title Bar Height##ndpl", &ndp->titleBarHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button Height##ndpl", &ndp->buttonHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button Margin##ndpl", &ndp->buttonMargin, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button Gap##ndpl", &ndp->buttonGap, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Quest Row Height##ndpl", &ndp->questRowHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Close Circle Radius##ndpl", &ndp->closeCircleRadius, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##ndpl", &ndp->borderWidth, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##ndp", 0)) {
            ImGui::ColorEdit4("Background##ndpc", &ndp->bgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##ndpc", &ndp->textColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##ndpc", &ndp->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gold##ndpc", &ndp->goldColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Button BG##ndpc", &ndp->buttonBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Button Border##ndpc", &ndp->buttonBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close BG##ndpc", &ndp->closeBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Divider##ndpc", &ndp->dividerColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
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
        ImGui::Checkbox("Has Arena##ndp", &ndp->hasArena); checkUndoCapture(uiMgr);
        ImGui::Checkbox("Has Battlefield##ndp", &ndp->hasBattlefield); checkUndoCapture(uiMgr);
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
        ImGui::Checkbox("Use Chrome (Checkpoint 1)##ndp", &ndp->useChrome_);
        checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Quests: %zu", ndp->quests.size());
    }
    else if (auto* sp2 = dynamic_cast<ShopPanel*>(selectedNode_)) {
        ImGui::SeparatorText("ShopPanel");
        ImGui::Checkbox("Use Chrome (Checkpoint 4)##sp", &sp2->useChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Shop Chrome (Checkpoint 4) toggle.");

        if (ImGui::TreeNodeEx("Chrome Panel Layout##shp", 0)) {
            ImGui::DragFloat("Border Width##shpcpl", &sp2->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##shpcpl", &sp2->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##shpcpl", &sp2->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##shpcpl", &sp2->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Panel Colors##shp", 0)) {
            ImGui::ColorEdit4("Background##shpcpc", &sp2->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##shpcpc", &sp2->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##shpcpc", &sp2->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##shpcpc", &sp2->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##shpcpc", &sp2->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Header BG##shpcpc", &sp2->chromeHeaderBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##shpcpc", &sp2->chromeTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Close Button##shp", 0)) {
            ImGui::ColorEdit4("Bg##shpccb", &sp2->chromeCloseBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##shpccb", &sp2->chromeCloseBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##shpccb", &sp2->chromeCloseBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Category Tab Rail##shpct", 0)) {
            ImGui::DragFloat("Bar Height##shpct", &sp2->categoryTabBarHeight, 0.5f, 16.0f, 80.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Font Size##shpct", &sp2->categoryTabFontSize, 0.5f, 8.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Padding X##shpct", &sp2->categoryTabPaddingX, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Gap##shpct", &sp2->categoryTabGap, 0.5f, 0.0f, 24.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("BG##shpct",          &sp2->categoryTabBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Active BG##shpct",    &sp2->categoryTabActiveColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##shpct",         &sp2->categoryTabTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Active Text##shpct",  &sp2->categoryTabActiveTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Position Offsets##shp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##shpo", &sp2->titleOffset.x, 0.5f, -500.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Sub-Header##shpo", &sp2->shopListOffset.x, 0.5f, -500.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Gold Bar##shpo", &sp2->goldOffset.x, 0.5f, -500.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Item Name##shpo", &sp2->itemNameOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Price##shpo", &sp2->priceOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Stock##shpo", &sp2->stockOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Font Sizes##shp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title##shpf", &sp2->titleFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Sub-Header##shpf", &sp2->subHeaderFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Item Name##shpf", &sp2->itemFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Price##shpf", &sp2->priceFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Gold Bar##shpf", &sp2->goldFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Stock##shpf", &sp2->stockFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout##shp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Header Height##shpl", &sp2->headerHeight, 1.0f, 0.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Row Height##shpl", &sp2->rowHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Gold Bar Height##shpl", &sp2->goldBarHeight, 1.0f, 0.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Buy Btn Width##shpl", &sp2->buyBtnWidth, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Buy Btn Height##shpl", &sp2->buyBtnHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Content Padding##shpl", &sp2->contentPadding, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Sub-Header Height##shpl", &sp2->subHeaderHeight, 1.0f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##shpl", &sp2->panelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##shp", 0)) {
            ImGui::ColorEdit4("Background##shpc", &sp2->bgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##shpc", &sp2->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##shpc", &sp2->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Header BG##shpc", &sp2->headerBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##shpc", &sp2->textColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gold Text##shpc", &sp2->goldColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Price Text##shpc2", &sp2->priceColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Stock Text##shpc2", &sp2->stockColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Sub-Header##shpc2", &sp2->subHeaderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Buy Btn##shpc", &sp2->buyBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Buy Disabled##shpc", &sp2->buyBtnDisabledColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Divider##shpc", &sp2->dividerColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Error##shpc", &sp2->errorColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Row BG##shpc2", &sp2->rowBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Row Alt BG##shpc2", &sp2->rowAltBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gold Bar BG##shpc2", &sp2->goldBarBgColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Close Button##shp", 0)) {
            ImGui::DragFloat("Radius##shpcb", &sp2->closeBtnRadius, 0.5f, 4.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Offset##shpcb", &sp2->closeBtnOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##shpcb", &sp2->closeBtnBorderW, 0.25f, 0.0f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Font Size##shpcb", &sp2->closeBtnFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bg Color##shpcb", &sp2->closeBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##shpcb", &sp2->closeBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text Color##shpcb", &sp2->closeBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Buy Button##shp", 0)) {
            ImGui::ColorEdit4("Border##shpbb", &sp2->buyBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Disabled Border##shpbb", &sp2->buyBtnDisabledBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##shpbb", &sp2->buyBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Disabled Text##shpbb", &sp2->buyBtnDisabledTextColor.r); checkUndoCapture(uiMgr);
            char buyLblBuf[64] = {};
            snprintf(buyLblBuf, sizeof(buyLblBuf), "%s", sp2->buyBtnLabel.c_str());
            if (ImGui::InputText("Label##shpbb", buyLblBuf, sizeof(buyLblBuf))) {
                sp2->buyBtnLabel = buyLblBuf;
            }
            checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Sell Popup##shp", 0)) {
            ImGui::DragFloat("Width##shpsp", &sp2->confirmPopupW, 1.0f, 100.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Height##shpsp", &sp2->confirmPopupH, 1.0f, 60.0f, 400.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Width##shpsp", &sp2->confirmBtnW, 1.0f, 30.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Height##shpsp", &sp2->confirmBtnH, 1.0f, 12.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Qty Btn Size##shpsp", &sp2->confirmQtyBtnSize, 1.0f, 10.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##shpsp", &sp2->confirmBorderW, 0.25f, 0.0f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Title Font##shpsp", &sp2->confirmTitleFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Qty Font##shpsp", &sp2->confirmQtyFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Price Font##shpsp", &sp2->confirmPriceFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Font##shpsp", &sp2->confirmBtnFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Qty Btn Font##shpsp", &sp2->confirmQtyBtnFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bg##shpsp", &sp2->confirmBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##shpsp", &sp2->confirmBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Confirm Btn##shpsp", &sp2->confirmBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Cancel Btn##shpsp", &sp2->cancelBtnColor.r); checkUndoCapture(uiMgr);
            char confLblBuf[64] = {};
            snprintf(confLblBuf, sizeof(confLblBuf), "%s", sp2->confirmBtnLabel.c_str());
            if (ImGui::InputText("Confirm Label##shpsp", confLblBuf, sizeof(confLblBuf))) {
                sp2->confirmBtnLabel = confLblBuf;
            }
            checkUndoCapture(uiMgr);
            char cancLblBuf[64] = {};
            snprintf(cancLblBuf, sizeof(cancLblBuf), "%s", sp2->cancelBtnLabel.c_str());
            if (ImGui::InputText("Cancel Label##shpsp", cancLblBuf, sizeof(cancLblBuf))) {
                sp2->cancelBtnLabel = cancLblBuf;
            }
            checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        char shopBuf[128] = {};
        snprintf(shopBuf, sizeof(shopBuf), "%s", sp2->shopName.c_str());
        if (ImGui::InputText("Shop Name", shopBuf, sizeof(shopBuf))) {
            sp2->shopName = shopBuf;
        }
        checkUndoCapture(uiMgr);
        char subHdrBuf[128] = {};
        snprintf(subHdrBuf, sizeof(subHdrBuf), "%s", sp2->subHeaderLabel.c_str());
        if (ImGui::InputText("Sub-Header Label", subHdrBuf, sizeof(subHdrBuf))) {
            sp2->subHeaderLabel = subHdrBuf;
        }
        checkUndoCapture(uiMgr);
        char goldPrefBuf[64] = {};
        snprintf(goldPrefBuf, sizeof(goldPrefBuf), "%s", sp2->goldLabelPrefix.c_str());
        if (ImGui::InputText("Gold Label Prefix", goldPrefBuf, sizeof(goldPrefBuf))) {
            sp2->goldLabelPrefix = goldPrefBuf;
        }
        checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Text("Shop Items: %zu", sp2->shopItems.size());
        ImGui::Text("NPC: %u", sp2->npcId);
    }
    else if (auto* bp = dynamic_cast<BankPanel*>(selectedNode_)) {
        ImGui::SeparatorText("BankPanel");
        if (ImGui::TreeNodeEx("Position Offsets##bnk", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##bnko", &bp->titleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Bank List##bnko", &bp->bankListOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Inventory##bnko", &bp->inventoryOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Gold##bnko", &bp->goldOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Font Sizes##bnk", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title##bnkf", &bp->titleFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Header##bnkf", &bp->headerFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Body##bnkf", &bp->bodyFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Small##bnkf", &bp->smallFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout##bnk", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Header Height##bnkl", &bp->headerHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Bottom Bar Height##bnkl", &bp->bottomBarHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Row Height##bnkl", &bp->rowHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Size##bnkl", &bp->slotSize, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##bnkl", &bp->borderWidth, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##bnk", 0)) {
            ImGui::ColorEdit4("Background##bnkc", &bp->bgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##bnkc", &bp->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##bnkc", &bp->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##bnkc", &bp->textColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gold##bnkc", &bp->goldColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Button##bnkc", &bp->buttonColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Withdraw##bnkc", &bp->withdrawBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Deposit##bnkc", &bp->depositBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Slot BG##bnkc", &bp->slotBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Divider##bnkc", &bp->dividerColorVal.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel (Checkpoint 6)##bnkpc", &bp->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        ImGui::Separator();

        if (ImGui::TreeNodeEx("Chrome Panel Layout##bnk", 0)) {
            ImGui::DragFloat("Border Width##bnkcpl", &bp->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##bnkcpl", &bp->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##bnkcpl", &bp->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##bnkcpl", &bp->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Panel Colors##bnk", 0)) {
            ImGui::ColorEdit4("Background##bnkcpc", &bp->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##bnkcpc", &bp->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##bnkcpc", &bp->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##bnkcpc", &bp->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##bnkcpc", &bp->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar BG##bnkcpc", &bp->chromeTitleBarBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##bnkcpc", &bp->chromeTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Close Button##bnk", 0)) {
            ImGui::ColorEdit4("Bg##bnkccb", &bp->chromeCloseBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##bnkccb", &bp->chromeCloseBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

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
        if (ImGui::TreeNodeEx("Position Offsets##tp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##tpo", &tp->titleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Label##tpo", &tp->labelOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Rows##tpo", &tp->rowOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Gold##tpo", &tp->goldOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        char titleBuf[128] = {};
        snprintf(titleBuf, sizeof(titleBuf), "%s", tp->title.c_str());
        if (ImGui::InputText("Title##tp", titleBuf, sizeof(titleBuf))) {
            tp->title = titleBuf;
        }
        checkUndoCapture(uiMgr);
        if (ImGui::TreeNodeEx("Layout##tp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title Font Size##tp", &tp->titleFontSize, 0.5f, 8.0f, 24.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name Font Size##tp", &tp->nameFontSize, 0.5f, 8.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Cost Font Size##tp", &tp->costFontSize, 0.5f, 6.0f, 18.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Label Font Size##tp", &tp->labelFontSize, 0.5f, 6.0f, 18.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Gold Font Size##tp", &tp->goldFontSize, 0.5f, 8.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Row Height##tp", &tp->rowHeight, 0.5f, 24.0f, 64.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##tp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit4("Background##tp", &tp->backgroundColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##tp", &tp->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar##tp", &tp->titleBarColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Text##tp", &tp->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn##tp", &tp->closeBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Label##tp", &tp->labelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##tp", &tp->textColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gold##tp", &tp->goldColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Disabled##tp", &tp->disabledColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Error##tp", &tp->errorColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel##tppc", &tp->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        if (ImGui::TreeNodeEx("Chrome Panel##tp", 0)) {
            ImGui::DragFloat("Border Width##tpcp", &tp->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##tpcp", &tp->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##tpcp", &tp->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##tpcp", &tp->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##tpcp", &tp->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##tpcp", &tp->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##tpcp", &tp->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##tpcp", &tp->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##tpcp", &tp->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar##tpcp", &tp->chromeTitleBarColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##tpcp", &tp->chromeTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Bg##tpcp", &tp->chromeCloseBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Text##tpcp", &tp->chromeCloseBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::Text("Destinations: %zu", tp->destinations.size());
        for (size_t i = 0; i < tp->destinations.size(); i++) {
            auto& d = tp->destinations[i];
            ImGui::Text("  [%zu] %s (%s) %lld gold Lv%d", i, d.name.c_str(),
                         d.sceneId.c_str(), (long long)d.cost, d.requiredLevel);
        }
    }
    else if (auto* ap = dynamic_cast<ArenaPanel*>(selectedNode_)) {
        ImGui::SeparatorText("ArenaPanel");
        if (ImGui::TreeNodeEx("Position Offsets##ap", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##apo", &ap->titleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Description##apo", &ap->descOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Buttons##apo", &ap->buttonOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Status##apo", &ap->statusOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout##arena", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title Font Size##arena", &ap->titleFontSize, 0.5f, 8.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Body Font Size##arena", &ap->bodyFontSize, 0.5f, 8.0f, 24.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button Height##arena", &ap->buttonHeight, 1.0f, 20.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button Spacing##arena", &ap->buttonSpacing, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##arena", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit4("Background##arena", &ap->backgroundColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##arena", &ap->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar##arena", &ap->titleBarColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Text##arena", &ap->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn##arena", &ap->closeBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Label##arena", &ap->labelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Button##arena", &ap->buttonColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Button Text##arena", &ap->buttonTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Cancel Btn##arena", &ap->cancelBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Registered##arena", &ap->registeredColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Status##arena", &ap->statusColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel##appc", &ap->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        if (ImGui::TreeNodeEx("Chrome Panel##arena", 0)) {
            ImGui::DragFloat("Border Width##apcp", &ap->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##apcp", &ap->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##apcp", &ap->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##apcp", &ap->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##apcp", &ap->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##apcp", &ap->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##apcp", &ap->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##apcp", &ap->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##apcp", &ap->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar##apcp", &ap->chromeTitleBarColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##apcp", &ap->chromeTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Bg##apcp", &ap->chromeCloseBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Text##apcp", &ap->chromeCloseBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::Text("NPC: %u", ap->npcId);
        ImGui::Text("Registered: %s (Mode: %u)", ap->isRegistered ? "Yes" : "No", ap->currentMode);
    }
    else if (auto* bp = dynamic_cast<BattlefieldPanel*>(selectedNode_)) {
        ImGui::SeparatorText("BattlefieldPanel");
        if (ImGui::TreeNodeEx("Position Offsets##bfp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##bfpo", &bp->titleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Description##bfpo", &bp->descOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Buttons##bfpo", &bp->buttonOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Status##bfpo", &bp->statusOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout##bf", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title Font Size##bf", &bp->titleFontSize, 0.5f, 8.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Body Font Size##bf", &bp->bodyFontSize, 0.5f, 8.0f, 24.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button Height##bf", &bp->buttonHeight, 1.0f, 20.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##bf", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit4("Background##bf", &bp->backgroundColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##bf", &bp->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar##bf", &bp->titleBarColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Text##bf", &bp->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn##bf", &bp->closeBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Label##bf", &bp->labelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Button##bf", &bp->buttonColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Button Text##bf", &bp->buttonTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Cancel Btn##bf", &bp->cancelBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Registered##bf", &bp->registeredColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Timer##bf", &bp->timerColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Status##bf", &bp->statusColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel##bfpc", &bp->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        if (ImGui::TreeNodeEx("Chrome Panel##bf", 0)) {
            ImGui::DragFloat("Border Width##bfcp", &bp->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##bfcp", &bp->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##bfcp", &bp->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##bfcp", &bp->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##bfcp", &bp->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##bfcp", &bp->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##bfcp", &bp->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##bfcp", &bp->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##bfcp", &bp->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar##bfcp", &bp->chromeTitleBarColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##bfcp", &bp->chromeTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Bg##bfcp", &bp->chromeCloseBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Text##bfcp", &bp->chromeCloseBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::Text("NPC: %u", bp->npcId);
        ImGui::Text("Registered: %s", bp->isRegistered ? "Yes" : "No");
        if (bp->timeUntilStart > 0) ImGui::Text("Starts in: %us", bp->timeUntilStart);
    }
    else if (auto* pp = dynamic_cast<PetPanel*>(selectedNode_)) {
        ImGui::SeparatorText("PetPanel");
        if (ImGui::TreeNodeEx("Position Offsets##pp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##ppo", &pp->titleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Active Label##ppo", &pp->activeLabelOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Pet Info##ppo", &pp->petInfoOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("List Label##ppo", &pp->listLabelOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout##pet", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title Font Size##pet", &pp->titleFontSize, 0.5f, 8.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name Font Size##pet", &pp->nameFontSize, 0.5f, 8.0f, 24.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Stat Font Size##pet", &pp->statFontSize, 0.5f, 8.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Portrait Size##pet", &pp->portraitSize, 1.0f, 32.0f, 128.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button Height##pet", &pp->buttonHeight, 1.0f, 20.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##pet", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit4("Background##pet", &pp->backgroundColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##pet", &pp->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar##pet", &pp->titleBarColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Text##pet", &pp->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn##pet", &pp->closeBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Label##pet", &pp->labelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Button##pet", &pp->buttonColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Button Text##pet", &pp->buttonTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Unequip Btn##pet", &pp->unequipBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Equipped##pet", &pp->equippedColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("XP Bar Bg##pet", &pp->xpBarBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("XP Bar Fill##pet", &pp->xpBarFillColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Portrait Bg##pet", &pp->portraitBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Selected Bg##pet", &pp->selectedBgColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel##petpc", &pp->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        if (ImGui::TreeNodeEx("Chrome Panel##pet", 0)) {
            ImGui::DragFloat("Border Width##petcp", &pp->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##petcp", &pp->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##petcp", &pp->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##petcp", &pp->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##petcp", &pp->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##petcp", &pp->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##petcp", &pp->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##petcp", &pp->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##petcp", &pp->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar##petcp", &pp->chromeTitleBarColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##petcp", &pp->chromeTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Bg##petcp", &pp->chromeCloseBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Text##petcp", &pp->chromeCloseBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Text("Has Pet: %s", pp->hasPet ? "Yes" : "No");
        if (pp->hasPet) {
            ImGui::Text("Name: %s  Level: %u", pp->petName.c_str(), pp->petLevel);
            ImGui::Text("XP: %d / %d", pp->petXP, pp->petXPToNext);
        }
        ImGui::Text("Owned Pets: %zu", pp->ownedPets.size());
    }
    else if (auto* cp = dynamic_cast<CraftingPanel*>(selectedNode_)) {
        ImGui::SeparatorText("CraftingPanel");
        if (ImGui::TreeNodeEx("Position Offsets##crp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##crpo", &cp->titleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Recipe List##crpo", &cp->recipeListOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Detail##crpo", &cp->detailOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Status##crpo", &cp->statusOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout##craft", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title Font Size##craft", &cp->titleFontSize, 0.5f, 8.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Recipe Font Size##craft", &cp->recipeFontSize, 0.5f, 8.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Size##craft", &cp->slotSize, 1.0f, 20.0f, 64.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Result Slot Size##craft", &cp->resultSlotSize, 1.0f, 24.0f, 80.0f); checkUndoCapture(uiMgr);
            ImGui::DragInt("Ingredient Columns##craft", &cp->ingredientColumns, 1.0f, 2, 6); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##craft", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit4("Background##craft", &cp->backgroundColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##craft", &cp->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar##craft", &cp->titleBarColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Text##craft", &cp->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn##craft", &cp->closeBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Label##craft", &cp->labelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Button##craft", &cp->buttonColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Btn Disabled##craft", &cp->buttonDisabledColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Button Text##craft", &cp->buttonTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Selected##craft", &cp->selectedColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Row##craft", &cp->rowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Has##craft", &cp->hasColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Missing##craft", &cp->missingColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gold##craft", &cp->goldColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Slot Bg##craft", &cp->slotBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Status##craft", &cp->statusColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel##craftpc", &cp->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        if (ImGui::TreeNodeEx("Chrome Panel##craft", 0)) {
            ImGui::DragFloat("Border Width##craftcp", &cp->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##craftcp", &cp->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##craftcp", &cp->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##craftcp", &cp->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##craftcp", &cp->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##craftcp", &cp->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##craftcp", &cp->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##craftcp", &cp->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##craftcp", &cp->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar##craftcp", &cp->chromeTitleBarColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##craftcp", &cp->chromeTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Bg##craftcp", &cp->chromeCloseBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Text##craftcp", &cp->chromeCloseBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::Text("Recipes: %zu", cp->recipes.size());
        ImGui::Text("Selected: %d", cp->selectedRecipe);
    }
    else if (auto* pcm = dynamic_cast<PlayerContextMenu*>(selectedNode_)) {
        ImGui::SeparatorText("PlayerContextMenu");
        if (ImGui::TreeNodeEx("Position Offsets##pcm", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Name##pcmo", &pcm->nameOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Items##pcmo", &pcm->itemOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##pcm", 0)) {
            ImGui::ColorEdit4("Background##pcmc", &pcm->bgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##pcmc", &pcm->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Name Header##pcmc", &pcm->nameHeaderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Separator##pcmc", &pcm->separatorColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Hover##pcmc", &pcm->hoverColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Pressed##pcmc", &pcm->pressedColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Enabled Text##pcmc", &pcm->enabledTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Disabled Text##pcmc", &pcm->disabledTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout##pcm", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Font Size##pcm", &pcm->menuFontSize, 0.5f, 8.0f, 24.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Item Height##pcm", &pcm->itemHeight, 1.0f, 16.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Menu Width##pcm", &pcm->menuWidth, 1.0f, 80.0f, 300.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Header Padding##pcm", &pcm->headerPadding, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##pcm", &pcm->borderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Sep Margin##pcm", &pcm->separatorMargin, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Sep Height##pcm", &pcm->separatorHeight, 0.25f, 0.0f, 4.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Text Padding##pcm", &pcm->itemTextPadding, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::Text("Target: %s", pcm->targetCharName.c_str());
        ImGui::Text("Can Trade: %s", pcm->canTrade ? "Yes" : "No");
        ImGui::Text("Can Add Friend: %s", pcm->canAddFriend ? "Yes" : "No");
        ImGui::Text("Can Guild Invite: %s", pcm->canGuildInvite ? "Yes" : "No");
    }
    else if (auto* tw = dynamic_cast<TradeWindow*>(selectedNode_)) {
        ImGui::SeparatorText("TradeWindow");
        if (ImGui::TreeNodeEx("Position Offsets##tw", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##two", &tw->titleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("My Offer##two", &tw->myOfferOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Their Offer##two", &tw->theirOfferOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Buttons##two", &tw->buttonOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Font Sizes##tw", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title##twf", &tw->titleFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Label##twf", &tw->labelFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Body##twf", &tw->bodyFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Small##twf", &tw->smallFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout##tw", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Header Height##twl", &tw->headerHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button Row Height##twl", &tw->buttonRowHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Padding##twl", &tw->padding, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##twl", &tw->borderWidth, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##tw", 0)) {
            ImGui::ColorEdit4("Background##twc", &tw->bgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##twc", &tw->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##twc", &tw->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Label##twc", &tw->labelColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gold##twc", &tw->goldColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Accept Btn##twc", &tw->acceptBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Cancel Btn##twc", &tw->cancelBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Divider##twc", &tw->dividerColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel##twpc", &tw->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        if (ImGui::TreeNodeEx("Chrome Panel##tw", 0)) {
            ImGui::DragFloat("Border Width##twcp", &tw->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##twcp", &tw->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##twcp", &tw->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##twcp", &tw->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##twcp", &tw->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##twcp", &tw->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##twcp", &tw->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##twcp", &tw->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##twcp", &tw->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##twcp", &tw->chromeTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Bg##twcp", &tw->chromeCloseBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Border##twcp", &tw->chromeCloseBtnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Text##twcp", &tw->chromeCloseBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

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
    else if (auto* col = dynamic_cast<CollectionPanel*>(selectedNode_)) {
        ImGui::SeparatorText("CollectionPanel");
        if (ImGui::TreeNodeEx("Position Offsets##col", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##colo", &col->titleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Tabs##colo", &col->tabOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Entries##colo", &col->entryOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title Font Size", &col->titleFontSize, 0.5f, 8.0f, 36.0f);
            checkUndoCapture(uiMgr);
            ImGui::DragFloat("Entry Font Size", &col->entryFontSize, 0.5f, 8.0f, 36.0f);
            checkUndoCapture(uiMgr);
            ImGui::DragFloat("Reward Font Size", &col->rewardFontSize, 0.5f, 6.0f, 24.0f);
            checkUndoCapture(uiMgr);
            ImGui::DragFloat("Category Tab Height", &col->categoryTabHeight, 0.5f, 16.0f, 48.0f);
            checkUndoCapture(uiMgr);
            ImGui::DragFloat("Entry Height", &col->entryHeight, 0.5f, 24.0f, 80.0f);
            checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width", &col->borderWidth, 0.1f, 0.0f, 8.0f);
            checkUndoCapture(uiMgr);
            ImGui::DragFloat("Header Height", &col->headerHeight, 0.5f, 16.0f, 48.0f);
            checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit4("Background", &col->backgroundColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border", &col->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar", &col->titleBarColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Text", &col->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn", &col->closeBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Tab Active", &col->tabActiveColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Tab Inactive", &col->tabInactiveColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Tab Active Text", &col->tabActiveTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Tab Inactive Text", &col->tabInactiveTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Completed", &col->completedColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Incomplete", &col->incompleteColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Reward", &col->rewardColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Progress", &col->progressColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel##colpc", &col->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        if (ImGui::TreeNodeEx("Chrome Panel##col", 0)) {
            ImGui::DragFloat("Border Width##colcp", &col->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##colcp", &col->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##colcp", &col->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##colcp", &col->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##colcp", &col->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##colcp", &col->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##colcp", &col->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##colcp", &col->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##colcp", &col->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar##colcp", &col->chromeTitleBarColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##colcp", &col->chromeTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Bg##colcp", &col->chromeCloseBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Text##colcp", &col->chromeCloseBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
    }
    else if (auto* cos = dynamic_cast<CostumePanel*>(selectedNode_)) {
        ImGui::SeparatorText("CostumePanel");
        if (ImGui::TreeNodeEx("Position Offsets##cos", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##coso", &cos->titleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Toggle##coso", &cos->toggleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Grid##coso", &cos->gridOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Info##coso", &cos->infoOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title Font Size", &cos->titleFontSize, 0.5f, 8.0f, 36.0f);
            checkUndoCapture(uiMgr);
            ImGui::DragFloat("Body Font Size", &cos->bodyFontSize, 0.5f, 8.0f, 36.0f);
            checkUndoCapture(uiMgr);
            ImGui::DragFloat("Info Font Size", &cos->infoFontSize, 0.5f, 6.0f, 24.0f);
            checkUndoCapture(uiMgr);
            ImGui::DragInt("Grid Columns", &cos->gridCols, 0.1f, 2, 8);
            checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Size", &cos->slotSize, 0.5f, 24.0f, 96.0f);
            checkUndoCapture(uiMgr);
            ImGui::DragFloat("Slot Spacing", &cos->slotSpacing, 0.5f, 0.0f, 16.0f);
            checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button Height", &cos->buttonHeight, 0.5f, 20.0f, 60.0f);
            checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button Spacing", &cos->buttonSpacing, 0.5f, 0.0f, 16.0f);
            checkUndoCapture(uiMgr);
            ImGui::DragFloat("Filter Tab Height", &cos->filterTabHeight, 0.5f, 16.0f, 40.0f);
            checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width", &cos->borderWidth, 0.1f, 0.0f, 8.0f);
            checkUndoCapture(uiMgr);
            ImGui::DragFloat("Header Height", &cos->headerHeight, 0.5f, 16.0f, 48.0f);
            checkUndoCapture(uiMgr);
            ImGui::DragFloat("Bottom Reserve", &cos->bottomReserveHeight, 0.5f, 30.0f, 120.0f);
            checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit4("Background", &cos->backgroundColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border", &cos->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar", &cos->titleBarColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Text", &cos->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn", &cos->closeBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Tab", &cos->tabColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Tab Active", &cos->tabActiveColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Tab Text", &cos->tabTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Tab Active Text", &cos->tabActiveTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Slot", &cos->slotColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Slot Selected", &cos->slotSelectedColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Equipped Indicator", &cos->equippedIndicatorColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Name", &cos->nameColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Equip Btn", &cos->equipBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Unequip Btn", &cos->unequipBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Button Text", &cos->buttonTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Hint", &cos->hintColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel##cospc", &cos->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        if (ImGui::TreeNodeEx("Chrome Panel##cos", 0)) {
            ImGui::DragFloat("Border Width##coscp", &cos->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##coscp", &cos->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##coscp", &cos->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##coscp", &cos->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##coscp", &cos->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##coscp", &cos->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##coscp", &cos->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##coscp", &cos->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##coscp", &cos->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar##coscp", &cos->chromeTitleBarColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##coscp", &cos->chromeTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Bg##coscp", &cos->chromeCloseBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Text##coscp", &cos->chromeCloseBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
    }
    else if (auto* sp = dynamic_cast<SettingsPanel*>(selectedNode_)) {
        ImGui::SeparatorText("SettingsPanel");

        if (ImGui::TreeNodeEx("Position Offsets##setp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##setpo",   &sp->titleOffset.x,   0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Display##setpo", &sp->displayOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Logout##setpo",  &sp->logoutOffset.x,  0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Font Sizes##setp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title##setfs",   &sp->titleFontSize,   0.5f, 4.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Section##setfs", &sp->sectionFontSize, 0.5f, 4.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Label##setfs",   &sp->labelFontSize,   0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button##setfs",  &sp->buttonFontSize,  0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Button Dimensions##setp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Logout Width",    &sp->logoutButtonWidth,  1.0f, 40.0f, 400.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Logout Height",   &sp->logoutButtonHeight, 1.0f, 16.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius",   &sp->buttonCornerRadius, 0.5f, 0.0f, 20.0f);  checkUndoCapture(uiMgr);
            ImGui::DragFloat("Toggle Width",    &sp->toggleBtnWidth,     1.0f, 20.0f, 120.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Toggle Height",   &sp->toggleBtnHeight,    1.0f, 12.0f, 60.0f);  checkUndoCapture(uiMgr);
            ImGui::DragFloat("Checkbox Size",   &sp->checkboxSize,       0.5f, 8.0f, 30.0f);   checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Spacing##setp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Section Spacing", &sp->sectionSpacing, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Item Spacing",    &sp->itemSpacing,    0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width",    &sp->borderWidth,    0.5f, 0.0f, 10.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Colors##setp", 0)) {
            ImGui::ColorEdit4("Title##setpc",        &sp->titleColor.r);          checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Section##setpc",      &sp->sectionColor.r);        checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Label##setpc",        &sp->labelColor.r);          checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Logout Btn##setpc",   &sp->logoutBtnColor.r);      checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Logout Hover##setpc", &sp->logoutBtnHoverColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Logout Text##setpc",  &sp->logoutTextColor.r);     checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Divider##setpc",      &sp->dividerColor.r);        checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Toggle On##setpc",    &sp->toggleOnColor.r);       checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Toggle Off##setpc",   &sp->toggleOffColor.r);      checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Check On##setpc",     &sp->checkOnColor.r);        checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::SeparatorText("SettingsPanel — Chrome");
        ImGui::Checkbox("panelUseChrome", &sp->panelUseChrome_); checkUndoCapture(uiMgr);
        if (ImGui::TreeNodeEx("Chrome — Panel##sp", 0)) {
            ImGui::DragFloat("chromePanelBorderWidth",   &sp->chromePanelBorderWidth,   0.25f, 0.0f, 16.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("chromePanelCornerRadius",  &sp->chromePanelCornerRadius,  0.5f, 0.0f, 64.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("chromePanelShadowOffset", &sp->chromePanelShadowOffset.x, 0.5f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("chromePanelShadowBlur",    &sp->chromePanelShadowBlur,    0.5f, 0.0f, 64.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromePanelBgColor",        &sp->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromePanelGradientTop",    &sp->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromePanelGradientBottom", &sp->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromePanelBorderColor",    &sp->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromePanelShadowColor",    &sp->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeTitleBarColor",       &sp->chromeTitleBarColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeTitleColor",          &sp->chromeTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome — Bespoke##sp", 0)) {
            ImGui::ColorEdit4("chromeLogoutBgColor",     &sp->chromeLogoutBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeLogoutBorderColor", &sp->chromeLogoutBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeLogoutTextColor",   &sp->chromeLogoutTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeToggleOnBgColor",   &sp->chromeToggleOnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("chromeToggleOffBgColor",  &sp->chromeToggleOffBgColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
    }
    else if (auto* fc = dynamic_cast<FpsCounter*>(selectedNode_)) {
        ImGui::SeparatorText("FpsCounter");
        ImGui::DragFloat2("Text Offset##fpc", &fc->textOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Font Size##fpc", &fc->fontSize, 0.5f, 4.0f, 40.0f); checkUndoCapture(uiMgr);
        ImGui::Checkbox("Show ms##fpc", &fc->showMs); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Text Color##fpc", &fc->textColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Shadow Color##fpc", &fc->shadowColor.r); checkUndoCapture(uiMgr);

        ImGui::Separator();
        if (ImGui::TreeNode("Chrome FpsCounter##fps_cp")) {
            ImGui::Checkbox  ("Use Chrome##fps_cp",                  &fc->hudUseChrome_);                     checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Border Width##fps_cp",                &fc->chromePanelBorderWidth, 0.1f);     checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Corner Radius##fps_cp",               &fc->chromePanelCornerRadius, 0.5f);    checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##fps_cp",               &fc->chromePanelShadowOffset.x, 0.1f);  checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Shadow Blur##fps_cp",                 &fc->chromePanelShadowBlur, 0.5f);      checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bg Color##fps_cp",                    &fc->chromePanelBgColor.r);             checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##fps_cp",                &fc->chromePanelGradientTop.r);         checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##fps_cp",             &fc->chromePanelGradientBottom.r);      checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##fps_cp",                &fc->chromePanelBorderColor.r);         checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow Color##fps_cp",                &fc->chromePanelShadowColor.r);         checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
    }
    else if (auto* lbp = dynamic_cast<LeaderboardPanel*>(selectedNode_)) {
        ImGui::SeparatorText("LeaderboardPanel");
        if (ImGui::TreeNodeEx("Position Offsets##lbp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##lbpo", &lbp->titleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Columns##lbpo", &lbp->columnOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("List##lbpo", &lbp->listOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Page Info##lbpo", &lbp->pageInfoOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Font Sizes##lbp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title##lbpf", &lbp->titleFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Tab##lbpf", &lbp->tabFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Filter##lbpf", &lbp->filterFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Header##lbpf", &lbp->headerFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Row##lbpf", &lbp->rowFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Page##lbpf", &lbp->pageFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Close##lbpf", &lbp->closeFontSize, 0.5f, 4.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout##lbp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title Bar Height##lbpl", &lbp->titleBarHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Cat Tab Height##lbpl", &lbp->catTabHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Faction Btn Height##lbpl", &lbp->facBtnHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Row Height##lbpl", &lbp->rowHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Header Row Height##lbpl", &lbp->headerRowHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Pag Btn Height##lbpl", &lbp->pagBtnHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Pag Btn Width##lbpl", &lbp->pagBtnWidth, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Close Circle Radius##lbpl", &lbp->closeCircleRadius, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##lbpl", &lbp->borderWidth, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Content Padding##lbpl", &lbp->contentPadding, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##lbp", 0)) {
            ImGui::ColorEdit4("Background##lbpc", &lbp->bgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##lbpc", &lbp->textColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##lbpc", &lbp->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##lbpc", &lbp->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Divider##lbpc", &lbp->dividerColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Header BG##lbpc", &lbp->headerBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Row Even##lbpc", &lbp->rowEvenColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Row Odd##lbpc", &lbp->rowOddColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Active BG##lbpc", &lbp->activeBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close BG##lbpc", &lbp->closeBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Rank Gold##lbpc", &lbp->rankGoldColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Rank Silver##lbpc", &lbp->rankSilverColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Rank Bronze##lbpc", &lbp->rankBronzeColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Btn BG##lbpc", &lbp->btnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Btn Border##lbpc", &lbp->btnBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gold Accent##lbpc", &lbp->goldAccentColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close X##lbpc", &lbp->closeXColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel##lbppc", &lbp->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        if (ImGui::TreeNodeEx("Chrome Panel##lbp", 0)) {
            ImGui::DragFloat("Border Width##lbpcp", &lbp->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##lbpcp", &lbp->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##lbpcp", &lbp->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##lbpcp", &lbp->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##lbpcp", &lbp->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##lbpcp", &lbp->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##lbpcp", &lbp->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##lbpcp", &lbp->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##lbpcp", &lbp->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##lbpcp", &lbp->chromeTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Bg##lbpcp", &lbp->chromeCloseBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Text##lbpcp", &lbp->chromeCloseBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::Text("Category: %u", lbp->currentCategory);
        ImGui::Text("Page: %u", lbp->currentPage);
        ImGui::Text("Entries: %zu / %u", lbp->entries.size(), lbp->totalEntries);
    }
    else if (auto* mp = dynamic_cast<MarketPanel*>(selectedNode_)) {
        ImGui::SeparatorText("MarketPanel");
        if (ImGui::TreeNodeEx("Layout##mkt", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Category Width##mktl", &mp->categoryWidth, 1.0f, 0.0f, 400.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Header Height##mktl", &mp->headerHeight, 1.0f, 0.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Row Height##mktl", &mp->rowHeight, 1.0f, 10.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Icon Size##mktl", &mp->iconSize, 1.0f, 8.0f, 128.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Enchant Badge Size##mktl", &mp->enchantBadgeSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Search Bar Height##mktl", &mp->searchBarHeight, 1.0f, 0.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Page Nav Height##mktl", &mp->pageNavHeight, 1.0f, 0.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Category Item Height##mktl", &mp->categoryItemHeight, 0.5f, 12.0f, 80.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Category Spacing##mktl", &mp->categorySpacing, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Row Padding##mktl", &mp->rowPadding, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##mktl", &mp->borderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Content Padding##mktl", &mp->contentPadding, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Title Bar Height##mktl", &mp->titleBarHeight, 1.0f, 10.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragInt("Rows Per Page##mktl", &mp->rowsPerPage, 1, 1, 50); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Font Sizes##mkt", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title##mktf", &mp->titleFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Header##mktf", &mp->headerFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Item Name##mktf", &mp->itemNameFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Enchant##mktf", &mp->enchantFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Seller##mktf", &mp->sellerFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Price##mktf", &mp->priceFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Category##mktf", &mp->categoryFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Search##mktf", &mp->searchFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Page##mktf", &mp->pageFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Status Badge##mktf", &mp->statusBadgeFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Position Offsets##mkt", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##mkto", &mp->titleOffset.x, 0.5f, -500.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Category##mkto", &mp->categoryOffset.x, 0.5f, -500.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Listing Area##mkto", &mp->listingAreaOffset.x, 0.5f, -500.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Item Name##mkto", &mp->itemNameOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Registrant##mkto", &mp->registrantOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Price##mkto", &mp->priceOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Icon##mkto", &mp->iconOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Enchant Badge##mkto", &mp->enchantBadgeOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Search Bar##mkto", &mp->searchBarOffset.x, 0.5f, -500.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Page Nav##mkto", &mp->pageNavOffset.x, 0.5f, -500.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##mkt", 0)) {
            ImGui::ColorEdit4("Background##mktc", &mp->backgroundColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##mktc", &mp->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar##mktc", &mp->titleBarColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##mktc", &mp->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Category BG##mktc", &mp->categoryBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Category Active##mktc", &mp->categoryActiveColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Category Text##mktc", &mp->categoryTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Category Active Text##mktc", &mp->categoryActiveTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Header BG##mktc", &mp->headerBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Header Text##mktc", &mp->headerTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Row Even##mktc", &mp->rowEvenColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Row Odd##mktc", &mp->rowOddColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Row Selected##mktc", &mp->rowSelectedColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Item Name##mktc", &mp->itemNameColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Item Name (Sold)##mktc", &mp->itemNameSoldColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Item Name (Expired)##mktc", &mp->itemNameExpiredColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Enchant Text##mktc", &mp->enchantTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Seller Name##mktc", &mp->sellerNameColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Seller Name (Sold)##mktc", &mp->sellerNameSoldColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Seller Name (Expired)##mktc", &mp->sellerNameExpiredColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Price##mktc", &mp->priceColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Price (Sold)##mktc", &mp->priceSoldColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Price (Expired)##mktc", &mp->priceExpiredColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Search BG##mktc", &mp->searchBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Search Border##mktc", &mp->searchBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Search Text##mktc", &mp->searchTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Search Placeholder##mktc", &mp->searchPlaceholderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Page Text##mktc", &mp->pageTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Page Arrow##mktc", &mp->pageArrowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Page Arrow Disabled##mktc", &mp->pageArrowDisabledColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn##mktc", &mp->closeBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Refresh Btn##mktc", &mp->refreshBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Sold Badge##mktc", &mp->soldBadgeColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Expired Badge##mktc", &mp->expiredBadgeColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Sold Badge Text##mktc", &mp->soldBadgeTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Expired Badge Text##mktc", &mp->expiredBadgeTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Cancel Confirm Layout##mkt", 0)) {
            ImGui::DragFloat("Width Ratio##ccl", &mp->cancelConfirmWidthRatio, 0.01f, 0.1f, 1.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Height Ratio##ccl", &mp->cancelConfirmHeightRatio, 0.01f, 0.1f, 1.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Title Font##ccl", &mp->cancelConfirmTitleFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Body Font##ccl", &mp->cancelConfirmBodyFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Width Ratio##ccl", &mp->cancelConfirmBtnWidthRatio, 0.01f, 0.1f, 0.5f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Height##ccl", &mp->cancelConfirmBtnHeight, 0.5f, 10.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Bottom Margin##ccl", &mp->cancelConfirmBtnBottomMargin, 0.5f, 5.0f, 80.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Spacing##ccl", &mp->cancelConfirmBtnSpacing, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Prompt Y Ratio##ccl", &mp->cancelConfirmPromptYRatio, 0.01f, 0.1f, 0.9f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Dialog Offset##ccl", &mp->cancelConfirmOffset.x, 0.5f, -500.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Title Offset##ccl", &mp->cancelConfirmTitleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Item Name Offset##ccl", &mp->cancelConfirmItemNameOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Cancel Confirm Colors##mkt", 0)) {
            ImGui::ColorEdit4("Dim##ccc", &mp->cancelConfirmDimColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##ccc", &mp->cancelConfirmBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##ccc", &mp->cancelConfirmBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##ccc", &mp->cancelConfirmTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Item Name##ccc", &mp->cancelConfirmItemNameColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Prompt##ccc", &mp->cancelConfirmPromptColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Yes Btn##ccc", &mp->cancelConfirmYesBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("No Btn##ccc", &mp->cancelConfirmNoBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Btn Text##ccc", &mp->cancelConfirmBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Buy Confirm Layout##mkt", 0)) {
            ImGui::DragFloat("Width Ratio##bcl", &mp->buyConfirmWidthRatio, 0.01f, 0.1f, 1.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Height Ratio##bcl", &mp->buyConfirmHeightRatio, 0.01f, 0.1f, 1.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Title Font##bcl", &mp->buyConfirmTitleFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Body Font##bcl", &mp->buyConfirmBodyFontSize, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Width Ratio##bcl", &mp->buyConfirmBtnWidthRatio, 0.01f, 0.1f, 0.5f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Height##bcl", &mp->buyConfirmBtnHeight, 0.5f, 10.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Bottom Margin##bcl", &mp->buyConfirmBtnBottomMargin, 0.5f, 5.0f, 80.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Spacing##bcl", &mp->buyConfirmBtnSpacing, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Prompt Y Ratio##bcl", &mp->buyConfirmPromptYRatio, 0.01f, 0.1f, 0.9f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Dialog Offset##bcl", &mp->buyConfirmOffset.x, 0.5f, -500.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Title Offset##bcl", &mp->buyConfirmTitleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Item Name Offset##bcl", &mp->buyConfirmItemNameOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Buy Confirm Colors##mkt", 0)) {
            ImGui::ColorEdit4("Dim##bcc", &mp->buyConfirmDimColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##bcc", &mp->buyConfirmBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##bcc", &mp->buyConfirmBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##bcc", &mp->buyConfirmTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Item Name##bcc", &mp->buyConfirmItemNameColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Prompt##bcc", &mp->buyConfirmPromptColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Yes Btn##bcc", &mp->buyConfirmYesBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("No Btn##bcc", &mp->buyConfirmNoBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Btn Text##bcc", &mp->buyConfirmBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Tooltip Layout##mkt", 0)) {
            ImGui::Checkbox("Auto Width##mkttt", &mp->tooltipAutoWidth); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Min Width##mkttt", &mp->tooltipWidth, 1.0f, 40.0f, 500.0f); checkUndoCapture(uiMgr);
            if (mp->tooltipAutoWidth) {
                ImGui::DragFloat("Max Width##mkttt", &mp->tooltipMaxWidth, 1.0f, 100.0f, 800.0f); checkUndoCapture(uiMgr);
            }
            ImGui::DragFloat("Padding##mkttt", &mp->tooltipPadding, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Offset##mkttt", &mp->tooltipOffset, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Offset##mkttt", &mp->tooltipShadowOffset, 0.5f, 0.0f, 10.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Line Spacing##mkttt", &mp->tooltipLineSpacing, 0.5f, 0.0f, 10.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##mkttt", &mp->tooltipBorderWidth, 0.25f, 0.0f, 5.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Sep Height##mkttt", &mp->tooltipSepHeight, 0.25f, 0.0f, 5.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name Font##mkttt", &mp->tooltipNameFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Stat Font##mkttt", &mp->tooltipStatFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Level Font##mkttt", &mp->tooltipLevelFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Tooltip Colors##mkt", 0)) {
            ImGui::ColorEdit4("Background##mktttc", &mp->tooltipBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##mktttc", &mp->tooltipBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##mktttc", &mp->tooltipShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Stat Text##mktttc", &mp->tooltipStatColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Separator##mktttc", &mp->tooltipSepColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Level Req##mktttc", &mp->tooltipLevelColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Checkbox("Use Chrome (Checkpoint 5)##mkttt", &mp->tooltipUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Chrome path reads Chrome Tooltip groups below;");
        ImGui::TextDisabled("legacy fields above are ignored when chrome is on.");
        ImGui::Separator();

        if (ImGui::TreeNodeEx("Chrome Tooltip Layout##mkt", 0)) {
            ImGui::DragFloat("Padding##mkcttl", &mp->chromeTooltipPadding, 0.5f, 0.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Row Offset##mkcttl", &mp->chromeTooltipOffset, 0.5f, 0.0f, 32.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Line Spacing##mkcttl", &mp->chromeTooltipLineSpacing, 0.5f, 0.0f, 16.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##mkcttl", &mp->chromeTooltipBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Separator Height##mkcttl", &mp->chromeTooltipSepHeight, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Separator Offset##mkcttl", &mp->chromeTooltipSepOffset.x, 0.5f, -100.0f, 100.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##mkcttl", &mp->chromeTooltipCornerRadius, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##mkcttl", &mp->chromeTooltipShadowOffset.x, 0.5f, -20.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##mkcttl", &mp->chromeTooltipShadowBlur, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Tooltip Fonts##mkt", 0)) {
            ImGui::DragFloat("Name Font##mkcttf", &mp->chromeTooltipNameFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Stat Font##mkcttf", &mp->chromeTooltipStatFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Level Font##mkcttf", &mp->chromeTooltipLevelFontSize, 0.5f, 4.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Tooltip Colors##mkt", 0)) {
            ImGui::ColorEdit4("Background##mkcttc", &mp->chromeTooltipBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##mkcttc", &mp->chromeTooltipGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##mkcttc", &mp->chromeTooltipGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##mkcttc", &mp->chromeTooltipBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##mkcttc", &mp->chromeTooltipShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Stat Text##mkcttc", &mp->chromeTooltipStatColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Separator##mkcttc", &mp->chromeTooltipSepColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Level Req##mkcttc", &mp->chromeTooltipLevelColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Rarity Colors##mkt", 0)) {
            ImGui::ColorEdit4("Common##mkcrar", &mp->chromeRarityCommonColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Uncommon##mkcrar", &mp->chromeRarityUncommonColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Rare##mkcrar", &mp->chromeRarityRareColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Epic##mkcrar", &mp->chromeRarityEpicColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Legendary##mkcrar", &mp->chromeRarityLegendaryColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel##mktpc", &mp->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        if (ImGui::TreeNodeEx("Chrome Panel##mkt", 0)) {
            ImGui::DragFloat("Border Width##mktcp", &mp->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##mktcp", &mp->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##mktcp", &mp->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##mktcp", &mp->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##mktcp", &mp->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##mktcp", &mp->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##mktcp", &mp->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##mktcp", &mp->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##mktcp", &mp->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar##mktcp", &mp->chromeTitleBarColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##mktcp", &mp->chromeTitleColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Text("Listings: %zu", mp->listings.size());
        ImGui::Text("Page: %d / %d", mp->currentPage, mp->totalPages);
    }
    else if (auto* lp = dynamic_cast<LoadingPanel*>(selectedNode_)) {
        ImGui::SeparatorText("LoadingPanel");
        if (ImGui::TreeNodeEx("Layout##lp", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Bar Height", &lp->barHeight, 0.5f, 4.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Bar Pad X", &lp->barPadX, 0.5f, 0.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Bar Bottom Y", &lp->barBottomY, 0.5f, 0.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name Font Size", &lp->nameFontSize, 0.5f, 8.0f, 48.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Pct Font Size", &lp->pctFontSize, 0.5f, 6.0f, 36.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Offset##lp", &lp->shadowOffset, 0.1f, 0.0f, 5.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##lp", ImGuiTreeNodeFlags_None)) {
            ImGui::ColorEdit4("BG##lp", &lp->bgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bar BG##lp", &lp->barBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bar Fill##lp", &lp->barFillColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Name##lp", &lp->nameColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Pct##lp", &lp->pctColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##lp", &lp->shadowColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
    }
    else if (auto* tt = dynamic_cast<Tooltip*>(selectedNode_)) {
        ImGui::SeparatorText("Tooltip");

        char textBuf[512] = {};
        snprintf(textBuf, sizeof(textBuf), "%s", tt->tooltipText.c_str());
        if (ImGui::InputText("Tooltip Text", textBuf, sizeof(textBuf))) tt->tooltipText = textBuf;
        checkUndoCapture(uiMgr);
        ImGui::DragFloat("Max Width", &tt->maxWidth, 1.0f, 50.0f, 800.0f); checkUndoCapture(uiMgr);

        ImGui::Separator();
        ImGui::Checkbox("Use Chrome (Checkpoint 2)##tt", &tt->useChrome_); checkUndoCapture(uiMgr);

        if (ImGui::TreeNodeEx("Chrome Positioning##tt", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Cursor Offset X##tt", &tt->chromeCursorOffsetX, 0.5f, 0.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Cursor Offset Y##tt", &tt->chromeCursorOffsetY, 0.5f, 0.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Padding##tt", &tt->chromePadding.x, 0.5f, 0.0f, 50.0f); checkUndoCapture(uiMgr);
            ImGui::TextDisabled("Padding 0,0 -> use theme");
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Shape##tt", 0)) {
            ImGui::DragFloat("Corner Radius##tt", &tt->chromeCornerRadius, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##tt", &tt->chromeBorderWidth, 0.25f, 0.0f, 6.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##tt", &tt->chromeShadowOffset.x, 0.5f, -20.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##tt", &tt->chromeShadowBlur, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TextDisabled("0 / 0,0 -> use theme");
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Fonts##tt", 0)) {
            ImGui::DragFloat("Font Size##tt", &tt->chromeFontSize, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            char fontBuf[64] = {};
            snprintf(fontBuf, sizeof(fontBuf), "%s", tt->chromeFontName.c_str());
            if (ImGui::InputText("Font Name##tt", fontBuf, sizeof(fontBuf))) tt->chromeFontName = fontBuf;
            checkUndoCapture(uiMgr);
            ImGui::TextDisabled("blank -> use theme");
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Chrome Colors##tt", 0)) {
            ImGui::ColorEdit4("Background##tt",      &tt->chromeBgColor.r);        checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##tt",    &tt->chromeGradientTop.r);    checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##tt", &tt->chromeGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##tt",          &tt->chromeBorderColor.r);    checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##tt",          &tt->chromeShadowColor.r);    checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text##tt",            &tt->chromeTextColor.r);      checkUndoCapture(uiMgr);
            ImGui::TextDisabled("Alpha 0 -> use theme");
            ImGui::TreePop();
        }
    }
    else if (auto* p = dynamic_cast<BountyPanel*>(selectedNode_)) {
        ImGui::SeparatorText("BountyPanel");
        ImGui::DragFloat("Tab Height##bp",    &p->tabHeight,    0.5f, 16.0f, 80.0f);   checkUndoCapture(uiMgr);
        ImGui::DragFloat("Row Height##bp",    &p->rowHeight,    0.5f, 16.0f, 80.0f);   checkUndoCapture(uiMgr);
        ImGui::DragFloat("Header Height##bp", &p->headerHeight, 0.5f, 24.0f, 120.0f);  checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Background##bp",   &p->bgColor.r);     checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Border##bp",       &p->borderColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Row Even##bp",     &p->rowBgEven.r);   checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Row Odd##bp",      &p->rowBgOdd.r);    checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Text##bp",         &p->textColor.r);   checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Gold##bp",         &p->goldColor.r);   checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel##bppc", &p->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        if (ImGui::TreeNodeEx("Chrome Panel##bp", 0)) {
            ImGui::DragFloat("Border Width##bpcp", &p->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##bpcp", &p->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##bpcp", &p->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##bpcp", &p->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##bpcp", &p->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##bpcp", &p->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##bpcp", &p->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##bpcp", &p->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##bpcp", &p->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
    }
    else if (auto* p = dynamic_cast<FriendsPanel*>(selectedNode_)) {
        ImGui::SeparatorText("FriendsPanel");
        ImGui::DragFloat("Tab Height##fp",    &p->tabHeight,    0.5f, 16.0f, 80.0f);   checkUndoCapture(uiMgr);
        ImGui::DragFloat("Row Height##fp",    &p->rowHeight,    0.5f, 16.0f, 80.0f);   checkUndoCapture(uiMgr);
        ImGui::DragFloat("Header Height##fp", &p->headerHeight, 0.5f, 24.0f, 120.0f);  checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Background##fp",   &p->bgColor.r);      checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Border##fp",       &p->borderColor.r);  checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Row Even##fp",     &p->rowBgEven.r);    checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Row Odd##fp",      &p->rowBgOdd.r);     checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Text##fp",         &p->textColor.r);    checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Online##fp",       &p->onlineColor.r);  checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Offline##fp",      &p->offlineColor.r); checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel##fppc", &p->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        if (ImGui::TreeNodeEx("Chrome Panel##fp", 0)) {
            ImGui::DragFloat("Border Width##fpcp", &p->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##fpcp", &p->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##fpcp", &p->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##fpcp", &p->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##fpcp", &p->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##fpcp", &p->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##fpcp", &p->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##fpcp", &p->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##fpcp", &p->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
    }
    else if (auto* w = dynamic_cast<GauntletHUD*>(selectedNode_)) {
        ImGui::SeparatorText("GauntletHUD");
        ImGui::DragFloat("Font Size##gh", &w->fontSize, 0.5f, 8.0f, 32.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Background##gh", &w->bgColor.r);     checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Border##gh",     &w->borderColor.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Text##gh",       &w->textColor.r);   checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Gold##gh",       &w->goldColor.r);   checkUndoCapture(uiMgr);
        ImGui::Checkbox("Active (test)##gh", &w->active);

        if (ImGui::TreeNode("Chrome GauntletHUD##ghud_cp")) {
            ImGui::Checkbox  ("Use Chrome##ghud_cp",                  &w->hudUseChrome_);                     checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Border Width##ghud_cp",                &w->chromePanelBorderWidth, 0.1f);     checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Corner Radius##ghud_cp",               &w->chromePanelCornerRadius, 0.5f);    checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##ghud_cp",               &w->chromePanelShadowOffset.x, 0.1f);  checkUndoCapture(uiMgr);
            ImGui::DragFloat ("Shadow Blur##ghud_cp",                 &w->chromePanelShadowBlur, 0.5f);      checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Bg Color##ghud_cp",                    &w->chromePanelBgColor.r);             checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##ghud_cp",                &w->chromePanelGradientTop.r);         checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##ghud_cp",             &w->chromePanelGradientBottom.r);      checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border Color##ghud_cp",                &w->chromePanelBorderColor.r);         checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow Color##ghud_cp",                &w->chromePanelShadowColor.r);         checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Bar Color##ghud_cp",             &w->chromeTitleBarColor.r);            checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title Color##ghud_cp",                 &w->chromeTitleColor.r);               checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
    }
    else if (auto* w = dynamic_cast<GauntletResultModal*>(selectedNode_)) {
        ImGui::SeparatorText("GauntletResultModal");
        ImGui::DragFloat("Title Font Size##grm", &w->titleFontSize, 0.5f, 12.0f, 48.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Row Font Size##grm",   &w->rowFontSize,   0.5f, 8.0f,  32.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Row Height##grm",      &w->rowHeight,     0.5f, 16.0f, 80.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Background##grm",    &w->bgColor.r);        checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Border##grm",        &w->borderColor.r);    checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Row Even##grm",      &w->rowBgEven.r);      checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Row Odd##grm",       &w->rowBgOdd.r);       checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Text##grm",          &w->textColor.r);      checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Gold##grm",          &w->goldColor.r);      checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Highlight##grm",     &w->highlightColor.r); checkUndoCapture(uiMgr);
        ImGui::Separator();
        ImGui::Checkbox("Use Chrome Panel##grmpc", &w->panelUseChrome_); checkUndoCapture(uiMgr);
        ImGui::TextDisabled("Driven by View > Panel Chrome (Checkpoint 3) toggle.");
        if (ImGui::TreeNodeEx("Chrome Panel##grm", 0)) {
            ImGui::DragFloat("Border Width##grmcp", &w->chromePanelBorderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Corner Radius##grmcp", &w->chromePanelCornerRadius, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Shadow Offset##grmcp", &w->chromePanelShadowOffset.x, 0.5f, -30.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Shadow Blur##grmcp", &w->chromePanelShadowBlur, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Background##grmcp", &w->chromePanelBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Top##grmcp", &w->chromePanelGradientTop.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Gradient Bottom##grmcp", &w->chromePanelGradientBottom.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##grmcp", &w->chromePanelBorderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Shadow##grmcp", &w->chromePanelShadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Bg##grmcp", &w->chromeCloseBtnBgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Close Btn Text##grmcp", &w->chromeCloseBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Checkbox("Visible (test)##grm",  &w->visible);
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
            // sourceDir_ is FATE_SOURCE_DIR/assets/scenes — go up to project root
            std::string projectRoot = sourceDir_;
            auto pos = projectRoot.rfind("/assets/scenes");
            if (pos == std::string::npos) pos = projectRoot.rfind("\\assets\\scenes");
            if (pos != std::string::npos) projectRoot = projectRoot.substr(0, pos);
            std::string srcPath = projectRoot + "/" + relPath;
            UISerializer::saveToFile(srcPath, selectedScreenId_, uiMgr.getScreen(selectedScreenId_));
            LOG_INFO("UI", "Saved screen (source): %s", srcPath.c_str());
        }
        uiMgr.suppressHotReload();
    }
    ImGui::SeparatorText("Bindings");
    if (ImGui::TreeNode("Event Bindings")) {
        auto& map = selectedNode_->eventBindings;
        std::string toRemove, renameKey, renameVal;
        int idx = 0;
        for (auto& [k, v] : map) {
            ImGui::PushID(idx++);
            char kBuf[128] = {}, vBuf[256] = {};
            snprintf(kBuf, sizeof(kBuf), "%s", k.c_str());
            snprintf(vBuf, sizeof(vBuf), "%s", v.c_str());
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputText("##ek", kBuf, sizeof(kBuf))) {
                if (std::string(kBuf) != k) { toRemove = k; renameKey = kBuf; renameVal = v; }
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputText("##ev", vBuf, sizeof(vBuf))) { v = vBuf; }
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) { toRemove = k; }
            checkUndoCapture(uiMgr);
            ImGui::PopID();
        }
        if (!toRemove.empty()) { map.erase(toRemove); if (!renameKey.empty()) map[renameKey] = renameVal; }
        if (ImGui::SmallButton("+ Event")) { map["new_event"] = "handler"; }
        checkUndoCapture(uiMgr);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Data Bindings")) {
        auto& map = selectedNode_->dataBindings;
        std::string toRemove, renameKey, renameVal;
        int idx = 1000;
        for (auto& [k, v] : map) {
            ImGui::PushID(idx++);
            char kBuf[128] = {}, vBuf[256] = {};
            snprintf(kBuf, sizeof(kBuf), "%s", k.c_str());
            snprintf(vBuf, sizeof(vBuf), "%s", v.c_str());
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputText("##dk", kBuf, sizeof(kBuf))) {
                if (std::string(kBuf) != k) { toRemove = k; renameKey = kBuf; renameVal = v; }
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputText("##dv", vBuf, sizeof(vBuf))) { v = vBuf; }
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) { toRemove = k; }
            checkUndoCapture(uiMgr);
            ImGui::PopID();
        }
        if (!toRemove.empty()) { map.erase(toRemove); if (!renameKey.empty()) map[renameKey] = renameVal; }
        if (ImGui::SmallButton("+ Binding")) { map["new_key"] = "value"; }
        checkUndoCapture(uiMgr);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Custom Properties")) {
        auto& map = selectedNode_->properties;
        std::string toRemove, renameKey, renameVal;
        int idx = 2000;
        for (auto& [k, v] : map) {
            ImGui::PushID(idx++);
            char kBuf[128] = {}, vBuf[256] = {};
            snprintf(kBuf, sizeof(kBuf), "%s", k.c_str());
            snprintf(vBuf, sizeof(vBuf), "%s", v.c_str());
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputText("##pk", kBuf, sizeof(kBuf))) {
                if (std::string(kBuf) != k) { toRemove = k; renameKey = kBuf; renameVal = v; }
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputText("##pv", vBuf, sizeof(vBuf))) { v = vBuf; }
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) { toRemove = k; }
            checkUndoCapture(uiMgr);
            ImGui::PopID();
        }
        if (!toRemove.empty()) { map.erase(toRemove); if (!renameKey.empty()) map[renameKey] = renameVal; }
        if (ImGui::SmallButton("+ Property")) { map["new_prop"] = "value"; }
        checkUndoCapture(uiMgr);
        ImGui::TreePop();
    }

#endif // FATE_HAS_GAME

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
    ImGui::DragFloat2("Offset %", &anchor.offsetPercent.x, 0.01f, 0.0f, 1.0f); checkUndoCapture(uiMgr);
    ImGui::DragFloat2("Size %", &anchor.sizePercent.x, 0.01f, 0.0f, 1.0f); checkUndoCapture(uiMgr);

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

    ImGui::Separator();
    ImGui::Text("Responsive");

    float minVals[2] = {anchor.minSize.x, anchor.minSize.y};
    if (ImGui::DragFloat2("Min Size", minVals, 1.0f, 0.0f, 4000.0f)) {
        anchor.minSize.x = minVals[0]; anchor.minSize.y = minVals[1];
    }
    checkUndoCapture(uiMgr);

    float maxVals[2] = {anchor.maxSize.x, anchor.maxSize.y};
    if (ImGui::DragFloat2("Max Size", maxVals, 1.0f, 0.0f, 4000.0f)) {
        anchor.maxSize.x = maxVals[0]; anchor.maxSize.y = maxVals[1];
    }
    checkUndoCapture(uiMgr);

    ImGui::Checkbox("Use Safe Area", &anchor.useSafeArea);
    checkUndoCapture(uiMgr);

    ImGui::DragFloat("Max Aspect Ratio", &anchor.maxAspectRatio, 0.01f, 0.0f, 4.0f);
    checkUndoCapture(uiMgr);

    // ------------------------------------------------------------------
    // Child Layout — auto-divider between siblings + flow hint.
    // Does NOT reposition children; it only paints separators along the
    // chosen axis. Use with children whose anchors already lay them out
    // horizontally or vertically.
    // ------------------------------------------------------------------
    ImGui::SeparatorText("Child Layout");
    ChildLayout& cl = node->childLayout();
    const char* flowNames[] = {"None", "Horizontal", "Vertical"};
    int flowIdx = static_cast<int>(cl.flowDirection);
    if (ImGui::Combo("Flow Direction##cl", &flowIdx, flowNames, 3)) {
        cl.flowDirection = static_cast<ChildFlow>(flowIdx);
    }
    checkUndoCapture(uiMgr);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Tells the renderer which axis to scan when drawing auto-dividers between siblings.");

    ImGui::Checkbox("Draw Divider Between Children##cl", &cl.drawDivider); checkUndoCapture(uiMgr);
    if (cl.drawDivider) {
        ImGui::ColorEdit4("Divider Color##cl", &cl.dividerColor.r); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Divider Thickness##cl", &cl.dividerThickness, 0.1f, 0.0f, 20.0f, "%.1f px");
        checkUndoCapture(uiMgr);
        ImGui::DragFloat("Divider Padding##cl", &cl.dividerPadding, 0.5f, 0.0f, 200.0f, "%.1f px");
        checkUndoCapture(uiMgr);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Inset from the parent's cross-axis edges (top/bottom for horizontal flow, left/right for vertical flow).");
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

    // Border style (Solid/Double active; Dashed/Inset fall back to Solid until
    // a line-style renderer lands).
    {
        const char* borderStyleNames[] = {"Solid", "Double", "Dashed (TBD)", "Inset (TBD)"};
        int idx = static_cast<int>(style.borderStyle);
        if (ImGui::Combo("Border Style", &idx, borderStyleNames, IM_ARRAYSIZE(borderStyleNames))) {
            style.borderStyle = static_cast<BorderStyle>(idx);
        }
        checkUndoCapture(uiMgr);
    }

    ImGui::ColorEdit4("Text Color", &style.textColor.r); checkUndoCapture(uiMgr);
    ImGui::DragFloat("Font Size", &style.fontSize, 0.5f, 6.0f, 72.0f); checkUndoCapture(uiMgr);
    ImGui::DragFloat("Opacity", &style.opacity, 0.01f, 0.0f, 1.0f); checkUndoCapture(uiMgr);
    ImGui::DragFloat("BG Tint Opacity", &style.backgroundTintOpacity, 0.01f, 0.0f, 1.0f,
                     "%.2f"); checkUndoCapture(uiMgr);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fades only the background fill — border/gradient/shadow stay at full alpha.");

    // --- Font: Family + Weight ---
    // Family dropdown lists unique families across the FontRegistry. Weight
    // dropdown narrows to variants actually registered for that family. When
    // the selected family has only one weight, the Weight combo is a single
    // entry (cosmetically still useful — shows what's baked). Falls back to
    // a flat Font Name combo below for any legacy name that doesn't map.
    {
        auto& reg = FontRegistry::instance();
        auto families = reg.families();

        // Resolve current selection from style.fontName — look up its SDFFont
        // and read back family/weight.
        std::string currentFamily, currentWeight;
        if (!style.fontName.empty()) {
            if (auto* f = reg.getFont(style.fontName)) {
                currentFamily = f->family;
                currentWeight = f->weight;
            }
        }

        // Family combo (index 0 = "(default)").
        int famIdx = 0;
        for (int i = 0; i < static_cast<int>(families.size()); ++i) {
            if (families[i] == currentFamily) { famIdx = i + 1; break; }
        }
        auto famGetter = [](void* data, int idx, const char** out) -> bool {
            auto* v = static_cast<std::vector<std::string>*>(data);
            if (idx == 0) { *out = "(default)"; return true; }
            if (idx - 1 < static_cast<int>(v->size())) { *out = (*v)[idx - 1].c_str(); return true; }
            return false;
        };
        if (ImGui::Combo("Font Family", &famIdx, famGetter, &families,
                         static_cast<int>(families.size()) + 1)) {
            if (famIdx == 0) {
                style.fontName = "";
                currentFamily.clear();
                currentWeight.clear();
            } else {
                currentFamily = families[famIdx - 1];
                auto weights = reg.weightsForFamily(currentFamily);
                std::string pick = weights.empty() ? std::string() : weights.front();
                if (auto* f = reg.getByFamilyWeight(currentFamily, pick)) {
                    style.fontName = f->name;
                    currentWeight = f->weight;
                } else if (auto* f2 = reg.getFont(currentFamily)) {
                    style.fontName = f2->name;
                    currentWeight = f2->weight;
                }
            }
        }
        checkUndoCapture(uiMgr);

        // Weight combo — disabled when no family selected.
        std::vector<std::string> weights;
        if (!currentFamily.empty()) weights = reg.weightsForFamily(currentFamily);
        int wIdx = 0;
        for (int i = 0; i < static_cast<int>(weights.size()); ++i) {
            if (weights[i] == currentWeight) { wIdx = i; break; }
        }
        auto wGetter = [](void* data, int idx, const char** out) -> bool {
            auto* v = static_cast<std::vector<std::string>*>(data);
            if (idx < static_cast<int>(v->size())) { *out = (*v)[idx].c_str(); return true; }
            return false;
        };
        ImGui::BeginDisabled(weights.empty());
        if (ImGui::Combo("Font Weight", &wIdx, wGetter, &weights,
                         static_cast<int>(weights.size()))) {
            if (!weights.empty()) {
                if (auto* f = reg.getByFamilyWeight(currentFamily, weights[wIdx])) {
                    style.fontName = f->name;
                }
            }
        }
        ImGui::EndDisabled();
        checkUndoCapture(uiMgr);

        // Fallback: raw Font Name input for edge cases (custom names outside the
        // family/weight system). Shown read-only as a small hint.
        ImGui::TextDisabled("fontName: %s",
            style.fontName.empty() ? "(default)" : style.fontName.c_str());
    }

    // --- Background Texture / Nine-Slice ---
    if (ImGui::TreeNode("Background Texture")) {
        char texBuf[128] = {};
        if (!style.backgroundTexture.empty())
            snprintf(texBuf, sizeof(texBuf), "%s", style.backgroundTexture.c_str());
        if (ImGui::InputText("Texture Key", texBuf, sizeof(texBuf))) {
            style.backgroundTexture = texBuf;
        }
        checkUndoCapture(uiMgr);
        ImGui::DragFloat("9S Left", &style.nineSlice.left, 0.5f, 0.0f, 256.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("9S Right", &style.nineSlice.right, 0.5f, 0.0f, 256.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("9S Top", &style.nineSlice.top, 0.5f, 0.0f, 256.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("9S Bottom", &style.nineSlice.bottom, 0.5f, 0.0f, 256.0f); checkUndoCapture(uiMgr);
        ImGui::TreePop();
    }

    // --- Rounded Rect ---
    if (ImGui::TreeNode("Rounded Rect")) {
        ImGui::DragFloat("Corner Radius", &style.cornerRadius, 0.5f, 0.0f, 50.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Gradient Top", &style.gradientTop.r); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Gradient Bottom", &style.gradientBottom.r); checkUndoCapture(uiMgr);
        ImGui::DragFloat2("Shadow Offset", &style.shadowOffset.x, 0.5f, -20.0f, 20.0f); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Shadow Blur", &style.shadowBlur, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
        ImGui::ColorEdit4("Shadow Color", &style.shadowColor.r); checkUndoCapture(uiMgr);
        ImGui::TreePop();
    }

    // --- Header Strip ---
    if (ImGui::TreeNode("Header Strip")) {
        ImGui::ColorEdit4("Header Color", &style.headerColor.r); checkUndoCapture(uiMgr);
        ImGui::DragFloat("Header Height", &style.headerHeight, 0.5f, 0.0f, 200.0f, "%.1f px");
        checkUndoCapture(uiMgr);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Colored bar at the top edge. Disable with height = 0 or fully transparent color.");
        ImGui::TreePop();
    }

    // --- Typography ---
    if (ImGui::TreeNode("Typography")) {
        ImGui::DragFloat("Line Height", &style.lineHeight, 0.01f, 0.5f, 3.0f, "%.2f\xc3\x97");
        checkUndoCapture(uiMgr);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Multiplier on the font's native line height. 1.0 = default.");
        ImGui::DragFloat("Letter Spacing", &style.letterSpacing, 0.1f, -5.0f, 20.0f, "%.1f px");
        checkUndoCapture(uiMgr);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Extra pixels added between each glyph.");
        ImGui::TreePop();
    }

    // --- Text Effects ---
    if (ImGui::TreeNode("Text Effects")) {
        int ts = static_cast<int>(style.textStyle) - 1; // TextStyle enum starts at 1
        const char* styleNames[] = {"Normal", "Outlined", "Glow", "Shadow"};
        if (ImGui::Combo("Text Style", &ts, styleNames, 4)) {
            style.textStyle = static_cast<fate::TextStyle>(ts + 1);
        }
        checkUndoCapture(uiMgr);
        if (style.textStyle != fate::TextStyle::Normal) {
            auto& te = style.textEffects;
            ImGui::ColorEdit4("Outline Color", &te.outlineColor.r); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Outline Width", &te.outlineWidth, 0.01f, 0.0f, 0.5f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Text Shadow Offset", &te.shadowOffset.x, 0.001f, -0.01f, 0.01f); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Text Shadow Color", &te.shadowColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Glow Color", &te.glowColor.r); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Glow Radius", &te.glowRadius, 0.05f, 0.0f, 1.0f); checkUndoCapture(uiMgr);
        }
        ImGui::TreePop();
    }

    ImGui::ColorEdit4("Hover Color", &style.hoverColor.r); checkUndoCapture(uiMgr);
    ImGui::ColorEdit4("Pressed Color", &style.pressedColor.r); checkUndoCapture(uiMgr);
    ImGui::ColorEdit4("Disabled Color", &style.disabledColor.r); checkUndoCapture(uiMgr);
}

} // namespace fate

#endif // FATE_HAS_GAME
