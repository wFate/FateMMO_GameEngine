#include "engine/ui/widgets/chat_ticker.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <SDL.h>

namespace fate {

ChatTicker::ChatTicker(const std::string& id)
    : UINode(id, "chat_ticker") {}

void ChatTicker::addMessage(const std::string& msg) {
    if (messages_.size() >= MAX_MESSAGES) messages_.pop_front();
    messages_.push_back(msg);

    // Rebuild display string: join with separator
    currentDisplay_.clear();
    for (size_t i = 0; i < messages_.size(); ++i) {
        if (i > 0) currentDisplay_ += "  |  ";
        currentDisplay_ += messages_[i];
    }

    // Reset scroll to start of the new message
    scrollOffset = 0.0f;
}

void ChatTicker::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // Advance scroll using SDL ticks as a simple clock
    float nowSec = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    float dt = (lastUpdateTime > 0.0f) ? (nowSec - lastUpdateTime) : 0.0f;
    lastUpdateTime = nowSec;
    scrollOffset += scrollSpeed * dt;

    // Semi-transparent background bar
    Color bg = {0.0f, 0.0f, 0.0f, 0.6f};
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, bg, d);

    if (currentDisplay_.empty()) {
        renderChildren(batch, sdf);
        return;
    }

    float fontSize = 11.0f;
    Vec2 ts = sdf.measure(currentDisplay_.c_str(), fontSize);

    // Wrap scroll: once text has fully scrolled past left edge, reset
    float totalWidth = ts.x;
    if (scrollOffset > totalWidth + rect.w) {
        scrollOffset = 0.0f;
    }

    // Draw text scrolling left — starts at right edge, moves left
    float textX = rect.x + rect.w - scrollOffset;
    float textY = rect.y + (rect.h - ts.y) * 0.5f;

    Color white = {1.0f, 1.0f, 1.0f, 0.92f};
    sdf.drawScreen(batch, currentDisplay_.c_str(), {textX, textY},
                   fontSize, white, d + 0.1f);

    renderChildren(batch, sdf);
}

} // namespace fate
