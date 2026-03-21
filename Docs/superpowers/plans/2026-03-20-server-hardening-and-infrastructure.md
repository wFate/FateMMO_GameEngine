# Server Hardening & Infrastructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close 14 critical security, reliability, and infrastructure gaps identified in the research audit — covering rate limiting, inventory safety, loot atomicity, chat filtering, IPv6, WAL crash recovery, DB circuit breaker, per-player mutation safety, Android builds, cross-platform fibers, mobile reconnection, connection cookies, server-side target validation, and CI/CD.

**Architecture:** Each subsystem is independently implementable and testable. Server-side changes are in `server/` and `engine/net/`. Shared game logic lives in `game/shared/`. The engine uses doctest for testing, C++20 (targeting C++23), CMake with FetchContent, and a 20 tick/sec headless server (`FateServer`).

**Tech Stack:** C++20, SDL2, Winsock2 (with POSIX compat), libpqxx 7.x, PostgreSQL, doctest 2.4.11, CMake 3.20+, GitHub Actions

**Codebase conventions:**
- All code in `namespace fate {}`
- Logging: `LOG_INFO("Tag", "msg %s", arg)` / `LOG_WARN` / `LOG_ERROR` (spdlog macros)
- Tests live in `tests/` with `test_*.cpp` naming
- Test runner: `fate_tests.exe` (doctest)
- Build from VS: `Ctrl+Shift+B`, run tests: `out/build/x64-Debug/fate_tests.exe`
- CRITICAL: `touch` all edited `.cpp` files before building (CMake misses changes silently)
- Server binary: `out/build/x64-Debug/FateServer.exe` — must restart after server code changes

---

## Task 1: Token Bucket Rate Limiting

**Files:**
- Create: `server/rate_limiter.h`
- Modify: `server/server_app.h` (add member)
- Modify: `server/server_app.cpp:1127` (hook into `onPacketReceived`)
- Test: `tests/test_rate_limiter.cpp`

Rate limiter uses per-client, per-message-type token buckets. O(1) per packet, ~1KB per client.

- [ ] **Step 1: Write failing tests for TokenBucket**

```cpp
// tests/test_rate_limiter.cpp
#include <doctest/doctest.h>
#include "server/rate_limiter.h"

using namespace fate;

TEST_CASE("TokenBucket: allows burst up to capacity") {
    TokenBucket bucket(3.0f, 1.0f); // burst=3, refill=1/sec
    CHECK(bucket.tryConsume(0.0) == true);
    CHECK(bucket.tryConsume(0.0) == true);
    CHECK(bucket.tryConsume(0.0) == true);
    CHECK(bucket.tryConsume(0.0) == false); // exhausted
}

TEST_CASE("TokenBucket: refills over time") {
    TokenBucket bucket(2.0f, 1.0f);
    bucket.tryConsume(0.0);
    bucket.tryConsume(0.0);
    CHECK(bucket.tryConsume(0.0) == false);
    CHECK(bucket.tryConsume(1.5) == true);  // 1.5s elapsed -> 1.5 tokens refilled
}

TEST_CASE("TokenBucket: tokens cap at capacity") {
    TokenBucket bucket(3.0f, 10.0f); // fast refill
    bucket.tryConsume(0.0);
    // After 100 seconds, should still cap at 3 (not exceed capacity)
    CHECK(bucket.tryConsume(100.0) == true);
    CHECK(bucket.tryConsume(200.0) == true);
    CHECK(bucket.tryConsume(300.0) == true);
    CHECK(bucket.tryConsume(300.0) == false); // same timestamp — no refill, bucket empty
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build out/build/x64-Debug && out/build/x64-Debug/fate_tests.exe -tc="TokenBucket*"`
Expected: FAIL — `rate_limiter.h` not found

- [ ] **Step 3: Write failing tests for ClientRateLimiter**

```cpp
// append to tests/test_rate_limiter.cpp
TEST_CASE("ClientRateLimiter: blocks flooding CmdUseSkill") {
    ClientRateLimiter limiter;
    // Skills: burst=3, sustained=1/sec
    double now = 0.0;
    CHECK(limiter.check(PacketType::CmdUseSkill, now) == RateLimitResult::Ok);
    CHECK(limiter.check(PacketType::CmdUseSkill, now) == RateLimitResult::Ok);
    CHECK(limiter.check(PacketType::CmdUseSkill, now) == RateLimitResult::Ok);
    CHECK(limiter.check(PacketType::CmdUseSkill, now) == RateLimitResult::Dropped);
}

TEST_CASE("ClientRateLimiter: tracks violations for disconnect") {
    ClientRateLimiter limiter;
    double now = 0.0;
    // Exhaust bucket
    for (int i = 0; i < 3; ++i) limiter.check(PacketType::CmdUseSkill, now);
    // Generate violations
    for (int i = 0; i < 49; ++i) {
        auto r = limiter.check(PacketType::CmdUseSkill, now);
        CHECK(r == RateLimitResult::Dropped);
    }
    auto r = limiter.check(PacketType::CmdUseSkill, now);
    CHECK(r == RateLimitResult::Disconnect); // 50th violation
}

TEST_CASE("ClientRateLimiter: movement has higher burst") {
    ClientRateLimiter limiter;
    double now = 0.0;
    for (int i = 0; i < 25; ++i) {
        CHECK(limiter.check(PacketType::CmdMove, now) == RateLimitResult::Ok);
    }
    CHECK(limiter.check(PacketType::CmdMove, now) == RateLimitResult::Dropped);
}

TEST_CASE("ClientRateLimiter: unknown packet types get default limits") {
    ClientRateLimiter limiter;
    CHECK(limiter.check(0xFF, 0.0) == RateLimitResult::Ok);
}
```

- [ ] **Step 4: Implement rate_limiter.h**

