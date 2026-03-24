#include "engine/ui/widgets/login_screen.h"
#include "engine/net/auth_protocol.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include "engine/core/logger.h"
#include <SDL_scancode.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>

namespace fate {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

LoginScreen::LoginScreen(const std::string& id) : UINode(id, "login_screen") {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

int LoginScreen::maxFieldCount() const {
    return (mode == LoginMode::Login) ? 4 : 6;
}

std::string* LoginScreen::focusedString() {
    switch (focusedField_) {
        case 0: return &serverHost;
        case 1: return &portStr_;
        case 2: return &username;
        case 3: return &password;
        case 4: return &email;
        case 5: return &confirmPassword;
        default: return nullptr;
    }
}

void LoginScreen::syncPortStr() {
    if (portStr_.empty()) {
        serverPort = 0;
        return;
    }
    try {
        int p = std::stoi(portStr_);
        if (p < 0) p = 0;
        if (p > 65535) p = 65535;
        serverPort = p;
    } catch (...) {
        serverPort = 0;
    }
}

void LoginScreen::setStatus(const std::string& msg, bool error) {
    statusMessage = msg;
    isError = error;
}

void LoginScreen::reset() {
    // Securely clear password strings
    if (!password.empty()) {
        volatile char* p = &password[0];
        for (size_t i = 0; i < password.size(); ++i) p[i] = '\0';
    }
    if (!confirmPassword.empty()) {
        volatile char* p = &confirmPassword[0];
        for (size_t i = 0; i < confirmPassword.size(); ++i) p[i] = '\0';
    }
    username.clear();
    password.clear();
    confirmPassword.clear();
    email.clear();
    statusMessage.clear();
    isError = false;
    mode = LoginMode::Login;
    focusedField_ = 2;
}

bool LoginScreen::validate() {
    if (!AuthValidation::isValidUsername(username)) {
        setStatus("Username: 3-20 chars, alphanumeric or underscore", true);
        return false;
    }
    if (!AuthValidation::isValidPassword(password)) {
        setStatus("Password: 8-128 printable ASCII characters", true);
        return false;
    }
    if (mode == LoginMode::Register) {
        if (!AuthValidation::isValidEmail(email)) {
            setStatus("Please enter a valid email address", true);
            return false;
        }
        if (confirmPassword != password) {
            setStatus("Passwords do not match", true);
            return false;
        }
    }
    if (serverHost.empty()) {
        setStatus("Server host cannot be empty", true);
        return false;
    }
    if (serverPort <= 0 || serverPort > 65535) {
        setStatus("Port must be 1-65535", true);
        return false;
    }
    return true;
}

void LoginScreen::submit() {
    if (mode == LoginMode::Login) {
        if (onLogin) onLogin(username, password, serverHost, serverPort);
    } else {
        if (onRegister) onRegister(username, password, email, serverHost, serverPort);
    }
}

// ---------------------------------------------------------------------------
// Preferences persistence
// ---------------------------------------------------------------------------

void LoginScreen::loadPreferences(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) return; // file not found is fine
        auto data = nlohmann::json::parse(file);
        username   = data.value("username", "");
        serverHost = data.value("serverHost", "127.0.0.1");
        serverPort = data.value("serverPort", 7778);
        rememberMe = data.value("rememberMe", false);
        portStr_   = std::to_string(serverPort);
    } catch (const std::exception& e) {
        LOG_WARN("UI", "LoginScreen: failed to load preferences from '%s': %s", path.c_str(), e.what());
    }
}

