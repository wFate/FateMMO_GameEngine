#include "engine/ui/widgets/npc_dialogue_panel.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"
#include <SDL_scancode.h>
#include <cstdio>
#include <algorithm>

namespace fate {

NpcDialoguePanel::NpcDialoguePanel(const std::string& id)
    : UINode(id, "npc_dialogue_panel") {}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void NpcDialoguePanel::open() {
    setVisible(true);
    expandedQuestIndex_ = -1;
    rebuild();
}

void NpcDialoguePanel::close() {
    setVisible(false);
    if (onClose) onClose(id_);
}

void NpcDialoguePanel::rebuild() {
    expandedQuestIndex_ = -1;
}

// ---------------------------------------------------------------------------
// Color constants
// ---------------------------------------------------------------------------
static const Color kBg         = {0.08f, 0.08f, 0.12f, 0.95f};
static const Color kText       = {0.9f, 0.9f, 0.85f, 1.0f};
static const Color kGold       = {1.0f, 0.84f, 0.0f, 1.0f};
static const Color kBtnBg      = {0.15f, 0.15f, 0.2f, 0.8f};
static const Color kBtnBdr     = {0.3f, 0.3f, 0.38f, 0.7f};
static const Color kCloseBg    = {0.35f, 0.15f, 0.15f, 0.9f};
static const Color kCloseX     = {1.0f, 0.9f, 0.9f, 1.0f};
static const Color kTitleColor = {0.95f, 0.92f, 0.82f, 1.0f};
static const Color kDivider    = {0.3f, 0.3f, 0.35f, 0.5f};

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------
static constexpr float kTitleBarH   = 28.0f;
static constexpr float kGreetingH   = 60.0f;
static constexpr float kBtnH        = 36.0f;
static constexpr float kBtnMargin   = 4.0f;
static constexpr float kBtnGap      = 4.0f;
static constexpr float kQuestRowH   = 30.0f;
static constexpr float kQuestExpandH = 80.0f;
static constexpr float kCloseBtnW   = 80.0f;
static constexpr float kCloseBtnH   = 30.0f;
static constexpr float kCloseCircleR = 10.0f;
static constexpr float kBorderW     = 2.0f;

// ---------------------------------------------------------------------------
// Helper: draw a rectangular button with centered text
// ---------------------------------------------------------------------------
static void drawButton(SpriteBatch& batch, SDFText& sdf, const char* label,
                       float x, float y, float w, float h, float depth,
                       const Color& bg, const Color& textColor) {
    float cx = x + w * 0.5f;
    float cy = y + h * 0.5f;
    float bw = 1.5f;
    float iH = h - bw * 2.0f;

    batch.drawRect({cx, cy}, {w, h}, bg, depth);
    // Border edges
    batch.drawRect({cx, y + bw * 0.5f},     {w, bw}, kBtnBdr, depth + 0.05f);
    batch.drawRect({cx, y + h - bw * 0.5f}, {w, bw}, kBtnBdr, depth + 0.05f);
    batch.drawRect({x + bw * 0.5f, cy},     {bw, iH}, kBtnBdr, depth + 0.05f);
    batch.drawRect({x + w - bw * 0.5f, cy}, {bw, iH}, kBtnBdr, depth + 0.05f);

    // Centered text
    Vec2 ts = sdf.measure(label, 12.0f);
    sdf.drawScreen(batch, label,
        {cx - ts.x * 0.5f, cy - ts.y * 0.5f},
        12.0f, textColor, depth + 0.1f);
}

// ---------------------------------------------------------------------------
// Helper: simple word-wrapped text drawing (returns Y after last line)
// ---------------------------------------------------------------------------
static float drawWrappedText(SpriteBatch& batch, SDFText& sdf,
                             const std::string& text,
                             float x, float y, float maxW,
                             float fontSize, const Color& color, float depth) {
    if (text.empty()) return y;

    float lineY = y;
    float spaceW = sdf.measure(" ", fontSize).x;

    // Split into words and wrap
    size_t pos = 0;
    std::string line;
    float lineW = 0.0f;

    while (pos < text.size()) {
        // Find next word
        size_t wordStart = pos;
        while (pos < text.size() && text[pos] != ' ' && text[pos] != '\n')
            ++pos;
        std::string word = text.substr(wordStart, pos - wordStart);

        // Check for newline
        bool newline = (pos < text.size() && text[pos] == '\n');

        // Skip delimiter
        if (pos < text.size()) ++pos;

        if (word.empty()) {
            if (newline) {
                // Flush current line and advance
                if (!line.empty()) {
                    sdf.drawScreen(batch, line, {x, lineY}, fontSize, color, depth);
                    line.clear();
                    lineW = 0.0f;
                }
                lineY += fontSize + 2.0f;
            }
            continue;
        }

        float wordW = sdf.measure(word, fontSize).x;

        // Does the word fit on current line?
        float testW = line.empty() ? wordW : (lineW + spaceW + wordW);
        if (testW > maxW && !line.empty()) {
            // Flush current line
            sdf.drawScreen(batch, line, {x, lineY}, fontSize, color, depth);
            lineY += fontSize + 2.0f;
            line = word;
            lineW = wordW;
        } else {
            if (!line.empty()) {
                line += ' ';
                lineW += spaceW;
            }
            line += word;
            lineW += wordW;
        }

        if (newline && !line.empty()) {
            sdf.drawScreen(batch, line, {x, lineY}, fontSize, color, depth);
            lineY += fontSize + 2.0f;
            line.clear();
            lineW = 0.0f;
        }
    }

    // Flush remaining text
    if (!line.empty()) {
        sdf.drawScreen(batch, line, {x, lineY}, fontSize, color, depth);
        lineY += fontSize + 2.0f;
    }

    return lineY;
}

// ---------------------------------------------------------------------------
// Helper: hit-test a rectangular region
// ---------------------------------------------------------------------------
static bool hitRect(const Vec2& localPos, float x, float y, float w, float h) {
    return localPos.x >= x && localPos.x < x + w &&
           localPos.y >= y && localPos.y < y + h;
}

// ---------------------------------------------------------------------------
// Render: Story mode
// ---------------------------------------------------------------------------
static void renderStoryMode(SpriteBatch& batch, SDFText& sdf,
                            const Rect& rect, float d,
                            const std::string& npcName,
                            const std::string& storyText,
                            const std::vector<NpcDialoguePanel::StoryChoice>& storyChoices) {
    // Title bar
    Vec2 titleSize = sdf.measure(npcName, 14.0f);
    sdf.drawScreen(batch, npcName,
        {rect.x + (rect.w - titleSize.x) * 0.5f, rect.y + 7.0f},
        14.0f, kTitleColor, d + 0.2f);

    // Divider below title
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + kTitleBarH},
                   {rect.w - kBorderW * 2.0f, 1.5f}, kDivider, d + 0.1f);

    // Story text area
    float textX = rect.x + 8.0f;
    float textY = rect.y + kTitleBarH + 6.0f;
    float maxTextW = rect.w - 16.0f;
    float storyEndY = drawWrappedText(batch, sdf, storyText,
                                       textX, textY, maxTextW,
                                       11.0f, kText, d + 0.2f);

    // Story choice buttons
    float choiceY = storyEndY + 8.0f;
    float choiceBtnW = rect.w - kBtnMargin * 2.0f;

    for (size_t i = 0; i < storyChoices.size(); ++i) {
        float btnY = choiceY + static_cast<float>(i) * (kBtnH + kBtnGap);
        drawButton(batch, sdf, storyChoices[i].text.c_str(),
                   rect.x + kBtnMargin, btnY, choiceBtnW, kBtnH, d + 0.2f,
                   kBtnBg, kText);
    }

    // Close button at bottom
    float closeBtnX = rect.x + (rect.w - kCloseBtnW) * 0.5f;
    float closeBtnY = rect.y + rect.h - kCloseBtnH - 6.0f;
    drawButton(batch, sdf, "Close",
               closeBtnX, closeBtnY, kCloseBtnW, kCloseBtnH, d + 0.2f,
               kBtnBg, kText);
}

