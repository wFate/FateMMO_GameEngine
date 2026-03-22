#include "engine/ui/widgets/window.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"

namespace fate {

Window::Window(const std::string& id) : UINode(id, "window") {}

bool Window::onPress(const Vec2& localPos) {
    if (!enabled_) return false;

    // Close button (top-right corner of title bar)
    if (closeable && localPos.y < titleBarHeight) {
        float closeX = computedRect_.w - titleBarHeight;
        if (localPos.x >= closeX) {
            setVisible(false);
            if (onClose) onClose(id_);
            return true;
        }
    }

    // Resize handle (bottom-right 16x16 corner)
    if (resizable && localPos.x > computedRect_.w - 16 && localPos.y > computedRect_.h - 16) {
        isResizing_ = true;
        return true;
    }

    // Title bar drag
    if (localPos.y < titleBarHeight) {
        isDragging_ = true;
        dragOffset_ = localPos;
        return true;
    }

    return true;  // consume all clicks within window
}

void Window::onRelease(const Vec2&) {
    isDragging_ = false;
    isResizing_ = false;
}

void Window::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& style = resolvedStyle_;
    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    if (minimized) {
        // Just render title bar
        batch.drawRect({rect.x + rect.w * 0.5f, rect.y + titleBarHeight * 0.5f},
                       {rect.w, titleBarHeight},
                       Color(0.15f, 0.12f, 0.1f, 0.95f), d);
        if (!title.empty()) {
            float fontSize = style.fontSize > 0 ? style.fontSize : 14.0f;
            sdf.drawScreen(batch, title, {rect.x + 8.0f, rect.y + 6.0f},
                           fontSize, Color::white(), d + 0.2f);
        }
        return;
    }

    // Window background
    Color bg = style.backgroundColor.a > 0 ? style.backgroundColor : Color(0.08f, 0.08f, 0.12f, 0.95f);
    bg.a *= style.opacity;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, bg, d);

    // Title bar
    Color titleBg(0.15f, 0.12f, 0.1f, 0.95f);
    titleBg.a *= style.opacity;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + titleBarHeight * 0.5f},
                   {rect.w, titleBarHeight}, titleBg, d + 0.05f);

    // Title text
    if (!title.empty()) {
        float fontSize = style.fontSize > 0 ? style.fontSize : 14.0f;
        Color tc = style.textColor;
        tc.a *= style.opacity;
        sdf.drawScreen(batch, title, {rect.x + 8.0f, rect.y + 6.0f},
                       fontSize, tc, d + 0.2f);
    }

    // Close button (X in top-right)
    if (closeable) {
        float cx = rect.x + rect.w - titleBarHeight * 0.5f;
        float cy = rect.y + titleBarHeight * 0.5f;
        sdf.drawScreen(batch, "X", {cx - 5.0f, cy - 7.0f},
                       14.0f, Color(0.8f, 0.3f, 0.3f, style.opacity), d + 0.3f);
    }

    // Border
    Color bc = style.borderColor.a > 0 ? style.borderColor : Color(0.4f, 0.35f, 0.2f, 1.0f);
    bc.a *= style.opacity;
    float bw = style.borderWidth > 0 ? style.borderWidth : 2.0f;
    float innerH = rect.h - bw * 2.0f;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + bw * 0.5f}, {rect.w, bw}, bc, d + 0.15f);
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h - bw * 0.5f}, {rect.w, bw}, bc, d + 0.15f);
    batch.drawRect({rect.x + bw * 0.5f, rect.y + rect.h * 0.5f}, {bw, innerH}, bc, d + 0.15f);
    batch.drawRect({rect.x + rect.w - bw * 0.5f, rect.y + rect.h * 0.5f}, {bw, innerH}, bc, d + 0.15f);

    // Resize handle indicator
    if (resizable) {
        float rx = rect.x + rect.w - 12.0f;
        float ry = rect.y + rect.h - 12.0f;
        batch.drawRect({rx + 4.0f, ry + 4.0f}, {8.0f, 2.0f},
                       Color(0.5f, 0.5f, 0.5f, 0.6f), d + 0.2f);
        batch.drawRect({rx + 6.0f, ry + 2.0f}, {2.0f, 8.0f},
                       Color(0.5f, 0.5f, 0.5f, 0.6f), d + 0.2f);
    }

    renderChildren(batch, sdf);
}

} // namespace fate
