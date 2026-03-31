# Invite Prompt Panel + PartyFrame Serialization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a reusable InvitePromptPanel widget for party/guild/trade invites with Accept/Decline buttons, extract all PartyFrame magic numbers into serialized fields, and add server-side busy flag to prevent overlapping invites.

**Architecture:** InvitePromptPanel is a new UINode subclass following the ConfirmDialog pattern (modal, two buttons, TWOM style). Every visual property is serialized to JSON and editable in the editor inspector. Server tracks `hasActivePrompt` per connection to prevent concurrent invites. Guild invite flow changes from auto-join to pending-invite model.

**Tech Stack:** C++17, custom UINode hierarchy, custom serialization (ByteReader/ByteWriter + JSON), ImGui (editor inspector)

**Spec:** `docs/superpowers/specs/2026-03-31-invite-prompt-panel-design.md`

**IMPORTANT:** After editing ANY `.cpp` or `.h` file, run `touch` on it before building. CMake misses changes silently.

**Build command:** `export LIB="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64" && export INCLUDE="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared" && export PATH="/c/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64:$PATH" && CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" && "$CMAKE" --build out/build/x64-Debug --config Debug --target fate_engine FateServer 2>&1 | tail -30`

---

## Task 1: PartyFrame Magic Number Extraction

**Files:**
- Modify: `engine/ui/widgets/party_frame.h`
- Modify: `engine/ui/widgets/party_frame.cpp`
- Modify: `engine/ui/ui_serializer.cpp` (~line 762)
- Modify: `engine/ui/ui_manager.cpp` (~line 1206)
- Modify: `engine/editor/ui_editor_panel.cpp` (~line 1294)
- Modify: `assets/ui/screens/fate_social.json` (party_frame node, ~line 199)

- [ ] **Step 1: Add 9 new float fields to party_frame.h**

After `borderWidth` (line 39), add:

```cpp
    // Fine layout (extracted from render — all serialized)
    float portraitPadLeft = 6.0f;
    float portraitRimWidth = 1.5f;
    float crownSize = 5.0f;
    float textGapAfterPortrait = 6.0f;
    float textPadRight = 4.0f;
    float namePadTop = 6.0f;
    float levelPadRight = 5.0f;
    float barOffsetY = 13.0f;
    float barGap = 2.0f;
```

- [ ] **Step 2: Replace all magic numbers in party_frame.cpp render()**

Replace each hard-coded literal with the corresponding field:

Line 51: `6.0f * s` → `portraitPadLeft * s`
```cpp
        float portCX = cardX + portraitPadLeft * s + portR + portraitOffset.x * s;
```

Line 55: `1.5f * s` → `portraitRimWidth * s`
```cpp
        batch.drawRing  ({portCX, portCY}, portR, portraitRimWidth * s, portraitRimColor, d + 0.2f, 16);
```

Line 62: `5.0f * s` → `crownSize * s`
```cpp
            float sz = crownSize * s;
```

Line 68: `6.0f * s` → `textGapAfterPortrait * s`
```cpp
        float textX    = portCX + portR + textGapAfterPortrait * s;
```

Line 69: `4.0f * s` → `textPadRight * s`
```cpp
        float textMaxW = cW - (textX - cardX) - textPadRight * s;
```

Line 72: `6.0f * s` → `namePadTop * s`
```cpp
        float nameY = cardY + namePadTop * s;
```

Line 79: `5.0f * s` → `levelPadRight * s`
```cpp
        float lvX = cardX + cW - lvSize.x - levelPadRight * s;
```

Line 85: `13.0f * s` → `barOffsetY * s`
```cpp
        float hpBarY = nameY + barOffsetY * s + barOffset.y * s;
```

Line 99: `2.0f * s` → `barGap * s`
```cpp
        float mpBarY = hpBarY + hpBarH + barGap * s;
```

- [ ] **Step 3: Add serialization in ui_serializer.cpp**

In the `party_frame` block (~line 776, after `j["borderWidth"]`), add:

```cpp
            j["portraitPadLeft"]       = w->portraitPadLeft;
            j["portraitRimWidth"]      = w->portraitRimWidth;
            j["crownSize"]             = w->crownSize;
            j["textGapAfterPortrait"]  = w->textGapAfterPortrait;
            j["textPadRight"]          = w->textPadRight;
            j["namePadTop"]            = w->namePadTop;
            j["levelPadRight"]         = w->levelPadRight;
            j["barOffsetY"]            = w->barOffsetY;
            j["barGap"]                = w->barGap;
```

- [ ] **Step 4: Add deserialization in ui_manager.cpp**

In the `party_frame` block (~line 1225, after `pf->borderWidth`), add:

