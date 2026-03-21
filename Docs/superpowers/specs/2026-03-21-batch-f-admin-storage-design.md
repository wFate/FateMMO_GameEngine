# Batch F: Admin & Storage — Design Spec

**Date:** 2026-03-21
**Scope:** Wire vault/bank system to server, add GM command framework with 9 commands.

---

## 1. Vault/Bank (Wire Existing)

### Problem
BankStorage, BankRepository, BankStorageComponent, BankerComponent, and bank UI all exist. DB tables exist (character_bank, character_bank_gold). Bank gold loads on login and saves on logout. But there are no network messages — bank item deposits/withdrawals are client-only and never reach the server mid-session.

### Design

**Network messages:**

`CmdBank` (client → server) — packet `0x25`:
- `uint8_t action` — 0=DepositItem, 1=WithdrawItem, 2=DepositGold, 3=WithdrawGold
- `uint8_t inventorySlot` — for item deposit/withdraw (inventory side)
- `uint8_t bankSlot` — for item deposit/withdraw (bank side)
- `int32_t quantity` — item quantity or gold amount

`SvBankUpdate` (server → client) — packet `0xAF`:
- `uint8_t action` — mirrors the request action
- `uint8_t success` — 1 if operation succeeded
- `int64_t bankGold` — current bank gold balance
- `std::string message` — result text

**Server handler flow (processBankCommand):**

**DepositItem (action 0):**
1. Validate: player is near a banker NPC (optional for MVP — skip distance check)
2. Validate: item exists in inventory slot, quantity valid
3. Validate: bank has space (30-slot limit)
4. Remove item from inventory
5. Add item to bank via `bankRepo_->depositItem()`
6. Send SvBankUpdate + SvInventorySync

**WithdrawItem (action 1):**
1. Validate: bank slot has the item
2. Validate: inventory has space
3. Remove from bank via `bankRepo_->withdrawItem()`
4. Add to inventory
5. Send SvBankUpdate + SvInventorySync

**DepositGold (action 2):**
1. Validate: player has enough gold
2. Deduct from inventory gold via `setGold(current - amount)`
3. Add to bank gold via `bankRepo_->depositGold()`
4. Send SvBankUpdate + sendPlayerState

**WithdrawGold (action 3):**
1. Validate: bank has enough gold
2. Deduct from bank via `bankRepo_->withdrawGold()`
3. Add to inventory gold via `setGold(current + amount)`
4. Send SvBankUpdate + sendPlayerState

### Files
- Modify: `engine/net/packet.h` — CmdBank (0x25), SvBankUpdate (0xAF)
- Modify: `engine/net/game_messages.h` — CmdBankMsg, SvBankUpdateMsg
- Modify: `server/server_app.h` — Declare processBankCommand()
- Modify: `server/server_app.cpp` — Packet dispatch + handler
- Modify: `engine/net/net_client.h/cpp` — onBankUpdate callback
- Modify: `tests/test_protocol.cpp` — Serialization tests

---

## 2. GM Command Framework

### Problem
No admin/GM system exists. `is_banned` field on accounts is never checked. No chat command parser. No way to moderate players or manage the game world.

### Design

**DB migration:** Add `admin_role` column to accounts table.
```sql
ALTER TABLE accounts ADD COLUMN IF NOT EXISTS admin_role INTEGER DEFAULT 0;
-- 0 = player, 1 = GM, 2 = admin
```

**Ban enforcement on login:** In the auth flow (where pending sessions are consumed), check `is_banned` and `ban_expires_at`. If banned and not expired, reject login with reason. If expired, clear the ban.

**Chat command parser:** In the `CmdChat` handler in `server/server_app.cpp`, before broadcasting:
1. Check if message starts with "/"
2. If yes, parse command name and arguments
3. Look up command in a registry
4. Check admin_role meets command requirement
5. Execute command handler
6. Do NOT broadcast the chat message (commands are private)

**Command registry:**

| Command | Syntax | Min Role | Action |
|---|---|---|---|
| /kick | /kick \<player\> | GM (1) | Disconnect player |
| /ban | /ban \<player\> \<minutes\> \<reason\> | GM (1) | Set is_banned + ban_expires_at + ban_reason |
| /unban | /unban \<player\> | GM (1) | Clear is_banned |
| /tp | /tp \<player\> | GM (1) | Teleport self to target player's position/scene |
| /tphere | /tphere \<player\> | GM (1) | Teleport target player to self |
| /announce | /announce \<message\> | GM (1) | System chat broadcast to all players |
| /setlevel | /setlevel \<player\> \<level\> | Admin (2) | Set player's level (1-50) |
| /additem | /additem \<player\> \<itemId\> [quantity] | Admin (2) | Give item to player |
| /addgold | /addgold \<player\> \<amount\> | Admin (2) | Give gold to player |

**Implementation:**

`GMCommandHandler` — processes parsed commands:
```cpp
struct GMCommand {
    std::string name;
    int minRole; // 0=player, 1=GM, 2=admin
    std::function<void(uint16_t callerClientId, const std::vector<std::string>& args)> handler;
};
```

Commands registered at server startup. Handler looks up target player by name using a name→clientId map.

**Admin role loading:** Store `admin_role` on `ClientConnection` (loaded during auth). Check before executing any GM command.

**GM action logging:** All GM actions logged via `LOG_INFO("GM", ...)` with caller name, target name, action, and parameters.

### Files
- Create: `server/gm_commands.h` — Command registry, parser, handlers
- Create: `Docs/migrations/009_admin_roles.sql` — admin_role column
- Modify: `server/server_app.h` — Add GMCommandHandler member, admin_role on ClientConnection or separate map
- Modify: `server/server_app.cpp` — Chat command intercept, init GM commands, ban check on login
- Modify: `engine/net/net_client.h` — No changes needed (uses existing chat for responses)
- Modify: `tests/test_protocol.cpp` — Bank message serialization tests

---

## Testing Plan

| Test | Validates |
|---|---|
| CmdBankMsg/SvBankUpdateMsg round-trip | Protocol |
| Bank deposit gold: inventory decreases, bank increases | Gold deposit |
| Bank withdraw gold: bank decreases, inventory increases | Gold withdraw |
| Bank deposit with insufficient gold rejected | Validation |
| Bank withdraw with insufficient bank gold rejected | Validation |
| GM command parser: "/kick player" → command="kick", args=["player"] | Parser |
| GM command parser: regular chat not intercepted | Non-command chat |
| GM command parser: "/unknown" → error message | Unknown command |
| GM role check: player cannot use /kick | Permission |
| GM role check: GM can use /kick | Permission |
| Admin role check: GM cannot use /setlevel | Admin-only |
| Ban check on login: banned account rejected | Ban enforcement |
| Ban expiry: expired ban cleared on login | Ban expiry |