void LoginScreen::savePreferences(const std::string& path) const {
    try {
        // Create directory if needed
        std::filesystem::path p(path);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }

        nlohmann::json data;
        data["username"]   = username;
        data["serverHost"] = serverHost;
        data["serverPort"] = serverPort;
        data["rememberMe"] = rememberMe;
        // NEVER save password

        std::ofstream file(path);
        if (!file.is_open()) {
            LOG_ERROR("UI", "LoginScreen: failed to open '%s' for writing", path.c_str());
            return;
        }
        file << data.dump(2);
    } catch (const std::exception& e) {
        LOG_ERROR("UI", "LoginScreen: failed to save preferences to '%s': %s", path.c_str(), e.what());
    }
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void LoginScreen::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // --- Full-screen dark background ---
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h},
                   {0.04f, 0.04f, 0.07f, 1.0f}, d);

    // --- Compute panel dimensions ---
    // Field count: serverHost+port row, username, password, [email, confirmPassword in Register]
    int fieldCount = (mode == LoginMode::Login) ? 3 : 5;
    // label height (12px + 2px gap) + field + spacing per field
    float labelH = 14.0f;
    float fieldBlock = labelH + FIELD_HEIGHT + FIELD_SPACING;
    float titleH = 40.0f;
    float subtitleH = 24.0f;
    float rememberH = 24.0f;
    float statusH = 20.0f;
    float toggleH = 24.0f;

    float panelH = PADDING + titleH + subtitleH +
                   static_cast<float>(fieldCount) * fieldBlock +
                   BUTTON_HEIGHT + FIELD_SPACING +
                   rememberH + FIELD_SPACING +
                   toggleH + FIELD_SPACING +
                   statusH + PADDING;

    float panelX = rect.x + (rect.w - PANEL_WIDTH) * 0.5f;
    float panelY = rect.y + (rect.h - panelH) * 0.5f;

    // --- Panel background ---
    Color panelBg(0.08f, 0.08f, 0.10f, 0.95f);
    batch.drawRect({panelX + PANEL_WIDTH * 0.5f, panelY + panelH * 0.5f},
                   {PANEL_WIDTH, panelH}, panelBg, d + 0.05f);

    // --- Panel border ---
    Color bc(0.6f, 0.5f, 0.25f, 1.0f);
    float bw = 2.0f;
    float innerH = panelH - bw * 2.0f;
    batch.drawRect({panelX + PANEL_WIDTH * 0.5f, panelY + bw * 0.5f}, {PANEL_WIDTH, bw}, bc, d + 0.15f);
    batch.drawRect({panelX + PANEL_WIDTH * 0.5f, panelY + panelH - bw * 0.5f}, {PANEL_WIDTH, bw}, bc, d + 0.15f);
    batch.drawRect({panelX + bw * 0.5f, panelY + panelH * 0.5f}, {bw, innerH}, bc, d + 0.15f);
    batch.drawRect({panelX + PANEL_WIDTH - bw * 0.5f, panelY + panelH * 0.5f}, {bw, innerH}, bc, d + 0.15f);

    float curY = panelY + PADDING;
    float contentX = panelX + PADDING;
    float contentW = PANEL_WIDTH - PADDING * 2.0f;

    // --- Title ---
    {
        const char* title = "FateMMO";
        float tfs = 22.0f;
        Vec2 tsz = sdf.measure(title, tfs);
        Color gold(0.95f, 0.80f, 0.20f, 1.0f);
        float titleX = panelX + (PANEL_WIDTH - tsz.x) * 0.5f;
        sdf.drawScreen(batch, title, {titleX, curY}, tfs, gold, d + 1.0f);
        curY += titleH;
    }

    // --- Subtitle ---
    {
        const char* sub = (mode == LoginMode::Login) ? "Sign In" : "Create Account";
        float sfs = 13.0f;
        Vec2 ssz = sdf.measure(sub, sfs);
        sdf.drawScreen(batch, sub,
                       {panelX + (PANEL_WIDTH - ssz.x) * 0.5f, curY},
                       sfs, {0.65f, 0.65f, 0.75f, 1.0f}, d + 1.0f);
        curY += subtitleH;
    }

    // --- Helper lambda: draw a labeled text field ---
    Color fieldBg(0.10f, 0.10f, 0.15f, 1.0f);
    Color fieldBgFocused(0.15f, 0.15f, 0.22f, 1.0f);
    Color fieldBorder(0.35f, 0.35f, 0.50f, 1.0f);
    Color fieldBorderFocused(0.6f, 0.5f, 0.3f, 1.0f);
    Color labelColor(0.55f, 0.55f, 0.65f, 1.0f);
    float labelFs = 11.0f;
    float fieldFs = 14.0f;
    float fieldPad = 8.0f;
    float fbw = 1.5f;

    auto drawField = [&](const char* label, float fx, float fy, float fw,
                         const std::string& value, bool focused, bool masked) {
        // Label
        sdf.drawScreen(batch, label, {fx, fy}, labelFs, labelColor, d + 1.0f);
        float fFieldY = fy + labelH;

        // Background
        Color bg = focused ? fieldBgFocused : fieldBg;
        batch.drawRect({fx + fw * 0.5f, fFieldY + FIELD_HEIGHT * 0.5f},
                       {fw, FIELD_HEIGHT}, bg, d + 0.1f);

        // Border
        Color brc = focused ? fieldBorderFocused : fieldBorder;
        float fih = FIELD_HEIGHT - fbw * 2.0f;
        batch.drawRect({fx + fw * 0.5f, fFieldY + fbw * 0.5f}, {fw, fbw}, brc, d + 0.15f);
        batch.drawRect({fx + fw * 0.5f, fFieldY + FIELD_HEIGHT - fbw * 0.5f}, {fw, fbw}, brc, d + 0.15f);
        batch.drawRect({fx + fbw * 0.5f, fFieldY + FIELD_HEIGHT * 0.5f}, {fbw, fih}, brc, d + 0.15f);
        batch.drawRect({fx + fw - fbw * 0.5f, fFieldY + FIELD_HEIGHT * 0.5f}, {fbw, fih}, brc, d + 0.15f);

        // Text
        float textY = fFieldY + (FIELD_HEIGHT - fieldFs) * 0.5f;
        if (!value.empty()) {
            std::string displayText = masked ? std::string(value.size(), '*') : value;
            sdf.drawScreen(batch, displayText, {fx + fieldPad, textY},
                           fieldFs, Color::white(), d + 1.0f);
        }

        // Cursor
        if (focused) {
            std::string before = masked ? std::string(value.size(), '*') : value;
            Vec2 cOff = sdf.measure(before, fieldFs);
            float cx = fx + fieldPad + cOff.x;
            batch.drawRect({cx + 0.5f, fFieldY + FIELD_HEIGHT * 0.5f},
                           {1.0f, FIELD_HEIGHT - 8.0f}, Color::white(), d + 1.1f);
        }
    };

    // --- Server Host + Port (side by side) ---
    {
        float portW = 70.0f;
        float gap = 8.0f;
        float hostW = contentW - portW - gap;
        drawField("Server", contentX, curY, hostW, serverHost, focusedField_ == 0, false);
        drawField("Port", contentX + hostW + gap, curY, portW, portStr_, focusedField_ == 1, false);
        curY += fieldBlock;
    }

    // --- Username ---
    drawField("Username", contentX, curY, contentW, username, focusedField_ == 2, false);
    curY += fieldBlock;

    // --- Password ---
    drawField("Password", contentX, curY, contentW, password, focusedField_ == 3, true);
    curY += fieldBlock;

    // --- Register-only fields ---
    if (mode == LoginMode::Register) {
        drawField("Email", contentX, curY, contentW, email, focusedField_ == 4, false);
        curY += fieldBlock;

        drawField("Confirm Password", contentX, curY, contentW, confirmPassword, focusedField_ == 5, true);
        curY += fieldBlock;
    }

    // --- Submit button ---
    {
        const char* btnLabel = (mode == LoginMode::Login) ? "Login" : "Register";
        Color btnBg(0.55f, 0.45f, 0.25f, 1.0f);
        if (submitPressed_) btnBg = Color(0.70f, 0.58f, 0.30f, 1.0f);

        submitBtnRect_ = {contentX - panelX, curY - panelY, contentW, BUTTON_HEIGHT};

        batch.drawRect({contentX + contentW * 0.5f, curY + BUTTON_HEIGHT * 0.5f},
                       {contentW, BUTTON_HEIGHT}, btnBg, d + 0.1f);

        // Button border
        Color btnBorder(0.70f, 0.58f, 0.30f, 1.0f);
        float bih = BUTTON_HEIGHT - fbw * 2.0f;
        batch.drawRect({contentX + contentW * 0.5f, curY + fbw * 0.5f}, {contentW, fbw}, btnBorder, d + 0.15f);
        batch.drawRect({contentX + contentW * 0.5f, curY + BUTTON_HEIGHT - fbw * 0.5f}, {contentW, fbw}, btnBorder, d + 0.15f);
        batch.drawRect({contentX + fbw * 0.5f, curY + BUTTON_HEIGHT * 0.5f}, {fbw, bih}, btnBorder, d + 0.15f);
        batch.drawRect({contentX + contentW - fbw * 0.5f, curY + BUTTON_HEIGHT * 0.5f}, {fbw, bih}, btnBorder, d + 0.15f);

        float bfs = 14.0f;
        Vec2 bsz = sdf.measure(btnLabel, bfs);
        sdf.drawScreen(batch, btnLabel,
                       {contentX + (contentW - bsz.x) * 0.5f,
                        curY + (BUTTON_HEIGHT - bsz.y) * 0.5f},
                       bfs, Color::white(), d + 0.25f);
        curY += BUTTON_HEIGHT + FIELD_SPACING;
    }

    // --- Remember Me checkbox ---
    {
        float checkSize = 16.0f;
        float checkX = contentX;
        float checkY = curY + (rememberH - checkSize) * 0.5f;

        rememberMeRect_ = {checkX - panelX, curY - panelY, contentW, rememberH};

        // Checkbox box
        Color boxBg = rememberMe ? Color(0.55f, 0.45f, 0.25f, 1.0f) : Color(0.10f, 0.10f, 0.15f, 1.0f);
        batch.drawRect({checkX + checkSize * 0.5f, checkY + checkSize * 0.5f},
                       {checkSize, checkSize}, boxBg, d + 0.1f);
        // Box border
        Color boxBorder(0.45f, 0.45f, 0.55f, 1.0f);
        float bxih = checkSize - 1.0f * 2.0f;
        batch.drawRect({checkX + checkSize * 0.5f, checkY + 0.5f}, {checkSize, 1.0f}, boxBorder, d + 0.15f);
        batch.drawRect({checkX + checkSize * 0.5f, checkY + checkSize - 0.5f}, {checkSize, 1.0f}, boxBorder, d + 0.15f);
        batch.drawRect({checkX + 0.5f, checkY + checkSize * 0.5f}, {1.0f, bxih}, boxBorder, d + 0.15f);
        batch.drawRect({checkX + checkSize - 0.5f, checkY + checkSize * 0.5f}, {1.0f, bxih}, boxBorder, d + 0.15f);

        // Check mark
        if (rememberMe) {
            float inset = 3.0f;
            batch.drawRect({checkX + checkSize * 0.5f, checkY + checkSize * 0.5f},
                           {checkSize - inset * 2.0f, checkSize - inset * 2.0f},
                           Color::white(), d + 1.0f);
        }

        // Label
        const char* rmLabel = "Remember Me";
        float rmFs = 12.0f;
        Vec2 rmSz = sdf.measure(rmLabel, rmFs);
        sdf.drawScreen(batch, rmLabel,
                       {checkX + checkSize + 6.0f, checkY + (checkSize - rmSz.y) * 0.5f},
                       rmFs, {0.65f, 0.65f, 0.75f, 1.0f}, d + 1.0f);

        curY += rememberH + FIELD_SPACING;
    }

    // --- Toggle mode link ---
    {
        const char* toggleLabel = (mode == LoginMode::Login) ? "Create Account" : "Back to Login";
        float tfs = 12.0f;
        Vec2 tsz = sdf.measure(toggleLabel, tfs);
        float toggleX = panelX + (PANEL_WIDTH - tsz.x) * 0.5f;
        float toggleY = curY;

        toggleModeRect_ = {toggleX - panelX, toggleY - panelY, tsz.x, tsz.y + 4.0f};

        Color toggleColor = togglePressed_ ? Color(0.95f, 0.80f, 0.20f, 1.0f)
                                            : Color(0.5f, 0.7f, 0.9f, 1.0f);
        sdf.drawScreen(batch, toggleLabel, {toggleX, toggleY}, tfs, toggleColor, d + 1.0f);

        // Underline
        batch.drawRect({toggleX + tsz.x * 0.5f, toggleY + tsz.y + 1.0f},
                       {tsz.x, 1.0f}, toggleColor, d + 1.0f);

        curY += toggleH + FIELD_SPACING;
    }

    // --- Status message ---
    if (!statusMessage.empty()) {
        float sfs = 12.0f;
        Vec2 ssz = sdf.measure(statusMessage, sfs);
        Color sc = isError ? Color(1.0f, 0.35f, 0.35f, 1.0f)
                           : Color(0.35f, 0.95f, 0.45f, 1.0f);
        sdf.drawScreen(batch, statusMessage,
                       {panelX + (PANEL_WIDTH - ssz.x) * 0.5f, curY},
                       sfs, sc, d + 1.0f);
    }

    renderChildren(batch, sdf);
}

