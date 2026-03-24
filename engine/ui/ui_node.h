#pragma once
#include "engine/ui/ui_anchor.h"
#include "engine/ui/ui_style.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace fate {

struct DragPayload;

class SpriteBatch;
class SDFText;

class UINode {
public:
    UINode(const std::string& id, const std::string& type);
    virtual ~UINode() = default;

    // Identity
    const std::string& id() const { return id_; }
    const std::string& type() const { return type_; }

    // Tree operations
    void addChild(std::unique_ptr<UINode> child);
    void removeChild(const std::string& id);
    UINode* findById(const std::string& id);
    const UINode* findById(const std::string& id) const;
    UINode* parent() const { return parent_; }
    size_t childCount() const { return children_.size(); }
    UINode* childAt(size_t index) const;
    const std::vector<std::unique_ptr<UINode>>& children() const { return children_; }
    void sortChildrenByZOrder();

    // Layout
    UIAnchor& anchor() { return anchor_; }
    const UIAnchor& anchor() const { return anchor_; }
    void setAnchor(const UIAnchor& a) { anchor_ = a; }
    void computeLayout(const Rect& parentRect);
    const Rect& computedRect() const { return computedRect_; }

    // Style
    const std::string& styleName() const { return styleName_; }
    void setStyleName(const std::string& name) { styleName_ = name; }
    UIStyle& resolvedStyle() { return resolvedStyle_; }
    const UIStyle& resolvedStyle() const { return resolvedStyle_; }
    void setResolvedStyle(const UIStyle& s) { resolvedStyle_ = s; }

    // State
    bool visible() const {
        // Walk up parent chain — a node is only visible if ALL ancestors are
        for (const UINode* n = this; n; n = n->parent_)
            if (!n->visible_) return false;
        return true;
    }
    bool visibleSelf() const { return visible_; } // own flag only (layout/render)
    void setVisible(bool v) { visible_ = v; }
    bool enabled() const { return enabled_; }
    void setEnabled(bool e) { enabled_ = e; }
    bool hovered() const { return hovered_; }
    void setHovered(bool h) { hovered_ = h; }
    int zOrder() const { return zOrder_; }
    void setZOrder(int z) { zOrder_ = z; }

    // Hit-testing
    bool hitTest(const Vec2& point) const;

    // Interaction hooks (widgets override these)
    virtual bool onPress(const Vec2& localPos);
    virtual void onRelease(const Vec2& localPos);
    virtual void onHoverEnter();
    virtual void onHoverExit();
    virtual void onFocusGained();
    virtual void onFocusLost();
    virtual bool onKeyInput(int scancode, bool pressed);
    virtual bool onTextInput(const std::string& text);
    virtual bool acceptsDrop(const DragPayload& payload) const;
    virtual void onDrop(const DragPayload& payload);

    // Rendering (virtual — widgets override)
    virtual void render(SpriteBatch& batch, SDFText& text);

    // Event/data bindings (stored, resolved later)
    std::unordered_map<std::string, std::string> eventBindings;
    std::unordered_map<std::string, std::string> dataBindings;

    // Custom properties (widget-specific, loaded from JSON)
    std::unordered_map<std::string, std::string> properties;

protected:
    std::string id_;
    std::string type_;
    UINode* parent_ = nullptr;
    std::vector<std::unique_ptr<UINode>> children_;

    UIAnchor anchor_;
    UIStyle resolvedStyle_;
    std::string styleName_;
    Rect computedRect_;

    bool visible_ = true;
    bool enabled_ = true;
    bool hovered_ = false;
    bool focused_ = false;
    bool pressed_ = false;
    int zOrder_ = 0;

    // Draw widget background: uses 9-slice texture if backgroundTexture is set,
    // otherwise solid color rect. Call this from render() instead of manual drawRect.
    void drawBackground(SpriteBatch& batch, float depth);

    // Render children (called by subclass render after drawing self)
    void renderChildren(SpriteBatch& batch, SDFText& text);
};

} // namespace fate
