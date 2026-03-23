#pragma once
#include "engine/ui/ui_node.h"
#include <string>
#include <functional>

namespace fate {

enum class LoginMode : uint8_t { Login, Register };

class LoginScreen : public UINode {
public:
    LoginScreen(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;
    void onRelease(const Vec2& localPos) override;
    bool onKeyInput(int scancode, bool pressed) override;
    bool onTextInput(const std::string& text) override;

    LoginMode mode = LoginMode::Login;

    // Field values
    std::string username;
    std::string password;
    std::string confirmPassword;
    std::string email;
    std::string serverHost = "127.0.0.1";
    int serverPort = 7778;

    // Status
    std::string statusMessage;
    bool isError = false;

    // Remember me
    bool rememberMe = false;

    // Callbacks
    std::function<void(const std::string& user, const std::string& pass,
                       const std::string& server, int port)> onLogin;
    std::function<void(const std::string& user, const std::string& pass,
                       const std::string& email,
                       const std::string& server, int port)> onRegister;

    void setStatus(const std::string& msg, bool error);
    void reset();

    // Remember-me persistence
    void loadPreferences(const std::string& path = "assets/config/login_prefs.json");
    void savePreferences(const std::string& path = "assets/config/login_prefs.json") const;

private:
    // Field focus: 0=serverHost, 1=port, 2=username, 3=password, 4=email, 5=confirmPassword
    int focusedField_ = 2;
    std::string portStr_ = "7778";

    // Button hit areas (local space, recomputed each render)
    Rect submitBtnRect_ = {};
    Rect toggleModeRect_ = {};
    Rect rememberMeRect_ = {};
    bool submitPressed_ = false;
    bool togglePressed_ = false;

    // Helpers
    std::string* focusedString();
    int maxFieldCount() const; // 4 for Login, 6 for Register
    void syncPortStr();
    bool validate();
    void submit();

    // Layout constants
    static constexpr float PANEL_WIDTH = 360.0f;
    static constexpr float FIELD_HEIGHT = 32.0f;
    static constexpr float FIELD_SPACING = 8.0f;
    static constexpr float BUTTON_HEIGHT = 36.0f;
    static constexpr float PADDING = 20.0f;
};

} // namespace fate
