# Security & Protocol Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close four security/protocol gaps: harden ByteReader against malicious packets, enforce per-tick skill command caps, add per-entity sequence counters to prevent stale unreliable updates, and eliminate command injection via `system()`.

**Architecture:** All four changes are independent and surgical. ByteReader gains NaN/string/enum validation in-place. ServerApp gets a skill-command-per-tick map mirroring the existing `moveCountThisTick_` pattern. Entity updates get a 1-byte sequence counter written by the server and checked by the client. The editor's `system()` calls become `ShellExecuteW`.

**Tech Stack:** C++20, doctest, Win32 API (`ShellExecuteW`), existing ByteReader/ByteWriter, existing ECS replication pipeline.

**Build command:** `"C:/Program Files/Microsoft Visual Studio/2025/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build`

**Test command:** `./build/Debug/fate_tests.exe`

**IMPORTANT:** Before building, `touch` every edited `.cpp` file (CMake misses changes silently on this setup).

---

## File Map

| Action | File | Responsibility |
|--------|------|----------------|
| Modify | `engine/net/byte_stream.h` | Add NaN rejection, string length cap, enum reader, `ok()` alias |
| Modify | `tests/test_byte_stream.cpp` | Tests for NaN, oversized string, enum bounds, sticky error |
| Modify | `server/server_app.h` | Add `skillCommandsThisTick_` map |
| Modify | `server/server_app.cpp` | Reset map in `tick()`, increment+check in `processUseSkill()`, clean up on disconnect |
| Modify | `engine/net/protocol.h` | Add `updateSeq` field to `SvEntityUpdateMsg` |
| Modify | `engine/net/replication.h` | Add `entitySeqCounters_` map |
| Modify | `engine/net/replication.cpp` | Increment seq on entity update, write to message |
| Modify | `game/game_app.h` | Add `ghostUpdateSeqs_` map |
| Modify | `game/game_app.cpp` | Check seq on `onEntityUpdate`, seed on `onEntityEnter`, clean on `onEntityLeave` |
| Modify | `tests/test_protocol.cpp` | Round-trip test for updateSeq field |
| Modify | `engine/editor/editor.cpp` | Replace `system()` with `ShellExecuteW` |

---

### Task 1: Harden ByteReader — NaN/Inf float rejection

**Files:**
- Modify: `engine/net/byte_stream.h:70` (readFloat method)
- Modify: `tests/test_byte_stream.cpp` (add new test cases)

- [ ] **Step 1: Write failing tests for NaN and Inf floats**

Add to `tests/test_byte_stream.cpp`:

```cpp
TEST_CASE("ByteReader rejects NaN float") {
    // IEEE 754 quiet NaN: 0x7FC00000
    uint8_t buf[4];
    uint32_t nan_bits = 0x7FC00000;
    std::memcpy(buf, &nan_bits, 4);

    fate::ByteReader r(buf, 4);
    float val = r.readFloat();
    CHECK(val == 0.0f);
    CHECK(r.overflowed());
}

TEST_CASE("ByteReader rejects Inf float") {
    uint8_t buf[4];
    uint32_t inf_bits = 0x7F800000; // +Infinity
    std::memcpy(buf, &inf_bits, 4);

    fate::ByteReader r(buf, 4);
    float val = r.readFloat();
    CHECK(val == 0.0f);
    CHECK(r.overflowed());
}
```

- [ ] **Step 2: Touch and build, run tests to verify they fail**

```bash
touch engine/net/byte_stream.h tests/test_byte_stream.cpp
```
Run build, then: `./build/Debug/fate_tests.exe -tc="ByteReader rejects NaN float"`
Expected: FAIL (NaN passes through currently)

- [ ] **Step 3: Implement NaN/Inf rejection in readFloat()**

In `engine/net/byte_stream.h`, replace the `readFloat()` method:

```cpp
float readFloat() {
    float v = 0;
    readRaw(&v, sizeof(v));
    if (!overflow_ && (std::isnan(v) || std::isinf(v))) {
        overflow_ = true;
        return 0.0f;
    }
    return v;
}
```

Add `#include <cmath>` at the top of the file (after `#include <cstring>`).

- [ ] **Step 4: Touch, build, run tests to verify they pass**

```bash
touch engine/net/byte_stream.h tests/test_byte_stream.cpp
```
Run build, then: `./build/Debug/fate_tests.exe -tc="ByteReader rejects*"`
Expected: PASS

