# Batch F: Admin & Storage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire vault/bank to server with network messages, add GM command framework with 9 commands, enforce ban checks on login.

**Architecture:** Bank uses existing BankRepository with new CmdBank/SvBankUpdate messages. GM commands are parsed from chat messages (detect "/" prefix), dispatched via a command registry, and require admin_role validation.

**Tech Stack:** C++23, doctest, existing BankRepository (libpqxx).

**Spec:** `Docs/superpowers/specs/2026-03-21-batch-f-admin-storage-design.md`

---

### Task 1: Bank Protocol Messages + Tests

**Files:**
- Modify: `engine/net/packet.h` — Add CmdBank (0x25), SvBankUpdate (0xAF)
- Modify: `engine/net/game_messages.h` — Add message structs
- Modify: `tests/test_protocol.cpp` — Serialization tests

- [ ] **Step 1: Add packet types and message structs**

Packet types:
```cpp
    constexpr uint8_t CmdBank      = 0x25;  // Client -> Server
    constexpr uint8_t SvBankUpdate = 0xAF;  // Server -> Client
```

Message structs:
```cpp
struct CmdBankMsg {
    uint8_t action = 0;       // 0=DepositItem, 1=WithdrawItem, 2=DepositGold, 3=WithdrawGold
    uint8_t inventorySlot = 0;
    uint8_t bankSlot = 0;
    int32_t quantity = 0;     // item qty or gold amount

    void write(ByteWriter& w) const {
        w.writeU8(action); w.writeU8(inventorySlot);
        w.writeU8(bankSlot); w.writeI32(quantity);
    }
    static CmdBankMsg read(ByteReader& r) {
        CmdBankMsg m;
        m.action = r.readU8(); m.inventorySlot = r.readU8();
        m.bankSlot = r.readU8(); m.quantity = r.readI32();
        return m;
    }
};

struct SvBankUpdateMsg {
    uint8_t action = 0;
    uint8_t success = 0;
    int64_t bankGold = 0;
    std::string message;

    void write(ByteWriter& w) const {
        w.writeU8(action); w.writeU8(success);
        w.writeI64(bankGold); w.writeString(message);
    }
    static SvBankUpdateMsg read(ByteReader& r) {
        SvBankUpdateMsg m;
        m.action = r.readU8(); m.success = r.readU8();
        m.bankGold = r.readI64(); m.message = r.readString();
        return m;
    }
};
```

**NOTE:** Check if ByteWriter has `writeI64`/`readI64`. If not, use `writeU64`/`readU64` with casts, or split into two I32s. Look at how gold is serialized in other messages (SvPlayerStateMsg has `int64_t gold`).

- [ ] **Step 2: Add serialization tests and build**

- [ ] **Step 3: Commit**
```bash
git add engine/net/packet.h engine/net/game_messages.h tests/test_protocol.cpp
git commit -m "feat: add bank deposit/withdraw protocol messages"
```

---

### Task 2: Bank Server Handler

**Files:**
- Modify: `server/server_app.h` — Declare processBankCommand()
- Modify: `server/server_app.cpp` — Packet dispatch + handler
- Modify: `engine/net/net_client.h/cpp` — onBankUpdate callback

- [ ] **Step 1: Add handler**

Declare in `server/server_app.h`:
```cpp
void processBankCommand(uint16_t clientId, const CmdBankMsg& msg);
```

Add dispatch in packet switch:
```cpp
case PacketType::CmdBank: {
    auto msg = CmdBankMsg::read(payload);
    processBankCommand(clientId, msg);
    break;
}
```

Implement `processBankCommand()` — handle all 4 sub-actions using existing `bankRepo_` methods:
- DepositItem: `inv->inventory.getSlot()` → validate → `inv->inventory.removeItem()` → `bankRepo_->depositItem()`
- WithdrawItem: `bankRepo_->withdrawItem()` → `inv->inventory.addItem()`
- DepositGold: validate amount → `inv->inventory.setGold(current - amount)` → `bankRepo_->depositGold()`
- WithdrawGold: `bankRepo_->withdrawGold()` → `inv->inventory.setGold(current + amount)`

Send `SvBankUpdateMsg` after each operation with success/fail + current bank gold.

Read `server/db/bank_repository.h` to get exact method signatures before coding.

- [ ] **Step 2: Add client callback**

```cpp
std::function<void(const SvBankUpdateMsg&)> onBankUpdate;
```

- [ ] **Step 3: Build and test**