```cpp
        pf->portraitPadLeft      = j.value("portraitPadLeft", 6.0f);
        pf->portraitRimWidth     = j.value("portraitRimWidth", 1.5f);
        pf->crownSize            = j.value("crownSize", 5.0f);
        pf->textGapAfterPortrait = j.value("textGapAfterPortrait", 6.0f);
        pf->textPadRight         = j.value("textPadRight", 4.0f);
        pf->namePadTop           = j.value("namePadTop", 6.0f);
        pf->levelPadRight        = j.value("levelPadRight", 5.0f);
        pf->barOffsetY           = j.value("barOffsetY", 13.0f);
        pf->barGap               = j.value("barGap", 2.0f);
```

- [ ] **Step 5: Add inspector editors in ui_editor_panel.cpp**

In the PartyFrame "Layout" tree (~line 1294), after the existing `borderWidth` DragFloat, add:

```cpp
            ImGui::DragFloat("Portrait Pad L##pfl", &pf->portraitPadLeft, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Portrait Rim##pfl", &pf->portraitRimWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Crown Size##pfl", &pf->crownSize, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Text Gap##pfl", &pf->textGapAfterPortrait, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Text Pad R##pfl", &pf->textPadRight, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Name Pad Top##pfl", &pf->namePadTop, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Lvl Pad R##pfl", &pf->levelPadRight, 0.5f, 0.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Bar Offset Y##pfl", &pf->barOffsetY, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Bar Gap##pfl", &pf->barGap, 0.5f, 0.0f, 20.0f); checkUndoCapture(uiMgr);
```

- [ ] **Step 6: Add default values to fate_social.json**

In `assets/ui/screens/fate_social.json`, in the `party_frame` node, add the 9 new keys (alphabetical order to match JSON convention):

```json
        "barGap": 2.0,
        "barOffsetY": 13.0,
        "crownSize": 5.0,
        "levelPadRight": 5.0,
        "namePadTop": 6.0,
        "portraitPadLeft": 6.0,
        "portraitRimWidth": 1.5,
        "textGapAfterPortrait": 6.0,
        "textPadRight": 4.0,
```

- [ ] **Step 7: Touch and build**

```bash
touch engine/ui/widgets/party_frame.h engine/ui/widgets/party_frame.cpp engine/ui/ui_serializer.cpp engine/ui/ui_manager.cpp engine/editor/ui_editor_panel.cpp
```

Build fate_engine target. Expected: clean build.

- [ ] **Step 8: Commit**

```bash
git add -f engine/ui/widgets/party_frame.h engine/ui/widgets/party_frame.cpp engine/ui/ui_serializer.cpp engine/ui/ui_manager.cpp engine/editor/ui_editor_panel.cpp assets/ui/screens/fate_social.json
git commit -m "fix: extract PartyFrame magic numbers into 9 serialized fields

portraitPadLeft, portraitRimWidth, crownSize, textGapAfterPortrait,
textPadRight, namePadTop, levelPadRight, barOffsetY, barGap — all
now round-trip through JSON and editable in the inspector."
```

Do NOT add a Co-Authored-By line.

---

## Task 2: InvitePromptPanel Widget (Header + Implementation)

**Files:**
- Create: `engine/ui/widgets/invite_prompt_panel.h`
- Create: `engine/ui/widgets/invite_prompt_panel.cpp`

- [ ] **Step 1: Create invite_prompt_panel.h**

```cpp
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
```

- [ ] **Step 2: Create invite_prompt_panel.cpp**

```cpp
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
    // Reason is shown by the caller via system chat
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
    if (!enabled_) return true; // modal — consume

    acceptPressed_ = acceptButtonRect().contains(localPos);
    declinePressed_ = declineButtonRect().contains(localPos);

    return true; // modal — always consume
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

    // Center panel in the widget's computed rect
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

    // Title (centered)
    {
        float fontSize = scaledFont(titleFontSize);
        const char* title = titleForType();
        Vec2 ts = sdf.measure(title, fontSize);
        float tx = panelX + (pW - ts.x) * 0.5f + titleOffset.x * s;
        float ty = panelY + titlePadTop * s + titleOffset.y * s;
        sdf.drawScreen(batch, title, {tx, ty}, fontSize, titleColor, d + 0.2f);
    }

    // Message (centered, below title)
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
```

- [ ] **Step 3: Touch and build**

```bash
touch engine/ui/widgets/invite_prompt_panel.h engine/ui/widgets/invite_prompt_panel.cpp
```

Build. The new files will be picked up by GLOB_RECURSE in CMakeLists.txt. Expected: clean build (widget defined but not yet used).

- [ ] **Step 4: Commit**

```bash
git add -f engine/ui/widgets/invite_prompt_panel.h engine/ui/widgets/invite_prompt_panel.cpp
git commit -m "feat: add InvitePromptPanel widget for party/guild/trade invites

Reusable modal accept/decline panel with TWOM styling. All 15 layout
floats and 10 colors fully serializable. Supports Party, Guild, Trade
invite types via InviteType enum."
```

Do NOT add a Co-Authored-By line.

---

## Task 3: InvitePromptPanel Serialization + Inspector + JSON