- [ ] **Step 5: Verify existing float tests still pass**

`./build/Debug/fate_tests.exe -tc="ByteStream*"`
Expected: All existing ByteStream tests PASS (normal floats unaffected)

---

### Task 2: Harden ByteReader — string length cap

**Files:**
- Modify: `engine/net/byte_stream.h:78-87` (readString method)
- Modify: `tests/test_byte_stream.cpp`

- [ ] **Step 1: Write failing test for oversized string**

Add to `tests/test_byte_stream.cpp`:

```cpp
TEST_CASE("ByteReader rejects oversized string") {
    // Craft a packet claiming a 60000-byte string but only 10 bytes of actual data
    uint8_t buf[12];
    fate::ByteWriter w(buf, sizeof(buf));
    w.writeU16(60000); // length prefix claiming 60KB
    w.writeU8(0x41);   // only 1 byte of actual string data

    fate::ByteReader r(buf, w.size());
    std::string s = r.readString();
    CHECK(s.empty());
    CHECK(r.overflowed());
}

TEST_CASE("ByteReader readString with maxLen rejects long strings") {
    uint8_t buf[512];
    fate::ByteWriter w(buf, sizeof(buf));
    std::string longStr(300, 'A');
    w.writeString(longStr);

    fate::ByteReader r(buf, w.size());
    std::string s = r.readString(256); // max 256 chars
    CHECK(s.empty());
    CHECK(r.overflowed());
}

TEST_CASE("ByteReader readString with maxLen accepts valid strings") {
    uint8_t buf[512];
    fate::ByteWriter w(buf, sizeof(buf));
    w.writeString("hello");

    fate::ByteReader r(buf, w.size());
    std::string s = r.readString(256);
    CHECK(s == "hello");
    CHECK_FALSE(r.overflowed());
}
```

- [ ] **Step 2: Touch, build, verify failing tests**

The first test already passes (overflow detection catches truncated data).
The second test (`readString(256)`) will fail because `readString()` has no `maxLen` parameter.

- [ ] **Step 3: Add maxLen parameter to readString()**

In `engine/net/byte_stream.h`, replace the `readString()` method:

```cpp
std::string readString(uint16_t maxLen = 4096) {
    uint16_t len = readU16();
    if (overflow_) return {};
    if (len > maxLen) {
        overflow_ = true;
        return {};
    }
    if (pos_ + len > size_) {
        overflow_ = true;
        return {};
    }
    std::string s(reinterpret_cast<const char*>(buffer_ + pos_), len);
    pos_ += len;
    return s;
}
```

- [ ] **Step 4: Touch, build, run tests**

```bash
touch engine/net/byte_stream.h tests/test_byte_stream.cpp
```
`./build/Debug/fate_tests.exe -tc="ByteReader*string*"`
Expected: All 3 new string tests PASS

- [ ] **Step 5: Verify existing string test still passes**

