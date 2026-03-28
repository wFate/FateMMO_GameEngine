#include "engine/ui/widgets/settings_panel.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <algorithm>

namespace fate {

SettingsPanel::SettingsPanel(const std::string& id)
    : UINode(id, "settings_panel") {}

void SettingsPanel::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);
    float s = layoutScale_;

    // Parchment background + border (same style as StatusPanel)
    Color parchment{0.92f, 0.87f, 0.72f, 0.95f * resolvedStyle_.opacity};
    Color border{0.55f, 0.45f, 0.25f, 1.0f * resolvedStyle_.opacity};
    float bw = borderWidth * s;
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, border, d);
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w - bw * 2.0f, rect.h - bw * 2.0f}, parchment, d + 0.01f);

    float pad = 12.0f * s;
    float curY = rect.y + pad;

    // Title: "Settings"
    {
        float fs = scaledFont(titleFontSize);
        Vec2 off = {titleOffset.x * s, titleOffset.y * s};
        Color shadow{0.0f, 0.0f, 0.0f, 0.5f * resolvedStyle_.opacity};
        Color tc = {titleColor.r, titleColor.g, titleColor.b, titleColor.a * resolvedStyle_.opacity};
        std::string title("Settings");
        Vec2 sz = sdf.measure(title, fs);
        float tx = rect.x + (rect.w - sz.x) * 0.5f + off.x;
        sdf.drawScreen(batch, title, Vec2{tx + 1.0f, curY + 1.0f + off.y}, fs, shadow, d + 0.1f);
        sdf.drawScreen(batch, title, Vec2{tx, curY + off.y}, fs, tc, d + 0.15f);
        curY += sz.y + sectionSpacing * s;
    }

    // Divider
    {
        Color dc = {dividerColor.r, dividerColor.g, dividerColor.b, dividerColor.a * resolvedStyle_.opacity};
        batch.drawRect({rect.x + rect.w * 0.5f, curY}, {rect.w - pad * 4.0f, 1.0f}, dc, d + 0.05f);
        curY += itemSpacing * s;
    }

    // Section: "Account"
    {
        float fs = scaledFont(sectionFontSize);
        Color sc = {sectionColor.r, sectionColor.g, sectionColor.b, sectionColor.a * resolvedStyle_.opacity};
        sdf.drawScreen(batch, "Account", Vec2{rect.x + pad * 2.0f, curY}, fs, sc, d + 0.15f);
        Vec2 sz = sdf.measure("Account", fs);
        curY += sz.y + itemSpacing * s * 2.0f;
    }

    // Logout button (centered)
    {
        float bw = logoutButtonWidth * s;
        float bh = logoutButtonHeight * s;
        Vec2 off = {logoutOffset.x * s, logoutOffset.y * s};
        float bx = rect.x + (rect.w - bw) * 0.5f + off.x;
        float by = curY + off.y;

        logoutBtnRect_ = {bx, by, bw, bh};

        Color btnCol = {logoutBtnColor.r, logoutBtnColor.g, logoutBtnColor.b,
                        logoutBtnColor.a * resolvedStyle_.opacity};
        batch.drawRect({bx + bw * 0.5f, by + bh * 0.5f}, {bw, bh}, btnCol, d + 0.1f);

        // Button text
        float fs = scaledFont(buttonFontSize);
        Color tc = {logoutTextColor.r, logoutTextColor.g, logoutTextColor.b,
                    logoutTextColor.a * resolvedStyle_.opacity};
        std::string label("Logout");
        Vec2 sz = sdf.measure(label, fs);
        sdf.drawScreen(batch, label,
            Vec2{bx + (bw - sz.x) * 0.5f, by + (bh - sz.y) * 0.5f},
            fs, tc, d + 0.15f);
    }

    // Close button (top-right circle, same pattern as StatusPanel)
    {
        float cbR = 10.0f * s;
        Vec2 cbCenter = {rect.x + rect.w - pad - cbR, rect.y + pad + cbR};
        closeBtnRect_ = {cbCenter.x - cbR, cbCenter.y - cbR, cbR * 2.0f, cbR * 2.0f};

        Color closeBg{0.45f, 0.20f, 0.15f, 0.85f * resolvedStyle_.opacity};
        batch.drawCircle(cbCenter, cbR, closeBg, d + 0.2f, 16);

        float xFont = scaledFont(11.0f);
        Color white{1.0f, 1.0f, 1.0f, resolvedStyle_.opacity};
        Vec2 xSz = sdf.measure("X", xFont);
        sdf.drawScreen(batch, "X",
            Vec2{cbCenter.x - xSz.x * 0.5f, cbCenter.y - xSz.y * 0.5f},
            xFont, white, d + 0.25f);
    }

    renderChildren(batch, sdf);
}

bool SettingsPanel::onPress(const Vec2& localPos) {
    if (!enabled_) return false;

    Vec2 gp = {computedRect_.x + localPos.x, computedRect_.y + localPos.y};

    // Close button hit-test
    if (closeBtnRect_.w > 0.0f) {
        float cx = closeBtnRect_.x + closeBtnRect_.w * 0.5f;
        float cy = closeBtnRect_.y + closeBtnRect_.h * 0.5f;
        float r = closeBtnRect_.w * 0.5f;
        float dx = gp.x - cx, dy = gp.y - cy;
        if (dx * dx + dy * dy <= r * r) {
            if (onClose) onClose(id_);
            return true;
        }
    }

    // Logout button hit-test
    if (logoutBtnRect_.w > 0.0f) {
        if (gp.x >= logoutBtnRect_.x && gp.x <= logoutBtnRect_.x + logoutBtnRect_.w &&
            gp.y >= logoutBtnRect_.y && gp.y <= logoutBtnRect_.y + logoutBtnRect_.h) {
            if (onLogout) onLogout();
            return true;
        }
    }

    // Consume all clicks inside the panel
    return true;
}

} // namespace fate