- [ ] **Step 4: Commit**
```bash
git add server/server_app.cpp server/server_app.h engine/net/net_client.h engine/net/net_client.cpp
git commit -m "feat: add server bank handler with deposit/withdraw for items and gold"
```

---

### Task 3: GM Command Framework + Tests

**Files:**
- Create: `server/gm_commands.h` — Command registry, parser, handlers
- Create: `tests/test_gm_commands.cpp` — Parser and permission tests
- Create: `Docs/migrations/009_admin_roles.sql` — admin_role column

- [ ] **Step 1: Create migration**

```sql
-- Migration 009: Admin roles for GM commands
ALTER TABLE accounts ADD COLUMN IF NOT EXISTS admin_role INTEGER DEFAULT 0;
-- 0 = player, 1 = GM, 2 = admin
```

- [ ] **Step 2: Write GM command tests**

```cpp
#include <doctest/doctest.h>
#include "server/gm_commands.h"

using namespace fate;

TEST_SUITE("GMCommands") {

TEST_CASE("Parse simple command") {
    auto result = GMCommandParser::parse("/kick TestPlayer");
    CHECK(result.isCommand);
    CHECK(result.commandName == "kick");
    CHECK(result.args.size() == 1);
    CHECK(result.args[0] == "TestPlayer");
}

TEST_CASE("Parse command with multiple args") {
    auto result = GMCommandParser::parse("/ban TestPlayer 60 cheating");
    CHECK(result.commandName == "ban");
    CHECK(result.args.size() == 3);
    CHECK(result.args[0] == "TestPlayer");
    CHECK(result.args[1] == "60");
    CHECK(result.args[2] == "cheating");
}

TEST_CASE("Non-command returns isCommand=false") {
    auto result = GMCommandParser::parse("hello world");
    CHECK_FALSE(result.isCommand);
}

TEST_CASE("Empty command returns isCommand=false") {
    auto result = GMCommandParser::parse("/");
    CHECK_FALSE(result.isCommand);
}

TEST_CASE("Registry: can register and find command") {
    GMCommandRegistry registry;
    registry.registerCommand({"kick", 1, nullptr});
    auto* cmd = registry.findCommand("kick");
    REQUIRE(cmd != nullptr);
    CHECK(cmd->minRole == 1);
}

TEST_CASE("Registry: unknown command returns nullptr") {
    GMCommandRegistry registry;
    CHECK(registry.findCommand("unknown") == nullptr);
}

TEST_CASE("Permission: role 0 cannot use GM command (minRole=1)") {
    GMCommandRegistry registry;
    registry.registerCommand({"kick", 1, nullptr});
    auto* cmd = registry.findCommand("kick");
    CHECK_FALSE(GMCommandRegistry::hasPermission(0, cmd->minRole));
}

TEST_CASE("Permission: role 1 can use GM command") {
    GMCommandRegistry registry;
    registry.registerCommand({"kick", 1, nullptr});
    auto* cmd = registry.findCommand("kick");
    CHECK(GMCommandRegistry::hasPermission(1, cmd->minRole));
}

TEST_CASE("Permission: role 2 can use admin command") {
    CHECK(GMCommandRegistry::hasPermission(2, 2));
}

TEST_CASE("Permission: role 1 cannot use admin command") {
    CHECK_FALSE(GMCommandRegistry::hasPermission(1, 2));
}

} // TEST_SUITE
```

- [ ] **Step 3: Implement gm_commands.h**

```cpp
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <sstream>

namespace fate {

struct ParsedCommand {
    bool isCommand = false;
    std::string commandName;
    std::vector<std::string> args;
};

struct GMCommandParser {
    static ParsedCommand parse(const std::string& message) {
        ParsedCommand result;
        if (message.empty() || message[0] != '/') return result;
        if (message.size() <= 1) return result;

        std::istringstream iss(message.substr(1)); // skip '/'
        iss >> result.commandName;
        if (result.commandName.empty()) return result;

        result.isCommand = true;
        std::string arg;
        while (iss >> arg) {
            result.args.push_back(arg);
        }
        return result;
    }
};

struct GMCommand {
    std::string name;
    int minRole = 1; // 0=player, 1=GM, 2=admin
    std::function<void(uint16_t callerClientId, const std::vector<std::string>& args)> handler;
};

class GMCommandRegistry {
public:
    void registerCommand(GMCommand cmd) {
        commands_[cmd.name] = std::move(cmd);
    }

    const GMCommand* findCommand(const std::string& name) const {
        auto it = commands_.find(name);
        return it != commands_.end() ? &it->second : nullptr;
    }

    static bool hasPermission(int playerRole, int requiredRole) {
        return playerRole >= requiredRole;
    }

private:
    std::unordered_map<std::string, GMCommand> commands_;
};

} // namespace fate
```

