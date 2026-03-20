# Death & Respawn System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement TWOM-style player death experience with sprite rotation, death overlay UI, three respawn options (town/map spawn/Phoenix Down), and server-authoritative respawn protocol.

**Architecture:** New `SvDeathNotifyMsg` and `CmdRespawn`/`SvRespawnMsg` protocol messages. `DeathOverlayUI` renders respawn options. `SpawnPointComponent` marks respawn locations in scenes. `GameplaySystem` modified to stop auto-respawn. Server validates all respawn requests and determines positions.

**Tech Stack:** C++ 20, custom ECS, ImGui, UDP netcode with reliability layer

**Spec:** `Docs/superpowers/specs/2026-03-19-death-respawn-design.md`

**Build command:**
```bash
export LIB="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64"
export INCLUDE="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build out/build/x64-Debug --config Debug 2>&1 | grep -E "error C|FAILED|Linking|ninja: build"
```

**CRITICAL:** `touch` ALL edited .cpp files before building (CMake misses static lib changes on this machine).

**Test command:** `./out/build/x64-Debug/fate_tests.exe`

**Do NOT add `Co-Authored-By` lines to commit messages.**

**Key codebase facts:**
- `World::forEach` supports 1 or 2 template component types only (NOT 3)
- Rotation lives on `Transform::rotation` in **radians** (not degrees, not on SpriteComponent)
- `ByteWriter` requires buffer+capacity: `uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));`
- Components register with `reg.registerComponent<T>()` — no `REG()` macro
- CombatActionSystem already blocks dead players at line 434: `if (playerStats.isDead) return;` — no changes needed there

---

## File Map

| File | Action | Purpose |
|------|--------|---------|
| `engine/net/packet.h` | Modify | Add `CmdRespawn = 0x1B`, `SvDeathNotify = 0xA0`, `SvRespawn = 0xA1` |
| `engine/net/game_messages.h` | Modify | Add `SvDeathNotifyMsg`, `CmdRespawnMsg`, `SvRespawnMsg` structs |
| `engine/net/net_client.h` | Modify | Add `sendRespawn()`, `onDeathNotify`, `onRespawn` callback |
| `engine/net/net_client.cpp` | Modify | Add `sendRespawn()` impl + packet handlers for new types |
| `game/components/spawn_point_component.h` | Create | `SpawnPointComponent` with `isTownSpawn` flag |
| `game/register_components.h` | Modify | Register `SpawnPointComponent` with custom serializer |
| `game/shared/character_stats.cpp` | Modify | `respawn()` also restores MP |
| `game/systems/gameplay_system.h` | Modify | Stop auto-respawn, add death rotation + gray tint visual |
| `game/systems/movement_system.h` | Modify | Block input when dead |
| `game/ui/death_overlay_ui.h` | Create | `DeathOverlayUI` class |
| `game/ui/death_overlay_ui.cpp` | Create | Death overlay rendering |
| `game/game_app.h` | Modify | Add `DeathOverlayUI` member |
| `game/game_app.cpp` | Modify | Wire death/respawn callbacks, render overlay |
| `game/ui/skill_bar_ui.h` | Modify | Gray out skills when dead |
| `tests/test_death_respawn.cpp` | Create | All death/respawn tests |

---

## Task 1: Protocol Messages + Tests

**Files:**
- Modify: `engine/net/packet.h:47,65`
- Modify: `engine/net/game_messages.h:277`
- Create: `tests/test_death_respawn.cpp`

- [ ] **Step 1: Create test file with failing tests**

Create `tests/test_death_respawn.cpp`:

