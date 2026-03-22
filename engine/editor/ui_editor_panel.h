#pragma once
#include "engine/ui/ui_manager.h"

namespace fate {

class UIEditorPanel {
public:
    void draw(UIManager& uiMgr);

    UINode* selectedNode() const { return selectedNode_; }

    bool showHierarchy = true;
    bool showInspector = true;

private:
    UINode* selectedNode_ = nullptr;
    std::string selectedScreenId_;

    void drawHierarchy(UIManager& uiMgr);
    void drawNodeTree(UINode* node, const std::string& screenId);
    void drawInspector(UIManager& uiMgr);
    void drawAnchorEditor(UINode* node);
    void drawStyleEditor(UINode* node, UIManager& uiMgr);
};

} // namespace fate
