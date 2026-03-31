#pragma once
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_input.h"
#include <functional>
#include <string>

namespace fate {

class PlayerContextMenu : public UINode {
public:
    PlayerContextMenu(const std::string& id);
    void render(SpriteBatch& batch, SDFText& text) override;
    bool onPress(const Vec2& localPos) override;
    void onRelease(const Vec2& localPos) override;

    // Position offsets (unscaled, multiplied by layoutScale_ at render)
    Vec2 nameOffset = {0.0f, 0.0f};  // player name header
    Vec2 itemOffset = {0.0f, 0.0f};  // menu item text

    // Editable layout properties (serialized)
    float menuFontSize = 13.0f;
    float itemHeight = 28.0f;
    float menuWidth = 140.0f;

    // Colors
    Color bgColor = {0.08f, 0.08f, 0.11f, 0.92f};
    Color borderColor = {0.6f, 0.5f, 0.25f, 0.9f};
    Color nameHeaderColor = {0.95f, 0.82f, 0.45f, 1.0f};
    Color separatorColor = {0.4f, 0.35f, 0.2f, 0.7f};
    Color pressedColor = {0.25f, 0.22f, 0.12f, 0.6f};
    Color enabledTextColor = {0.92f, 0.90f, 0.85f, 1.0f};
    Color disabledTextColor = {0.45f, 0.43f, 0.40f, 0.7f};

    // Runtime state (NOT serialized)
    std::string targetCharName;
    std::string targetCharId;
    uint64_t targetEntityId = 0;
    bool canTrade = false;     // Same faction + safe zone
    bool canParty = false;
    bool canAddFriend = false;
    bool canGuildInvite = false;

    // Callbacks
    std::function<void(const std::string& charId)> onTrade;
    std::function<void(const std::string& charId)> onPartyInvite;
    std::function<void(const std::string& charId)> onWhisper;
    std::function<void(const std::string& charId)> onAddFriend;
    std::function<void(const std::string& charId)> onGuildInvite;
    UIClickCallback onClose;

    void show(const Vec2& screenPos, const std::string& charName, const std::string& charId,
              uint64_t entityId, bool sameFaction, bool inSafeZone, bool hasGuild = false);
    void hide();

private:
    // Menu item indices
    static constexpr int kItemTrade       = 0;
    static constexpr int kItemParty       = 1;
    static constexpr int kItemWhisper     = 2;
    static constexpr int kItemAddFriend   = 3;
    static constexpr int kItemGuildInvite = 4;
    static constexpr int kItemCount       = 5;

    int pressedItem_ = -1;

    // Returns the rect for a menu item in local space (relative to computedRect_)
    Rect itemRect(int index) const;
    // Header height for the player name
    float headerHeight() const;
};

} // namespace fate