```cpp
#include <doctest/doctest.h>
#include "engine/net/game_messages.h"
#include "engine/net/packet.h"
#include "game/shared/character_stats.h"

using namespace fate;

TEST_CASE("SvDeathNotifyMsg round-trip") {
    SvDeathNotifyMsg src;
    src.deathSource = 1;
    src.respawnTimer = 5.0f;
    src.xpLost = 1234;
    src.honorLost = 30;

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    src.write(w);
    ByteReader r(buf, w.size());
    auto dst = SvDeathNotifyMsg::read(r);

    CHECK(dst.deathSource == 1);
    CHECK(dst.respawnTimer == doctest::Approx(5.0f));
    CHECK(dst.xpLost == 1234);
    CHECK(dst.honorLost == 30);
}

TEST_CASE("CmdRespawnMsg round-trip") {
    CmdRespawnMsg src;
    src.respawnType = 2;

    uint8_t buf[64];
    ByteWriter w(buf, sizeof(buf));
    src.write(w);
    ByteReader r(buf, w.size());
    auto dst = CmdRespawnMsg::read(r);

    CHECK(dst.respawnType == 2);
}

TEST_CASE("SvRespawnMsg round-trip") {
    SvRespawnMsg src;
    src.respawnType = 1;
    src.spawnX = 320.0f;
    src.spawnY = 480.0f;

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    src.write(w);
    ByteReader r(buf, w.size());
    auto dst = SvRespawnMsg::read(r);

    CHECK(dst.respawnType == 1);
    CHECK(dst.spawnX == doctest::Approx(320.0f));
    CHECK(dst.spawnY == doctest::Approx(480.0f));
}

TEST_CASE("Death/respawn packet IDs are unique") {
    CHECK(PacketType::CmdRespawn == 0x1B);
    CHECK(PacketType::SvDeathNotify == 0xA0);
    CHECK(PacketType::SvRespawn == 0xA1);
    CHECK(PacketType::CmdRespawn != PacketType::CmdZoneTransition);
    CHECK(PacketType::SvDeathNotify != PacketType::SvQuestUpdate);
}

TEST_CASE("respawn restores HP and MP to full") {
    CharacterStats stats;
    stats.level = 10;
    stats.recalculateStats();
    stats.currentHP = 0;
    stats.currentMP = 0;
    stats.isDead = true;

    stats.respawn();

    CHECK(stats.isDead == false);
    CHECK(stats.currentHP == stats.maxHP);
    CHECK(stats.currentMP == stats.maxMP);
}

TEST_CASE("die applies XP loss for PvE, not PvP") {
    CharacterStats stats;
    stats.level = 5;
    stats.recalculateStats();
    stats.recalculateXPRequirement();
    stats.currentHP = stats.maxHP;
    stats.currentXP = 1000;

    SUBCASE("PvE death loses XP") {
        stats.die(DeathSource::PvE);
        CHECK(stats.isDead == true);
        CHECK(stats.currentHP == 0);
        CHECK(stats.currentXP < 1000);
    }

    SUBCASE("PvP death keeps XP") {
        stats.die(DeathSource::PvP);
        CHECK(stats.isDead == true);
        CHECK(stats.currentXP == 1000);
    }

    SUBCASE("die is idempotent") {
        stats.die(DeathSource::PvE);
        int64_t xpAfter = stats.currentXP;
        stats.die(DeathSource::PvE);
        CHECK(stats.currentXP == xpAfter);
    }
}
```

- [ ] **Step 2: Add packet type constants**

In `engine/net/packet.h`, after `CmdZoneTransition = 0x1A;` (line 47):

```cpp
    constexpr uint8_t CmdRespawn       = 0x1B;
```

After `SvQuestUpdate = 0x9F;` (line 65):

```cpp
    constexpr uint8_t SvDeathNotify    = 0xA0;
    constexpr uint8_t SvRespawn        = 0xA1;
```

- [ ] **Step 3: Add message structs**

In `engine/net/game_messages.h`, after `SvZoneTransitionMsg` (after line 277), before closing `}`:

```cpp
struct SvDeathNotifyMsg {
    uint8_t deathSource  = 0;  // 0=PvE, 1=PvP, 2=Gauntlet, 3=Environment
    float   respawnTimer = 5.0f;
    int32_t xpLost       = 0;
    int32_t honorLost    = 0;

    void write(ByteWriter& w) const {
        w.writeU8(deathSource);
        w.writeFloat(respawnTimer);
        w.writeI32(xpLost);
        w.writeI32(honorLost);
    }
    static SvDeathNotifyMsg read(ByteReader& r) {
        SvDeathNotifyMsg m;
        m.deathSource  = r.readU8();
        m.respawnTimer = r.readFloat();
        m.xpLost       = r.readI32();
        m.honorLost    = r.readI32();
        return m;
    }
};

struct CmdRespawnMsg {
    uint8_t respawnType = 0;  // 0=town, 1=map spawn, 2=here (Phoenix Down)

    void write(ByteWriter& w) const {
        w.writeU8(respawnType);
    }
    static CmdRespawnMsg read(ByteReader& r) {
        CmdRespawnMsg m;
        m.respawnType = r.readU8();
        return m;
    }
};

struct SvRespawnMsg {
    uint8_t respawnType = 0;
    float   spawnX      = 0.0f;
    float   spawnY      = 0.0f;

    void write(ByteWriter& w) const {
        w.writeU8(respawnType);
        w.writeFloat(spawnX);
        w.writeFloat(spawnY);
    }
    static SvRespawnMsg read(ByteReader& r) {
        SvRespawnMsg m;
        m.respawnType = r.readU8();
        m.spawnX      = r.readFloat();
        m.spawnY      = r.readFloat();
        return m;
    }
};
```

- [ ] **Step 4: Fix respawn() to restore MP**

In `game/shared/character_stats.cpp`, line 305, add after `currentHP = maxHP;`:

```cpp
    currentMP = maxMP;
```

- [ ] **Step 5: Build and run tests**

```bash
touch engine/net/packet.h engine/net/game_messages.h game/shared/character_stats.cpp tests/test_death_respawn.cpp
# Build all targets, run fate_tests.exe
```

Expected: All new + existing tests pass.

- [ ] **Step 6: Commit**

```bash
git add engine/net/packet.h engine/net/game_messages.h game/shared/character_stats.cpp tests/test_death_respawn.cpp
git commit -m "feat: add death/respawn protocol messages, fix respawn() to restore MP"
```

---

## Task 2: SpawnPointComponent

**Files:**
- Create: `game/components/spawn_point_component.h`
- Modify: `game/register_components.h`

- [ ] **Step 1: Create the component**

Create `game/components/spawn_point_component.h`:

```cpp
#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"

namespace fate {

struct SpawnPointComponent {
    FATE_COMPONENT_COLD(SpawnPointComponent)
    bool isTownSpawn = false;
};

} // namespace fate

FATE_REFLECT(fate::SpawnPointComponent,
    FATE_FIELD(isTownSpawn, Bool)
)
```

- [ ] **Step 2: Register in component registry**

In `game/register_components.h`, add include after line 22 (`#include "engine/render/point_light_component.h"`):

```cpp
#include "game/components/spawn_point_component.h"
```

Add trait specialization after the existing trait blocks (around line 162):

```cpp
// --- SpawnPointComponent: serialized (placed in scene editor as respawn marker) ---
// Distinct from BossSpawnPointComponent which tracks mob boss spawn rotation/timers.
template<> struct component_traits<SpawnPointComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};
```

In `registerAllComponents()`, after the zone component registrations (around line 297, after `reg.registerComponent<PortalComponent>();`):

```cpp
    reg.registerComponent<SpawnPointComponent>();
```

- [ ] **Step 3: Build**

```bash
touch game/components/spawn_point_component.h game/register_components.h
# Build
```

- [ ] **Step 4: Commit**

```bash
git add game/components/spawn_point_component.h game/register_components.h
git commit -m "feat: add SpawnPointComponent for player respawn locations"
```

---

## Task 3: NetClient — sendRespawn + Callbacks + Packet Handlers

**Files:**
- Modify: `engine/net/net_client.h:24,47`
- Modify: `engine/net/net_client.cpp`

- [ ] **Step 1: Add method and callbacks to header**

In `engine/net/net_client.h`, after `sendZoneTransition` (line 24):

```cpp
    void sendRespawn(uint8_t respawnType);
```

After `onZoneTransition` callback (line 47):