```cpp
// server/rate_limiter.h
#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include "engine/net/packet.h"

namespace fate {

enum class RateLimitResult : uint8_t { Ok, Dropped, Disconnect };

struct TokenBucket {
    float tokens;
    float capacity;
    float refillRate;  // tokens per second
    double lastTime;

    TokenBucket() : tokens(0), capacity(0), refillRate(0), lastTime(0) {}
    TokenBucket(float cap, float rate)
        : tokens(cap), capacity(cap), refillRate(rate), lastTime(0) {}

    bool tryConsume(double now) {
        float elapsed = static_cast<float>(now - lastTime);
        if (elapsed > 0) {
            tokens = std::min(capacity, tokens + elapsed * refillRate);
            lastTime = now;
        }
        if (tokens >= 1.0f) { tokens -= 1.0f; return true; }
        return false;
    }
};

struct RateLimitConfig {
    float burstCapacity;
    float sustainedRate;
    uint32_t disconnectThreshold;
};

class ClientRateLimiter {
public:
    ClientRateLimiter() {
        // Initialize all buckets with generous defaults
        for (auto& b : buckets_) b = TokenBucket(10.0f, 5.0f);
        // Specific limits per message type
        configure(PacketType::CmdMove,       25.0f, 20.0f, 200);
        configure(PacketType::CmdAction,     5.0f,  2.0f,  100);
        configure(PacketType::CmdUseSkill,   3.0f,  1.0f,  50);
        configure(PacketType::CmdChat,       3.0f,  0.33f, 30);
        configure(PacketType::CmdMarket,     3.0f,  0.5f,  50);
        configure(PacketType::CmdTrade,      5.0f,  2.0f,  50);
        configure(PacketType::CmdBounty,     2.0f,  0.5f,  30);
        configure(PacketType::CmdGuild,      3.0f,  1.0f,  30);
        configure(PacketType::CmdSocial,     3.0f,  1.0f,  30);
        configure(PacketType::CmdGauntlet,   3.0f,  1.0f,  30);
        configure(PacketType::CmdQuestAction,5.0f,  2.0f,  50);
        configure(PacketType::CmdZoneTransition, 2.0f, 0.5f, 20);
        configure(PacketType::CmdRespawn,    2.0f,  0.33f, 20);
    }

    RateLimitResult check(uint8_t packetType, double now) {
        auto& bucket = buckets_[packetType];
        if (bucket.tryConsume(now)) {
            return RateLimitResult::Ok;
        }
        ++violations_;
        auto threshold = thresholds_[packetType];
        if (threshold == 0) threshold = 100;
        if (violations_ >= threshold) {
            return RateLimitResult::Disconnect;
        }
        return RateLimitResult::Dropped;
    }

    void resetTickCounters() {} // reserved for future per-tick tracking
    uint32_t violations() const { return violations_; }

private:
    void configure(uint8_t type, float burst, float rate, uint32_t disconnectAt) {
        buckets_[type] = TokenBucket(burst, rate);
        thresholds_[type] = disconnectAt;
    }

    std::array<TokenBucket, 256> buckets_;
    std::array<uint32_t, 256> thresholds_{};
    uint32_t violations_ = 0;
};

} // namespace fate
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build out/build/x64-Debug && out/build/x64-Debug/fate_tests.exe -tc="TokenBucket*,ClientRateLimiter*"`
Expected: All PASS

- [ ] **Step 6: Wire into ServerApp**

Add to `server/server_app.h` after line 126:
```cpp
std::unordered_map<uint16_t, ClientRateLimiter> rateLimiters_;
```

Add to `server/server_app.cpp` at the top of `onPacketReceived()` (line ~1128, before the switch):
```cpp
auto& limiter = rateLimiters_[clientId];
auto rlResult = limiter.check(type, static_cast<double>(gameTime_));
if (rlResult == RateLimitResult::Disconnect) {
    LOG_WARN("Net", "Client %u rate limit disconnect (violations: %u)", clientId, limiter.violations());
    server_.connections().removeClient(clientId);
    onClientDisconnected(clientId);
    return;
}
if (rlResult == RateLimitResult::Dropped) {
    return; // silent drop
}
```

Clean up in `onClientDisconnected()`:
```cpp
rateLimiters_.erase(clientId);
```

- [ ] **Step 7: Commit**

```bash
git add server/rate_limiter.h tests/test_rate_limiter.cpp server/server_app.h server/server_app.cpp
git commit -m "feat: add per-command token bucket rate limiting on server"
```

---

## Task 2: Fix addItemToSlot() Silent Overwrite

**Files:**
- Modify: `game/shared/inventory.cpp:98-106`
- Modify: `game/shared/inventory.h` (add `replaceItemInSlot`)
- Test: `tests/test_inventory.cpp` (add new cases)

- [ ] **Step 1: Write failing test**

```cpp
// Add to tests/test_inventory.cpp
TEST_CASE("Inventory: addItemToSlot rejects occupied slot") {
    Inventory inv;
    ItemInstance sword;
    sword.itemId = "sword_01";
    sword.instanceId = "inst_001";
    sword.quantity = 1;

    CHECK(inv.addItemToSlot(0, sword) == true);

    ItemInstance shield;
    shield.itemId = "shield_01";
    shield.instanceId = "inst_002";
    shield.quantity = 1;

    CHECK(inv.addItemToSlot(0, shield) == false); // occupied!
    CHECK(inv.getSlot(0).itemId == "sword_01");   // original preserved
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL — `addItemToSlot` currently overwrites, so shield replaces sword

- [ ] **Step 3: Fix addItemToSlot to check occupancy**

In `game/shared/inventory.cpp`, change lines 98-106:

```cpp
bool Inventory::addItemToSlot(int slotIndex, const ItemInstance& item) {
    if (!isValidSlot(slotIndex)) return false;
    if (!item.isValid()) return false;
    if (isSlotLocked(slotIndex)) return false;
    if (slots_[slotIndex].isValid()) return false; // REJECT if occupied

    slots_[slotIndex] = item;
    if (onInventoryChanged) onInventoryChanged();
    return true;
}
```

- [ ] **Step 4: Run all inventory tests**

Run: `out/build/x64-Debug/fate_tests.exe -tc="Inventory*"`
Expected: All PASS (verify no existing tests relied on overwrite behavior)

- [ ] **Step 5: Add DB migration for UNIQUE constraint**

Create `Docs/migrations/006_inventory_slot_unique.sql`:
```sql
-- Migration 006: Add unique constraint to prevent duplicate slot assignments
-- This ensures the database rejects a second item in the same slot even if app code has a bug.

-- First, clean up any duplicate (character_id, slot_index) rows that might exist
-- Keep only the most recently inserted one per slot
DELETE FROM character_inventory a
USING character_inventory b
WHERE a.ctid < b.ctid
  AND a.character_id = b.character_id
  AND a.slot_index = b.slot_index
  AND a.slot_index IS NOT NULL;

-- Add unique constraint
-- PostgreSQL: partial unique constraints require CREATE UNIQUE INDEX, not ALTER TABLE
CREATE UNIQUE INDEX uq_character_inventory_slot
  ON character_inventory (character_id, slot_index)
  WHERE slot_index IS NOT NULL;
```

- [ ] **Step 6: Commit**

```bash
git add game/shared/inventory.cpp tests/test_inventory.cpp Docs/migrations/006_inventory_slot_unique.sql
git commit -m "fix: addItemToSlot rejects occupied slots, add DB unique constraint"
```

---

## Task 3: Loot Pickup Atomicity

**Files:**
- Modify: `game/components/dropped_item_component.h` (add `claimed` flag)
- Modify: `server/server_app.cpp:2739-2810` (check-and-set in pickup handler)
- Test: `tests/test_loot_pickup.cpp`

The server game loop is single-threaded, so a simple `bool claimed` flag suffices — no atomic needed. The key is setting the flag *before* any async work and *before* inventory mutation, so a second `CmdAction(type=3)` in the same tick gets rejected.

- [ ] **Step 1: Write failing test**

```cpp
// tests/test_loot_pickup.cpp
#include <doctest/doctest.h>
#include "game/components/dropped_item_component.h"

using namespace fate;

TEST_CASE("DroppedItem: tryClaim succeeds once") {
    DroppedItemComponent drop;
    drop.itemId = "sword_01";
    CHECK(drop.tryClaim(42) == true);
    CHECK(drop.claimedBy == 42);
}

TEST_CASE("DroppedItem: tryClaim fails if already claimed") {
    DroppedItemComponent drop;
    drop.itemId = "sword_01";
    drop.tryClaim(42);
    CHECK(drop.tryClaim(99) == false);
    CHECK(drop.claimedBy == 42); // first claimer wins
}