- [ ] **Step 4: Build and run tests**
```bash
cmake --build build --target fate_tests && ./build/Debug/fate_tests --test-suite="GMCommands"
```

- [ ] **Step 5: Commit**
```bash
git add server/gm_commands.h tests/test_gm_commands.cpp Docs/migrations/009_admin_roles.sql
git commit -m "feat: add GM command parser and registry with permission checks"
```

---

### Task 4: Wire GM Commands into Server

**Files:**
- Modify: `server/server_app.h` — Add GMCommandRegistry, admin role tracking
- Modify: `server/server_app.cpp` — Register 9 commands, intercept chat, ban enforcement on login

- [ ] **Step 1: Add GMCommandRegistry to ServerApp**

In `server/server_app.h`:
```cpp
#include "server/gm_commands.h"
GMCommandRegistry gmCommands_;
std::unordered_map<uint16_t, int> clientAdminRoles_; // clientId → admin_role
void initGMCommands();
```

- [ ] **Step 2: Load admin_role on auth**

In the auth flow where a new session is accepted, after loading account data, store the admin role:
```cpp
clientAdminRoles_[clientId] = accountRecord.admin_role; // default 0
```

Clean up in `onClientDisconnected`:
```cpp
clientAdminRoles_.erase(clientId);
```

- [ ] **Step 3: Enforce ban check on login**

In the auth flow (where pending sessions are consumed), before accepting:
```cpp
if (accountRecord.is_banned) {
    // Check if ban has expired
    if (accountRecord.ban_expires_at > 0 && currentTime > accountRecord.ban_expires_at) {
        // Clear expired ban in DB
    } else {
        // Reject login with ban reason
        return;
    }
}
```

Read the auth handling code to find the exact location and how to send a rejection.

- [ ] **Step 4: Intercept chat commands**

In the `CmdChat` handler (find `case PacketType::CmdChat`), before broadcasting:

```cpp
auto parsed = GMCommandParser::parse(chatMsg.message);
if (parsed.isCommand) {
    auto* cmd = gmCommands_.findCommand(parsed.commandName);
    if (!cmd) {
        // Send "Unknown command" to caller only
        return;
    }
    int role = clientAdminRoles_.count(clientId) ? clientAdminRoles_[clientId] : 0;
    if (!GMCommandRegistry::hasPermission(role, cmd->minRole)) {
        // Send "Insufficient permission" to caller only
        return;
    }
    cmd->handler(clientId, parsed.args);
    return; // don't broadcast command as chat
}
// ... existing chat broadcast continues
```

- [ ] **Step 5: Register all 9 commands in initGMCommands()**