```cpp
    std::function<void(const SvDeathNotifyMsg&)> onDeathNotify;
    std::function<void(const SvRespawnMsg&)> onRespawn;
```

- [ ] **Step 2: Implement sendRespawn**

In `engine/net/net_client.cpp`, after the last `send*` method (before `handlePacket`):

```cpp
void NetClient::sendRespawn(uint8_t respawnType) {
    CmdRespawnMsg msg;
    msg.respawnType = respawnType;
    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    sendPacket(Channel::ReliableOrdered, PacketType::CmdRespawn, w.data(), w.size());
}
```

- [ ] **Step 3: Add packet handler cases**

In `handlePacket()`, before the `default:` case (line 266):

```cpp
        case PacketType::SvDeathNotify: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            auto msg = SvDeathNotifyMsg::read(payload);
            if (onDeathNotify) onDeathNotify(msg);
            break;
        }
        case PacketType::SvRespawn: {
            ByteReader payload(data + r.position(), hdr.payloadSize);
            auto msg = SvRespawnMsg::read(payload);
            if (onRespawn) onRespawn(msg);
            break;
        }
```

- [ ] **Step 4: Build**

```bash
touch engine/net/net_client.h engine/net/net_client.cpp
# Build
```

- [ ] **Step 5: Commit**

```bash
git add engine/net/net_client.h engine/net/net_client.cpp
git commit -m "feat: add sendRespawn, onDeathNotify, onRespawn to NetClient"
```

---

## Task 4: GameplaySystem — Remove Auto-Respawn, Add Death Visual

**Files:**
- Modify: `game/systems/gameplay_system.h:75-97`

- [ ] **Step 1: Replace the isDead block**

In `game/systems/gameplay_system.h`, replace lines 75-97 (the entire `if (stats.isDead) { ... }` block including the `return;`):

```cpp
                // -- Death state management --
                if (stats.isDead) {
                    // Initialize death visual on first frame of death
                    if (stats.respawnTimeRemaining <= 0.0f) {
                        stats.respawnTimeRemaining = respawnCooldown;
                        stats.currentHP = 0;
                        // Death visual: gray tint
                        auto* spr = entity->getComponent<SpriteComponent>();
                        if (spr) spr->tint = Color(0.3f, 0.3f, 0.3f, 0.6f);
                        // Lay down: rotate 90 degrees (Transform uses radians)
                        auto* t = entity->getComponent<Transform>();
                        if (t) t->rotation = -1.5708f;  // -PI/2
                        // Stop animation
                        auto* anim = entity->getComponent<Animator>();
                        if (anim) anim->stop();
                    }

                    // Tick countdown (DeathOverlayUI reads this for button activation)
                    if (stats.respawnTimeRemaining > 0.0f) {
                        stats.respawnTimeRemaining -= dt;
                        if (stats.respawnTimeRemaining < 0.0f)
                            stats.respawnTimeRemaining = 0.0f;
                    }

                    // Do NOT auto-respawn — player chooses via DeathOverlayUI
                    syncNameplate(entity, stats);
                    return;
                }
```

- [ ] **Step 2: Build and run tests**

```bash
touch game/systems/gameplay_system.h
# Build and run
```

- [ ] **Step 3: Commit**

```bash
git add game/systems/gameplay_system.h
git commit -m "feat: replace auto-respawn with death visual (lay-down rotation + gray tint)"
```

---

## Task 5: MovementSystem — Block Input When Dead

**Files:**
- Modify: `game/systems/movement_system.h`

- [ ] **Step 1: Add include and isDead check**

In `game/systems/movement_system.h`, add include after line 6 (`#include "game/components/animator.h"`):

```cpp
#include "game/components/game_components.h"
```

Inside the `forEach<Transform, PlayerController>` lambda, after `if (!ctrl->isLocalPlayer) return;` (line 27), add:

```cpp
                // Dead players cannot move
                auto* statsComp = entity->getComponent<CharacterStatsComponent>();
                if (statsComp && statsComp->stats.isDead) {
                    ctrl->isMoving = false;
                    return;
                }
```

- [ ] **Step 2: Build and run**