// ---------------------------------------------------------------------------
// Input — press
// ---------------------------------------------------------------------------

bool LoginScreen::onPress(const Vec2& localPos) {
    if (!enabled_) return true; // still modal, consume click

    // Convert local to panel-relative coords
    const auto& rect = computedRect_;
    float panelX = (rect.w - PANEL_WIDTH) * 0.5f;

    int fieldCount = (mode == LoginMode::Login) ? 3 : 5;
    float labelH = 14.0f;
    float fieldBlock = labelH + FIELD_HEIGHT + FIELD_SPACING;
    float titleH = 40.0f;
    float subtitleH = 24.0f;
    float rememberH = 24.0f;
    float statusH = 20.0f;
    float toggleH = 24.0f;

    float totalPanelH = PADDING + titleH + subtitleH +
                        static_cast<float>(fieldCount) * fieldBlock +
                        BUTTON_HEIGHT + FIELD_SPACING +
                        rememberH + FIELD_SPACING +
                        toggleH + FIELD_SPACING +
                        statusH + PADDING;

    float pX = panelX;
    float pY = (rect.h - totalPanelH) * 0.5f;

    // Panel-relative position
    float plX = localPos.x - pX;
    float plY = localPos.y - pY;

    float contentX = PADDING;
    float contentW = PANEL_WIDTH - PADDING * 2.0f;
    float curY = PADDING + titleH + subtitleH;

    submitPressed_ = false;
    togglePressed_ = false;

    // --- Server Host field ---
    {
        float portW = 70.0f;
        float gap = 8.0f;
        float hostW = contentW - portW - gap;
        Rect hostR = {contentX, curY + labelH, hostW, FIELD_HEIGHT};
        Rect portR = {contentX + hostW + gap, curY + labelH, portW, FIELD_HEIGHT};
        if (hostR.contains({plX, plY})) { focusedField_ = 0; return true; }
        if (portR.contains({plX, plY})) { focusedField_ = 1; return true; }
        curY += fieldBlock;
    }

    // --- Username field ---
    {
        Rect r = {contentX, curY + labelH, contentW, FIELD_HEIGHT};
        if (r.contains({plX, plY})) { focusedField_ = 2; return true; }
        curY += fieldBlock;
    }

    // --- Password field ---
    {
        Rect r = {contentX, curY + labelH, contentW, FIELD_HEIGHT};
        if (r.contains({plX, plY})) { focusedField_ = 3; return true; }
        curY += fieldBlock;
    }

    // --- Register-only fields ---
    if (mode == LoginMode::Register) {
        // Email
        {
            Rect r = {contentX, curY + labelH, contentW, FIELD_HEIGHT};
            if (r.contains({plX, plY})) { focusedField_ = 4; return true; }
            curY += fieldBlock;
        }
        // Confirm Password
        {
            Rect r = {contentX, curY + labelH, contentW, FIELD_HEIGHT};
            if (r.contains({plX, plY})) { focusedField_ = 5; return true; }
            curY += fieldBlock;
        }
    }

    // --- Submit button ---
    {
        Rect r = {contentX, curY, contentW, BUTTON_HEIGHT};
        if (r.contains({plX, plY})) {
            submitPressed_ = true;
            return true;
        }
        curY += BUTTON_HEIGHT + FIELD_SPACING;
    }

    // --- Remember Me ---
    {
        Rect r = {contentX, curY, contentW, rememberH};
        if (r.contains({plX, plY})) {
            rememberMe = !rememberMe;
            return true;
        }
        curY += rememberH + FIELD_SPACING;
    }

    // --- Toggle mode ---
    if (toggleModeRect_.w > 0 && toggleModeRect_.h > 0) {
        Rect r = toggleModeRect_;
        if (r.contains({plX, plY})) {
            togglePressed_ = true;
            return true;
        }
    }

    return true; // modal -- always consume
}

