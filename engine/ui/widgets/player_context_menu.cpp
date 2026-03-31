#include "engine/ui/widgets/player_context_menu.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/sdf_text.h"

namespace fate {

PlayerContextMenu::PlayerContextMenu(const std::string& id)
    : UINode(id, "player_context_menu") {}

void PlayerContextMenu::show(const Vec2& screenPos, const std::string& charName,
                              const std::string& charId, uint64_t entityId,
                              bool sameFaction, bool inSafeZone, bool hasGuild) {
    targetCharName = charName;
    targetCharId = charId;
    targetEntityId = entityId;
    canTrade = sameFaction && inSafeZone;
    canParty = sameFaction;
    canAddFriend = sameFaction;
    canGuildInvite = sameFaction && hasGuild;

    // Position the menu at the click location
    auto& anch = anchor();
    anch.offset = screenPos;
    anch.size = { menuWidth * layoutScale_, headerHeight() + itemHeight * layoutScale_ * kItemCount };

    setVisible(true);
}

void PlayerContextMenu::hide() {
    setVisible(false);
    pressedItem_ = -1;
    if (onClose) onClose(id_);
}

float PlayerContextMenu::headerHeight() const {
    return (menuFontSize + 12.0f) * layoutScale_; // font + padding
}

Rect PlayerContextMenu::itemRect(int index) const {
    float s = layoutScale_;
    float y = headerHeight() + itemHeight * s * static_cast<float>(index);
    return { 0.0f, y, menuWidth * s, itemHeight * s };
}

bool PlayerContextMenu::onPress(const Vec2& localPos) {
    if (!enabled_) return false;
    float s = layoutScale_;

    // Check if click is inside the menu bounds
    float totalH = headerHeight() + itemHeight * s * kItemCount;
    if (localPos.x < 0 || localPos.x > menuWidth * s || localPos.y < 0 || localPos.y > totalH) {
        // Click outside — hide menu and let click propagate
        hide();
        return false;
    }

    // Check which item was pressed
    pressedItem_ = -1;
    for (int i = 0; i < kItemCount; ++i) {
        Rect r = itemRect(i);
        if (r.contains(localPos)) {
            // Only allow pressing enabled items
            if (i == kItemTrade && !canTrade) break;
            if (i == kItemParty && !canParty) break;
            if (i == kItemWhisper && !canParty) break;  // whisper = same faction
            if (i == kItemAddFriend && !canAddFriend) break;
            if (i == kItemGuildInvite && !canGuildInvite) break;
            pressedItem_ = i;
            break;
        }
    }

    return true; // consume click inside menu
}

void PlayerContextMenu::onRelease(const Vec2& localPos) {
    if (!enabled_ || pressedItem_ < 0) {
        pressedItem_ = -1;
        return;
    }

    Rect r = itemRect(pressedItem_);
    if (r.contains(localPos)) {
        switch (pressedItem_) {
            case kItemTrade:
                if (canTrade && onTrade) onTrade(targetCharId);
                break;
            case kItemParty:
                if (canParty && onPartyInvite) onPartyInvite(targetCharId);
                break;
            case kItemWhisper:
                if (canParty && onWhisper) onWhisper(targetCharId);
                break;
            case kItemAddFriend:
                if (canAddFriend && onAddFriend) onAddFriend(targetCharId);
                break;
            case kItemGuildInvite:
                if (canGuildInvite && onGuildInvite) onGuildInvite(targetCharId);
                break;
        }
        hide();
    }

    pressedItem_ = -1;
}

void PlayerContextMenu::render(SpriteBatch& batch, SDFText& sdf) {
    if (!visible_) return;

    const auto& rect = computedRect_;
    float d = static_cast<float>(zOrder_);
    float s = layoutScale_;
    float fontSize = scaledFont(menuFontSize);
    float totalH = headerHeight() + itemHeight * s * kItemCount;
    float sMenuWidth = menuWidth * s;

    // Dark semi-transparent background
    batch.drawRect({rect.x + sMenuWidth * 0.5f, rect.y + totalH * 0.5f},
                   {sMenuWidth, totalH}, bgColor, d);

    // Thin gold border
    float bw = 1.5f * s;
    float innerH = totalH - bw * 2.0f;
    batch.drawRect({rect.x + sMenuWidth * 0.5f, rect.y + bw * 0.5f}, {sMenuWidth, bw}, borderColor, d + 0.15f);
    batch.drawRect({rect.x + sMenuWidth * 0.5f, rect.y + totalH - bw * 0.5f}, {sMenuWidth, bw}, borderColor, d + 0.15f);
    batch.drawRect({rect.x + bw * 0.5f, rect.y + totalH * 0.5f}, {bw, innerH}, borderColor, d + 0.15f);
    batch.drawRect({rect.x + sMenuWidth - bw * 0.5f, rect.y + totalH * 0.5f}, {bw, innerH}, borderColor, d + 0.15f);

    // Player name header (gold/highlighted)
    {
        Vec2 nameSize = sdf.measure(targetCharName, fontSize);
        float nameX = rect.x + (sMenuWidth - nameSize.x) * 0.5f + nameOffset.x * s;
        float nameY = rect.y + (headerHeight() - nameSize.y) * 0.5f + nameOffset.y * s;
        sdf.drawScreen(batch, targetCharName, {nameX, nameY}, fontSize, nameHeaderColor, d + 0.2f);
    }

    // Separator line below header
    {
        float sepY = rect.y + headerHeight();
        batch.drawRect({rect.x + sMenuWidth * 0.5f, sepY}, {sMenuWidth - 8.0f * s, 1.0f * s}, separatorColor, d + 0.15f);
    }

    // Menu items
    const char* labels[kItemCount] = { "Trade", "Party Invite", "Whisper", "Add Friend", "Guild Invite" };
    bool enabled[kItemCount] = { canTrade, canParty, canParty, canAddFriend, canGuildInvite };

    for (int i = 0; i < kItemCount; ++i) {
        Rect ir = itemRect(i);
        float absX = rect.x + ir.x;
        float absY = rect.y + ir.y;

        // Hover/press highlight for enabled items
        if (enabled[i]) {
            bool isHovered = hovered_ && ir.contains({
                computedRect_.w * 0.5f, ir.y + ir.h * 0.5f // approximate
            });
            bool isPressed = (pressedItem_ == i);

            if (isPressed) {
                batch.drawRect({absX + ir.w * 0.5f, absY + ir.h * 0.5f},
                               {ir.w, ir.h}, pressedColor, d + 0.05f);
            }
        }

        // Text color: white for enabled, gray for disabled
        Color textColor = enabled[i] ? enabledTextColor : disabledTextColor;

        Vec2 textSize = sdf.measure(labels[i], fontSize);
        float textX = absX + 10.0f * s + itemOffset.x * s; // left-aligned with padding
        float textY = absY + (ir.h - textSize.y) * 0.5f + itemOffset.y * s;
        sdf.drawScreen(batch, labels[i], {textX, textY}, fontSize, textColor, d + 0.2f);
    }

    renderChildren(batch, sdf);
}

} // namespace fate
