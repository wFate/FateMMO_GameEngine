#include "engine/ui/widgets/confirm_dialog.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"

namespace fate {

ConfirmDialog::ConfirmDialog(const std::string& id) : UINode(id, "confirm_dialog") {}

Rect ConfirmDialog::confirmButtonRect() const {
    const auto& rect = computedRect_;
    float totalButtonWidth = buttonWidth * 2.0f + buttonSpacing;
    float startX = (rect.w - totalButtonWidth) * 0.5f;
    float btnY = rect.h - buttonHeight - 16.0f;
    return { startX, btnY, buttonWidth, buttonHeight };
}

Rect ConfirmDialog::cancelButtonRect() const {
    const auto& rect = computedRect_;
    float totalButtonWidth = buttonWidth * 2.0f + buttonSpacing;
    float startX = (rect.w - totalButtonWidth) * 0.5f;
    float btnY = rect.h - buttonHeight - 16.0f;
    return { startX + buttonWidth + buttonSpacing, btnY, buttonWidth, buttonHeight };
}

bool ConfirmDialog::onPress(const Vec2& localPos) {
    if (!enabled_) return true; // still modal, consume click

    Rect confirmR = confirmButtonRect();
    Rect cancelR = cancelButtonRect();

    confirmPressed_ = confirmR.contains(localPos);
    cancelPressed_ = cancelR.contains(localPos);

    return true; // modal -- always consume
}

void ConfirmDialog::onRelease(const Vec2& localPos) {
    if (!enabled_) {
        confirmPressed_ = false;
        cancelPressed_ = false;
        return;
    }

    Rect confirmR = confirmButtonRect();
    Rect cancelR = cancelButtonRect();

    if (confirmPressed_ && confirmR.contains(localPos)) {
        if (onConfirm) onConfirm(id_);
    }
    if (cancelPressed_ && cancelR.contains(localPos)) {
        if (onCancel) onCancel(id_);
    }

    confirmPressed_ = false;
    cancelPressed_ = false;
}

void ConfirmDialog::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& style = resolvedStyle_;
    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // Dark background panel
    Color bg(0.10f, 0.10f, 0.13f, 0.95f);
    bg.a *= style.opacity;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, bg, d);

    // Gold/warm border
    Color bc = style.borderColor.a > 0 ? style.borderColor : Color(0.6f, 0.5f, 0.25f, 1.0f);
    bc.a *= style.opacity;
    float bw = style.borderWidth > 0 ? style.borderWidth : 2.0f;
    float innerH = rect.h - bw * 2.0f;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + bw * 0.5f}, {rect.w, bw}, bc, d + 0.15f);
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h - bw * 0.5f}, {rect.w, bw}, bc, d + 0.15f);
    batch.drawRect({rect.x + bw * 0.5f, rect.y + rect.h * 0.5f}, {bw, innerH}, bc, d + 0.15f);
    batch.drawRect({rect.x + rect.w - bw * 0.5f, rect.y + rect.h * 0.5f}, {bw, innerH}, bc, d + 0.15f);

    // Message text centered in upper portion
    if (!message.empty()) {
        float fontSize = style.fontSize > 0 ? style.fontSize : 13.0f;
        Color tc = style.textColor;
        tc.a *= style.opacity;
        Vec2 textSize = sdf.measure(message, fontSize);
        float textX = rect.x + (rect.w - textSize.x) * 0.5f;
        float textY = rect.y + (rect.h - buttonHeight - 32.0f - textSize.y) * 0.5f;
        sdf.drawScreen(batch, message, {textX, textY}, fontSize, tc, d + 0.2f);
    }

    // Button colors
    Color btnNormal(0.55f, 0.45f, 0.25f, 1.0f);
    Color btnHover(0.70f, 0.58f, 0.30f, 1.0f);
    Color btnTextColor = Color::white();
    float btnFontSize = 13.0f;

    btnNormal.a *= style.opacity;
    btnHover.a *= style.opacity;
    btnTextColor.a *= style.opacity;

    // Confirm button
    {
        Rect r = confirmButtonRect();
        float absX = rect.x + r.x;
        float absY = rect.y + r.y;
        Color btnBg = confirmHovered_ ? btnHover : btnNormal;
        batch.drawRect({absX + r.w * 0.5f, absY + r.h * 0.5f},
                       {r.w, r.h}, btnBg, d + 0.1f);

        Vec2 ts = sdf.measure(confirmText, btnFontSize);
        float tx = absX + (r.w - ts.x) * 0.5f;
        float ty = absY + (r.h - ts.y) * 0.5f;
        sdf.drawScreen(batch, confirmText, {tx, ty}, btnFontSize, btnTextColor, d + 0.25f);
    }

    // Cancel button
    {
        Rect r = cancelButtonRect();
        float absX = rect.x + r.x;
        float absY = rect.y + r.y;
        Color btnBg = cancelHovered_ ? btnHover : btnNormal;
        batch.drawRect({absX + r.w * 0.5f, absY + r.h * 0.5f},
                       {r.w, r.h}, btnBg, d + 0.1f);

        Vec2 ts = sdf.measure(cancelText, btnFontSize);
        float tx = absX + (r.w - ts.x) * 0.5f;
        float ty = absY + (r.h - ts.y) * 0.5f;
        sdf.drawScreen(batch, cancelText, {tx, ty}, btnFontSize, btnTextColor, d + 0.25f);
    }

    renderChildren(batch, sdf);
}

} // namespace fate