```bash
touch game/systems/movement_system.h
# Build and run
```

- [ ] **Step 3: Commit**

```bash
git add game/systems/movement_system.h
git commit -m "feat: block player movement input while dead"
```

---

## Task 6: DeathOverlayUI

**Files:**
- Create: `game/ui/death_overlay_ui.h`
- Create: `game/ui/death_overlay_ui.cpp`

- [ ] **Step 1: Create header**

Create `game/ui/death_overlay_ui.h`:

```cpp
#pragma once
#include "engine/ecs/entity.h"
#include <cstdint>
#include <functional>

namespace fate {

class DeathOverlayUI {
public:
    std::function<void(uint8_t respawnType)> onRespawnRequested;

    void onDeath(int32_t xpLost, int32_t honorLost, float respawnTimer);
    void render(Entity* player);

private:
    bool active_ = false;
    float countdown_ = 0.0f;
    int32_t xpLost_ = 0;
    int32_t honorLost_ = 0;
};

} // namespace fate
```

- [ ] **Step 2: Create implementation**

Create `game/ui/death_overlay_ui.cpp`:

```cpp
#include "game/ui/death_overlay_ui.h"
#include "game/ui/game_viewport.h"
#include "game/components/game_components.h"
#include "imgui.h"
#include <cstdio>

namespace fate {

void DeathOverlayUI::onDeath(int32_t xpLost, int32_t honorLost, float respawnTimer) {
    active_ = true;
    countdown_ = respawnTimer;
    xpLost_ = xpLost;
    honorLost_ = honorLost;
}

void DeathOverlayUI::render(Entity* player) {
    if (!player) return;

    auto* statsComp = player->getComponent<CharacterStatsComponent>();
    if (!statsComp || !statsComp->stats.isDead) {
        active_ = false;
        return;
    }

    if (!active_) return;

    // Tick local countdown
    float dt = ImGui::GetIO().DeltaTime;
    if (countdown_ > 0.0f) {
        countdown_ -= dt;
        if (countdown_ < 0.0f) countdown_ = 0.0f;
    }

    // Panel sizing and positioning via GameViewport
    float panelW = 300.0f;
    float panelH = 220.0f;
    ImVec2 center(GameViewport::centerX(), GameViewport::centerY());

    ImGui::SetNextWindowPos(ImVec2(center.x - panelW * 0.5f, center.y - panelH * 0.5f));
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.02f, 0.02f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.5f, 0.1f, 0.1f, 0.8f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav;

    if (!ImGui::Begin("##DeathOverlay", nullptr, flags)) {
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        return;
    }

    // "You have died."
    ImVec2 titleSize = ImGui::CalcTextSize("You have died.");
    ImGui::SetCursorPosX((panelW - titleSize.x) * 0.5f);
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "You have died.");
    ImGui::Spacing();

    // Penalty display
    if (xpLost_ > 0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Lost %d XP", xpLost_);
        ImVec2 sz = ImGui::CalcTextSize(buf);
        ImGui::SetCursorPosX((panelW - sz.x) * 0.5f);
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", buf);
    }
    if (honorLost_ > 0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Lost %d Honor", honorLost_);
        ImVec2 sz = ImGui::CalcTextSize(buf);
        ImGui::SetCursorPosX((panelW - sz.x) * 0.5f);
        ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.8f, 1.0f), "%s", buf);
    }

    ImGui::Spacing();

    // Countdown
    bool timerReady = countdown_ <= 0.0f;
    if (!timerReady) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "Respawn available in %d...", (int)countdown_ + 1);
        ImVec2 sz = ImGui::CalcTextSize(buf);
        ImGui::SetCursorPosX((panelW - sz.x) * 0.5f);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", buf);
    }

    ImGui::Spacing();
    float buttonW = panelW - 40.0f;

    // Respawn in Town
    ImGui::SetCursorPosX(20.0f);
    if (!timerReady) ImGui::BeginDisabled();
    if (ImGui::Button("Respawn in Town", ImVec2(buttonW, 28.0f))) {
        if (onRespawnRequested) onRespawnRequested(0);
    }
    if (!timerReady) ImGui::EndDisabled();

    // Respawn at Spawn Point
    ImGui::SetCursorPosX(20.0f);
    if (!timerReady) ImGui::BeginDisabled();
    if (ImGui::Button("Respawn at Spawn Point", ImVec2(buttonW, 28.0f))) {
        if (onRespawnRequested) onRespawnRequested(1);
    }
    if (!timerReady) ImGui::EndDisabled();

    // Phoenix Down — only if player has the item
    auto* invComp = player->getComponent<InventoryComponent>();
    int phoenixCount = 0;
    if (invComp) {
        phoenixCount = invComp->inventory.countItem("phoenix_down");
    }

    if (phoenixCount > 0) {
        ImGui::SetCursorPosX(20.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.5f, 0.15f, 1.0f));
        char label[64];
        std::snprintf(label, sizeof(label), "Respawn Here (Phoenix Down x%d)", phoenixCount);
        if (ImGui::Button(label, ImVec2(buttonW, 28.0f))) {
            if (onRespawnRequested) onRespawnRequested(2);
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

} // namespace fate
```

