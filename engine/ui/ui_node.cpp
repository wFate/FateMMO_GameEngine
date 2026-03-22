#include "engine/ui/ui_node.h"
#include <algorithm>

namespace fate {

UINode::UINode(const std::string& id, const std::string& type)
    : id_(id), type_(type) {}

void UINode::addChild(std::unique_ptr<UINode> child) {
    child->parent_ = this;
    children_.push_back(std::move(child));
}

void UINode::removeChild(const std::string& id) {
    children_.erase(
        std::remove_if(children_.begin(), children_.end(),
            [&](const std::unique_ptr<UINode>& c) { return c->id_ == id; }),
        children_.end());
}

UINode* UINode::findById(const std::string& id) {
    if (id_ == id) return this;
    for (auto& child : children_) {
        if (auto* found = child->findById(id)) return found;
    }
    return nullptr;
}

const UINode* UINode::findById(const std::string& id) const {
    if (id_ == id) return this;
    for (auto& child : children_) {
        if (auto* found = child->findById(id)) return found;
    }
    return nullptr;
}

UINode* UINode::childAt(size_t index) const {
    if (index >= children_.size()) return nullptr;
    return children_[index].get();
}

void UINode::sortChildrenByZOrder() {
    std::stable_sort(children_.begin(), children_.end(),
        [](const std::unique_ptr<UINode>& a, const std::unique_ptr<UINode>& b) {
            return a->zOrder_ < b->zOrder_;
        });
}

bool UINode::hitTest(const Vec2& point) const {
    return visible_ && computedRect_.contains(point);
}

void UINode::computeLayout(const Rect& parentRect) {
    float px = parentRect.x;
    float py = parentRect.y;
    float pw = parentRect.w;
    float ph = parentRect.h;

    // Apply margins to available parent area
    float ml = anchor_.margin.w;  // left
    float mr = anchor_.margin.y;  // right
    float mt = anchor_.margin.x;  // top
    float mb = anchor_.margin.z;  // bottom
    px += ml;  py += mt;
    pw -= (ml + mr);  ph -= (mt + mb);

    // size == 0 means "inherit full parent dimension"
    float w = (anchor_.size.x > 0.0f) ? anchor_.size.x : pw;
    float h = (anchor_.size.y > 0.0f) ? anchor_.size.y : ph;
    float ox = anchor_.offset.x;
    float oy = anchor_.offset.y;
    float cx = 0.0f, cy = 0.0f;

    switch (anchor_.preset) {
        case AnchorPreset::TopLeft:
            cx = px + ox;
            cy = py + oy;
            break;
        case AnchorPreset::TopCenter:
            cx = px + pw * 0.5f - w * 0.5f + ox;
            cy = py + oy;
            break;
        case AnchorPreset::TopRight:
            cx = px + pw - w + ox;
            cy = py + oy;
            break;
        case AnchorPreset::CenterLeft:
            cx = px + ox;
            cy = py + ph * 0.5f - h * 0.5f + oy;
            break;
        case AnchorPreset::Center:
            cx = px + pw * 0.5f - w * 0.5f + ox;
            cy = py + ph * 0.5f - h * 0.5f + oy;
            break;
        case AnchorPreset::CenterRight:
            cx = px + pw - w + ox;
            cy = py + ph * 0.5f - h * 0.5f + oy;
            break;
        case AnchorPreset::BottomLeft:
            cx = px + ox;
            cy = py + ph - h + oy;
            break;
        case AnchorPreset::BottomCenter:
            cx = px + pw * 0.5f - w * 0.5f + ox;
            cy = py + ph - h + oy;
            break;
        case AnchorPreset::BottomRight:
            cx = px + pw - w + ox;
            cy = py + ph - h + oy;
            break;
        case AnchorPreset::StretchX:
            cx = px;
            cy = py + oy;
            w = pw;
            break;
        case AnchorPreset::StretchY:
            cx = px + ox;
            cy = py;
            h = ph;
            break;
        case AnchorPreset::StretchAll:
            cx = px;
            cy = py;
            w = pw;
            h = ph;
            break;
    }

    computedRect_ = {cx, cy, w, h};

    // Children layout in content area (minus padding)
    Rect contentRect = {
        cx + anchor_.padding.w,
        cy + anchor_.padding.x,
        w - anchor_.padding.w - anchor_.padding.y,
        h - anchor_.padding.x - anchor_.padding.z
    };

    for (auto& child : children_) {
        if (child->visible_) {
            child->computeLayout(contentRect);
        }
    }
}

void UINode::render(SpriteBatch& batch, SDFText& text) {
    if (!visible_) return;
    renderChildren(batch, text);
}

void UINode::renderChildren(SpriteBatch& batch, SDFText& text) {
    for (auto& child : children_) {
        if (child->visible_) {
            child->render(batch, text);
        }
    }
}

} // namespace fate