TEST_CASE("DroppedItem: releaseClaim allows reclaim") {
    DroppedItemComponent drop;
    drop.itemId = "sword_01";
    drop.tryClaim(42);
    drop.releaseClaim();
    CHECK(drop.tryClaim(99) == true);
    CHECK(drop.claimedBy == 99);
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL — `tryClaim` and `claimedBy` don't exist

- [ ] **Step 3: Add claim fields to DroppedItemComponent**

```cpp
// game/components/dropped_item_component.h — add after despawnAfter field:
    uint32_t claimedBy = 0;  // 0 = unclaimed

    // Single-threaded claim — first caller wins
    bool tryClaim(uint32_t claimantEntityId) {
        if (claimedBy != 0) return false;
        claimedBy = claimantEntityId;
        return true;
    }

    void releaseClaim() { claimedBy = 0; }
```

- [ ] **Step 4: Run tests to verify they pass**

Expected: All PASS

- [ ] **Step 5: Wire into server pickup handler**

In `server/server_app.cpp`, in the `actionType == 3` (pickup) section (~line 2739), add the claim check right after proximity/ownership validation and before inventory mutation:

```cpp
// After ownership validation, before inventory mutation:
if (!dropComp->tryClaim(attackerHandle.value)) {
    return; // Already claimed by another player this tick
}

// ... existing inventory mutation code ...

// If inventory is full (addItem returns false), release the claim:
if (!inv->inventory.addItem(item)) {
    dropComp->releaseClaim();
    return;
}
```

- [ ] **Step 6: Commit**

```bash
git add game/components/dropped_item_component.h server/server_app.cpp tests/test_loot_pickup.cpp
git commit -m "fix: atomic loot pickup via single-threaded claim flag prevents duplication"
```

---

## Task 4: Wire Profanity Filter Server-Side + Chat Rate Limiting

**Files:**
- Modify: `server/server_app.cpp:1196-1228` (CmdChat handler)
- Test: `tests/test_chat_server.cpp`

The profanity filter already exists at `game/shared/profanity_filter.h` with `filterChatMessage()`, `validateCharacterName()`, etc. It just needs to be called in the server chat handler. Rate limiting is handled by Task 1's token bucket (CmdChat burst=3, sustained=0.33/sec).

- [ ] **Step 1: Write test for server chat filtering logic**

```cpp
// tests/test_chat_server.cpp
#include <doctest/doctest.h>
#include "game/shared/profanity_filter.h"

using namespace fate;

TEST_CASE("ProfanityFilter: censors bad words in chat") {
    // Censor mode returns isClean=true with asterisked text
    auto result = ProfanityFilter::filterChatMessage("you are a damn fool", FilterMode::Censor);
    CHECK(result.isClean == true);
    CHECK(result.filteredText.find("****") != std::string::npos);
}

TEST_CASE("ProfanityFilter: validate mode rejects bad words") {
    auto result = ProfanityFilter::filterChatMessage("you are a damn fool", FilterMode::Validate);
    CHECK(result.isClean == false);
}

TEST_CASE("ProfanityFilter: rejects blocked phrases") {
    auto result = ProfanityFilter::filterChatMessage("kys noob", FilterMode::Validate);
    CHECK(result.isClean == false);
}

TEST_CASE("ProfanityFilter: passes clean messages") {
    auto result = ProfanityFilter::filterChatMessage("hello friend, nice day!", FilterMode::Censor);
    CHECK(result.isClean == true);
    CHECK(result.filteredText == "hello friend, nice day!");
}

TEST_CASE("ProfanityFilter: enforces max message length") {
    std::string longMsg(250, 'a');
    auto result = ProfanityFilter::filterChatMessage(longMsg, FilterMode::Validate);
    CHECK(result.isClean == false);
}
```

- [ ] **Step 2: Run tests**

Expected: PASS (filter already implemented, we're just testing it)

- [ ] **Step 3: Wire filter into server CmdChat handler**

In `server/server_app.cpp`, modify the `case PacketType::CmdChat:` block (~line 1196). Add after reading the CmdChat:

```cpp
case PacketType::CmdChat: {
    auto chat = CmdChat::read(payload);

    // --- NEW: Server-side profanity filter ---
    if (chat.message.empty() || chat.message.size() > 200) return;

    auto filterResult = ProfanityFilter::filterChatMessage(chat.message, FilterMode::Censor);
    chat.message = filterResult.filteredText;
    // --- END profanity filter ---

    // ... existing sender lookup and broadcast code ...
}
```

Add include at top of server_app.cpp:
```cpp
#include "game/shared/profanity_filter.h"
```

- [ ] **Step 4: Commit**

```bash
git add server/server_app.cpp tests/test_chat_server.cpp
git commit -m "feat: wire profanity filter server-side for all chat messages"
```

---

## Task 5: IPv6 Support

**Files:**
- Modify: `engine/net/socket.h` (change `NetAddress` to support IPv6)
- Modify: `engine/net/socket_win32.cpp` (use `AF_INET6` dual-stack or `AF_UNSPEC`)
- Create: `engine/net/socket_posix.cpp` (POSIX implementation for future)
- Modify: `engine/net/net_client.cpp` (use `getaddrinfo` for hostname resolution)
- Test: `tests/test_ipv6.cpp`

The approach: use `sockaddr_storage` internally, keep `AF_INET` for the server bind (IPv4 server is fine — iOS DNS64/NAT64 translates), and use `getaddrinfo()` on the client side for hostname resolution (which returns IPv6 addresses on IPv6-only networks).

- [ ] **Step 1: Write test for address resolution**

```cpp
// tests/test_ipv6.cpp
#include <doctest/doctest.h>
#include "engine/net/socket.h"

using namespace fate;

TEST_CASE("NetAddress: resolveHostname resolves localhost") {
    NetAddress addr;
    bool ok = NetAddress::resolve("127.0.0.1", 7777, addr);
    CHECK(ok == true);
    CHECK(addr.port == 7777);
}

TEST_CASE("NetAddress: resolveHostname handles invalid host") {
    NetAddress addr;
    bool ok = NetAddress::resolve("this.does.not.exist.invalid", 7777, addr);
    CHECK(ok == false);
}
```

- [ ] **Step 2: Update NetAddress with resolve() and sockaddr_storage support**

In `engine/net/socket.h`, replace `NetAddress`:

```cpp
struct NetAddress {
    // Store as sockaddr_storage to support both IPv4 and IPv6
    uint32_t ip = 0;      // host byte order (IPv4 — kept for backward compat)
    uint16_t port = 0;    // host byte order

    bool operator==(const NetAddress& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const NetAddress& o) const { return !(*this == o); }

    // Resolve hostname via getaddrinfo (supports IPv4 and IPv6 on iOS DNS64)
    static bool resolve(const char* host, uint16_t port, NetAddress& out);
};
```

- [ ] **Step 3: Implement NetAddress::resolve using getaddrinfo**

In `engine/net/socket_win32.cpp`, add:

```cpp
bool NetAddress::resolve(const char* host, uint16_t port, NetAddress& out) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;     // Accept IPv4 or IPv6
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%u", port);

    addrinfo* result = nullptr;
    int ret = ::getaddrinfo(host, portStr, &hints, &result);
    if (ret != 0 || !result) {
        LOG_ERROR("Net", "getaddrinfo failed for '%s': %d", host, ret);
        return false;
    }

    // Prefer IPv4 (server stays IPv4; iOS DNS64/NAT64 provides IPv4-mapped addresses)
    for (auto* rp = result; rp; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET) {
            auto* sin = reinterpret_cast<sockaddr_in*>(rp->ai_addr);
            out.ip = ntohl(sin->sin_addr.s_addr);
            out.port = port;
            ::freeaddrinfo(result);
            return true;
        }
    }
    // No IPv4 address found — on IPv6-only networks, DNS64 should have
    // synthesized an IPv4-mapped address. If not, we can't connect.
    LOG_ERROR("Net", "No IPv4 address found for '%s' (IPv6-only not yet supported)", host);
    ::freeaddrinfo(result);
    return false;
}
```

- [ ] **Step 4: Update NetClient::connect to use resolve()**

In `engine/net/net_client.cpp`, find the connect method and replace any manual IP parsing with:

```cpp
NetAddress addr;
if (!NetAddress::resolve(host.c_str(), port, addr)) {
    LOG_ERROR("Net", "Failed to resolve host: %s", host.c_str());
    return false;
}
serverAddress_ = addr;
```

- [ ] **Step 5: Run tests**

Expected: All PASS

- [ ] **Step 6: Commit**

```bash
git add engine/net/socket.h engine/net/socket_win32.cpp engine/net/net_client.cpp tests/test_ipv6.cpp
git commit -m "feat: IPv6 support via getaddrinfo for iOS App Store compliance"
```

---

## Task 6: Write-Ahead Log (WAL) for Crash Recovery

**Files:**
- Create: `server/wal/write_ahead_log.h`
- Create: `server/wal/write_ahead_log.cpp`
- Modify: `server/server_app.cpp` (journal critical mutations, replay on startup)
- Test: `tests/test_wal.cpp`

Binary WAL with CRC32 per entry, batched fsync per tick, replay on crash recovery.

- [ ] **Step 1: Write failing tests**

```cpp
// tests/test_wal.cpp
#include <doctest/doctest.h>
#include "server/wal/write_ahead_log.h"
#include <filesystem>

using namespace fate;

TEST_CASE("WAL: write and read back entries") {
    const char* path = "test_wal.bin";
    std::filesystem::remove(path);

    {
        WriteAheadLog wal;
        CHECK(wal.open(path) == true);
        wal.appendGoldChange("char_001", 500);
        wal.appendGoldChange("char_001", -100);
        wal.flush();
    }

    {
        WriteAheadLog wal;
        wal.open(path);
        auto entries = wal.readAll();
        CHECK(entries.size() == 2);
        CHECK(entries[0].characterId == "char_001");
        CHECK(entries[0].type == WalEntryType::GoldChange);
        CHECK(entries[0].intValue == 500);
        CHECK(entries[1].intValue == -100);
    }

    std::filesystem::remove(path);
}

TEST_CASE("WAL: detects corrupted entry via CRC") {
    const char* path = "test_wal_corrupt.bin";
    std::filesystem::remove(path);

    {
        WriteAheadLog wal;
        wal.open(path);
        wal.appendGoldChange("char_001", 999);
        wal.flush();
    }

    // Corrupt a byte in the file
    {
        auto f = fopen(path, "r+b");
        fseek(f, 20, SEEK_SET);  // somewhere in payload
        char bad = 0xFF;
        fwrite(&bad, 1, 1, f);
        fclose(f);
    }

    {
        WriteAheadLog wal;
        wal.open(path);
        auto entries = wal.readAll();
        CHECK(entries.size() == 0); // corrupted entry rejected
    }

    std::filesystem::remove(path);
}

TEST_CASE("WAL: truncate clears all entries") {
    const char* path = "test_wal_trunc.bin";
    std::filesystem::remove(path);

    WriteAheadLog wal;
    wal.open(path);
    wal.appendGoldChange("char_001", 100);
    wal.flush();
    wal.truncate();

    auto entries = wal.readAll();
    CHECK(entries.size() == 0);

    std::filesystem::remove(path);
}
```

- [ ] **Step 2: Implement WriteAheadLog**

```cpp
// server/wal/write_ahead_log.h
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>

namespace fate {

enum class WalEntryType : uint8_t {
    GoldChange = 1,
    ItemAdd = 2,
    ItemRemove = 3,
    XPGain = 4,
    LevelUp = 5,
};

struct WalEntry {
    uint64_t sequence;
    WalEntryType type;
    std::string characterId;
    int64_t intValue;       // gold delta, XP amount, slot index
    std::string strValue;   // itemId, serialized item JSON
    double timestamp;
};

class WriteAheadLog {
public:
    bool open(const char* path);
    void close();

    void appendGoldChange(const std::string& charId, int64_t delta);
    void appendItemAdd(const std::string& charId, int slot, const std::string& itemJson);
    void appendItemRemove(const std::string& charId, int slot);
    void appendXPGain(const std::string& charId, int64_t xp);

    void flush();     // fsync to disk
    void truncate();  // clear after successful DB checkpoint

    std::vector<WalEntry> readAll();   // for recovery
    uint64_t lastSequence() const { return sequence_; }

private:
    void appendEntry(WalEntryType type, const std::string& charId,
                     int64_t intVal, const std::string& strVal);
    static uint32_t crc32(const uint8_t* data, size_t len);

    FILE* file_ = nullptr;
    std::string path_;
    uint64_t sequence_ = 0;
};

} // namespace fate
```

The `.cpp` implementation writes a binary format per entry: `[sequence:8][type:1][charIdLen:2][charId:N][intValue:8][strLen:2][str:N][timestamp:8][crc32:4]`. CRC covers all preceding fields in the entry. On read, recompute CRC and reject mismatches.

- [ ] **Step 3: Run tests**

Expected: All PASS

- [ ] **Step 4: Wire WAL into ServerApp**

Add `WriteAheadLog wal_` member to `server/server_app.h`. Open in `init()`, flush at end of each `tick()`, call `appendGoldChange()`/`appendItemAdd()` at critical mutation points (processAction loot, market buy, trade execute). On startup, call `wal_.readAll()` and replay entries with sequence > last DB save. After successful auto-save, call `wal_.truncate()`.

- [ ] **Step 5: Commit**

```bash
git add server/wal/write_ahead_log.h server/wal/write_ahead_log.cpp tests/test_wal.cpp server/server_app.h server/server_app.cpp
git commit -m "feat: write-ahead log for crash recovery of critical mutations"
```

---

## Task 7: DB Circuit Breaker

**Files:**
- Create: `server/db/circuit_breaker.h`
- Modify: `server/db/db_pool.h` (integrate breaker)
- Test: `tests/test_circuit_breaker.cpp`

Three states: Closed (normal) → Open (after N failures, reject for cooldown) → HalfOpen (probe one query).

- [ ] **Step 1: Write failing tests**

```cpp
// tests/test_circuit_breaker.cpp
#include <doctest/doctest.h>
#include "server/db/circuit_breaker.h"

using namespace fate;

TEST_CASE("CircuitBreaker: starts closed") {
    CircuitBreaker cb(5, 30.0);
    CHECK(cb.state() == CircuitState::Closed);
    CHECK(cb.allowRequest() == true);
}

TEST_CASE("CircuitBreaker: opens after N failures") {
    CircuitBreaker cb(3, 10.0);
    cb.recordFailure(0.0);
    cb.recordFailure(0.0);
    CHECK(cb.state() == CircuitState::Closed);
    cb.recordFailure(0.0);
    CHECK(cb.state() == CircuitState::Open);
    CHECK(cb.allowRequest() == false);
}

TEST_CASE("CircuitBreaker: transitions to half-open after cooldown") {
    CircuitBreaker cb(3, 10.0);
    cb.recordFailure(0.0);
    cb.recordFailure(0.0);
    cb.recordFailure(0.0);
    CHECK(cb.allowRequest() == false);  // Open at t=0

    cb.updateTime(15.0); // past cooldown
    CHECK(cb.state() == CircuitState::HalfOpen);
    CHECK(cb.allowRequest() == true);  // allow one probe
}

TEST_CASE("CircuitBreaker: closes on success after half-open") {
    CircuitBreaker cb(3, 10.0);
    for (int i = 0; i < 3; ++i) cb.recordFailure(0.0);
    cb.updateTime(15.0);
    cb.recordSuccess();
    CHECK(cb.state() == CircuitState::Closed);
}

TEST_CASE("CircuitBreaker: re-opens on failure in half-open") {
    CircuitBreaker cb(3, 10.0);
    for (int i = 0; i < 3; ++i) cb.recordFailure(0.0);
    cb.updateTime(15.0); // half-open
    cb.recordFailure(15.0);
    CHECK(cb.state() == CircuitState::Open);
}
```

- [ ] **Step 2: Implement CircuitBreaker**

```cpp
// server/db/circuit_breaker.h
#pragma once
#include <cstdint>

namespace fate {

enum class CircuitState : uint8_t { Closed, Open, HalfOpen };

class CircuitBreaker {
public:
    CircuitBreaker(uint32_t failureThreshold = 5, double cooldownSeconds = 30.0)
        : threshold_(failureThreshold), cooldown_(cooldownSeconds) {}

    CircuitState state() const { return state_; }

    bool allowRequest() {
        if (state_ == CircuitState::Closed) return true;
        if (state_ == CircuitState::Open) {
            if (currentTime_ >= openedAt_ + cooldown_) {
                state_ = CircuitState::HalfOpen;
                return true;
            }
            return false;
        }
        return true; // HalfOpen: allow one probe
    }

    void recordSuccess() {
        consecutiveFailures_ = 0;
        state_ = CircuitState::Closed;
    }

    void recordFailure(double now) {
        currentTime_ = now;
        ++consecutiveFailures_;
        if (state_ == CircuitState::HalfOpen || consecutiveFailures_ >= threshold_) {
            state_ = CircuitState::Open;
            openedAt_ = now;
        }
    }

    void updateTime(double now) { currentTime_ = now; }

private:
    CircuitState state_ = CircuitState::Closed;
    uint32_t threshold_;
    double cooldown_;
    uint32_t consecutiveFailures_ = 0;
    double openedAt_ = 0.0;
    double currentTime_ = 0.0;
};

} // namespace fate
```

- [ ] **Step 3: Run tests**

Expected: All PASS

- [ ] **Step 4: Integrate into DbPool**

Add a `CircuitBreaker` member to `DbPool`. In `acquire()`, check `breaker_.allowRequest()` first — if false, throw/return null. On successful query, call `recordSuccess()`. On `pqxx::broken_connection`, call `recordFailure()`. The ServerApp ticks `breaker_.updateTime(gameTime_)` each tick. When the breaker is Open, the WAL (Task 6) continues capturing mutations.

- [ ] **Step 5: Commit**

```bash
git add server/db/circuit_breaker.h tests/test_circuit_breaker.cpp server/db/db_pool.h server/db/db_pool.cpp
git commit -m "feat: circuit breaker on DB pool for graceful degradation"
```

---

## Task 8: Per-Player Mutation Serialization

**Files:**
- Create: `server/player_lock.h`
- Modify: `server/server_app.h` (add lock map)
- Modify: `server/server_app.cpp` (acquire lock at mutation points)
- Test: `tests/test_player_lock.cpp`

Since all gameplay commands already run on the single game thread, the primary race is between game-thread mutations and async fiber DB operations. The solution: a per-player `std::mutex` acquired for any inventory/gold mutation, including auto-save reads.

- [ ] **Step 1: Write test**

```cpp
// tests/test_player_lock.cpp
#include <doctest/doctest.h>
#include "server/player_lock.h"

using namespace fate;

TEST_CASE("PlayerLockMap: returns same mutex for same player") {
    PlayerLockMap locks;
    auto& m1 = locks.get("char_001");
    auto& m2 = locks.get("char_001");
    CHECK(&m1 == &m2);
}

TEST_CASE("PlayerLockMap: different players get different mutexes") {
    PlayerLockMap locks;
    auto& m1 = locks.get("char_001");
    auto& m2 = locks.get("char_002");
    CHECK(&m1 != &m2);
}

TEST_CASE("PlayerLockMap: erase removes player lock") {
    PlayerLockMap locks;
    locks.get("char_001");
    locks.erase("char_001");
    // After erase, get returns a new mutex (different address possible)
    // Just verify it doesn't crash
    auto& m3 = locks.get("char_001");
    (void)m3;
    CHECK(true);
}

TEST_CASE("PlayerLockMap: scoped lock for two players uses consistent ordering") {
    PlayerLockMap locks;
    auto& m1 = locks.get("aaa");
    auto& m2 = locks.get("zzz");
    // Lock in sorted order to prevent deadlocks
    std::scoped_lock lock(
        (&m1 < &m2) ? m1 : m2,
        (&m1 < &m2) ? m2 : m1
    );
    CHECK(true); // no deadlock
}
```

- [ ] **Step 2: Implement PlayerLockMap**

```cpp
// server/player_lock.h
#pragma once
#include <mutex>
#include <unordered_map>
#include <string>

namespace fate {

class PlayerLockMap {
public:
    std::mutex& get(const std::string& characterId) {
        std::lock_guard<std::mutex> guard(mapMutex_);
        auto& ptr = locks_[characterId];
        if (!ptr) ptr = std::make_unique<std::mutex>();
        return *ptr;
    }

    void erase(const std::string& characterId) {
        std::lock_guard<std::mutex> guard(mapMutex_);
        locks_.erase(characterId);
    }

private:
    std::mutex mapMutex_;
    // std::mutex is not movable, so use unique_ptr to allow map rehashing
    std::unordered_map<std::string, std::unique_ptr<std::mutex>> locks_;
};

} // namespace fate
```

- [ ] **Step 3: Run tests**

Expected: All PASS

- [ ] **Step 4: Wire into ServerApp**

Add `PlayerLockMap playerLocks_` to `server_app.h`. At trade execution, market buy, loot pickup, and auto-save, acquire the lock:

```cpp
// Example in trade execution (two-player lock with consistent ordering):
auto& lock1 = playerLocks_.get(charId1);
auto& lock2 = playerLocks_.get(charId2);
std::scoped_lock tradeLock(
    (&lock1 < &lock2) ? lock1 : lock2,
    (&lock1 < &lock2) ? lock2 : lock1
);
// ... transfer items/gold ...
```

Clean up in `onClientDisconnected`: `playerLocks_.erase(characterId)`.

- [ ] **Step 5: Commit**

```bash
git add server/player_lock.h tests/test_player_lock.cpp server/server_app.h server/server_app.cpp
git commit -m "feat: per-player mutex map serializes concurrent inventory/gold mutations"
```

---

## Task 9: Android Build Pipeline

**Files:**
- Create: `android/` directory structure (Gradle wrapper project)
- Create: `android/app/build.gradle.kts`
- Create: `android/app/src/main/AndroidManifest.xml`
- Create: `android/app/src/main/java/com/fatemmo/game/FateActivity.java`
- Create: `android/app/src/main/jni/CMakeLists.txt`
- Create: `android/settings.gradle.kts`
- Create: `android/build.gradle.kts`
- Create: `android/gradle.properties`

This task creates the Gradle project shell that wraps the existing CMake build for Android NDK compilation. The game code compiles to `libmain.so` loaded by SDL2's `SDLActivity`.

- [ ] **Step 1: Create Gradle project structure**

```
android/
  settings.gradle.kts
  build.gradle.kts
  gradle.properties
  app/
    build.gradle.kts
    src/main/
      AndroidManifest.xml
      java/com/fatemmo/game/FateActivity.java
      jni/CMakeLists.txt
```

- [ ] **Step 2: Write root build.gradle.kts**

```kotlin
// android/build.gradle.kts
plugins {
    id("com.android.application") version "8.5.0" apply false
}
```

```kotlin
// android/settings.gradle.kts
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
rootProject.name = "FateMMO"
include(":app")
```

- [ ] **Step 3: Write app/build.gradle.kts**

```kotlin
// android/app/build.gradle.kts
plugins {
    id("com.android.application")
}
android {
    namespace = "com.fatemmo.game"
    compileSdk = 35
    ndkVersion = "27.1.12297006"
    defaultConfig {
        applicationId = "com.fatemmo.game"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"
        ndk { abiFilters += listOf("arm64-v8a") }
        externalNativeBuild {
            cmake { cppFlags += "-std=c++20" }
        }
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/jni/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    sourceSets {
        getByName("main") {
            assets.srcDirs("../../../assets")
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }
}
```

- [ ] **Step 4: Write AndroidManifest.xml**

```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-feature android:glEsVersion="0x00030000" android:required="true" />
    <application
        android:label="FateMMO"
        android:hasCode="true"
        android:allowBackup="false">
        <activity
            android:name=".FateActivity"
            android:configChanges="orientation|screenSize|keyboardHidden"
            android:screenOrientation="sensorLandscape"
            android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
```

- [ ] **Step 5: Write FateActivity.java**

```java
// android/app/src/main/java/com/fatemmo/game/FateActivity.java
package com.fatemmo.game;
import org.libsdl.app.SDLActivity;

public class FateActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[]{ "SDL2", "main" };
    }
}
```

- [ ] **Step 6: Write JNI CMakeLists.txt**

```cmake
# android/app/src/main/jni/CMakeLists.txt
cmake_minimum_required(VERSION 3.22)
project(FateMMO_Android)

# Point to the main engine CMakeLists
set(FATEMMO_BUILD_MOBILE ON CACHE BOOL "" FORCE)
set(FATEMMO_PLATFORM_ANDROID ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/../../../../.. ${CMAKE_BINARY_DIR}/engine)
```

- [ ] **Step 7: Commit**

```bash
git add android/
git commit -m "feat: Android build pipeline with Gradle + NDK + SDL2"
```

---

## Task 10: Cross-Platform Fibers via minicoro

**Files:**
- Create: `engine/job/minicoro.h` (vendored single-header)
- Create: `engine/job/fiber_minicoro.cpp`
- Modify: `CMakeLists.txt` (conditionally compile fiber backend)
- Test: existing `tests/test_job_system.cpp` should continue to pass

- [ ] **Step 1: Vendor minicoro.h**

Download minicoro.h from https://github.com/edubart/minicoro into `engine/job/minicoro.h`. It's a single-header C library (~650 SLOC) supporting Windows, macOS, iOS, Android, Linux.

- [ ] **Step 2: Create fiber_minicoro.cpp**

```cpp
// engine/job/fiber_minicoro.cpp
#ifndef _WIN32  // Only compile on non-Windows platforms

#define MINICORO_IMPL
#include "engine/job/minicoro.h"
#include "engine/job/fiber.h"
#include <cstdlib>

namespace fate {
namespace fiber {

// Thread-local main coroutine handle
static thread_local mco_coro* tls_mainCoro = nullptr;
static thread_local mco_coro* tls_currentCoro = nullptr;

FiberHandle convertThreadToFiber() {
    // Create a dummy coroutine to represent the main thread
    mco_desc desc = mco_desc_init([](mco_coro*){}, 0);
    desc.malloc_cb = [](size_t sz, void*) -> void* { return malloc(sz); };
    desc.free_cb = [](void* p, size_t, void*) { free(p); };
    mco_coro* co = nullptr;
    mco_create(&co, &desc);
    tls_mainCoro = co;
    tls_currentCoro = co;
    return static_cast<FiberHandle>(co);
}

void convertFiberToThread() {
    if (tls_mainCoro) {
        mco_destroy(tls_mainCoro);
        tls_mainCoro = nullptr;
        tls_currentCoro = nullptr;
    }
}

FiberHandle create(size_t stackSize, FiberProc proc, void* param) {
    mco_desc desc = mco_desc_init([](mco_coro* co) {
        auto* data = static_cast<std::pair<FiberProc, void*>*>(co->user_data);
        data->first(data->second);
    }, stackSize);
    mco_coro* co = nullptr;
    mco_create(&co, &desc);
    // Store proc+param in user_data
    auto* data = new std::pair<FiberProc, void*>(proc, param);
    co->user_data = data;
    return static_cast<FiberHandle>(co);
}

void destroy(FiberHandle f) {
    if (f) {
        auto* co = static_cast<mco_coro*>(f);
        if (co->user_data) {
            delete static_cast<std::pair<FiberProc, void*>*>(co->user_data);
        }
        mco_destroy(co);
    }
}

void switchTo(FiberHandle f) {
    auto* target = static_cast<mco_coro*>(f);
    tls_currentCoro = target;
    mco_resume(target);
}

FiberHandle current() {
    return static_cast<FiberHandle>(tls_currentCoro);
}

} // namespace fiber
} // namespace fate

#endif // !_WIN32
```

- [ ] **Step 3: Update CMakeLists.txt**

In the source list for `fate_engine`, conditionally select the fiber backend:

```cmake
if(WIN32 AND NOT FATEMMO_BUILD_MOBILE)
    list(APPEND ENGINE_SOURCES engine/job/fiber_win32.cpp)
else()
    list(APPEND ENGINE_SOURCES engine/job/fiber_minicoro.cpp)
endif()
```

- [ ] **Step 4: Run existing fiber/job tests on Windows**

Expected: All PASS (Win32 path unchanged)

- [ ] **Step 5: Commit**

```bash
git add engine/job/minicoro.h engine/job/fiber_minicoro.cpp CMakeLists.txt
git commit -m "feat: cross-platform fiber backend via minicoro for iOS/Android"
```

---

## Task 11: Mobile Reconnection State Machine

**Files:**
- Create: `engine/net/reconnect_state.h`
- Modify: `engine/net/net_client.h` (add reconnect state)
- Modify: `engine/net/net_client.cpp` (reconnection logic)
- Test: `tests/test_reconnect.cpp`

States: `Connected → Disconnected → Reconnecting → Reconnected → or Failed`

- [ ] **Step 1: Write test**

```cpp
// tests/test_reconnect.cpp
#include <doctest/doctest.h>
#include "engine/net/reconnect_state.h"

using namespace fate;

TEST_CASE("ReconnectState: starts idle") {
    ReconnectState rs;
    CHECK(rs.state() == ReconnectPhase::Idle);
}

TEST_CASE("ReconnectState: transitions through phases") {
    ReconnectState rs;
    rs.beginReconnect(0.0);
    CHECK(rs.state() == ReconnectPhase::Reconnecting);

    CHECK(rs.shouldAttemptNow(0.5) == false); // too soon (1s first backoff)
    CHECK(rs.shouldAttemptNow(1.1) == true);

    rs.onAttemptFailed(1.1);
    CHECK(rs.shouldAttemptNow(2.0) == false); // next backoff is 2s
    CHECK(rs.shouldAttemptNow(3.2) == true);
}

TEST_CASE("ReconnectState: succeeds and resets") {
    ReconnectState rs;
    rs.beginReconnect(0.0);
    rs.onSuccess();
    CHECK(rs.state() == ReconnectPhase::Idle);
}

TEST_CASE("ReconnectState: gives up after timeout") {
    ReconnectState rs;
    rs.beginReconnect(0.0);
    CHECK(rs.isTimedOut(59.0) == false);
    CHECK(rs.isTimedOut(61.0) == true);
}
```

- [ ] **Step 2: Implement ReconnectState**

```cpp
// engine/net/reconnect_state.h
#pragma once
#include <cstdint>
#include <algorithm>

namespace fate {

enum class ReconnectPhase : uint8_t { Idle, Reconnecting };

class ReconnectState {
public:
    ReconnectPhase state() const { return phase_; }

    void beginReconnect(double now) {
        phase_ = ReconnectPhase::Reconnecting;
        startTime_ = now;
        nextAttemptTime_ = now + 1.0; // first retry after 1s
        backoff_ = 1.0;
        attempts_ = 0;
    }

    bool shouldAttemptNow(double now) const {
        return phase_ == ReconnectPhase::Reconnecting && now >= nextAttemptTime_;
    }

    void onAttemptFailed(double now) {
        ++attempts_;
        backoff_ = std::min(backoff_ * 2.0, 30.0); // cap at 30s
        nextAttemptTime_ = now + backoff_;
    }

    void onSuccess() {
        phase_ = ReconnectPhase::Idle;
        attempts_ = 0;
        backoff_ = 1.0;
    }

    bool isTimedOut(double now) const {
        return phase_ == ReconnectPhase::Reconnecting
            && (now - startTime_) > 60.0;
    }

    int attempts() const { return attempts_; }

private:
    ReconnectPhase phase_ = ReconnectPhase::Idle;
    double startTime_ = 0.0;
    double nextAttemptTime_ = 0.0;
    double backoff_ = 1.0;
    int attempts_ = 0;
};

} // namespace fate
```

- [ ] **Step 3: Run tests**

Expected: All PASS

- [ ] **Step 4: Integrate into NetClient**

Add `ReconnectState reconnect_` and `AuthToken lastAuthToken_` to `NetClient`. When heartbeat timeout is detected in `poll()`, call `reconnect_.beginReconnect(now)`. In each `poll()`, if `reconnect_.shouldAttemptNow(now)`, attempt `connectWithToken(lastHost_, lastPort_, lastAuthToken_)`. On success, call `reconnect_.onSuccess()`. On timeout, fire `onDisconnected` callback. The client UI shows "Reconnecting..." overlay while in `Reconnecting` phase.

- [ ] **Step 5: Commit**

```bash
git add engine/net/reconnect_state.h tests/test_reconnect.cpp engine/net/net_client.h engine/net/net_client.cpp
git commit -m "feat: mobile reconnection state machine with exponential backoff"
```

---

## Task 12: Connection Cookies (Anti-Flood)

**Files:**
- Create: `engine/net/connection_cookie.h`
- Modify: `engine/net/net_server.cpp` (challenge-response before allocating state)
- Modify: `engine/net/packet.h` (add ChallengeRequest/ChallengeResponse types)
- Test: `tests/test_connection_cookie.cpp`

- [ ] **Step 1: Write test**

```cpp
// tests/test_connection_cookie.cpp
#include <doctest/doctest.h>
#include "engine/net/connection_cookie.h"

using namespace fate;

TEST_CASE("ConnectionCookie: generate and validate") {
    ConnectionCookieGenerator gen("test_server_secret_key_32bytes!");
    uint32_t clientIp = 0x7F000001;  // 127.0.0.1
    uint16_t clientPort = 12345;
    uint64_t clientNonce = 0xDEADBEEF;
    double timestamp = 1000.0;

    auto cookie = gen.generate(clientIp, clientPort, clientNonce, timestamp);
    CHECK(cookie != 0);

    bool valid = gen.validate(cookie, clientIp, clientPort, clientNonce, timestamp);
    CHECK(valid == true);
}

TEST_CASE("ConnectionCookie: rejects wrong IP") {
    ConnectionCookieGenerator gen("test_server_secret_key_32bytes!");
    uint64_t nonce = 0x1234;
    auto cookie = gen.generate(0x7F000001, 100, nonce, 0.0);
    CHECK(gen.validate(cookie, 0x7F000002, 100, nonce, 0.0) == false);
}

TEST_CASE("ConnectionCookie: expires after 20 seconds (two bucket windows)") {
    ConnectionCookieGenerator gen("test_server_secret_key_32bytes!");
    auto cookie = gen.generate(1, 1, 1, 5.0);  // bucket 0
    CHECK(gen.validate(cookie, 1, 1, 1, 5.0) == true);   // same bucket
    CHECK(gen.validate(cookie, 1, 1, 1, 15.0) == true);   // next bucket (checks prev)
    CHECK(gen.validate(cookie, 1, 1, 1, 25.0) == false);  // two buckets later = expired
}
```

- [ ] **Step 2: Implement ConnectionCookieGenerator**

```cpp
// engine/net/connection_cookie.h
#pragma once
#include <cstdint>
#include <cstring>
#include <functional>

namespace fate {

class ConnectionCookieGenerator {
public:
    explicit ConnectionCookieGenerator(const char* secret) {
        // Simple FNV-1a hash of the secret for the HMAC key
        key_ = 0xcbf29ce484222325ULL;
        for (const char* p = secret; *p; ++p) {
            key_ ^= static_cast<uint64_t>(*p);
            key_ *= 0x100000001b3ULL;
        }
    }

    uint64_t generate(uint32_t ip, uint16_t port, uint64_t nonce, double timestamp) const {
        // Both generate and validate use the same 10s bucket to ensure consistency
        auto bucket = static_cast<uint64_t>(timestamp) / 10;
        return computeHmac(ip, port, nonce, bucket);
    }

    bool validate(uint64_t cookie, uint32_t ip, uint16_t port,
                  uint64_t nonce, double currentTime) const {
        // Check current 10s bucket and previous bucket (allows up to 20s validity)
        auto bucket = static_cast<uint64_t>(currentTime) / 10;
        if (computeHmac(ip, port, nonce, bucket) == cookie) return true;
        if (bucket > 0 && computeHmac(ip, port, nonce, bucket - 1) == cookie) return true;
        return false;
    }

private:
    uint64_t computeHmac(uint32_t ip, uint16_t port, uint64_t nonce, uint64_t ts) const {
        // FNV-1a hash of (key + ip + port + nonce + timestamp)
        uint64_t h = key_;
        auto mix = [&](uint64_t v) { h ^= v; h *= 0x100000001b3ULL; };
        mix(ip);
        mix(port);
        mix(nonce);
        mix(ts);
        return h;
    }

    uint64_t key_;
};

} // namespace fate
```

- [ ] **Step 3: Run tests**

Expected: All PASS

- [ ] **Step 4: Wire into NetServer handshake**

Add `PacketType::ChallengeRequest` (0x06) and `PacketType::ChallengeResponse` (0x07) to `packet.h`. In `NetServer::handleRawPacket()`, when receiving a `Connect` from an unknown address, don't allocate client state — instead send a `ChallengeRequest` with a server nonce. Only when the client echoes the correct cookie in `ChallengeResponse` does the server call `connections_.addClient()`. The client side sends `ChallengeResponse` in `NetClient::poll()` when it receives a `ChallengeRequest`.

- [ ] **Step 5: Commit**

```bash
git add engine/net/connection_cookie.h engine/net/packet.h engine/net/net_server.cpp engine/net/net_client.cpp tests/test_connection_cookie.cpp
git commit -m "feat: connection cookies prevent UDP spoofed-IP flooding"
```

---

## Task 13: Server-Side Target Validation Against AOI

**Files:**
- Modify: `server/server_app.cpp` (add validation in processAction and processUseSkill)
- Create: `server/target_validator.h`
- Test: `tests/test_target_validator.cpp`

Every `CmdAction` and `CmdUseSkill` referencing a target entity must validate: entity exists, entity is in the player's server-side AOI, and distance is within action range.

- [ ] **Step 1: Write test**

```cpp
// tests/test_target_validator.cpp
#include <doctest/doctest.h>
#include "server/target_validator.h"

using namespace fate;

TEST_CASE("TargetValidator: rejects entity not in AOI") {
    VisibilitySet aoi;
    // VisibilitySet uses std::vector<EntityHandle>, push_back + sort
    aoi.current.push_back(EntityHandle(100));
    aoi.current.push_back(EntityHandle(200));
    std::sort(aoi.current.begin(), aoi.current.end());

    CHECK(TargetValidator::isInAOI(aoi, 100) == true);
    CHECK(TargetValidator::isInAOI(aoi, 200) == true);
    CHECK(TargetValidator::isInAOI(aoi, 300) == false);
}

TEST_CASE("TargetValidator: range check with tolerance") {
    Vec2 playerPos{100.0f, 100.0f};
    Vec2 targetPos{148.0f, 100.0f}; // 48px away
    float maxRange = 64.0f;
    float latencyTolerance = 16.0f;

    CHECK(TargetValidator::isInRange(playerPos, targetPos, maxRange, latencyTolerance) == true);

    Vec2 farTarget{300.0f, 100.0f}; // 200px away
    CHECK(TargetValidator::isInRange(playerPos, farTarget, maxRange, latencyTolerance) == false);
}
```

- [ ] **Step 2: Implement TargetValidator**

```cpp
// server/target_validator.h
#pragma once
#include "engine/net/aoi.h"
#include "engine/core/types.h"
#include <algorithm>
#include <cmath>

namespace fate {

struct TargetValidator {
    // VisibilitySet.current is a sorted std::vector<EntityHandle>
    static bool isInAOI(const VisibilitySet& aoi, uint64_t targetId) {
        EntityHandle target(static_cast<uint32_t>(targetId));
        return std::binary_search(aoi.current.begin(), aoi.current.end(), target);
    }

    static bool isInRange(Vec2 playerPos, Vec2 targetPos,
                          float maxRange, float latencyTolerance = 16.0f) {
        float dx = playerPos.x - targetPos.x;
        float dy = playerPos.y - targetPos.y;
        float distSq = dx * dx + dy * dy;
        float allowed = maxRange + latencyTolerance;
        return distSq <= allowed * allowed;
    }
};

} // namespace fate
```

- [ ] **Step 3: Run tests**

Expected: All PASS

- [ ] **Step 4: Wire into processAction and processUseSkill**

In `server/server_app.cpp`, at the start of `processAction()` and `processUseSkill()`, after looking up the target entity, add:

```cpp
auto* client = server_.connections().findById(clientId);
if (!TargetValidator::isInAOI(client->aoi, action.targetId)) {
    LOG_WARN("Net", "Client %u targeted entity %llu not in AOI", clientId, action.targetId);
    return;
}
```

And add range validation using the action type's max range (melee=64px, ranged=128px, pickup=48px).

- [ ] **Step 5: Commit**

```bash
git add server/target_validator.h tests/test_target_validator.cpp server/server_app.cpp
git commit -m "feat: server-side AOI + range validation on all target actions"
```

---

## Task 14: CI/CD via GitHub Actions

**Files:**
- Create: `.github/workflows/ci.yml`

- [ ] **Step 1: Create CI workflow**

```yaml
# .github/workflows/ci.yml
name: CI

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  windows-msvc:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install vcpkg dependencies
        uses: lukka/run-vcpkg@v11
        with:
          vcpkgGitCommitId: '2024.01.12'

      - name: Configure CMake
        run: cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=${{ env.VCPKG_ROOT }}/scripts/buildsystems/vcpkg.cmake

      - name: Build
        run: cmake --build build --config Debug -j4

      - name: Run tests
        run: build/Debug/fate_tests.exe --reporters=console --no-colors

  linux-gcc:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install deps
        run: |
          sudo apt-get update
          sudo apt-get install -y libsdl2-dev libssl-dev libpq-dev xvfb mesa-utils

      - name: Configure CMake
        run: cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13

      - name: Build
        run: cmake --build build -j$(nproc)

      - name: Run tests (headless)
        run: xvfb-run -a build/fate_tests --reporters=console --no-colors
        env:
          LIBGL_ALWAYS_SOFTWARE: "1"
          MESA_GL_VERSION_OVERRIDE: "3.3"

  linux-clang:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install deps
        run: |
          sudo apt-get update
          sudo apt-get install -y libsdl2-dev libssl-dev libpq-dev clang-17 xvfb mesa-utils

      - name: Configure CMake
        run: cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++-17

      - name: Build
        run: cmake --build build -j$(nproc)

      - name: Run tests (headless)
        run: xvfb-run -a build/fate_tests --reporters=console --no-colors
        env:
          LIBGL_ALWAYS_SOFTWARE: "1"
          MESA_GL_VERSION_OVERRIDE: "3.3"
```

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "feat: GitHub Actions CI with MSVC, GCC-13, Clang-17 matrix"
```

---

## Implementation Order & Dependencies

```
Task 2  (addItemToSlot fix)     → independent, do first (blocks Task 3)
Task 3  (loot pickup atomicity) → depends on Task 2
Task 1  (rate limiting)         → independent
Task 4  (profanity filter)      → independent
Task 5  (IPv6)                  → independent
Task 8  (player locks)          → independent
Task 13 (target validation)     → independent

Task 6  (WAL)                   → independent
Task 7  (circuit breaker)       → integrates with Task 6

Task 10 (minicoro fibers)       → independent
Task 9  (Android build)         → benefits from Task 10

Task 11 (reconnection)          → independent
Task 12 (connection cookies)    → independent

Task 14 (CI/CD)                 → do last (validates everything builds)
```

**Recommended parallel execution groups:**
1. **Group A** (security): Tasks 1, 2, 3, 4, 13
2. **Group B** (infrastructure): Tasks 5, 6, 7, 8
3. **Group C** (mobile/platform): Tasks 9, 10, 11, 12
4. **Group D** (CI): Task 14