- [ ] **Step 3: Build**

```bash
touch game/ui/death_overlay_ui.h game/ui/death_overlay_ui.cpp
# Build (CMakeLists uses GLOB_RECURSE for game/*.cpp, so new file is auto-picked up)
```

- [ ] **Step 4: Commit**

```bash
git add game/ui/death_overlay_ui.h game/ui/death_overlay_ui.cpp
git commit -m "feat: add DeathOverlayUI with three respawn options"
```

---

## Task 7: Wire Everything in GameApp

**Files:**
- Modify: `game/game_app.h:13,52`
- Modify: `game/game_app.cpp`

- [ ] **Step 1: Add member to header**

In `game/game_app.h`, add include after line 13 (`#include "game/ui/chat_ui.h"`):

```cpp
#include "game/ui/death_overlay_ui.h"
```

Add member after line 52 (`ChatUI chatUI_;`):

```cpp
    DeathOverlayUI deathOverlayUI_;
```

- [ ] **Step 2: Wire callbacks in game_app.cpp**

In `game/game_app.cpp`, after the `onQuestUpdate` callback block (around line 989), add:

```cpp
    netClient_.onDeathNotify = [this](const SvDeathNotifyMsg& msg) {
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;

        scene->world().forEach<PlayerController, CharacterStatsComponent>(
            [&](Entity*, PlayerController* ctrl, CharacterStatsComponent* sc) {
                if (!ctrl->isLocalPlayer) return;
                sc->stats.isDead = true;
                sc->stats.currentHP = 0;
                sc->stats.respawnTimeRemaining = msg.respawnTimer;
            }
        );

        deathOverlayUI_.onDeath(msg.xpLost, msg.honorLost, msg.respawnTimer);
        LOG_INFO("Client", "You died! Lost %d XP, %d Honor", msg.xpLost, msg.honorLost);
    };

    netClient_.onRespawn = [this](const SvRespawnMsg& msg) {
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;

        scene->world().forEach<PlayerController, CharacterStatsComponent>(
            [&](Entity* entity, PlayerController* ctrl, CharacterStatsComponent* sc) {
                if (!ctrl->isLocalPlayer) return;
                sc->stats.respawn();
                // Restore visual
                auto* spr = entity->getComponent<SpriteComponent>();
                if (spr) spr->tint = Color::white();
                auto* t = entity->getComponent<Transform>();
                if (t) {
                    t->rotation = 0.0f;
                    t->position = {msg.spawnX, msg.spawnY};
                }
                auto* anim = entity->getComponent<Animator>();
                if (anim) anim->play("idle");
            }
        );

        LOG_INFO("Client", "Respawned at (%.0f, %.0f)", msg.spawnX, msg.spawnY);
    };

    deathOverlayUI_.onRespawnRequested = [this](uint8_t respawnType) {
        netClient_.sendRespawn(respawnType);
    };
```

- [ ] **Step 3: Render the death overlay**