**Files:**
- Modify: `engine/ui/ui_serializer.cpp`
- Modify: `engine/ui/ui_manager.cpp`
- Modify: `engine/editor/ui_editor_panel.cpp`
- Modify: `assets/ui/screens/fate_hud.json`

- [ ] **Step 1: Add serialization in ui_serializer.cpp**

Add `#include "engine/ui/widgets/invite_prompt_panel.h"` to the includes block (near the other widget includes, ~line 51).

After the `confirm_dialog` serialization block (~line 705), add:

```cpp
    else if (type == "invite_prompt") {
        if (auto* w = dynamic_cast<const InvitePromptPanel*>(node)) {
            j["titleOffset"]   = {w->titleOffset.x, w->titleOffset.y};
            j["messageOffset"] = {w->messageOffset.x, w->messageOffset.y};
            j["buttonOffset"]  = {w->buttonOffset.x, w->buttonOffset.y};
            j["titleFontSize"]   = w->titleFontSize;
            j["messageFontSize"] = w->messageFontSize;
            j["buttonFontSize"]  = w->buttonFontSize;
            j["panelWidth"]      = w->panelWidth;
            j["panelHeight"]     = w->panelHeight;
            j["buttonWidth"]     = w->buttonWidth;
            j["buttonHeight"]    = w->buttonHeight;
            j["buttonSpacing"]   = w->buttonSpacing;
            j["borderWidth"]     = w->borderWidth;
            j["titlePadTop"]     = w->titlePadTop;
            j["messagePadTop"]   = w->messagePadTop;
            j["buttonPadBottom"] = w->buttonPadBottom;
            j["bgColor"]              = {w->bgColor.r, w->bgColor.g, w->bgColor.b, w->bgColor.a};
            j["borderColor"]          = {w->borderColor.r, w->borderColor.g, w->borderColor.b, w->borderColor.a};
            j["titleColor"]           = {w->titleColor.r, w->titleColor.g, w->titleColor.b, w->titleColor.a};
            j["messageColor"]         = {w->messageColor.r, w->messageColor.g, w->messageColor.b, w->messageColor.a};
            j["acceptBtnColor"]       = {w->acceptBtnColor.r, w->acceptBtnColor.g, w->acceptBtnColor.b, w->acceptBtnColor.a};
            j["acceptBtnHoverColor"]  = {w->acceptBtnHoverColor.r, w->acceptBtnHoverColor.g, w->acceptBtnHoverColor.b, w->acceptBtnHoverColor.a};
            j["acceptBtnTextColor"]   = {w->acceptBtnTextColor.r, w->acceptBtnTextColor.g, w->acceptBtnTextColor.b, w->acceptBtnTextColor.a};
            j["declineBtnColor"]      = {w->declineBtnColor.r, w->declineBtnColor.g, w->declineBtnColor.b, w->declineBtnColor.a};
            j["declineBtnHoverColor"] = {w->declineBtnHoverColor.r, w->declineBtnHoverColor.g, w->declineBtnHoverColor.b, w->declineBtnHoverColor.a};
            j["declineBtnTextColor"]  = {w->declineBtnTextColor.r, w->declineBtnTextColor.g, w->declineBtnTextColor.b, w->declineBtnTextColor.a};
        }
    }
```

- [ ] **Step 2: Add deserialization in ui_manager.cpp**

Add `#include "engine/ui/widgets/invite_prompt_panel.h"` to includes (~line 42).

After the `confirm_dialog` deserialization block (~line 1773), add:

```cpp
    else if (type == "invite_prompt") {
        auto w = std::make_unique<InvitePromptPanel>(id);
        auto readVec2 = [&](const char* key, Vec2 def) -> Vec2 {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
                return {j[key][0].get<float>(), j[key][1].get<float>()};
            return def;
        };
        w->titleOffset   = readVec2("titleOffset",   w->titleOffset);
        w->messageOffset = readVec2("messageOffset", w->messageOffset);
        w->buttonOffset  = readVec2("buttonOffset",  w->buttonOffset);
        w->titleFontSize   = j.value("titleFontSize",   14.0f);
        w->messageFontSize = j.value("messageFontSize", 12.0f);
        w->buttonFontSize  = j.value("buttonFontSize",  12.0f);
        w->panelWidth      = j.value("panelWidth",      260.0f);
        w->panelHeight     = j.value("panelHeight",     120.0f);
        w->buttonWidth     = j.value("buttonWidth",      80.0f);
        w->buttonHeight    = j.value("buttonHeight",     28.0f);
        w->buttonSpacing   = j.value("buttonSpacing",    16.0f);
        w->borderWidth     = j.value("borderWidth",       1.5f);
        w->titlePadTop     = j.value("titlePadTop",      10.0f);
        w->messagePadTop   = j.value("messagePadTop",     8.0f);
        w->buttonPadBottom = j.value("buttonPadBottom",  12.0f);
        auto readColor = [&](const char* key, Color def) -> Color {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                auto& c = j[key];
                return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                        c.size() >= 4 ? c[3].get<float>() : 1.0f};
            }
            return def;
        };
        w->bgColor             = readColor("bgColor",             w->bgColor);
        w->borderColor         = readColor("borderColor",         w->borderColor);
        w->titleColor          = readColor("titleColor",          w->titleColor);
        w->messageColor        = readColor("messageColor",        w->messageColor);
        w->acceptBtnColor      = readColor("acceptBtnColor",      w->acceptBtnColor);
        w->acceptBtnHoverColor = readColor("acceptBtnHoverColor", w->acceptBtnHoverColor);
        w->acceptBtnTextColor  = readColor("acceptBtnTextColor",  w->acceptBtnTextColor);
        w->declineBtnColor     = readColor("declineBtnColor",     w->declineBtnColor);
        w->declineBtnHoverColor= readColor("declineBtnHoverColor",w->declineBtnHoverColor);
        w->declineBtnTextColor = readColor("declineBtnTextColor", w->declineBtnTextColor);
        node = std::move(w);
    }
```

