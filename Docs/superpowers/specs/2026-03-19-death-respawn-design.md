# Death & Respawn System — TWOM-Style

**Date:** 2026-03-19
**Status:** Approved

## Overview

When a player dies, their sprite falls over and they see a compact overlay with three respawn options. The server is fully authoritative over respawn location, Phoenix Down validation, and death penalties (already implemented). This spec covers the client-side death experience, server-side death triggering, and the client-server respawn protocol.

## Existing Code (No Changes Needed)

- `CharacterStats::die(DeathSource)` — applies XP loss (PvE) or honor loss (PvP)
- `CharacterStats::isDead`, `respawnTimeRemaining`, `respawn()` method
- `CharacterStats::onDied` / `onRespawned` callbacks
- `Inventory::findItemById()`, `countItem()`, `removeItemQuantity()` — for Phoenix Down
- `SvCombatEventMsg.isKill` — already broadcasts kill events
- `HonorSystem::processKill()` — PvP honor penalties
- `DeathSource` enum (PvE, PvP, Gauntlet, Environment)

## Existing Code (Needs Modification)

- `CharacterStats::respawn()` — currently restores HP only. Must also restore MP to full.
- `GameplaySystem` — currently auto-respawns at timer=0. Must stop auto-respawn; player-initiated only.
- `server_app.cpp` combat handler — currently only processes player→mob damage. Must also handle mob→player lethal damage (call `die()` on player when HP <= 0).

## New Component: SpawnPointComponent

```cpp
// game/components/spawn_point_component.h
struct SpawnPointComponent {
    FATE_COMPONENT_COLD(SpawnPointComponent)
    bool isTownSpawn = false;  // true = this is the town respawn point
};
```

- Placed as an invisible marker entity in the scene editor (Transform only, no sprite)
- Each scene should have one SpawnPointComponent entity for map-default spawn
- Town scene has one with `isTownSpawn = true`
- Registered in `register_components.h`
- Server reads position from its own world data — client never sends coordinates
- **Note:** Distinct from `BossSpawnPointComponent` which tracks mob boss spawn rotation/timers. SpawnPointComponent is strictly for player respawn locations.

## Network Protocol

### SvDeathNotifyMsg (Server → Client) — NEW

```
Packet type: 0xA0 (next available SvX slot)
Payload:
  uint8_t  deathSource    // 0=PvE, 1=PvP, 2=Gauntlet, 3=Environment
  float    respawnTimer   // seconds until timed respawn is available (5.0)
  int32_t  xpLost         // XP penalty applied (for display)
  int32_t  honorLost      // Honor penalty applied (for display, PvP only)
```

Sent by server when a player's HP reaches 0 and `die()` is called. The client uses this to:
- Set local `isDead = true` and start the death visual
- Show the DeathOverlayUI with penalty info
- Start the local 5-second countdown (mirror of server timer)

**Why a dedicated message:** `SvCombatEventMsg` is insufficient because it doesn't cover environment deaths, fall damage, DoT deaths, or other non-combat death sources. A dedicated message is clean and explicit.

### CmdRespawn (Client → Server)

```
Packet type: 0x1B (next available CmdX slot)
Payload:
  uint8_t respawnType   // 0 = town, 1 = map spawn, 2 = here (Phoenix Down)
```

### SvRespawnMsg (Server → Client)

```
Packet type: 0xA1
Payload:
  uint8_t respawnType   // echo back which type was used
  float   spawnX        // new position X
  float   spawnY        // new position Y
```

Used for type 1 and type 2 respawns (same-scene). Type 0 uses existing `SvZoneTransitionMsg`.

**After any respawn type:** Server also sends `SvPlayerStateMsg` to sync HP (now full), MP (now full), XP (reduced by death penalty), and gold.

### Server Handling

On receiving CmdRespawn:

