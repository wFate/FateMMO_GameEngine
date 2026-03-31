#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"
#include <functional>
#include <string>

namespace fate {

enum class InviteType : uint8_t { None = 0, Party = 1, Guild = 2, Trade = 3 };

class InvitePromptPanel : public UINode {
public:
    InvitePromptPanel(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;
    void onRelease(const Vec2& localPos) override;

    // Position offsets (unscaled, multiplied by layoutScale_ at render)
    Vec2 titleOffset = {0.0f, 0.0f};
    Vec2 messageOffset = {0.0f, 0.0f};
    Vec2 buttonOffset = {0.0f, 0.0f};

    // Layout
    float titleFontSize = 14.0f;
    float messageFontSize = 12.0f;
    float buttonFontSize = 12.0f;
    float panelWidth = 260.0f;
    float panelHeight = 120.0f;
    float buttonWidth = 80.0f;
    float buttonHeight = 28.0f;
    float buttonSpacing = 16.0f;
    float borderWidth = 1.5f;
    float titlePadTop = 10.0f;
    float messagePadTop = 8.0f;
    float buttonPadBottom = 12.0f;

    // Colors
    Color bgColor = {0.08f, 0.08f, 0.11f, 0.95f};
    Color borderColor = {0.6f, 0.5f, 0.25f, 0.9f};
    Color titleColor = {0.95f, 0.82f, 0.45f, 1.0f};
    Color messageColor = {0.92f, 0.90f, 0.85f, 1.0f};
    Color acceptBtnColor = {0.18f, 0.45f, 0.18f, 1.0f};
    Color acceptBtnHoverColor = {0.25f, 0.58f, 0.25f, 1.0f};
    Color acceptBtnTextColor = {1.0f, 1.0f, 1.0f, 1.0f};
    Color declineBtnColor = {0.50f, 0.20f, 0.18f, 1.0f};
    Color declineBtnHoverColor = {0.65f, 0.28f, 0.25f, 1.0f};
    Color declineBtnTextColor = {1.0f, 1.0f, 1.0f, 1.0f};

    // Callbacks
    std::function<void(InviteType type, const std::string& charId)> onAccept;
    std::function<void(InviteType type, const std::string& charId)> onDecline;

    // API
    void showInvite(InviteType type, const std::string& inviterName, const std::string& charId);
    void dismiss(const std::string& reason);
    bool isBusy() const;
    void hide();

private:
    InviteType inviteType_ = InviteType::None;
    std::string inviterName_;
    std::string inviterCharId_;

    bool acceptPressed_ = false;
    bool declinePressed_ = false;

    Rect acceptButtonRect() const;
    Rect declineButtonRect() const;

    const char* titleForType() const;
    std::string messageForType() const;
};

} // namespace fate