// ---------------------------------------------------------------------------
// Input — release
// ---------------------------------------------------------------------------

void LoginScreen::onRelease(const Vec2& localPos) {
    if (!enabled_) {
        submitPressed_ = false;
        togglePressed_ = false;
        return;
    }

    const auto& rect = computedRect_;
    float pX = (rect.w - PANEL_WIDTH) * 0.5f;

    int fieldCount = (mode == LoginMode::Login) ? 3 : 5;
    float labelH = 14.0f;
    float fieldBlock = labelH + FIELD_HEIGHT + FIELD_SPACING;
    float titleH = 40.0f;
    float subtitleH = 24.0f;
    float rememberH = 24.0f;
    float statusH = 20.0f;
    float toggleH = 24.0f;

    float totalPanelH = PADDING + titleH + subtitleH +
                        static_cast<float>(fieldCount) * fieldBlock +
                        BUTTON_HEIGHT + FIELD_SPACING +
                        rememberH + FIELD_SPACING +
                        toggleH + FIELD_SPACING +
                        statusH + PADDING;

    float pY = (rect.h - totalPanelH) * 0.5f;
    float plX = localPos.x - pX;
    float plY = localPos.y - pY;

    if (submitPressed_) {
        // Check if still over button
        Rect r = submitBtnRect_;
        if (r.contains({plX, plY})) {
            if (validate()) {
                submit();
            }
        }
        submitPressed_ = false;
    }

    if (togglePressed_) {
        if (toggleModeRect_.w > 0 && toggleModeRect_.contains({plX, plY})) {
            mode = (mode == LoginMode::Login) ? LoginMode::Register : LoginMode::Login;
            statusMessage.clear();
            isError = false;
            // Clamp focusedField_ for login mode
            if (mode == LoginMode::Login && focusedField_ > 3) {
                focusedField_ = 2;
            }
        }
        togglePressed_ = false;
    }
}