- [ ] **Step 3: Add inspector in ui_editor_panel.cpp**

Add `#include "engine/ui/widgets/invite_prompt_panel.h"` to includes (~line 51).

After the ConfirmDialog inspector block (~line 1224), add:

```cpp
    else if (auto* ip = dynamic_cast<InvitePromptPanel*>(selectedNode_)) {
        ImGui::SeparatorText("InvitePromptPanel");
        if (ImGui::TreeNodeEx("Position Offsets##ip", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Title##ipo", &ip->titleOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Message##ipo", &ip->messageOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat2("Buttons##ipo", &ip->buttonOffset.x, 0.5f, -200.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Font Sizes##ip", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Title##ipf", &ip->titleFontSize, 0.5f, 6.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Message##ipf", &ip->messageFontSize, 0.5f, 6.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Button##ipf", &ip->buttonFontSize, 0.5f, 6.0f, 30.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Layout##ip", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Panel Width##ipl", &ip->panelWidth, 1.0f, 100.0f, 500.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Panel Height##ipl", &ip->panelHeight, 1.0f, 60.0f, 300.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Width##ipl", &ip->buttonWidth, 1.0f, 40.0f, 200.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Height##ipl", &ip->buttonHeight, 1.0f, 16.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Spacing##ipl", &ip->buttonSpacing, 0.5f, 0.0f, 60.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Border Width##ipl", &ip->borderWidth, 0.25f, 0.0f, 8.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Title Pad Top##ipl", &ip->titlePadTop, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Msg Pad Top##ipl", &ip->messagePadTop, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::DragFloat("Btn Pad Bot##ipl", &ip->buttonPadBottom, 0.5f, 0.0f, 40.0f); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Colors##ip", 0)) {
            ImGui::ColorEdit4("Background##ipc", &ip->bgColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Border##ipc", &ip->borderColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Title##ipc", &ip->titleColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Message##ipc", &ip->messageColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Accept Btn##ipc", &ip->acceptBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Accept Hover##ipc", &ip->acceptBtnHoverColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Accept Text##ipc", &ip->acceptBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Decline Btn##ipc", &ip->declineBtnColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Decline Hover##ipc", &ip->declineBtnHoverColor.r); checkUndoCapture(uiMgr);
            ImGui::ColorEdit4("Decline Text##ipc", &ip->declineBtnTextColor.r); checkUndoCapture(uiMgr);
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::Text("Busy: %s", ip->isBusy() ? "Yes" : "No");
    }
```

Also add the badge registration (search for the badge map, typically where other widget types have badges like `if (type == "player_context_menu") return {...}`):

```cpp
    if (type == "invite_prompt")     return {{0.35f, 0.65f, 0.35f, 1.0f}, "INV"};
```

- [ ] **Step 4: Add node to fate_hud.json**

In `assets/ui/screens/fate_hud.json`, add the invite_prompt node to the children array (near the player_context_menu node, which is at zOrder 120). Place it as a sibling:

```json
      {
        "anchor": { "preset": "Center", "size": [280.0, 140.0] },
        "bgColor": [0.08, 0.08, 0.11, 0.95],
        "borderColor": [0.6, 0.5, 0.25, 0.9],
        "titleColor": [0.95, 0.82, 0.45, 1.0],
        "messageColor": [0.92, 0.90, 0.85, 1.0],
        "acceptBtnColor": [0.18, 0.45, 0.18, 1.0],
        "acceptBtnHoverColor": [0.25, 0.58, 0.25, 1.0],
        "acceptBtnTextColor": [1.0, 1.0, 1.0, 1.0],
        "declineBtnColor": [0.50, 0.20, 0.18, 1.0],
        "declineBtnHoverColor": [0.65, 0.28, 0.25, 1.0],
        "declineBtnTextColor": [1.0, 1.0, 1.0, 1.0],
        "titleOffset": [0.0, 0.0],
        "messageOffset": [0.0, 0.0],
        "buttonOffset": [0.0, 0.0],
        "titleFontSize": 14.0,
        "messageFontSize": 12.0,
        "buttonFontSize": 12.0,
        "panelWidth": 260.0,
        "panelHeight": 120.0,
        "buttonWidth": 80.0,
        "buttonHeight": 28.0,
        "buttonSpacing": 16.0,
        "borderWidth": 1.5,
        "titlePadTop": 10.0,
        "messagePadTop": 8.0,
        "buttonPadBottom": 12.0,
        "id": "invite_prompt",
        "type": "invite_prompt",
        "visible": false,
        "zOrder": 130
      }
```