Find where other UI is rendered in `onRender` or `onUpdate` (where `questLogUI_.render(...)` and `chatUI_.render()` are called). Add after them:

```cpp
    // Death overlay (renders on top when dead)
    {
        auto* scene = SceneManager::instance().currentScene();
        if (scene) {
            Entity* localPlayer = nullptr;
            scene->world().forEach<PlayerController>(
                [&](Entity* e, PlayerController* ctrl) {
                    if (ctrl->isLocalPlayer) localPlayer = e;
                }
            );
            deathOverlayUI_.render(localPlayer);
        }
    }
```

- [ ] **Step 4: Build and run all tests**

```bash
touch game/game_app.h game/game_app.cpp
# Build all targets, run tests
```

- [ ] **Step 5: Commit**

```bash
git add game/game_app.h game/game_app.cpp
git commit -m "feat: wire death/respawn callbacks and DeathOverlayUI in GameApp"
```

---

## Task 8: SkillBarUI Dead Check

**Files:**
- Modify: `game/ui/skill_bar_ui.h` (or its .cpp if `draw()` is in .cpp)

- [ ] **Step 1: Add isDead check to draw()**

In `SkillBarUI::draw(World* world)`, at the top of the method, add:

```cpp
    bool playerDead = false;
    world->forEach<PlayerController, CharacterStatsComponent>(
        [&](Entity*, PlayerController* ctrl, CharacterStatsComponent* sc) {
            if (ctrl->isLocalPlayer) playerDead = sc->stats.isDead;
        }
    );

    if (playerDead) ImGui::BeginDisabled();
```

And at the end of `draw()`, before the closing brace:

```cpp
    if (playerDead) ImGui::EndDisabled();
```

- [ ] **Step 2: Build and run**

```bash
touch game/ui/skill_bar_ui.h
# Build and run
```

- [ ] **Step 3: Commit**

```bash
git add game/ui/skill_bar_ui.h
git commit -m "feat: gray out skill bar when player is dead"
```

---

## Task 9: Scene Spawn Points + Final Verification

- [ ] **Step 1: Run full test suite**

```bash
./out/build/x64-Debug/fate_tests.exe
```

Expected: All tests pass (197 original + ~8 new).

- [ ] **Step 2: Place SpawnPointComponent entities in scene files**

Check the scene JSON format by reading an existing scene file. Then add a SpawnPoint entity to each scene. Example for Town scene (adjust coordinates to match existing layout):

```json
{
    "name": "TownSpawnPoint",
    "components": {
        "Transform": { "position": [256.0, 256.0], "rotation": 0.0, "depth": 0.0, "scale": [1.0, 1.0] },
        "SpawnPointComponent": { "isTownSpawn": true }
    }
}
```

For WhisperingWoods (isTownSpawn = false).

- [ ] **Step 3: Commit**

```bash
git add assets/scenes/
git commit -m "feat: add spawn point entities to scenes, complete death/respawn client system"
```

---

## Summary

| Task | What | Key Files |
|------|------|-----------|
| 1 | Protocol messages + tests + MP fix | packet.h, game_messages.h, character_stats.cpp, test_death_respawn.cpp |
| 2 | SpawnPointComponent | spawn_point_component.h, register_components.h |
| 3 | NetClient wiring | net_client.h, net_client.cpp |
| 4 | GameplaySystem death visual | gameplay_system.h |
| 5 | MovementSystem dead block | movement_system.h |
| 6 | DeathOverlayUI | death_overlay_ui.h, death_overlay_ui.cpp |
| 7 | GameApp wiring | game_app.h, game_app.cpp |
| 8 | SkillBarUI dead check | skill_bar_ui.h |
| 9 | Scene spawn points + verification | scene JSONs |

**Server-side handling** (CmdRespawn dispatch in server_app.cpp, mob lethal damage triggering die()) is a follow-up task — the client system is fully testable without it via SvDeathNotifyMsg/SvRespawnMsg.

**Gauntlet Phoenix Down blocking** is a follow-up — requires an `isInGauntlet` mechanism that doesn't exist yet. The server will enforce this regardless.