// ---------------------------------------------------------------------------
// Keyboard input
// ---------------------------------------------------------------------------

bool LoginScreen::onKeyInput(int scancode, bool pressed) {
    if (!pressed) return false;

    switch (scancode) {
        case SDL_SCANCODE_TAB: {
            int maxF = maxFieldCount();
            focusedField_ = (focusedField_ + 1) % maxF;
            // In Login mode skip fields 4,5
            if (mode == LoginMode::Login && focusedField_ >= 4) {
                focusedField_ = 0;
            }
            return true;
        }
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            if (validate()) submit();
            return true;
        case SDL_SCANCODE_BACKSPACE: {
            std::string* s = focusedString();
            if (s && !s->empty()) {
                s->pop_back();
                if (focusedField_ == 1) syncPortStr();
            }
            return true;
        }
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Text input
// ---------------------------------------------------------------------------

bool LoginScreen::onTextInput(const std::string& text) {
    std::string* s = focusedString();
    if (!s) return false;

    // Max lengths per field
    int maxLen = 128;
    switch (focusedField_) {
        case 0: maxLen = 128; break;  // serverHost
        case 1: maxLen = 5;   break;  // port (65535)
        case 2: maxLen = 20;  break;  // username
        case 3: maxLen = 128; break;  // password
        case 4: maxLen = 128; break;  // email
        case 5: maxLen = 128; break;  // confirmPassword
    }

    for (char c : text) {
        if (static_cast<int>(s->size()) >= maxLen) break;
        // Port: only accept digits
        if (focusedField_ == 1) {
            if (c < '0' || c > '9') continue;
        }
        s->push_back(c);
    }

    if (focusedField_ == 1) syncPortStr();
    return true;
}

} // namespace fate
