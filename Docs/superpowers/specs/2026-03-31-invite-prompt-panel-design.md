# Invite Prompt Panel + PartyFrame Serialization Design

**Date:** 2026-03-31
**Scope:** Reusable InvitePromptPanel widget for party/guild/trade invites, PartyFrame magic number extraction, server-side busy flag

---

## Overview

Add a single reusable invite prompt panel to the HUD that handles party, guild, and trade invites via Accept/Decline buttons. Replace the current chat-message-only invite flow. Extract all hard-coded visual values from PartyFrame into serialized fields. Every visual property on both widgets is fully serialized, deserialized, and editable in the inspector.

---

## 1. InvitePromptPanel Widget

### 1a. Class Definition

New `UINode` subclass: `InvitePromptPanel` in `engine/ui/widgets/invite_prompt_panel.h`.

**Invite type enum:**
```cpp
enum class InviteType : uint8_t { None = 0, Party = 1, Guild = 2, Trade = 3 };
```

**Serialized layout properties (TWOM defaults, all editable in inspector):**
- `titleOffset` (Vec2, default {0,0}) — title text position adjustment
- `messageOffset` (Vec2, default {0,0}) — message text position adjustment
- `buttonOffset` (Vec2, default {0,0}) — button row position adjustment
- `titleFontSize` (float, default 14.0) — "Party Invite" / "Guild Invite" / "Trade Request"
- `messageFontSize` (float, default 12.0) — "[PlayerName] has invited you"
- `buttonFontSize` (float, default 12.0)
- `panelWidth` (float, default 260.0)
- `panelHeight` (float, default 120.0)
- `buttonWidth` (float, default 80.0)
- `buttonHeight` (float, default 28.0)
- `buttonSpacing` (float, default 16.0)
- `borderWidth` (float, default 1.5)
- `titlePadTop` (float, default 10.0) — top padding for title text
- `messagePadTop` (float, default 8.0) — gap between title and message
- `buttonPadBottom` (float, default 12.0) — bottom padding for buttons

**Serialized colors:**
- `bgColor` (Color, default {0.08, 0.08, 0.11, 0.95})
- `borderColor` (Color, default {0.6, 0.5, 0.25, 0.9})
- `titleColor` (Color, default {0.95, 0.82, 0.45, 1.0}) — gold header
- `messageColor` (Color, default {0.92, 0.90, 0.85, 1.0})
- `acceptBtnColor` (Color, default {0.18, 0.45, 0.18, 1.0}) — green
- `acceptBtnHoverColor` (Color, default {0.25, 0.58, 0.25, 1.0})
- `acceptBtnTextColor` (Color, default {1, 1, 1, 1})
- `declineBtnColor` (Color, default {0.50, 0.20, 0.18, 1.0}) — red
- `declineBtnHoverColor` (Color, default {0.65, 0.28, 0.25, 1.0})
- `declineBtnTextColor` (Color, default {1, 1, 1, 1})

**Runtime state (NOT serialized):**
- `InviteType inviteType_ = InviteType::None`
- `std::string inviterName_`, `inviterCharId_`
- `bool acceptHovered_`, `declineHovered_`, `acceptPressed_`, `declinePressed_`

**Callbacks:**
- `std::function<void(InviteType type, const std::string& charId)> onAccept`
- `std::function<void(InviteType type, const std::string& charId)> onDecline`

**Public API:**
- `void showInvite(InviteType type, const std::string& inviterName, const std::string& charId)` — sets fields, makes visible
- `void dismiss(const std::string& reason)` — hides, fires no callback (server-forced close). Reason shown via system chat by the caller.
- `bool isBusy() const` — returns `visible_` (true when prompt is showing)
- `void hide()` — clears all state, sets invisible

### 1b. Rendering

Title line at top: "Party Invite" / "Guild Invite" / "Trade Request" (derived from `inviteType_`).

Message below: "[inviterName_] has invited you" for party/guild, "[inviterName_] wants to trade" for trade.

Accept and Decline buttons centered at bottom, side by side. Accept on left, Decline on right.

All positions computed from serialized offsets and padding fields. Zero hard-coded magic numbers in render().