1. **Validate** player `isDead == true` on server
2. **Rate limit:** Ignore if a CmdRespawn was processed for this player within the last 500ms
3. **Type 0 (Town):**
   - Check server-side `respawnTimeRemaining <= 0` (5s elapsed)
   - Town spawn position is stored in server config (not looked up from another scene's world data, since only the current scene is loaded). Config key: `town_spawn_scene`, `town_spawn_x`, `town_spawn_y`.
   - Send `SvZoneTransitionMsg` to town scene at configured spawn position
   - Call `stats.respawn()` after zone transition
4. **Type 1 (Map Spawn):**
   - Check server-side `respawnTimeRemaining <= 0` (5s elapsed)
   - Find SpawnPointComponent in current scene world (not isTownSpawn)
   - Call `stats.respawn()`, set player position to spawn point
   - Send `SvRespawnMsg` to client
5. **Type 2 (Here / Phoenix Down):**
   - No timer check (instant)
   - **Block in Gauntlet:** If player is in a Gauntlet instance, reject with system message "Cannot use Phoenix Down in Gauntlet."
   - Validate player has item `"phoenix_down"` in inventory (server-side). Phoenix Down is a stackable consumable; `findItemById` returns the first slot containing it.
   - Consume one Phoenix Down via `removeItemQuantity(slot, 1)`
   - Call `stats.respawn()` at current position
   - Send `SvRespawnMsg` + inventory update to client
6. **Reject** if conditions not met (send chat system message with reason)

### Server-Side Death Triggering

Currently `server_app.cpp` only processes player→mob combat. The server must also detect player death:

- **Mob→player damage:** When MobAISystem deals damage to a player and `stats.currentHP <= 0`, call `stats.die(DeathSource::PvE)` and send `SvDeathNotifyMsg`
- **PvP damage:** When another player deals lethal damage, call `stats.die(DeathSource::PvP)` and send `SvDeathNotifyMsg`
- **Environment:** Future (fall damage, hazard zones) — same pattern

The server's `GameplaySystem` ticks `respawnTimeRemaining` independently of the client. The client runs its own visual countdown but the server is authoritative on whether the timer has elapsed.

## Death Overlay UI: DeathOverlayUI

Small centered ImGui panel rendered when `isDead == true` on local player. NOT a full-screen takeover — game world remains visible behind it with the dead player sprite.

### Layout

```
+------------------------------------+
|          You have died.            |
|        Lost 1,234 XP              |  (shown if xpLost > 0)
|                                    |
|   Respawn available in 3...        |  (countdown, hidden at 0)
|                                    |
|   [ Respawn in Town         ]      |  (grayed 5s, then active)
|   [ Respawn at Spawn Point  ]      |  (grayed 5s, then active)
|   [ Respawn Here (Phoenix Down) ]  |  (instant, hidden if none)
|           Phoenix Down x2          |  (quantity display)
+------------------------------------+
```

### Behavior

- Panel appears immediately on death (triggered by `SvDeathNotifyMsg`)
- 5-second countdown displayed, ticks down each frame from `respawnTimer` in the death message
- Town and Map Spawn buttons are disabled (grayed) during countdown, enabled after
- Phoenix Down button is always enabled (instant respawn) but only visible if `countItem("phoenix_down") > 0`
- Phoenix Down button hidden during Gauntlet (server would reject anyway, but don't show it)
- Quantity shown below the Phoenix Down button
- Clicking a button sends `CmdRespawn` via callback (see below)
- Panel closes on respawn (isDead becomes false)
- Panel uses `GameViewport` for positioning (viewport-relative, not io.DisplaySize)
- Dark semi-transparent background: `ImVec4(0.05f, 0.02f, 0.02f, 0.85f)`

### Class

```cpp
// game/ui/death_overlay_ui.h
class DeathOverlayUI {
public:
    std::function<void(uint8_t respawnType)> onRespawnRequested;  // wired to NetClient::sendRespawn

    void onDeath(int32_t xpLost, int32_t honorLost, float respawnTimer);  // called from SvDeathNotifyMsg handler
    void render(Entity* player);  // called every frame, no-ops if alive

private:
    bool active_ = false;
    float countdown_ = 0.0f;
    int32_t xpLost_ = 0;
    int32_t honorLost_ = 0;
};
```

Member of GameApp, rendered in the game UI pass. Uses callback pattern (same as `ChatUI::onSendMessage`).

## Death Visual

When `SvDeathNotifyMsg` is received for the local player:

1. **Set isDead on client:** `stats.isDead = true` (mirror server state)
2. **Stop animation:** `animator->stop()`
3. **Rotate sprite 90 degrees:** `sprite->rotation = -90.0f` (laying on side, TWOM-style)
4. **Gray tint:** `sprite->tint = Color(0.3f, 0.3f, 0.3f, 0.6f)`
5. **Keep sprite enabled:** Unlike mobs (which hide on death), player sprite stays visible
6. **Nameplate stays visible** but dimmed (tint applied via nameplate color)

On respawn (`SvRespawnMsg` or `SvZoneTransitionMsg` received):

1. **Clear isDead:** `stats.isDead = false`
2. **Restore rotation:** `sprite->rotation = 0.0f`
3. **Restore tint:** `sprite->tint = Color::white()`
4. **Resume animation:** `animator->play("idle")`
5. **HP and MP restored to full** (done by `stats.respawn()`, modified to also restore MP)
6. **Position updated** from server message

### Remote Player Death (Follow-up)

Ghost (remote) player death visuals are a follow-up. Currently `SvEntityUpdateMsg` does not carry isDead state. For now, remote players who die will simply stop moving (server stops broadcasting position updates). A future update can add an `isDead` bit to the entity update fieldMask so ghosts also show the death visual.

## Systems Modified

### GameplaySystem

**Current behavior:** Auto-respawns player when `respawnTimeRemaining` hits 0.
**New behavior:** Do NOT auto-respawn. Only tick the countdown timer and apply death visual (gray tint, rotation). Respawn is player-initiated via DeathOverlayUI.

- On death detection: stop animator, rotate sprite 90deg, apply gray tint
- Tick `respawnTimeRemaining` down but do NOT call `respawn()` at 0
- On respawn (callback): restore animator, rotation, tint

### MovementSystem

- Skip movement input processing when `CharacterStatsComponent.stats.isDead == true` on local player
- Dead players cannot move

### CombatActionSystem

- Block attack input when local player `isDead` (early return in `processPlayerCombat`)
- Dead players are already non-targetable: mob targeting uses `EnemyStatsComponent.isAlive` check. For PvP targeting (future), add `isDead` check on `CharacterStatsComponent`.

### SkillBarUI

- Find local player's `CharacterStatsComponent` inside `draw()` and check `stats.isDead`
- Gray out all skill slots and block activation when dead

## Edge Cases

- **Death during zone transition:** MovementSystem blocks dead player movement, so they cannot walk into portals. If damage kills during the same frame as portal trigger, the zone transition takes priority (player arrives alive in new zone; death state is lost, which is acceptable — the portal "saved" them).
- **Simultaneous CmdRespawn:** Rate limit (500ms) prevents double-processing. First valid respawn wins, subsequent ones rejected because `isDead` is already false.
- **Phoenix Down at qty=1:** `removeItemQuantity(slot, 1)` sets slot to empty when quantity reaches 0. `onInventoryChanged` callback fires. No special handling needed.
- **Inventory full + respawn:** Respawn doesn't add items, so inventory state is irrelevant. Phoenix Down is consumed (reducing quantity), not added.

## Security

- Client sends only respawn TYPE (0, 1, 2) — no coordinates
- Server validates isDead state, timer elapsed, and Phoenix Down ownership
- Server determines spawn position from its own authoritative scene/config data
- Hacked clients cannot teleport via respawn — server ignores client position data
- Phoenix Down consumption is server-side atomic (check + remove in same handler)
- CmdRespawn rate-limited to 500ms per player to prevent spam
- Gauntlet blocks Phoenix Down respawn server-side

## Testing

- Unit test: `SvDeathNotifyMsg` serialization round-trip
- Unit test: `CmdRespawn` serialization round-trip
- Unit test: `SvRespawnMsg` serialization round-trip
- Unit test: SpawnPointComponent serialization
- Unit test: Phoenix Down consumption (has item → consumed, no item → rejected)
- Unit test: `respawn()` restores both HP and MP to max
- Unit test: CmdRespawn rejected when isDead=false
- Unit test: CmdRespawn type 0/1 rejected when timer > 0
- Unit test: CmdRespawn type 2 rejected in Gauntlet
- Integration: Death → overlay appears → countdown → respawn at correct location
