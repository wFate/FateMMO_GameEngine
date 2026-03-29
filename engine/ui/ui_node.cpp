#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"
#include "engine/ecs/component_meta.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/texture.h"
#include <nlohmann/json.hpp>
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

void UINode::computeLayout(const Rect& parentRect, float scale) {
    layoutScale_ = scale;

    float px = parentRect.x;
    float py = parentRect.y;
    float pw = parentRect.w;
    float ph = parentRect.h;

    // Apply margins to available parent area (scaled)
    float ml = anchor_.margin.w * scale;  // left
    float mr = anchor_.margin.y * scale;  // right
    float mt = anchor_.margin.x * scale;  // top
    float mb = anchor_.margin.z * scale;  // bottom
    px += ml;  py += mt;
    pw -= (ml + mr);  ph -= (mt + mb);

    // Resolve size: percentSize overrides pixel size; 0 = inherit parent
    // Pixel sizes are scaled; percentage sizes are not (already relative)
    float w = (anchor_.sizePercent.x > 0.0f) ? pw * anchor_.sizePercent.x
            : (anchor_.size.x > 0.0f) ? anchor_.size.x * scale : pw;
    float h = (anchor_.sizePercent.y > 0.0f) ? ph * anchor_.sizePercent.y
            : (anchor_.size.y > 0.0f) ? anchor_.size.y * scale : ph;
    // Resolve offset: percentOffset added to scaled pixel offset
    float ox = anchor_.offset.x * scale + pw * anchor_.offsetPercent.x;
    float oy = anchor_.offset.y * scale + ph * anchor_.offsetPercent.y;
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

    // Responsive: clamp to min/max size (in reference pixels, scaled)
    if (anchor_.minSize.x > 0) w = (std::max)(w, anchor_.minSize.x * scale);
    if (anchor_.minSize.y > 0) h = (std::max)(h, anchor_.minSize.y * scale);
    if (anchor_.maxSize.x > 0) w = (std::min)(w, anchor_.maxSize.x * scale);
    if (anchor_.maxSize.y > 0) h = (std::min)(h, anchor_.maxSize.y * scale);

    computedRect_ = {cx, cy, w, h};

    // Children layout in content area (minus scaled padding)
    Rect contentRect = {
        cx + anchor_.padding.w * scale,
        cy + anchor_.padding.x * scale,
        w - (anchor_.padding.w + anchor_.padding.y) * scale,
        h - (anchor_.padding.x + anchor_.padding.z) * scale
    };

    for (auto& child : children_) {
        if (child->visible_) {
            child->computeLayout(contentRect, scale);
        }
    }
}

void UINode::drawBackground(SpriteBatch& batch, float depth) {
    const auto& style = resolvedStyle_;
    const auto& rect = computedRect_;

    // 9-slice textured background (if style specifies a texture)
    if (!style.backgroundTexture.empty()) {
        auto tex = TextureCache::instance().get(style.backgroundTexture);
        if (!tex) tex = TextureCache::instance().load(style.backgroundTexture);
        if (tex && tex->width() > 0 && tex->height() > 0) {
            Color tint = (style.backgroundColor.a > 0.0f) ? style.backgroundColor : Color::white();
            tint.a *= style.opacity;
            batch.drawNineSlice(tex, rect, style.nineSlice, tint, depth);
            return;
        }
    }

    // Fallback: solid color rect
    if (style.backgroundColor.a > 0.0f) {
        Color bg = style.backgroundColor;
        bg.a *= style.opacity;
        batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                       {rect.w, rect.h}, bg, depth);
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

// ---------------------------------------------------------------------------
// Interaction hooks — default no-op implementations
// ---------------------------------------------------------------------------

bool UINode::onPress(const Vec2&) { return false; }
void UINode::onRelease(const Vec2&) {}
void UINode::onDragUpdate(const Vec2&) {}
void UINode::onHoverEnter() { hovered_ = true; }
void UINode::onHoverExit() { hovered_ = false; }
void UINode::onFocusGained() { focused_ = true; }
void UINode::onFocusLost() { focused_ = false; }
bool UINode::onKeyInput(int, bool) { return false; }
bool UINode::onTextInput(const std::string&) { return false; }
bool UINode::acceptsDrop(const DragPayload&) const { return false; }
void UINode::onDrop(const DragPayload&) {}

void UINode::serializeProperties(nlohmann::json& j) const {
    auto fields = reflectedProperties();
    if (!fields.empty()) autoToJson(this, j, fields);
}

void UINode::deserializeProperties(const nlohmann::json& j) {
    auto fields = reflectedProperties();
    if (!fields.empty()) autoFromJson(j, this, fields);
}

} // namespace fate