- [ ] **Step 5: Touch and build**

```bash
touch engine/ui/ui_serializer.cpp engine/ui/ui_manager.cpp engine/editor/ui_editor_panel.cpp
```

Build. Expected: clean build.

- [ ] **Step 6: Commit**

```bash
git add -f engine/ui/ui_serializer.cpp engine/ui/ui_manager.cpp engine/editor/ui_editor_panel.cpp assets/ui/screens/fate_hud.json
git commit -m "feat: add InvitePromptPanel serialization, inspector, and HUD node

Full round-trip JSON serialization for all 15 layout properties and
10 colors. Editor inspector with offsets, fonts, layout, and color
trees. HUD node at zOrder 130, centered, hidden by default."
```

Do NOT add a Co-Authored-By line.

---

## Task 4: GuildAction::DeclineInvite + Guild updateType=6

**Files:**
- Modify: `engine/net/game_messages.h` (GuildAction namespace, ~line 58)

- [ ] **Step 1: Add DeclineInvite to GuildAction namespace**

In `engine/net/game_messages.h`, after `Disband = 7` (line 67), add:

```cpp
    constexpr uint8_t DeclineInvite = 8;  // + inviterCharId:string
```

- [ ] **Step 2: Touch and build**

```bash
touch engine/net/game_messages.h engine/net/net_client.cpp server/server_app.cpp
```

Build. Expected: clean build (new constant defined but not yet used).

- [ ] **Step 3: Commit**

```bash
git add engine/net/game_messages.h
git commit -m "feat: add GuildAction::DeclineInvite (value 8)

Used when target declines a guild invite. Guild updateType=6 will be
used for invite-received (server sends to target instead of auto-joining)."
```

Do NOT add a Co-Authored-By line.

---

## Task 5: Server hasActivePrompt Flag

**Files:**
- Modify: `engine/net/connection.h` (~line 49, ClientConnection struct)

- [ ] **Step 1: Add hasActivePrompt to ClientConnection**

In `engine/net/connection.h`, in the `ClientConnection` struct, after `tradePartnerCharId` (last field before closing brace), add:

```cpp
    // Invite prompt busy state — prevents concurrent invites
    bool hasActivePrompt = false;
    int pendingGuildInviteId = 0;           // guild ID of pending invite
    std::string pendingGuildInviteFromCharId; // who sent the guild invite
```

- [ ] **Step 2: Touch and build**

```bash
touch engine/net/connection.h engine/net/net_client.cpp server/server_app.cpp
```

Build. Expected: clean build.

- [ ] **Step 3: Commit**

```bash
git add engine/net/connection.h
git commit -m "feat: add hasActivePrompt and pending guild invite fields to ClientConnection

Prevents concurrent invites. pendingGuildInviteId/FromCharId store
guild invite state until target accepts or declines."
```

Do NOT add a Co-Authored-By line.

---

## Task 6: Server Busy Check on All Invite Paths

**Files:**
- Modify: `server/handlers/party_handler.cpp` (Invite case)
- Modify: `server/handlers/trade_handler.cpp` (Initiate case)
- Modify: `server/handlers/guild_handler.cpp` (Invite case)
- Modify: `server/server_app.cpp` (disconnect handler, zone transition)

- [ ] **Step 1: Add busy check to party Invite**

In `server/handlers/party_handler.cpp`, in the `PartyAction::Invite` case, after finding the target entity and before the faction check, add:

```cpp
            // Busy check — target already has an invite prompt open
            auto* targetConn = server_.connections().findById(targetClientId);
            if (targetConn && targetConn->hasActivePrompt) {
                sendSystemChat(clientId, "[Party]", "Player is busy");
                break;
            }
```

After sending the `SvPartyUpdate(Invited)` to the target (after `server_.sendTo`), set the flag:

```cpp
            if (targetConn) targetConn->hasActivePrompt = true;
```

- [ ] **Step 2: Add busy check to trade Initiate**

In `server/handlers/trade_handler.cpp`, in `TradeAction::Initiate`, after the faction check block and before the "already in trade" check, add:

