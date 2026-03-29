#pragma once
#include "engine/ui/ui_manager.h"
#include <string>

namespace fate {

class UIEditorPanel {
public:
    void draw(UIManager& uiMgr);

    UINode* selectedNode() const { return selectedNode_; }
    const std::string& selectedScreenId() const { return selectedScreenId_; }

    // Re-resolve selectedNode_ after an undo/redo that reloads the screen.
    // Call this from the editor after UndoSystem::undo/redo.
    void revalidateSelection(UIManager& uiMgr);

    // Viewport drag: returns true if the click landed on the selected widget
    // (caller should skip entity selection). Call from editor mouse handling.
    bool handleViewportClick(const Vec2& viewportLocalPos);
    void handleViewportDrag(const Vec2& viewportLocalPos);
    void handleViewportRelease(UIManager* uiMgr);
    bool isDraggingWidget() const { return isDraggingWidget_; }
    bool hasSelection() const { return selectedNode_ != nullptr; }
    void clearSelection() { selectedNode_ = nullptr; selectedNodeId_.clear(); selectedScreenId_.clear(); }

    bool showHierarchy = true;
    bool showInspector = true;

    void setSourceDir(const std::string& dir) { sourceDir_ = dir; }

private:
    std::string sourceDir_;
    UINode* selectedNode_ = nullptr;
    std::string selectedScreenId_;
    std::string selectedNodeId_;

    // Monotonic counter for generating unique child IDs
    int nextChildId_ = 0;

    // Undo snapshot: JSON of the screen before editing started
    std::string pendingSnapshot_;

    // Viewport widget drag state
    bool isDraggingWidget_ = false;
    Vec2 dragStartMousePos_;
    Vec2 dragStartOffset_;

    void drawHierarchy(UIManager& uiMgr);
    void drawNodeTree(UINode* node, const std::string& screenId);
    void drawInspector(UIManager& uiMgr);
    void drawAnchorEditor(UINode* node, UIManager& uiMgr);
    void drawStyleEditor(UINode* node, UIManager& uiMgr);

    int hierarchyRowIdx_ = 0;  // for alternating row shading

    // Called after every editable ImGui widget to capture undo snapshots
    void checkUndoCapture(UIManager& uiMgr);
};

} // namespace fate