`./build/Debug/fate_tests.exe -tc="ByteStream string*"`
Expected: PASS (default maxLen=4096 doesn't affect "hello world")

---

### Task 3: Harden ByteReader — enum bounds checking

**Files:**
- Modify: `engine/net/byte_stream.h` (add readEnum template)
- Modify: `tests/test_byte_stream.cpp`

- [ ] **Step 1: Write failing test for out-of-range enum**

Add to `tests/test_byte_stream.cpp`:

```cpp
TEST_CASE("ByteReader readEnum rejects out-of-range value") {
    enum class TestEnum : uint8_t { A = 0, B = 1, C = 2, MAX = C };

    uint8_t buf[1] = { 5 }; // out of range
    fate::ByteReader r(buf, 1);
    auto val = r.readEnum<TestEnum>(TestEnum::MAX);
    CHECK(val == TestEnum::A); // returns 0 on error
    CHECK(r.overflowed());
}

TEST_CASE("ByteReader readEnum accepts valid value") {
    enum class TestEnum : uint8_t { A = 0, B = 1, C = 2, MAX = C };

    uint8_t buf[1] = { 2 };
    fate::ByteReader r(buf, 1);
    auto val = r.readEnum<TestEnum>(TestEnum::MAX);
    CHECK(val == TestEnum::C);
    CHECK_FALSE(r.overflowed());
}
```

- [ ] **Step 2: Touch, build, verify compile error (readEnum doesn't exist)**

- [ ] **Step 3: Add readEnum template method**

In `engine/net/byte_stream.h`, add after `readFloat()`:

```cpp
template<typename E>
E readEnum(E maxValid) {
    static_assert(sizeof(E) == 1, "readEnum only supports uint8_t-backed enums");
    uint8_t raw = readU8();
    if (!overflow_ && raw > static_cast<uint8_t>(maxValid)) {
        overflow_ = true;
        return static_cast<E>(0);
    }
    return static_cast<E>(raw);
}
```

- [ ] **Step 4: Touch, build, run tests**

```bash
touch engine/net/byte_stream.h tests/test_byte_stream.cpp
```
`./build/Debug/fate_tests.exe -tc="ByteReader readEnum*"`
Expected: Both PASS

---

### Task 4: ByteReader — add ok() alias and readVec2 NaN protection

**Files:**
- Modify: `engine/net/byte_stream.h` (add ok() method, harden readVec2)
- Modify: `tests/test_byte_stream.cpp`

- [ ] **Step 1: Write test for ok() and NaN Vec2**

```cpp
TEST_CASE("ByteReader ok() returns false after overflow") {
    uint8_t buf[1] = { 0x42 };
    fate::ByteReader r(buf, 1);
    CHECK(r.ok());
    r.readU32(); // overflow
    CHECK_FALSE(r.ok());
}

TEST_CASE("ByteReader rejects NaN in Vec2") {
    uint8_t buf[8];
    uint32_t nan_bits = 0x7FC00000;
    float good = 1.0f;
    std::memcpy(buf, &good, 4);
    std::memcpy(buf + 4, &nan_bits, 4); // y is NaN

    fate::ByteReader r(buf, 8);
    fate::Vec2 v = r.readVec2();
    CHECK(r.overflowed()); // NaN in y triggers overflow
}
```

- [ ] **Step 2: Add ok() alias**

In `engine/net/byte_stream.h`, add to the public section of ByteReader:

```cpp
[[nodiscard]] bool ok() const { return !overflow_; }
```

`readVec2()` already calls `readFloat()` twice, and `readFloat()` now rejects NaN/Inf. No changes needed to `readVec2()` — the sticky error propagates naturally.

- [ ] **Step 3: Touch, build, run all ByteReader tests**

```bash
touch engine/net/byte_stream.h tests/test_byte_stream.cpp
```
`./build/Debug/fate_tests.exe -tc="Byte*"`
Expected: All PASS

- [ ] **Step 4: Commit all ByteReader hardening**

```bash
git add engine/net/byte_stream.h tests/test_byte_stream.cpp
git commit -m "fix: harden ByteReader with NaN rejection, string length cap, enum bounds, ok() alias"
```

---

### Task 5: Per-tick skill command cap — server enforcement

**Files:**
- Modify: `server/server_app.h:115` (add `skillCommandsThisTick_` map)
- Modify: `server/server_app.cpp:233-235` (reset in tick)
- Modify: `server/server_app.cpp:1077-1082` (clean up on disconnect)
- Modify: `server/server_app.cpp:2082` (check in processUseSkill)

- [ ] **Step 1: Add the per-client skill command counter to ServerApp**

In `server/server_app.h`, add after `moveCountThisTick_` (line 115):

```cpp
std::unordered_map<uint16_t, int>   skillCommandsThisTick_;
```

- [ ] **Step 2: Reset counters each tick**

In `server/server_app.cpp`, in the `tick()` function, add after line 235 (`moveCountThisTick_` reset):

```cpp
    for (auto& [id, count] : skillCommandsThisTick_) count = 0;
```

- [ ] **Step 3: Clean up on disconnect**

In `server/server_app.cpp`, in `onClientDisconnected()`, add after `moveCountThisTick_.erase(clientId)` (line 1080):

```cpp
    skillCommandsThisTick_.erase(clientId);
```

- [ ] **Step 4: Initialize on connect**

In `server/server_app.cpp`, in the connect flow, add near `moveCountThisTick_[clientId] = 0` (line 780):

```cpp
    skillCommandsThisTick_[clientId] = 0;
```

- [ ] **Step 5: Enforce cap in processUseSkill()**

In `server/server_app.cpp`, at the top of `processUseSkill()` (line 2082), add after the `client` null check:

```cpp
    // Per-tick skill command cap: silently drop excess skill commands
    skillCommandsThisTick_[clientId]++;
    if (skillCommandsThisTick_[clientId] > 1) {
        return; // Only 1 skill command per client per tick
    }
```

Insert this block right after line 2085 (`if (!client || client->playerEntityId == 0) return;`).

- [ ] **Step 6: Touch, build, verify compilation**

```bash
touch server/server_app.h server/server_app.cpp
```
Run build. Expected: compiles cleanly.

- [ ] **Step 7: Commit**

```bash
git add server/server_app.h server/server_app.cpp
git commit -m "fix: enforce per-tick skill command cap to prevent cooldown bypass"
```

---

### Task 6: Per-entity sequence counters — protocol change

**Files:**
- Modify: `engine/net/protocol.h:186-215` (add updateSeq to SvEntityUpdateMsg)
- Modify: `tests/test_protocol.cpp` (round-trip test with updateSeq)

- [ ] **Step 1: Write failing round-trip test**

Add to `tests/test_protocol.cpp`:

```cpp
TEST_CASE("SvEntityUpdateMsg round-trip with updateSeq") {
    uint8_t buf[256];
    fate::SvEntityUpdateMsg src;
    src.persistentId = 0xCAFE;
    src.fieldMask    = 0x000F; // all 4 fields
    src.position     = {10.0f, 20.0f};
    src.animFrame    = 3;
    src.flipX        = 1;
    src.currentHP    = 500;
    src.updateSeq    = 42;

    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);
    CHECK_FALSE(w.overflowed());

    fate::ByteReader r(buf, w.size());
    auto dst = fate::SvEntityUpdateMsg::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(dst.updateSeq == 42);
    CHECK(dst.position.x == doctest::Approx(10.0f));
    CHECK(dst.currentHP == 500);
}
```

- [ ] **Step 2: Touch, build, verify compile error (updateSeq field doesn't exist)**

- [ ] **Step 3: Add updateSeq to SvEntityUpdateMsg**

In `engine/net/protocol.h`, modify the `SvEntityUpdateMsg` struct:

Add field after `int32_t currentHP = 0;`:
```cpp
    uint8_t updateSeq = 0;      // monotonic per-entity, for stale update rejection
```

In `write()`, add as the **first** field written (before persistentId):
```cpp
    void write(ByteWriter& w) const {
        w.writeU8(updateSeq);
        detail::writeU64(w, persistentId);
        w.writeU16(fieldMask);
        if (fieldMask & (1 << 0)) w.writeVec2(position);
        if (fieldMask & (1 << 1)) w.writeU8(animFrame);
        if (fieldMask & (1 << 2)) w.writeU8(flipX);
        if (fieldMask & (1 << 3)) w.writeI32(currentHP);
    }
```

In `read()`, read it first:
```cpp
    static SvEntityUpdateMsg read(ByteReader& r) {
        SvEntityUpdateMsg m;
        m.updateSeq    = r.readU8();
        m.persistentId = detail::readU64(r);
        m.fieldMask    = r.readU16();
        if (m.fieldMask & (1 << 0)) m.position  = r.readVec2();
        if (m.fieldMask & (1 << 1)) m.animFrame = r.readU8();
        if (m.fieldMask & (1 << 2)) m.flipX     = r.readU8();
        if (m.fieldMask & (1 << 3)) m.currentHP = r.readI32();
        return m;
    }
```

- [ ] **Step 4: Fix existing test size assertion**

In `tests/test_protocol.cpp`, the existing test "SvEntityUpdateMsg partial fields" has a hard-coded size check that will break. Update line 93-94:

Replace:
```cpp
    // Expected size: 8 (persistentId) + 2 (fieldMask) + 8 (Vec2) + 4 (i32) = 22
    CHECK(w.size() == 22);
```

With:
```cpp
    // Expected size: 1 (updateSeq) + 8 (persistentId) + 2 (fieldMask) + 8 (Vec2) + 4 (i32) = 23
    CHECK(w.size() == 23);
```

- [ ] **Step 5: Touch, build, run tests**

```bash
touch engine/net/protocol.h tests/test_protocol.cpp
```
`./build/Debug/fate_tests.exe -tc="SvEntityUpdate*"`
Expected: All PASS (both old and new tests)

---

### Task 7: Per-entity sequence counters — server-side increment

**Files:**
- Modify: `engine/net/replication.h:35` (add entitySeqCounters_ map)
- Modify: `engine/net/replication.cpp:193-210` (increment seq in sendDiffs stayed-entity loop)
- Modify: `engine/net/replication.cpp:137-144` (initialize seq for entered entities)

- [ ] **Step 1: Add sequence counter map to ReplicationManager**

In `engine/net/replication.h`, add to the private section (after `spatialIndex_`):

```cpp
    // Per-entity monotonic sequence counter for unreliable update ordering
    std::unordered_map<uint32_t, uint8_t> entitySeqCounters_; // key = EntityHandle packed value
```

- [ ] **Step 2: Initialize seq on entity register, remove on unregister**

In `engine/net/replication.cpp`, in `registerEntity()`:
```cpp
void ReplicationManager::registerEntity(EntityHandle handle, PersistentId pid) {
    handleToPid_[handle.value] = pid;
    pidToHandle_[pid.value()] = handle;
    entitySeqCounters_[handle.value] = 0;
}
```

In `unregisterEntity()`:
```cpp
void ReplicationManager::unregisterEntity(EntityHandle handle) {
    auto it = handleToPid_.find(handle.value);
    if (it != handleToPid_.end()) {
        pidToHandle_.erase(it->second.value());
        handleToPid_.erase(it);
    }
    entitySeqCounters_.erase(handle.value);
}
```

- [ ] **Step 3: Set updateSeq on entity enter messages**

In `sendDiffs()`, in the entered-entities loop, after `SvEntityEnterMsg enterMsg = buildEnterMessage(...)` (line 133), update the initial lastAckedState to include seq:

```cpp
        auto state = buildCurrentState(world, entity, pid);
        state.updateSeq = entitySeqCounters_[handle.value];
        client.lastAckedState[pid.value()] = state;
```

Replace the existing line 144 (`client.lastAckedState[pid.value()] = buildCurrentState(world, entity, pid);`).

- [ ] **Step 4: Increment and set seq on delta updates**

In `sendDiffs()`, in the stayed-entities loop, after `if (dirtyMask == 0) continue;` (line 191), and before building deltaMsg, increment the seq:

```cpp
        uint8_t& seq = entitySeqCounters_[handle.value];
        seq++; // wraps naturally at 255->0
```

Then set it on the deltaMsg:
```cpp
        deltaMsg.updateSeq = seq;
```

Also update the lastAckedState seq:
```cpp
        lastIt->second = current;
        lastIt->second.updateSeq = seq;
```

Replace the existing line 209 (`client.lastAckedState[pid.value()] = current;`).

- [ ] **Step 5: Touch, build, verify compilation**

```bash
touch engine/net/replication.h engine/net/replication.cpp engine/net/protocol.h
```
Run build. Expected: compiles cleanly.

- [ ] **Step 6: Run existing replication tests**

`./build/Debug/fate_tests.exe -tc="*replication*"`
Expected: All PASS

- [ ] **Step 7: Commit**

```bash
git add engine/net/protocol.h engine/net/replication.h engine/net/replication.cpp tests/test_protocol.cpp
git commit -m "feat: add per-entity sequence counters to unreliable entity updates"
```

---

### Task 8: Per-entity sequence counters — client-side rejection

**Files:**
- Modify: `game/game_app.h:66` (add ghostUpdateSeqs_ map)
- Modify: `game/game_app.cpp:830-864` (check seq in onEntityUpdate, seed in onEntityEnter, clean in onEntityLeave)

- [ ] **Step 1: Add tracking map to GameApp**

In `game/game_app.h`, add after `ghostEntities_`:

```cpp
    std::unordered_map<uint64_t, uint8_t> ghostUpdateSeqs_; // PersistentId -> last applied seq
```

- [ ] **Step 2: Clean seq on entity leave**

In the `onEntityLeave` lambda, after `ghostEntities_.erase(it);` add:

```cpp
        ghostUpdateSeqs_.erase(msg.persistentId);
```

Note: we do NOT seed `ghostUpdateSeqs_` in `onEntityEnter`. The first update for any entity is always accepted unconditionally (see Step 3). This avoids a bug where the server's seq counter may already be >127 by the time a new client enters, which would cause wrapping comparison to false-positive reject the first update.

- [ ] **Step 3: Add stale-update rejection in onEntityUpdate**

In the `onEntityUpdate` lambda, at the very top (before `auto it = ghostEntities_.find(...)`), add:

```cpp
        // Reject stale updates via sequence counter (wrapping comparison)
        // First update for any entity is accepted unconditionally (no entry yet).
        auto seqIt = ghostUpdateSeqs_.find(msg.persistentId);
        if (seqIt != ghostUpdateSeqs_.end()) {
            int8_t diff = static_cast<int8_t>(msg.updateSeq - seqIt->second);
            if (diff <= 0) return; // stale or duplicate, discard
        }
        ghostUpdateSeqs_[msg.persistentId] = msg.updateSeq;
```

- [ ] **Step 5: Touch, build, verify compilation**

```bash
touch game/game_app.h game/game_app.cpp
```
Run build. Expected: compiles cleanly.

- [ ] **Step 6: Commit**

```bash
git add game/game_app.h game/game_app.cpp
git commit -m "feat: client-side stale entity update rejection via sequence counters"
```

---

### Task 9: Replace system() with ShellExecuteW

**Files:**
- Modify: `engine/editor/editor.cpp:1076-1089`

- [ ] **Step 1: Add Windows header include**

In `engine/editor/editor.cpp`, add near the top includes (after SDL includes):

```cpp
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
```

- [ ] **Step 2: Replace "Open in VS Code" system() call**

Replace lines 1076-1079:

```cpp
                                if (ImGui::MenuItem("Open in VS Code")) {
                                    std::string cmd = "code \"" + asset.fullPath + "\"";
                                    system(cmd.c_str());
                                }
```

With (uses `MultiByteToWideChar` for UTF-8 safe conversion — simple `wstring(begin, end)` breaks on non-ASCII user paths):

```cpp
                                if (ImGui::MenuItem("Open in VS Code")) {
#ifdef _WIN32
                                    int wlen = MultiByteToWideChar(CP_UTF8, 0, asset.fullPath.c_str(), -1, nullptr, 0);
                                    std::wstring wpath(wlen, L'\0');
                                    MultiByteToWideChar(CP_UTF8, 0, asset.fullPath.c_str(), -1, wpath.data(), wlen);
                                    ShellExecuteW(nullptr, L"open", L"code", wpath.c_str(), nullptr, SW_SHOWNORMAL);
#endif
                                }
```

- [ ] **Step 3: Replace "Show in Explorer" system() call**

Replace lines 1081-1089:

```cpp
                            if (ImGui::MenuItem("Show in Explorer")) {
                                std::string dir = asset.fullPath;
                                size_t lastSlash = dir.find_last_of("/\\");
                                if (lastSlash != std::string::npos) dir = dir.substr(0, lastSlash);
                                std::string cmd = "explorer \"" + dir + "\"";
                                // Convert forward slashes to backslashes for Windows
                                for (auto& c : cmd) if (c == '/') c = '\\';
                                system(cmd.c_str());
                            }
```

With:

```cpp
                            if (ImGui::MenuItem("Show in Explorer")) {
#ifdef _WIN32
                                std::string dir = asset.fullPath;
                                size_t lastSlash = dir.find_last_of("/\\");
                                if (lastSlash != std::string::npos) dir = dir.substr(0, lastSlash);
                                for (auto& c : dir) if (c == '/') c = '\\';
                                int wlen = MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0);
                                std::wstring wdir(wlen, L'\0');
                                MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, wdir.data(), wlen);
                                ShellExecuteW(nullptr, L"open", L"explorer", wdir.c_str(), nullptr, SW_SHOWNORMAL);
#endif
                            }
```

- [ ] **Step 4: Touch, build, verify compilation**

```bash
touch engine/editor/editor.cpp
```
Run build. Expected: compiles cleanly.

- [ ] **Step 5: Commit**

```bash
git add engine/editor/editor.cpp
git commit -m "fix: replace system() with ShellExecuteW to prevent command injection"
```

---

### Task 10: Full test suite regression check

- [ ] **Step 1: Run the full test suite**

```bash
touch engine/net/byte_stream.h engine/net/protocol.h engine/net/replication.h engine/net/replication.cpp server/server_app.h server/server_app.cpp game/game_app.h game/game_app.cpp engine/editor/editor.cpp tests/test_byte_stream.cpp tests/test_protocol.cpp
```
Build, then: `./build/Debug/fate_tests.exe`
Expected: All tests pass, zero failures.

- [ ] **Step 2: Verify test count increased**

Previous count: 356 tests. New tests added: ~8 (ByteReader hardening) + 1 (protocol seq). Expected: ~365 tests total, all passing.
