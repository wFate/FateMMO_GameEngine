# Scene Loading from DB Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Load a player's saved scene from the database on connect, and save the current scene on zone transitions, so players return to where they left off in a multi-zone world.

**Architecture:** The character's `current_scene` is already saved in the `characters` table and loaded into `CharacterRecord`. On the server, when a player connects, the server should load the scene matching `rec.current_scene`. On the client, when the server sends `SvZoneTransition`, the client loads the new scene. The `SceneCache` (3 scenes loaded from DB) provides metadata like PvP status and level requirements. Portal transitions already set `current_scene` in the save — this plan ensures it's used for initial load and that transitions save to DB.

**Tech Stack:** C++20, existing SceneManager, SceneCache, CharacterRepository, ZoneSystem

---

## File Map

| File | Change |
|---|---|
| `server/server_app.cpp` | Modify: use `rec.current_scene` for player spawn scene, save scene on zone transition |
| `game/game_app.cpp` | Modify: handle SvZoneTransition to load new scene on client |
| `engine/net/net_client.h` | Modify: add onZoneTransition callback (SvZoneTransition packet 0x97 already defined but may not have a callback) |

---

### Task 1: Server Uses Saved Scene on Connect

**Files:**
- Modify: `server/server_app.cpp`

- [ ] **Step 1: Verify current_scene is already loaded from DB**

Check `CharacterRecord::current_scene` — it should already be populated by `characterRepo_->loadCharacter()`. Verify the field exists and has a default.

- [ ] **Step 2: Use rec.current_scene when creating the player entity**

Currently the player is created without regard to scene. If the engine has a concept of scene/zone assignment, ensure the player entity is associated with `rec.current_scene`. At minimum, log which scene the player should be in.

- [ ] **Step 3: Save scene name on zone transition**

When a player transitions zones (detected by portal collision or CmdZoneTransition), update `rec.current_scene` and save to DB. This may already happen in `savePlayerToDB()` — verify.

- [ ] **Step 4: Build, test**

- [ ] **Step 5: Commit**

---

### Task 2: Client Handles Zone Transition Packet

**Files:**
- Modify: `engine/net/net_client.h` (add callback if missing)
- Modify: `engine/net/net_client.cpp` (add case in handlePacket if missing)
- Modify: `game/game_app.cpp` (register callback)

- [ ] **Step 1: Check if SvZoneTransition (0x97) is handled in net_client.cpp**

The packet type exists in `packet.h` but may not be deserialized in `handlePacket()`.

- [ ] **Step 2: Define SvZoneTransitionMsg struct if missing**

In `protocol.h` or `game_messages.h`:

```cpp
struct SvZoneTransitionMsg {
    std::string targetScene;
    float spawnX = 0;
    float spawnY = 0;

    void write(ByteWriter& w) const {
        w.writeString(targetScene);
        w.writeFloat(spawnX);
        w.writeFloat(spawnY);
    }
    static SvZoneTransitionMsg read(ByteReader& r) {
        SvZoneTransitionMsg m;
        m.targetScene = r.readString();
        m.spawnX = r.readFloat();
        m.spawnY = r.readFloat();
        return m;
    }
};
```

- [ ] **Step 3: Add callback and handler**

In `net_client.h`: `std::function<void(const SvZoneTransitionMsg&)> onZoneTransition;`

In `net_client.cpp` handlePacket: deserialize and invoke.

In `game_app.cpp`: register callback that triggers scene load via SceneManager.

- [ ] **Step 4: Build, test**

- [ ] **Step 5: Commit**

---

### Task 3: SceneCache Integration for Level Gating

**Files:**
- Modify: `server/server_app.cpp`

- [ ] **Step 1: When processing portal transitions, check SceneCache for level requirements**

Before allowing a zone transition, look up the target scene in `sceneCache_`:

```cpp
const auto* targetScene = sceneCache_.get(targetSceneId);
if (targetScene && charStats->stats.level < targetScene->minLevel) {
    // Reject transition — player too low level
    // Send error message via system chat
}
```

- [ ] **Step 2: Build, test**

- [ ] **Step 3: Commit**

---

### Task 4: Update Docs

- [ ] **Step 1: Update ENGINE_STATE_AND_FEATURES.md**
- [ ] **Step 2: Commit**
