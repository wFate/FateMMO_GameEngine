# Client Message Handlers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire the 7 new server response packet types (SvTradeUpdate, SvMarketResult, SvBountyUpdate, SvGauntletUpdate, SvGuildUpdate, SvSocialUpdate, SvQuestUpdate) through the client's NetClient so they're deserialized and dispatched to the UI.

**Architecture:** For each new SvX message type: add a `std::function` callback to `NetClient`, add a `case` in `NetClient::handlePacket()` that deserializes the payload and invokes the callback, then register the callback in `GameApp::onInit()`. The callbacks route to the ChatUI for notifications (trade invite, guild update, etc.) and later to dedicated UI panels as they're built.

**Tech Stack:** C++20, existing NetClient callback pattern, existing message structs from `engine/net/game_messages.h`

**Depends on:** Chat UI plan (messages display via ChatUI)

---

## File Map

| File | Change |
|---|---|
| `engine/net/net_client.h` | Add 7 new `std::function` callback members |
| `engine/net/net_client.cpp` | Add 7 new `case` blocks in `handlePacket()` |
| `game/game_app.cpp` | Register 7 new callbacks in `onInit()` that route to ChatUI |

---

### Task 1: Add Callback Declarations to NetClient

**Files:**
- Modify: `engine/net/net_client.h`

- [ ] **Step 1: Add 7 new callback members after the existing ones**

After the existing `onLootPickup` callback, add:

```cpp
std::function<void(const SvTradeUpdateMsg&)> onTradeUpdate;
std::function<void(const SvMarketResultMsg&)> onMarketResult;
std::function<void(const SvBountyUpdateMsg&)> onBountyUpdate;
std::function<void(const SvGauntletUpdateMsg&)> onGauntletUpdate;
std::function<void(const SvGuildUpdateMsg&)> onGuildUpdate;
std::function<void(const SvSocialUpdateMsg&)> onSocialUpdate;
std::function<void(const SvQuestUpdateMsg&)> onQuestUpdate;
```

- [ ] **Step 2: Add `#include "engine/net/game_messages.h"` to net_client.h**

- [ ] **Step 3: Commit**

---

### Task 2: Add Packet Deserialization to handlePacket

**Files:**
- Modify: `engine/net/net_client.cpp`

- [ ] **Step 1: Add 7 new case blocks in handlePacket's switch statement**

After the existing `case PacketType::SvLootPickup:` block, add cases for each new type. Pattern for each:

```cpp
case PacketType::SvTradeUpdate: {
    auto msg = SvTradeUpdateMsg::read(payload);
    if (onTradeUpdate) onTradeUpdate(msg);
    break;
}
case PacketType::SvMarketResult: {
    auto msg = SvMarketResultMsg::read(payload);
    if (onMarketResult) onMarketResult(msg);
    break;
}
case PacketType::SvBountyUpdate: {
    auto msg = SvBountyUpdateMsg::read(payload);
    if (onBountyUpdate) onBountyUpdate(msg);
    break;
}
case PacketType::SvGauntletUpdate: {
    auto msg = SvGauntletUpdateMsg::read(payload);
    if (onGauntletUpdate) onGauntletUpdate(msg);
    break;
}
case PacketType::SvGuildUpdate: {
    auto msg = SvGuildUpdateMsg::read(payload);
    if (onGuildUpdate) onGuildUpdate(msg);
    break;
}
case PacketType::SvSocialUpdate: {
    auto msg = SvSocialUpdateMsg::read(payload);
    if (onSocialUpdate) onSocialUpdate(msg);
    break;
}
case PacketType::SvQuestUpdate: {
    auto msg = SvQuestUpdateMsg::read(payload);
    if (onQuestUpdate) onQuestUpdate(msg);
    break;
}
```

- [ ] **Step 2: Add `#include "engine/net/game_messages.h"` to net_client.cpp if not already present**

- [ ] **Step 3: Build and verify compiles**

- [ ] **Step 4: Commit**

---

### Task 3: Register Callbacks in GameApp

**Files:**
- Modify: `game/game_app.cpp`

- [ ] **Step 1: In onInit(), after the existing callback registrations, add handlers for all 7 types**

Each handler routes the message to the ChatUI as a system notification. Later, dedicated UI panels can override these.

```cpp
netClient_.onTradeUpdate = [this](const SvTradeUpdateMsg& msg) {
    // Show in chat as system message
    std::string text;
    switch (msg.updateType) {
        case 0: text = msg.otherPlayerName + " invited you to trade."; break;
        case 1: text = "Trade session started."; break;
        case 5: text = "Trade completed!"; break;
        case 6: text = msg.otherPlayerName.empty() ? "Trade cancelled." : msg.otherPlayerName; break;
        default: text = "Trade update."; break;
    }
    chatUI_.addMessage(6, "[Trade]", text, 0); // channel 6 = System
};

netClient_.onMarketResult = [this](const SvMarketResultMsg& msg) {
    chatUI_.addMessage(6, "[Market]", msg.message, 0);
};

netClient_.onBountyUpdate = [this](const SvBountyUpdateMsg& msg) {
    chatUI_.addMessage(6, "[Bounty]", msg.message, 0);
};

netClient_.onGauntletUpdate = [this](const SvGauntletUpdateMsg& msg) {
    chatUI_.addMessage(6, "[Gauntlet]", msg.message, 0);
};

netClient_.onGuildUpdate = [this](const SvGuildUpdateMsg& msg) {
    std::string text = msg.message;
    if (!msg.guildName.empty()) text = "[" + msg.guildName + "] " + text;
    chatUI_.addMessage(6, "[Guild]", text, 0);
};

netClient_.onSocialUpdate = [this](const SvSocialUpdateMsg& msg) {
    chatUI_.addMessage(6, "[Social]", msg.message, 0);
};

netClient_.onQuestUpdate = [this](const SvQuestUpdateMsg& msg) {
    chatUI_.addMessage(6, "[Quest]", msg.message, 0);
};
```

- [ ] **Step 2: Build and run**

- [ ] **Step 3: Commit**

---

### Task 4: Update Docs

- [ ] **Step 1: Update ENGINE_STATE_AND_FEATURES.md with changelog entry**
- [ ] **Step 2: Commit**