```cpp
            // Busy check — target has an invite prompt open
            {
                bool targetBusy = false;
                server_.connections().forEach([&](ClientConnection& c) {
                    if (c.character_id == targetCharId && c.hasActivePrompt)
                        targetBusy = true;
                });
                if (targetBusy) {
                    sendTradeResult(6, 10, "Player is busy"); break;
                }
            }
```

After sending the trade invite to the target (`server_.sendTo` for trade invite), set the flag:

```cpp
                server_.connections().forEach([&](ClientConnection& c) {
                    if (c.character_id == targetCharId)
                        c.hasActivePrompt = true;
                });
```

- [ ] **Step 3: Add busy check to guild Invite (will be reworked in Task 7)**

In `server/handlers/guild_handler.cpp`, in `GuildAction::Invite`, after finding the target entity, add:

```cpp
            // Busy check
            auto* targetConn = server_.connections().findById(targetClientId);
            if (targetConn && targetConn->hasActivePrompt) {
                sendGuildResult(1, "Player is busy");
                break;
            }
```

- [ ] **Step 4: Clear hasActivePrompt on disconnect**

In `server/server_app.cpp`, in the disconnect handler, before the party cleanup block, add:

```cpp
        client->hasActivePrompt = false;
        client->pendingGuildInviteId = 0;
        client->pendingGuildInviteFromCharId.clear();
```

- [ ] **Step 5: Clear hasActivePrompt on zone transition**

In `server/server_app.cpp`, in `processZoneTransition` (or wherever zone transitions are handled server-side), add:

```cpp
    client->hasActivePrompt = false;
    client->pendingGuildInviteId = 0;
    client->pendingGuildInviteFromCharId.clear();
```

- [ ] **Step 6: Clear hasActivePrompt when client sends accept/decline**

In `server/handlers/party_handler.cpp`, at the start of both `AcceptInvite` and `DeclineInvite` cases, add:

```cpp
            client->hasActivePrompt = false;
```

In `server/handlers/trade_handler.cpp`, in `AcceptInvite` and `Cancel` cases, add:

```cpp
            client->hasActivePrompt = false;
```

In `server/handlers/guild_handler.cpp`, in the `AcceptInvite` case (to be implemented in Task 7) and the new `DeclineInvite` case, add the same clear.

- [ ] **Step 7: Touch and build**

```bash
touch server/handlers/party_handler.cpp server/handlers/trade_handler.cpp server/handlers/guild_handler.cpp server/server_app.cpp
```

Build. Expected: clean build.

- [ ] **Step 8: No commit** — server files are gitignored. Edits applied locally.

---

## Task 7: Rework Guild Invite to Pending Model + AcceptInvite/DeclineInvite

**Files:**
- Modify: `server/handlers/guild_handler.cpp`

- [ ] **Step 1: Rework GuildAction::Invite to send invite instead of auto-joining**

Replace the existing Invite case's `addMember` block with pending invite logic:

After all validation passes (faction check, not in guild, not full), instead of calling `guildRepo_->addMember`, do:

```cpp
            // Store pending invite on target's connection
            if (targetConn) {
                targetConn->pendingGuildInviteId = guildComp->guild.guildId;
                targetConn->pendingGuildInviteFromCharId = client->character_id;
                targetConn->hasActivePrompt = true;
            }

            // Send invite notification to target (updateType=6)
            SvGuildUpdateMsg inviteResp;
            inviteResp.updateType = 6; // invite received
            inviteResp.resultCode = 0;
            inviteResp.guildName = guildInfo->guildName;
            inviteResp.message = client->character_id; // inviter's charId for accept/decline
            uint8_t tbuf[256]; ByteWriter tw(tbuf, sizeof(tbuf));
            inviteResp.write(tw);
            server_.sendTo(targetClientId, Channel::ReliableOrdered, PacketType::SvGuildUpdate, tbuf, tw.size());

            // Notify inviter
            sendGuildResult(0, "Guild invite sent to " + targetCharId);
```

- [ ] **Step 2: Implement GuildAction::AcceptInvite case**

Add before the `default:` case:

