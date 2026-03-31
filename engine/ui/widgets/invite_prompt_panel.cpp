#include "engine/ui/widgets/invite_prompt_panel.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"

namespace fate {

InvitePromptPanel::InvitePromptPanel(const std::string& id)
    : UINode(id, "invite_prompt") {}

void InvitePromptPanel::showInvite(InviteType type, const std::string& inviterName,
                                    const std::string& charId) {
    inviteType_ = type;
    inviterName_ = inviterName;
    inviterCharId_ = charId;
    acceptPressed_ = false;
    declinePressed_ = false;
    setVisible(true);
}

void InvitePromptPanel::dismiss(const std::string& /*reason*/) {
    hide();
}

bool InvitePromptPanel::isBusy() const {
    return visible_;
}

void InvitePromptPanel::hide() {
    setVisible(false);
    inviteType_ = InviteType::None;
    inviterName_.clear();
    inviterCharId_.clear();
    acceptPressed_ = false;
    declinePressed_ = false;
}

const char* InvitePromptPanel::titleForType() const {
    switch (inviteType_) {
        case InviteType::Party: return "Party Invite";
        case InviteType::Guild: return "Guild Invite";
        case InviteType::Trade: return "Trade Request";
        default: return "Invite";
    }
}

std::string InvitePromptPanel::messageForType() const {
    if (inviteType_ == InviteType::Trade)
        return inviterName_ + " wants to trade";
    return inviterName_ + " has invited you";
}

Rect InvitePromptPanel::acceptButtonRect() const {
    float s = layoutScale_;
    float bW = buttonWidth * s;
    float bH = buttonHeight * s;
    float bS = buttonSpacing * s;
    float totalBtnW = bW * 2.0f + bS;
    float startX = (panelWidth * s - totalBtnW) * 0.5f;
    float btnY = panelHeight * s - bH - buttonPadBottom * s;
    return { startX, btnY, bW, bH };
}

Rect InvitePromptPanel::declineButtonRect() const {
    Rect r = acceptButtonRect();
    r.x += r.w + buttonSpacing * layoutScale_;
    return r;
}

bool InvitePromptPanel::onPress(const Vec2& localPos) {
    if (!enabled_) return true;
    acceptPressed_ = acceptButtonRect().contains(localPos);
    declinePressed_ = declineButtonRect().contains(localPos);
    return true; // modal
}

void InvitePromptPanel::onRelease(const Vec2& localPos) {
    if (!enabled_) {
        acceptPressed_ = false;
        declinePressed_ = false;
        return;
    }

    if (acceptPressed_ && acceptButtonRect().contains(localPos)) {
        if (onAccept) onAccept(inviteType_, inviterCharId_);
    }
    if (declinePressed_ && declineButtonRect().contains(localPos)) {
        if (onDecline) onDecline(inviteType_, inviterCharId_);
    }

    acceptPressed_ = false;
    declinePressed_ = false;
}

void InvitePromptPanel::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);
    float s = layoutScale_;
    float pW = panelWidth * s;
    float pH = panelHeight * s;

    float panelX = rect.x + (rect.w - pW) * 0.5f;
    float panelY = rect.y + (rect.h - pH) * 0.5f;

    // Background
    batch.drawRect({panelX + pW * 0.5f, panelY + pH * 0.5f}, {pW, pH}, bgColor, d);

    // Border
    float bw = borderWidth * s;
    float innerH = pH - bw * 2.0f;
    batch.drawRect({panelX + pW * 0.5f, panelY + bw * 0.5f}, {pW, bw}, borderColor, d + 0.15f);
    batch.drawRect({panelX + pW * 0.5f, panelY + pH - bw * 0.5f}, {pW, bw}, borderColor, d + 0.15f);
    batch.drawRect({panelX + bw * 0.5f, panelY + pH * 0.5f}, {bw, innerH}, borderColor, d + 0.15f);
    batch.drawRect({panelX + pW - bw * 0.5f, panelY + pH * 0.5f}, {bw, innerH}, borderColor, d + 0.15f);

    // Title
    {
        float fontSize = scaledFont(titleFontSize);
        const char* title = titleForType();
        Vec2 ts = sdf.measure(title, fontSize);
        float tx = panelX + (pW - ts.x) * 0.5f + titleOffset.x * s;
        float ty = panelY + titlePadTop * s + titleOffset.y * s;
        sdf.drawScreen(batch, title, {tx, ty}, fontSize, titleColor, d + 0.2f);
    }

    // Message
    {
        float fontSize = scaledFont(messageFontSize);
        std::string msg = messageForType();
        Vec2 ms = sdf.measure(msg, fontSize);
        float titleH = scaledFont(titleFontSize);
        float mx = panelX + (pW - ms.x) * 0.5f + messageOffset.x * s;
        float my = panelY + titlePadTop * s + titleH + messagePadTop * s + messageOffset.y * s;
        sdf.drawScreen(batch, msg, {mx, my}, fontSize, messageColor, d + 0.2f);
    }

    // Accept button
    {
        Rect r = acceptButtonRect();
        float absX = panelX + r.x + buttonOffset.x * s;
        float absY = panelY + r.y + buttonOffset.y * s;
        Color btnBg = acceptPressed_ ? acceptBtnHoverColor : acceptBtnColor;
        batch.drawRect({absX + r.w * 0.5f, absY + r.h * 0.5f}, {r.w, r.h}, btnBg, d + 0.1f);

        float fontSize = scaledFont(buttonFontSize);
        Vec2 ts = sdf.measure("Accept", fontSize);
        sdf.drawScreen(batch, "Accept", {absX + (r.w - ts.x) * 0.5f, absY + (r.h - ts.y) * 0.5f},
                        fontSize, acceptBtnTextColor, d + 0.25f);
    }

    // Decline button
    {
        Rect r = declineButtonRect();
        float absX = panelX + r.x + buttonOffset.x * s;
        float absY = panelY + r.y + buttonOffset.y * s;
        Color btnBg = declinePressed_ ? declineBtnHoverColor : declineBtnColor;
        batch.drawRect({absX + r.w * 0.5f, absY + r.h * 0.5f}, {r.w, r.h}, btnBg, d + 0.1f);

        float fontSize = scaledFont(buttonFontSize);
        Vec2 ts = sdf.measure("Decline", fontSize);
        sdf.drawScreen(batch, "Decline", {absX + (r.w - ts.x) * 0.5f, absY + (r.h - ts.y) * 0.5f},
                        fontSize, declineBtnTextColor, d + 0.25f);
    }

    renderChildren(batch, sdf);
}

} // namespace fate
