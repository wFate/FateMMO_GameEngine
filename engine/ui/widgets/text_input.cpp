#include "engine/ui/widgets/text_input.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <SDL_scancode.h>
#include <algorithm>

namespace fate {

TextInput::TextInput(const std::string& id) : UINode(id, "text_input") {}

bool TextInput::onPress(const Vec2&) { return true; }
void TextInput::onFocusGained() { UINode::onFocusGained(); }
void TextInput::onFocusLost() { UINode::onFocusLost(); }

bool TextInput::onKeyInput(int scancode, bool pressed) {
    if (!pressed || !focused_) return false;
    switch (scancode) {
        case SDL_SCANCODE_BACKSPACE:
            if (cursorPos > 0 && !text.empty()) { text.erase(cursorPos - 1, 1); cursorPos--; }
            return true;
        case SDL_SCANCODE_DELETE:
            if (cursorPos < static_cast<int>(text.size())) text.erase(cursorPos, 1);
            return true;
        case SDL_SCANCODE_LEFT:
            if (cursorPos > 0) cursorPos--;
            return true;
        case SDL_SCANCODE_RIGHT:
            if (cursorPos < static_cast<int>(text.size())) cursorPos++;
            return true;
        case SDL_SCANCODE_HOME: cursorPos = 0; return true;
        case SDL_SCANCODE_END: cursorPos = static_cast<int>(text.size()); return true;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            if (onSubmit) onSubmit(id_, text);
            return true;
        default: return false;
    }
}

bool TextInput::onTextInput(const std::string& input) {
    if (!focused_) return false;
    for (char c : input) {
        if (maxLength > 0 && static_cast<int>(text.size()) >= maxLength) break;
        text.insert(text.begin() + cursorPos, c);
        cursorPos++;
    }
    return true;
}

void TextInput::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& style = resolvedStyle_;
    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // Background
    Color bg = style.backgroundColor;
    bg.a *= style.opacity;
    if (bg.a > 0.0f)
        batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                       {rect.w, rect.h}, bg, d);

    // Border (highlight when focused)
    Color bc = focused_ ? Color(0.6f, 0.5f, 0.3f, 1.0f) : style.borderColor;
    float bw = style.borderWidth > 0 ? style.borderWidth : 1.0f;
    if (bc.a > 0.0f) {
        bc.a *= style.opacity;
        float innerH = rect.h - bw * 2.0f;
        batch.drawRect({rect.x + rect.w * 0.5f, rect.y + bw * 0.5f}, {rect.w, bw}, bc, d + 0.1f);
        batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h - bw * 0.5f}, {rect.w, bw}, bc, d + 0.1f);
        batch.drawRect({rect.x + bw * 0.5f, rect.y + rect.h * 0.5f}, {bw, innerH}, bc, d + 0.1f);
        batch.drawRect({rect.x + rect.w - bw * 0.5f, rect.y + rect.h * 0.5f}, {bw, innerH}, bc, d + 0.1f);
    }

    float fontSize = style.fontSize > 0 ? style.fontSize : 14.0f;
    float textPadding = 6.0f;
    float textY = rect.y + (rect.h - fontSize) * 0.5f;

    const std::string& displayText = text.empty() ? placeholder : text;
    Color tc = text.empty() ? Color(0.5f, 0.5f, 0.5f, style.opacity) : style.textColor;
    tc.a *= style.opacity;

    if (!displayText.empty())
        sdf.drawScreen(batch, displayText, {rect.x + textPadding, textY}, fontSize, tc, d + 0.2f);

    if (focused_) {
        std::string beforeCursor = text.substr(0, cursorPos);
        Vec2 cursorOffset = sdf.measure(beforeCursor, fontSize);
        float cx = rect.x + textPadding + cursorOffset.x;
        batch.drawRect({cx + 0.5f, rect.y + rect.h * 0.5f},
                       {1.0f, rect.h - 8.0f}, Color::white(), d + 0.3f);
    }

    renderChildren(batch, sdf);
}

} // namespace fate