```cpp
        case GuildAction::AcceptInvite: {
            client->hasActivePrompt = false;

            if (client->pendingGuildInviteId == 0) break; // no pending invite

            int guildId = client->pendingGuildInviteId;
            std::string inviterCharId = client->pendingGuildInviteFromCharId;
            client->pendingGuildInviteId = 0;
            client->pendingGuildInviteFromCharId.clear();

            // Re-validate: guild still exists and has space
            auto guildInfo = guildRepo_->getGuildInfo(guildId);
            if (!guildInfo || guildInfo->memberCount >= guildInfo->maxMembers) {
                SvGuildUpdateMsg resp;
                resp.updateType = 5; resp.resultCode = 1;
                resp.message = guildInfo ? "Guild is full" : "Guild no longer exists";
                uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                resp.write(w);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvGuildUpdate, buf, w.size());
                break;
            }

            // Must not already be in a guild
            auto* guildComp = e->getComponent<GuildComponent>();
            if (guildComp && guildComp->guild.isInGuild()) break;

            // Add to guild
            GuildDbResult dbResult;
            if (guildRepo_->addMember(guildId, client->character_id, 0, dbResult)) {
                if (guildComp) {
                    guildComp->guild.setGuildData(guildId, guildInfo->guildName,
                                                   {}, GuildRank::Member, guildInfo->guildLevel);
                }
                // Notify joiner
                SvGuildUpdateMsg resp;
                resp.updateType = 1; // joined
                resp.resultCode = 0;
                resp.guildName = guildInfo->guildName;
                resp.message = "You joined " + guildInfo->guildName;
                uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));
                resp.write(w);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvGuildUpdate, buf, w.size());

                // Notify inviter
                server_.connections().forEach([&](ClientConnection& c) {
                    if (c.character_id == inviterCharId) {
                        SvGuildUpdateMsg notif;
                        notif.updateType = 5; notif.resultCode = 0;
                        notif.message = client->character_id + " joined the guild";
                        uint8_t nbuf[256]; ByteWriter nw(nbuf, sizeof(nbuf));
                        notif.write(nw);
                        server_.sendTo(c.clientId, Channel::ReliableOrdered, PacketType::SvGuildUpdate, nbuf, nw.size());
                    }
                });
            }
            break;
        }
```

- [ ] **Step 3: Implement GuildAction::DeclineInvite case**

Add after AcceptInvite:

```cpp
        case GuildAction::DeclineInvite: {
            client->hasActivePrompt = false;

            std::string inviterCharId = client->pendingGuildInviteFromCharId;
            client->pendingGuildInviteId = 0;
            client->pendingGuildInviteFromCharId.clear();

            // Notify inviter
            if (!inviterCharId.empty()) {
                server_.connections().forEach([&](ClientConnection& c) {
                    if (c.character_id == inviterCharId) {
                        SvChatMessageMsg chatMsg;
                        chatMsg.channel    = static_cast<uint8_t>(ChatChannel::System);
                        chatMsg.senderName = "[Guild]";
                        chatMsg.message    = client->character_id + " declined your guild invite";
                        chatMsg.faction    = 0;
                        uint8_t buf[512]; ByteWriter w(buf, sizeof(buf));
                        chatMsg.write(w);
                        server_.sendTo(c.clientId, Channel::ReliableOrdered, PacketType::SvChatMessage, buf, w.size());
                    }
                });
            }
            break;
        }
```

- [ ] **Step 4: Touch and build**

```bash
touch server/handlers/guild_handler.cpp
```

Build. Expected: clean build.

- [ ] **Step 5: No commit** — guild_handler.cpp is tracked but the previous Task 6 changes to other server files are gitignored, so commit only this file:

```bash
git add server/handlers/guild_handler.cpp
git commit -m "feat: rework guild invite to pending model with accept/decline

Guild invites no longer auto-join. Server sends updateType=6 to target,
stores pending invite on ClientConnection, waits for AcceptInvite or
DeclineInvite response. Validates guild capacity on accept."
```

Do NOT add a Co-Authored-By line.

---

## Task 8: Game-Side Wiring (game_app.cpp)

**Files:**
- Modify: `game/game_app.cpp`
- Modify: `game/game_app.h`

- [ ] **Step 1: Add invitePrompt_ member to game_app.h**

After `playerContextMenu_` declaration (~line 99), add:

```cpp
    InvitePromptPanel* invitePrompt_ = nullptr;
```

Add the include at the top: `#include "engine/ui/widgets/invite_prompt_panel.h"`

- [ ] **Step 2: Wire invitePrompt_ in both HUD load locations**

In both the initial wiring block (~line 2554) and the undo re-wire block (~line 1931), after the `playerContextMenu_` wiring, add:

```cpp
                    invitePrompt_ = dynamic_cast<InvitePromptPanel*>(h->findById("invite_prompt"));
                    if (invitePrompt_) {
                        invitePrompt_->onAccept = [this](InviteType type, const std::string& charId) {
                            switch (type) {
                                case InviteType::Party:
                                    netClient_.sendPartyAction(PartyAction::AcceptInvite, charId);
                                    break;
                                case InviteType::Guild:
                                    netClient_.sendGuildAction(GuildAction::AcceptInvite, charId);
                                    break;
                                case InviteType::Trade:
                                    netClient_.sendTradeAction(TradeAction::AcceptInvite, charId);
                                    break;
                                default: break;
                            }
                            invitePrompt_->hide();
                        };
                        invitePrompt_->onDecline = [this](InviteType type, const std::string& charId) {
                            switch (type) {
                                case InviteType::Party:
                                    netClient_.sendPartyAction(PartyAction::DeclineInvite, charId);
                                    break;
                                case InviteType::Guild:
                                    netClient_.sendGuildAction(GuildAction::DeclineInvite, charId);
                                    break;
                                case InviteType::Trade:
                                    netClient_.sendTradeAction(TradeAction::Cancel);
                                    break;
                                default: break;
                            }
                            invitePrompt_->hide();
                        };
                    }
```