// ---------------------------------------------------------------------------
// Render: Functional NPC mode
// ---------------------------------------------------------------------------
static void renderFunctionalMode(SpriteBatch& batch, SDFText& sdf,
                                  const Rect& rect, float d,
                                  const std::string& npcName,
                                  const std::string& greeting,
                                  bool hasShop, bool hasBank,
                                  bool hasTeleporter, bool hasGuild,
                                  bool hasDungeon,
                                  const std::vector<NpcDialoguePanel::QuestEntry>& quests,
                                  int expandedQuestIndex) {
    // Title bar
    Vec2 titleSize = sdf.measure(npcName, 14.0f);
    sdf.drawScreen(batch, npcName,
        {rect.x + (rect.w - titleSize.x) * 0.5f, rect.y + 7.0f},
        14.0f, kTitleColor, d + 0.2f);

    // Divider below title
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + kTitleBarH},
                   {rect.w - kBorderW * 2.0f, 1.5f}, kDivider, d + 0.1f);

    // Greeting text (word-wrapped)
    float textX = rect.x + 8.0f;
    float textY = rect.y + kTitleBarH + 6.0f;
    float maxTextW = rect.w - 16.0f;
    float greetEndY = textY;

    if (!greeting.empty()) {
        greetEndY = drawWrappedText(batch, sdf, greeting,
                                     textX, textY, maxTextW,
                                     11.0f, kText, d + 0.2f);
    }

    // Clamp greeting area to at most kGreetingH
    float contentY = rect.y + kTitleBarH + kGreetingH;
    if (greetEndY > contentY) contentY = greetEndY + 4.0f;

    // Role buttons
    float btnW = rect.w - kBtnMargin * 2.0f;
    float curY = contentY;

    struct RoleBtn { const char* label; bool show; };
    RoleBtn roles[] = {
        {"Shop",     hasShop},
        {"Bank",     hasBank},
        {"Teleport", hasTeleporter},
        {"Guild",    hasGuild},
        {"Dungeon",  hasDungeon},
    };

    for (const auto& role : roles) {
        if (!role.show) continue;
        drawButton(batch, sdf, role.label,
                   rect.x + kBtnMargin, curY, btnW, kBtnH, d + 0.2f,
                   kBtnBg, kText);
        curY += kBtnH + kBtnGap;
    }

    // Quest section
    if (!quests.empty()) {
        // Divider above quests
        if (curY > contentY) {
            batch.drawRect({rect.x + rect.w * 0.5f, curY + 2.0f},
                           {rect.w - kBorderW * 2.0f, 1.0f}, kDivider, d + 0.1f);
            curY += 6.0f;
        }

        // "Quests" label
        sdf.drawScreen(batch, "Quests",
            {rect.x + 8.0f, curY}, 10.0f, kGold, d + 0.2f);
        curY += 14.0f;

        for (int i = 0; i < static_cast<int>(quests.size()); ++i) {
            const auto& quest = quests[static_cast<size_t>(i)];
            bool expanded = (i == expandedQuestIndex);

            // Quest row background
            Color rowBg = (i % 2 == 0)
                ? Color{0.12f, 0.12f, 0.16f, 0.6f}
                : Color{0.10f, 0.10f, 0.14f, 0.6f};
            float rowH = expanded ? kQuestExpandH : kQuestRowH;

            batch.drawRect(
                {rect.x + rect.w * 0.5f, curY + rowH * 0.5f},
                {rect.w - kBtnMargin * 2.0f, rowH},
                rowBg, d + 0.1f);

            // Quest name
            Color nameColor = quest.isCompletable ? kGold : kText;
            sdf.drawScreen(batch, quest.questName,
                {rect.x + 12.0f, curY + 6.0f},
                11.0f, nameColor, d + 0.2f);

            // Status indicator
            if (quest.isCompletable) {
                sdf.drawScreen(batch, "[Complete]",
                    {rect.x + rect.w - 70.0f, curY + 8.0f},
                    9.0f, kGold, d + 0.2f);
            } else if (quest.isAccepted) {
                Color activeColor = {0.5f, 0.7f, 1.0f, 1.0f};
                sdf.drawScreen(batch, "[Active]",
                    {rect.x + rect.w - 56.0f, curY + 8.0f},
                    9.0f, activeColor, d + 0.2f);
            }

            // Expanded content: description + action buttons
            if (expanded) {
                // Description
                drawWrappedText(batch, sdf, quest.description,
                                rect.x + 16.0f, curY + kQuestRowH,
                                rect.w - 32.0f,
                                9.0f, kText, d + 0.2f);

                // Action buttons at bottom of expanded area
                float actionBtnY = curY + kQuestExpandH - 24.0f;
                float actionBtnW = 70.0f;
                float actionBtnH = 20.0f;

                if (quest.isCompletable) {
                    // Complete button
                    Color completeBg = {0.2f, 0.55f, 0.2f, 0.9f};
                    drawButton(batch, sdf, "Complete",
                               rect.x + rect.w - actionBtnW - 12.0f,
                               actionBtnY, actionBtnW, actionBtnH,
                               d + 0.3f, completeBg, kText);
                } else if (!quest.isAccepted) {
                    // Accept + Decline buttons
                    Color acceptBg  = {0.2f, 0.55f, 0.2f, 0.9f};
                    Color declineBg = {0.55f, 0.2f, 0.2f, 0.9f};
                    float gap = 6.0f;
                    drawButton(batch, sdf, "Accept",
                               rect.x + rect.w - actionBtnW * 2.0f - gap - 12.0f,
                               actionBtnY, actionBtnW, actionBtnH,
                               d + 0.3f, acceptBg, kText);
                    drawButton(batch, sdf, "Decline",
                               rect.x + rect.w - actionBtnW - 12.0f,
                               actionBtnY, actionBtnW, actionBtnH,
                               d + 0.3f, declineBg, kText);
                }
            }

            curY += rowH + 2.0f;
        }
    }

    // Close button at bottom
    float closeBtnX = rect.x + (rect.w - kCloseBtnW) * 0.5f;
    float closeBtnY = rect.y + rect.h - kCloseBtnH - 6.0f;
    drawButton(batch, sdf, "Close",
               closeBtnX, closeBtnY, kCloseBtnW, kCloseBtnH, d + 0.2f,
               kBtnBg, kText);
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
void NpcDialoguePanel::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);

    // ---- Dark background panel ----
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f},
                   {rect.w, rect.h}, kBg, d);

    // Border edges
    float innerH = rect.h - kBorderW * 2.0f;
    Color bdr = {0.3f, 0.3f, 0.35f, 0.8f};
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + kBorderW * 0.5f},
                   {rect.w, kBorderW}, bdr, d + 0.1f);
    batch.drawRect({rect.x + rect.w * 0.5f, rect.y + rect.h - kBorderW * 0.5f},
                   {rect.w, kBorderW}, bdr, d + 0.1f);
    batch.drawRect({rect.x + kBorderW * 0.5f, rect.y + rect.h * 0.5f},
                   {kBorderW, innerH}, bdr, d + 0.1f);
    batch.drawRect({rect.x + rect.w - kBorderW * 0.5f, rect.y + rect.h * 0.5f},
                   {kBorderW, innerH}, bdr, d + 0.1f);

    // ---- Close X button (top-right corner) ----
    float closeXCX = rect.x + rect.w - kCloseCircleR - 5.0f;
    float closeXCY = rect.y + kCloseCircleR + 5.0f;
    batch.drawCircle({closeXCX, closeXCY}, kCloseCircleR, kCloseBg, d + 0.2f, 16);
    Vec2 xts = sdf.measure("X", 10.0f);
    sdf.drawScreen(batch, "X",
        {closeXCX - xts.x * 0.5f, closeXCY - xts.y * 0.5f},
        10.0f, kCloseX, d + 0.4f);

    // ---- Mode-specific rendering ----
    if (isStoryMode) {
        renderStoryMode(batch, sdf, rect, d, npcName, storyText, storyChoices);
    } else {
        renderFunctionalMode(batch, sdf, rect, d, npcName, greeting,
                              hasShop, hasBank, hasTeleporter, hasGuild,
                              hasDungeon, quests, expandedQuestIndex_);
    }

    renderChildren(batch, sdf);
}