```cpp
void ServerApp::initGMCommands() {
    // /kick <player>
    gmCommands_.registerCommand({"kick", 1, [this](uint16_t callerId, const auto& args) {
        if (args.empty()) { /* send usage */ return; }
        auto targetId = findClientByName(args[0]);
        if (targetId == 0) { /* send "player not found" */ return; }
        server_.disconnect(targetId);
        LOG_INFO("GM", "Client %d kicked '%s'", callerId, args[0].c_str());
        // Send confirmation to caller
    }});

    // /ban <player> <minutes> <reason>
    gmCommands_.registerCommand({"ban", 1, [this](uint16_t callerId, const auto& args) {
        if (args.size() < 2) { /* send usage */ return; }
        auto targetId = findClientByName(args[0]);
        int minutes = std::atoi(args[1].c_str());
        std::string reason = args.size() > 2 ? args[2] : "No reason";
        // Set is_banned=true, ban_expires_at, ban_reason in DB
        // Kick the player
        if (targetId != 0) server_.disconnect(targetId);
        LOG_INFO("GM", "Client %d banned '%s' for %d min: %s", callerId, args[0].c_str(), minutes, reason.c_str());
    }});

    // /unban <player>
    gmCommands_.registerCommand({"unban", 1, [this](uint16_t callerId, const auto& args) {
        if (args.empty()) return;
        // Clear is_banned in DB by character name → account lookup
        LOG_INFO("GM", "Client %d unbanned '%s'", callerId, args[0].c_str());
    }});

    // /tp <player> — teleport self to target
    gmCommands_.registerCommand({"tp", 1, [this](uint16_t callerId, const auto& args) {
        if (args.empty()) return;
        auto targetId = findClientByName(args[0]);
        if (targetId == 0) return;
        // Get target's position and scene
        // Set caller's position and scene to match
        // Send SvZoneTransition if different scene, or just update position
        LOG_INFO("GM", "Client %d teleported to '%s'", callerId, args[0].c_str());
    }});

    // /tphere <player> — summon target to self
    gmCommands_.registerCommand({"tphere", 1, [this](uint16_t callerId, const auto& args) {
        if (args.empty()) return;
        auto targetId = findClientByName(args[0]);
        if (targetId == 0) return;
        // Get caller's position and scene
        // Set target's position and scene to match
        LOG_INFO("GM", "Client %d summoned '%s'", callerId, args[0].c_str());
    }});

    // /announce <message>
    gmCommands_.registerCommand({"announce", 1, [this](uint16_t callerId, const auto& args) {
        if (args.empty()) return;
        // Join all args into one message
        std::string msg;
        for (const auto& a : args) { if (!msg.empty()) msg += " "; msg += a; }
        // Broadcast as system chat
        LOG_INFO("GM", "Client %d announced: %s", callerId, msg.c_str());
    }});

    // /setlevel <player> <level> — Admin only
    gmCommands_.registerCommand({"setlevel", 2, [this](uint16_t callerId, const auto& args) {
        if (args.size() < 2) return;
        auto targetId = findClientByName(args[0]);
        if (targetId == 0) return;
        int level = std::atoi(args[1].c_str());
        if (level < 1 || level > 50) return;
        // Set target's level, recalculate stats
        LOG_INFO("GM", "Client %d set '%s' to level %d", callerId, args[0].c_str(), level);
    }});

    // /additem <player> <itemId> [quantity] — Admin only
    gmCommands_.registerCommand({"additem", 2, [this](uint16_t callerId, const auto& args) {
        if (args.size() < 2) return;
        auto targetId = findClientByName(args[0]);
        if (targetId == 0) return;
        std::string itemId = args[1];
        int qty = args.size() > 2 ? std::atoi(args[2].c_str()) : 1;
        // Validate item exists in itemDefCache_, create ItemInstance, add to inventory
        LOG_INFO("GM", "Client %d gave '%s' %dx '%s'", callerId, args[0].c_str(), qty, itemId.c_str());
    }});

    // /addgold <player> <amount> — Admin only
    gmCommands_.registerCommand({"addgold", 2, [this](uint16_t callerId, const auto& args) {
        if (args.size() < 2) return;
        auto targetId = findClientByName(args[0]);
        if (targetId == 0) return;
        int64_t amount = std::atoll(args[1].c_str());
        // Add gold to target's inventory via setGold(current + amount)
        LOG_INFO("GM", "Client %d gave '%s' %lld gold", callerId, args[0].c_str(), amount);
    }});
}
```

**Helper needed:** `findClientByName(name)` — iterate connections, find player entity, check nameplate, return clientId or 0. Read how existing code looks up players by name.

- [ ] **Step 6: Call initGMCommands() at startup**

After other init calls:
```cpp
initGMCommands();
```

- [ ] **Step 7: Build and test**
```bash
touch server/server_app.cpp server/server_app.h
cmake --build build --target FateServer && cmake --build build --target fate_tests && ./build/Debug/fate_tests
```

- [ ] **Step 8: Commit**
```bash
git add server/server_app.cpp server/server_app.h
git commit -m "feat: wire 9 GM commands with chat parser and ban enforcement"
```

---

### Task 5: Final Integration

- [ ] **Step 1: Run full test suite**
```bash
cmake --build build --target fate_tests && ./build/Debug/fate_tests
```

- [ ] **Step 2: Build both targets**
```bash
cmake --build build --target FateServer && cmake --build build --target FateEngine
```

- [ ] **Step 3: Verify test count**
```bash
./build/Debug/fate_tests -c
```

**DB Migrations needed:**
```sql
-- Run migration 009
ALTER TABLE accounts ADD COLUMN IF NOT EXISTS admin_role INTEGER DEFAULT 0;

-- To make yourself admin:
UPDATE accounts SET admin_role = 2 WHERE username = 'your_username';
```

**REMINDER:** Restart FateServer.exe after deploying server changes.