The panel uses `onPress`/`onRelease` for button interaction (same pattern as ConfirmDialog). Modal — consumes all clicks inside the panel bounds.

### 1c. Serialization

- `ui_serializer.cpp`: write all layout floats, Vec2s, and Colors for type `"invite_prompt"`
- `ui_manager.cpp`: read all fields with defaults for type `"invite_prompt"`
- `ui_editor_panel.cpp`: inspector with Position Offsets tree, Layout tree (all float fields), Colors tree (all 10 colors)
- `fate_hud.json`: new node `"id": "invite_prompt"`, `"type": "invite_prompt"`, `"visible": false`, `"zOrder": 130`

---

## 2. Server-Side Busy Flag

### 2a. ClientConnection Field

Add `bool hasActivePrompt = false` to `ClientConnection` (or the server's per-client connection struct).

### 2b. Setting the Flag

When the server is about to send an invite to a target client:
1. Check `hasActivePrompt` — if true, fail the invite with "Player is busy"
2. If false, set `hasActivePrompt = true`, then send the invite

This applies to:
- Party invite (party_handler.cpp, Invite case)
- Guild invite (guild_handler.cpp, Invite case)
- Trade initiate (trade_handler.cpp, Initiate case)

### 2c. Clearing the Flag

`hasActivePrompt` is cleared when:
- Client sends an accept or decline response for any invite type
- Client disconnects (existing disconnect handler)
- Client transitions to a new zone/scene (zone_transition_handler or server_app.cpp zone transition code)

### 2d. Loading Screen / Scene Transition

On zone transition, the server clears `hasActivePrompt`. The client hides the invite prompt (same cleanup block where `chatPanel_` resets and `playerContextMenu_->hide()` runs).

### 2e. Inviter Disconnect / Party Disband

When the inviter disconnects or the party/guild/trade is cancelled:
- Party: server sends `SvPartyUpdate` with `PartyEvent::Disbanded` → client calls `dismiss("Party was disbanded")`
- Trade: server sends `SvTradeUpdate` with cancel type → client calls `dismiss("Trade cancelled")`
- Guild: server sends `SvGuildUpdate` with error → client calls `dismiss("Guild invite withdrawn")`
- In all cases, the server also clears `hasActivePrompt` on the target client

---

## 3. Client Wiring (game_app.cpp)

### 3a. Widget Lookup

After HUD loads, find `InvitePromptPanel*` via `findById("invite_prompt")`. Store as `invitePrompt_` member (same pattern as `playerContextMenu_`). Wire in both initial-load and undo-rewire locations.

### 3b. Accept/Decline Callbacks

```
onAccept(type, charId):
    Party  → sendPartyAction(PartyAction::AcceptInvite, charId)
    Guild  → sendGuildAction(GuildAction::AcceptInvite, charId)
    Trade  → sendTradeAction(TradeAction::AcceptInvite, charId)
    hide()

onDecline(type, charId):
    Party  → sendPartyAction(PartyAction::DeclineInvite, charId)
    Guild  → sendGuildAction(GuildAction::DeclineInvite, charId)  // new sub-action value
    Trade  → sendTradeAction(TradeAction::Cancel)
    hide()
```

### 3c. Incoming Invite Handling

Replace current chat-only approach:

- `onPartyUpdate` with `PartyEvent::Invited`:
  - If `invitePrompt_->isBusy()` — ignore (server should have blocked, but defensive check)
  - Otherwise → `invitePrompt_->showInvite(InviteType::Party, name, charId)`

- `onTradeUpdate` with `updateType==0` (invited):
  - Same busy check → `invitePrompt_->showInvite(InviteType::Trade, name, charId)`

- `onGuildUpdate` needs a new `updateType` value for invite received (currently: 0=created, 1=joined, 2=left, 3=disbanded, 4=rankChanged, 5=result). Use `updateType==6` for invite received.
  - `invitePrompt_->showInvite(InviteType::Guild, guildName, inviterCharId)`

### 3d. Server Dismiss Handling

- `onPartyUpdate` with `PartyEvent::Disbanded` or `PartyEvent::Kicked` while prompt is showing → `invitePrompt_->dismiss("Party was disbanded")`
- `onTradeUpdate` with cancel → `invitePrompt_->dismiss("Trade cancelled")`
- Zone transition cleanup → `invitePrompt_->hide()`

---

## 4. PartyFrame Magic Number Extraction

9 hard-coded values in `party_frame.cpp` become serialized fields on `PartyFrame`:

| New Field | Type | Default | Replaces |
|-----------|------|---------|----------|
| `portraitPadLeft` | float | 6.0 | Portrait left margin (line 51) |
| `portraitRimWidth` | float | 1.5 | Portrait ring stroke (line 55) |
| `crownSize` | float | 5.0 | Leader diamond size (line 62) |
| `textGapAfterPortrait` | float | 6.0 | Gap after portrait circle (line 68) |
| `textPadRight` | float | 4.0 | Right padding for text area (line 69) |
| `namePadTop` | float | 6.0 | Name top padding (line 72) |
| `levelPadRight` | float | 5.0 | Level text right margin (line 79) |
| `barOffsetY` | float | 13.0 | HP bar Y below name (line 85) |
| `barGap` | float | 2.0 | Gap between HP and MP bars (line 99) |

All 9 added to:
- `party_frame.h` — public float fields
- `party_frame.cpp` — render() uses fields instead of literals
- `ui_serializer.cpp` — write all 9 floats
- `ui_manager.cpp` — read all 9 with defaults
- `ui_editor_panel.cpp` — DragFloat editors under existing "Layout" tree
- `fate_hud.json` — party_frame node gains 9 new keys

---

## 5. Guild Invite Protocol Changes

**SvGuildUpdateMsg.updateType** gains value `6` for invite-received. The server guild_handler Invite case sends this to the target client (currently sends `updateType=1` "joined" which is wrong — the target should see an invite prompt, not auto-join).

**GuildAction** gains `DeclineInvite = 8` (values 0-7 are taken). Used when the target declines a guild invite.

Guild invite flow changes:
- **Current:** Server receives Invite → immediately adds member to DB → sends "joined" to target
- **New:** Server receives Invite → sends `updateType=6` (invite) to target with inviter charId and guild name → waits for `GuildAction::AcceptInvite` or `GuildAction::DeclineInvite` → on accept: adds to DB and sends "joined"; on decline: notifies inviter

This requires the guild_handler.cpp Invite case to be reworked: instead of calling `addMember` immediately, it stores a pending invite (in-memory on the target's `ClientConnection`, e.g. `pendingGuildInviteId` + `pendingGuildInviteFromCharId`) and waits for the accept/decline response. The `AcceptInvite` handler (currently unimplemented) performs the actual `addMember` call.

---

## Files Modified (Estimated)

**New files:**
- `engine/ui/widgets/invite_prompt_panel.h`
- `engine/ui/widgets/invite_prompt_panel.cpp`

**Engine (tracked):**
- `engine/ui/widgets/party_frame.h` — 9 new float fields
- `engine/ui/widgets/party_frame.cpp` — use fields instead of magic numbers
- `engine/ui/ui_serializer.cpp` — InvitePromptPanel + PartyFrame new fields
- `engine/ui/ui_manager.cpp` — InvitePromptPanel + PartyFrame new fields
- `engine/editor/ui_editor_panel.cpp` — InvitePromptPanel + PartyFrame inspector
- `engine/net/game_messages.h` — guild updateType=6 documentation
- `assets/ui/screens/fate_hud.json` — invite_prompt node + party_frame new fields

**Server (gitignored):**
- `server/server_app.cpp` — hasActivePrompt clear on disconnect/zone transition
- `server/handlers/party_handler.cpp` — check hasActivePrompt before invite
- `server/handlers/guild_handler.cpp` — rework Invite to pending + check hasActivePrompt
- `server/handlers/trade_handler.cpp` — check hasActivePrompt before initiate
- Connection struct — `bool hasActivePrompt` field

**Game (gitignored):**
- `game/game_app.cpp` — invitePrompt_ wiring, accept/decline callbacks, incoming invite handling, zone transition cleanup
- `game/game_app.h` — `InvitePromptPanel* invitePrompt_` member