// ---------------------------------------------------------------------------
// Input: onPress
// ---------------------------------------------------------------------------
bool NpcDialoguePanel::onPress(const Vec2& localPos) {
    if (!enabled_) return false;

    const float w = computedRect_.w;
    const float h = computedRect_.h;

    // ---- Close X button (top-right) ----
    {
        float closeCX = w - kCloseCircleR - 5.0f;
        float closeCY = kCloseCircleR + 5.0f;
        float dx = localPos.x - closeCX;
        float dy = localPos.y - closeCY;
        if (dx * dx + dy * dy <= kCloseCircleR * kCloseCircleR) {
            close();
            return true;
        }
    }

    // ---- Close button at bottom ----
    {
        float closeBtnX = (w - kCloseBtnW) * 0.5f;
        float closeBtnY = h - kCloseBtnH - 6.0f;
        if (hitRect(localPos, closeBtnX, closeBtnY, kCloseBtnW, kCloseBtnH)) {
            close();
            return true;
        }
    }

    // ---- Story mode input ----
    if (isStoryMode) {
        // Calculate story text end to find choice button positions
        // (approximate: use same layout logic as render)
        float textX = 8.0f;
        float textY = kTitleBarH + 6.0f;
        float maxTextW = w - 16.0f;

        // Estimate story text height by counting wrapped lines
        float storyEndY = textY;
        if (!storyText.empty()) {
            // Simple estimation: count words and measure wrap
            float spaceW = 6.0f; // approximate
            float lineW = 0.0f;
            float lineH = 11.0f + 2.0f;
            storyEndY = textY;
            size_t pos = 0;
            while (pos < storyText.size()) {
                size_t wordStart = pos;
                while (pos < storyText.size() && storyText[pos] != ' ' && storyText[pos] != '\n')
                    ++pos;
                std::string word = storyText.substr(wordStart, pos - wordStart);
                bool newline = (pos < storyText.size() && storyText[pos] == '\n');
                if (pos < storyText.size()) ++pos;

                if (word.empty()) {
                    if (newline) storyEndY += lineH;
                    continue;
                }

                // Approximate word width
                float wordW = static_cast<float>(word.size()) * 6.5f;
                float testW = (lineW == 0.0f) ? wordW : (lineW + spaceW + wordW);
                if (testW > maxTextW && lineW > 0.0f) {
                    storyEndY += lineH;
                    lineW = wordW;
                } else {
                    lineW = (lineW == 0.0f) ? wordW : (lineW + spaceW + wordW);
                }
                if (newline) {
                    storyEndY += lineH;
                    lineW = 0.0f;
                }
            }
            storyEndY += lineH; // last line
        }

        float choiceY = storyEndY + 8.0f;
        float choiceBtnW = w - kBtnMargin * 2.0f;

        for (size_t i = 0; i < storyChoices.size(); ++i) {
            float btnY = choiceY + static_cast<float>(i) * (kBtnH + kBtnGap);
            if (hitRect(localPos, kBtnMargin, btnY, choiceBtnW, kBtnH)) {
                if (onStoryChoice) onStoryChoice(storyChoices[i].nextNodeId);
                return true;
            }
        }

        return true; // consume all clicks on panel
    }

    // ---- Functional mode input ----

    // Calculate content layout (matching render)
    float contentY = kTitleBarH + kGreetingH;
    float btnW = w - kBtnMargin * 2.0f;
    float curY = contentY;

    // Role buttons
    struct RoleInfo { bool show; };
    RoleInfo roles[] = { {hasShop}, {hasBank}, {hasTeleporter}, {hasGuild}, {hasDungeon} };

    int roleIndex = 0;
    for (const auto& role : roles) {
        if (!role.show) { ++roleIndex; continue; }
        if (hitRect(localPos, kBtnMargin, curY, btnW, kBtnH)) {
            switch (roleIndex) {
                case 0: if (onOpenShop)         onOpenShop(npcId);         break;
                case 1: if (onOpenBank)          onOpenBank(npcId);         break;
                case 2: if (onOpenTeleporter)    onOpenTeleporter(npcId);   break;
                case 3: if (onOpenGuildCreation) onOpenGuildCreation(npcId); break;
                case 4: if (onOpenDungeon) onOpenDungeon(npcId); break;
            }
            return true;
        }
        curY += kBtnH + kBtnGap;
        ++roleIndex;
    }

    // Quest rows
    if (!quests.empty()) {
        // Skip divider + "Quests" label
        if (curY > contentY) curY += 6.0f;
        curY += 14.0f;

        for (int i = 0; i < static_cast<int>(quests.size()); ++i) {
            const auto& quest = quests[static_cast<size_t>(i)];
            bool expanded = (i == expandedQuestIndex_);
            float rowH = expanded ? kQuestExpandH : kQuestRowH;

            if (hitRect(localPos, kBtnMargin, curY, w - kBtnMargin * 2.0f, rowH)) {
                if (expanded) {
                    // Check action buttons within expanded area
                    float actionBtnY = curY + kQuestExpandH - 24.0f;
                    float actionBtnW = 70.0f;
                    float actionBtnH = 20.0f;

                    if (quest.isCompletable) {
                        // Complete button
                        float btnX = w - actionBtnW - 12.0f;
                        if (hitRect(localPos, btnX, actionBtnY, actionBtnW, actionBtnH)) {
                            if (onQuestComplete) onQuestComplete(quest.questId);
                            return true;
                        }
                    } else if (!quest.isAccepted) {
                        // Accept button
                        float gap = 6.0f;
                        float acceptX = w - actionBtnW * 2.0f - gap - 12.0f;
                        if (hitRect(localPos, acceptX, actionBtnY, actionBtnW, actionBtnH)) {
                            if (onQuestAccept) onQuestAccept(quest.questId);
                            return true;
                        }
                        // Decline button — just collapse
                        float declineX = w - actionBtnW - 12.0f;
                        if (hitRect(localPos, declineX, actionBtnY, actionBtnW, actionBtnH)) {
                            expandedQuestIndex_ = -1;
                            return true;
                        }
                    }

                    // Clicking expanded row (but not on an action button) collapses it
                    expandedQuestIndex_ = -1;
                } else {
                    // Expand this quest
                    expandedQuestIndex_ = i;
                }
                return true;
            }

            curY += rowH + 2.0f;
        }
    }

    return true; // consume all clicks on panel
}

// ---------------------------------------------------------------------------
// Input: onKeyInput
// ---------------------------------------------------------------------------
bool NpcDialoguePanel::onKeyInput(int scancode, bool pressed) {
    if (!pressed) return false;

    if (scancode == SDL_SCANCODE_ESCAPE) {
        close();
        return true;
    }

    return false;
}

} // namespace fate