- [ ] **Step 3: Update onPartyUpdate Invited handler**

In the `onPartyUpdate` callback, replace the `PartyEvent::Invited` case:

```cpp
            case PartyEvent::Invited: {
                std::string inviterName = msg.members.empty() ? msg.actorCharId : msg.members[0].name;
                if (invitePrompt_ && !invitePrompt_->isBusy()) {
                    invitePrompt_->showInvite(InviteType::Party, inviterName, msg.actorCharId);
                }
                PartyInviteInfo invite;
                invite.fromCharacterId = msg.actorCharId;
                invite.fromCharacterName = inviterName;
                partyComp->party.addInvite(invite);
                break;
            }
```

- [ ] **Step 4: Update onTradeUpdate to show invite prompt**

Replace the `onTradeUpdate` callback's case 0 (invited):

```cpp
    netClient_.onTradeUpdate = [this](const SvTradeUpdateMsg& msg) {
        switch (msg.updateType) {
            case 0: // invited
                if (invitePrompt_ && !invitePrompt_->isBusy()) {
                    invitePrompt_->showInvite(InviteType::Trade, msg.otherPlayerName, msg.otherPlayerName);
                }
                if (chatPanel_) chatPanel_->addMessage(6, "[Trade]",
                    msg.otherPlayerName + " invited you to trade.", 0);
                break;
            case 1:
                if (chatPanel_) chatPanel_->addMessage(6, "[Trade]", "Trade session started.", 0);
                break;
            case 5:
                if (chatPanel_) chatPanel_->addMessage(6, "[Trade]", "Trade completed!", 0);
                break;
            case 6:
                if (invitePrompt_ && invitePrompt_->isBusy()) {
                    invitePrompt_->dismiss("Trade cancelled");
                }
                if (chatPanel_) chatPanel_->addMessage(6, "[Trade]",
                    msg.otherPlayerName.empty() ? "Trade cancelled." : msg.otherPlayerName, 0);
                break;
            default:
                if (chatPanel_) chatPanel_->addMessage(6, "[Trade]", "Trade update.", 0);
                break;
        }
    };
```

- [ ] **Step 5: Add guild invite handler (updateType=6)**

In the `onGuildUpdate` callback, add a case for updateType 6:

```cpp
        case 6: // guild invite received
            if (invitePrompt_ && !invitePrompt_->isBusy()) {
                // msg.message contains inviter's charId, msg.guildName has guild name
                invitePrompt_->showInvite(InviteType::Guild, msg.guildName, msg.message);
            }
            if (chatPanel_) chatPanel_->addMessage(6, "[Guild]",
                "You have been invited to join " + msg.guildName, 0);
            break;
```

- [ ] **Step 6: Add dismiss on party disband/kick**

In the `onPartyUpdate` callback, in the `Disbanded` and `Kicked` (when targeting local player) cases, add:

```cpp
                if (invitePrompt_ && invitePrompt_->isBusy()) {
                    invitePrompt_->dismiss("Party was disbanded");
                }
```

- [ ] **Step 7: Add zone transition cleanup**

In the zone transition cleanup block (~line 3352, where `playerContextMenu_->hide()` is called), add:

```cpp
                if (invitePrompt_) invitePrompt_->hide();
```

- [ ] **Step 8: Null out in all cleanup sites**

In all 5+ cleanup sites where `dungeonInviteDialog_` is nulled (disconnect, screen reload, first InGame setup, LoadingScene completion, debug disconnect, shutdown), add:

```cpp
    invitePrompt_ = nullptr;
```

- [ ] **Step 9: Touch and build**

```bash
touch game/game_app.cpp game/game_app.h
```

Build all targets. Expected: clean build.

- [ ] **Step 10: No commit** — game files are gitignored.

---

## Task 9: Full Build Verification

- [ ] **Step 1: Touch all modified files and build all targets**

```bash
touch engine/ui/widgets/party_frame.h engine/ui/widgets/party_frame.cpp engine/ui/widgets/invite_prompt_panel.h engine/ui/widgets/invite_prompt_panel.cpp engine/ui/ui_serializer.cpp engine/ui/ui_manager.cpp engine/editor/ui_editor_panel.cpp engine/net/game_messages.h engine/net/connection.h engine/net/net_client.cpp server/server_app.cpp server/handlers/party_handler.cpp server/handlers/trade_handler.cpp server/handlers/guild_handler.cpp game/game_app.cpp game/game_app.h
```

Build all targets (fate_engine, FateServer, fate_tests).

Expected: clean build, zero errors.

- [ ] **Step 2: Run tests**

Expected: 1197+ tests pass, 0 failures.

- [ ] **Step 3: Remind user**

After server code changes, remind user to restart FateServer.exe.
