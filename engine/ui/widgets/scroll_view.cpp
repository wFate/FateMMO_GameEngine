#include "engine/ui/widgets/scroll_view.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <SDL_scancode.h>
#include <algorithm>

namespace fate {

ScrollView::ScrollView(const std::string& id) : UINode(id, "scroll_view") {}

void ScrollView::scroll(float deltaY) {
    scrollOffset += deltaY;
    float maxScroll = std::max(0.0f, contentHeight - computedRect_.h);
    scrollOffset = std::clamp(scrollOffset, 0.0f, maxScroll);
}

bool ScrollView::onPress(const Vec2&) { return true; }

bool ScrollView::onKeyInput(int scancode, bool pressed) {
    if (!pressed) return false;
    if (scancode == SDL_SCANCODE_UP) { scroll(-scrollSpeed); return true; }
    if (scancode == SDL_SCANCODE_DOWN) { scroll(scrollSpeed); return true; }
    if (scancode == SDL_SCANCODE_PAGEUP) { scroll(-computedRect_.h); return true; }
    if (scancode == SDL_SCANCODE_PAGEDOWN) { scroll(computedRect_.h); return true; }
    return false;
}

void ScrollView::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& style = resolvedStyle_;
    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // Background
    if (style.backgroundColor.a > 0.0f) {
        Color bg = style.backgroundColor;
        bg.a *= style.opacity;
        batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                       {rect.w, rect.h}, bg, d);
    }

    // Auto-compute content height from children
    contentHeight = 0;
    for (auto& child : children_) {
        if (!child->visible()) continue;
        float childBottom = child->computedRect().y + child->computedRect().h - rect.y;
        if (childBottom > contentHeight) contentHeight = childBottom;
    }

    // Scissor-clip children to scroll viewport
    batch.pushScissorRect({rect.x, rect.y, rect.w, rect.h});
    for (auto& child : children_) {
        if (!child->visible()) continue;
        float childTop = child->computedRect().y - scrollOffset;
        float childBottom = childTop + child->computedRect().h;
        // Skip fully off-screen children (saves draw calls)
        if (childBottom < rect.y || childTop > rect.y + rect.h) continue;
        child->render(batch, sdf);
    }
    batch.popScissorRect();

    // Scrollbar
    float maxScroll = std::max(0.0f, contentHeight - rect.h);
    if (maxScroll > 0.0f) {
        float trackWidth = 6.0f;
        float trackX = rect.x + rect.w - trackWidth - 2.0f;

        batch.drawRect({trackX + trackWidth * 0.5f, rect.y + rect.h * 0.5f},
                       {trackWidth, rect.h - 4.0f},
                       Color(0.2f, 0.2f, 0.25f, 0.5f), d + 0.1f);

        float thumbRatio = rect.h / contentHeight;
        float thumbH = std::max(20.0f, rect.h * thumbRatio);
        float scrollRatio = scrollOffset / maxScroll;
        float thumbY = rect.y + 2.0f + scrollRatio * (rect.h - 4.0f - thumbH);

        batch.drawRect({trackX + trackWidth * 0.5f, thumbY + thumbH * 0.5f},
                       {trackWidth, thumbH},
                       Color(0.5f, 0.45f, 0.3f, 0.8f), d + 0.2f);
    }
}

} // namespace fate
