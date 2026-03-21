# Batch C: PvP Events & Rankings — Design Spec

**Date:** 2026-03-21
**Scope:** Event scheduler FSM, MVP battlefield, arena system (1v1/2v2/3v3), honor ranking with badges.

---

## 1. Event Scheduler FSM

### Problem
No timer infrastructure for recurring world events. Battlefield needs a 2-hour cycle. Future events (Ancient Dungeon, Sky Castle rotation) will also need scheduling. Need a generic reusable system.

### Design

**EventScheduler** manages multiple registered events. Ticks in the server's main loop via `scheduler_.tick(deltaTime)`.

**Per-event configuration:**
- `eventId` (string) — unique identifier
- `intervalSeconds` (float) — time between events
- `signupDuration` (float) — signup window before active phase
- `activeDuration` (float) — how long the event runs

**State machine per event:**
```
Idle → Signup → Active → Idle (cycle repeats)
```

- `Idle`: timer counting down to next signup window
- `Signup`: registration open, timer counting down to active start
- `Active`: event running, timer counting down to end

**Callbacks per event:**
- `onSignupStart()` — announce event, open registration
- `onEventStart()` — teleport players, begin combat
- `onEventEnd()` — determine winners, distribute rewards, teleport back
- `onTick(float dt)` — per-frame update during active phase

**Server integration:**
- `EventScheduler scheduler_` member on `ServerApp`
- `scheduler_.registerEvent(config, callbacks)` during startup
- `scheduler_.tick(dt)` called every server tick (50ms)
- Events can query state: `getState(eventId)`, `getTimeRemaining(eventId)`

**Interaction with other systems:**
- Arena and battlefield check a central `PlayerEventLock` map (`std::unordered_map<uint32_t, std::string>` mapping playerId → eventType) on ServerApp to prevent double-registration across all events
- Gauntlet keeps its own internal timing for now (tech debt — could migrate to EventScheduler in a future session)

### Files
- Create: `game/shared/event_scheduler.h` — state machine, event config, registration
- Modify: `server/server_app.h/cpp` — add scheduler member, tick in main loop

---

## 2. MVP Battlefield

### Problem
No faction PvP event. Need a recurring event where Siras and Lanos fight with tracked kills and rewards for the winning faction.

### Design

**Registered with event scheduler:** 2-hour interval, 5-minute signup, 15-minute active.

**Signup phase (5 min):**
- System chat broadcast: "Battlefield opens in 5 minutes! Use /bf to join"
- Players sign up via `CmdBattlefield` (action: Register)
- Cannot sign up while in arena queue or gauntlet
- Server stores player's original scene + position for return teleport
- Players can unregister during signup

**Active phase (15 min):**
- All registered players teleported to "battlefield" scene
- No PK status changes during battlefield — all PvP is free, no penalties
- Kill tracking: per-faction kill counters (`std::unordered_map<Faction, int>`) — supports all 4 factions (Xyros, Fenor, Zethos, Solis)
- Player stats tracked: personal kills, deaths
- On death: respawn in battlefield after 5 seconds (no XP loss, no honor loss). Respawn timer communicated via existing `SvDeathNotifyMsg.respawnTimer` with `DeathSource::Battlefield`.
- Players who disconnect during battlefield are removed from the event. Their kills still count for their faction (already recorded).
- `onClientDisconnected` in `server_app.cpp` must call `battlefieldManager_.removePlayer(clientId)`.
- Minimum player count: battlefield requires at least 2 players from different factions to start. If not met at event start, event is cancelled and signup refunded.

**End phase:**
- Faction with most kills wins. If multiple factions tied for most, all tied factions get winner rewards.
- Winners: 3× Pendant of Honor + 20 honor
- Losers: 1× Pendant of Honor + 5 honor
- All players teleported back to original scene + position
- System chat: "[Faction] wins the Battlefield! (Xyros: X, Fenor: Y, Zethos: Z, Solis: W)" — only shows factions that participated.

**BattlefieldManager** — server component that handles all battlefield logic:
- `registerPlayer(clientId, faction)` / `unregisterPlayer(clientId)`
- `onPlayerKill(killerId, victimId)` — increment killer's faction kill count
- `removePlayer(clientId)` — cleanup on disconnect
- `getState()` — current phase info for UI
- `hasMinimumPlayers()` — at least 2 players from 2+ different factions

**Network messages:**
- `CmdBattlefield` (client → server) — packet `0x22`:
  - `uint8_t action` — 0=Register, 1=Unregister
- `SvBattlefieldUpdate` (server → client) — packet `0xAC`:
  - `uint8_t state` — 0=Idle, 1=Signup, 2=Active, 3=Ended
  - `uint16_t timeRemaining` — seconds left in current phase (not sent during idle)
  - `uint8_t factionCount` — number of participating factions (1-4)
  - `uint8_t[] factionIds` — faction enum values (factionCount entries)
  - `uint16_t[] factionKills` — kill counts per faction (factionCount entries)
  - `uint16_t personalKills`
  - `uint8_t result` — 0=ongoing, 1=win, 2=loss, 3=tie

### Files
- Create: `game/shared/battlefield_manager.h` — player tracking, kill counting, reward distribution
- Modify: `game/shared/game_types.h` — Add `DeathSource::Battlefield = 4` and `DeathSource::Arena = 5` to enum
- Modify: `engine/net/protocol.h` or `game_messages.h` — CmdBattlefield, SvBattlefieldUpdate
- Modify: `engine/net/packet.h` — packet types (`CmdBattlefield = 0x22`, `SvBattlefieldUpdate = 0xAC`)
- Modify: `server/server_app.cpp` — register battlefield event, handle CmdBattlefield, hook kill tracking, cleanup in `onClientDisconnected`
- Modify: `engine/net/net_client.h/cpp` — client callback

---

## 3. Arena System

### Problem
No competitive PvP. Need structured 1v1, 2v2, 3v3 matches with matchmaking, rewards, and anti-abuse measures.

### Design

**Modes:**
- Solo (1v1): any player, no party required
- Duo (2v2): must be in a party of exactly 2
- Team (3v3): must be in a party of exactly 3

**Signup flow:**
- Player sends `CmdArena` (action: Register, mode: 1/2/3)
- Validation:
  - Correct party size for mode (solo=1, duo=2, team=3)
  - Not already registered for arena
  - Not currently in an active event (gauntlet, battlefield, another arena match)
  - Not dead
- While signed up:
  - If any party member leaves, is kicked, or party disbands → **entire group auto-unregistered**, all members notified
  - Cannot join gauntlet or battlefield (event lock)
  - Must unregister (`CmdArena` action: Unregister) to clear event lock
- Unregister: removes group from queue, clears event lock on all members

**Matchmaking:**
- `ArenaManager` checks queue every 20 ticks (1 second), not every tick, to avoid unnecessary iteration
- Match criteria for each mode:
  - Two groups from **different factions** (prevents same-faction win-trading)
  - All participants within 5 levels of each other (compare highest vs lowest across both groups)
- When valid match found → match starts immediately
- Queue timeout: 5 minutes. Auto-unregister with notification if no match found.

**Match flow:**
1. Store original scene + position for all players
2. Teleport all players to "arena" scene
3. 3-second countdown (no combat allowed)
4. 3-minute combat timer starts
5. No PK status changes during arena — all PvP is free
6. **Win condition:** All opponents dead → instant win, match ends immediately
7. **Death:** Dead players stay dead for the rest of the match. No respawn.
8. **Tie condition:** Both sides have living players at 3:00 → tie
9. Rewards distributed, all players teleported back

**Rewards:**
- Win: 30 honor
- Loss: 5 honor
- Tie: 5 honor (minimal payout)
- Must have dealt at least 1 damage to any opponent to qualify for rewards (0 damage = 0 rewards even on winning team)

**Anti-AFK measures:**
- Track `damageDealt` per player during match
- Track `lastActionTime` (move, attack, skill use) per player
- **Only check living players** — dead players are exempt (they died legitimately)
- If a **living** player has no actions for 30 consecutive seconds → auto-forfeit:
  - Player removed from match (counted as dead for their team)
  - Gets 0 rewards regardless of match outcome
  - Team continues playing (it becomes a disadvantage)
- 0 damage dealt = 0 rewards (catches players who move but never fight)

**ArenaManager** — server component:
- `registerGroup(clientIds[], faction, mode)` / `unregisterGroup(clientIds[])`
- `tryMatchmaking()` — called each tick, finds valid matches
- `onPlayerKill(matchId, killerId, victimId)` — track kills, check win condition
- `onPlayerAction(matchId, playerId)` — update lastActionTime
- `tickMatches(float dt)` — check timer expiry, AFK checks on living players
- `isPlayerInArena(playerId)` — for event lock checks

**Arena disconnect:** If a player disconnects during a match, they are treated as dead (removed from living count). Their team plays on at a disadvantage. Disconnected player gets 0 rewards.

**Simultaneous kill:** If both last players die on the same tick, it's a tie.

**Party callback:** Use existing `PartyManager::onPartyChanged` callback. ArenaManager listens and checks if any registered group's members have changed — if so, auto-unregister the group.

**Network messages:**
- `CmdArena` (client → server) — packet `0x23`:
  - `uint8_t action` — 0=Register, 1=Unregister
  - `uint8_t mode` — 1=Solo, 2=Duo, 3=Team
- `SvArenaUpdate` (server → client) — packet `0xAD`:
  - `uint8_t state` — 0=Queued, 1=Countdown, 2=Active, 3=Ended
  - `uint16_t timeRemaining` — seconds left
  - `uint8_t teamAlive` — count of living teammates
  - `uint8_t enemyAlive` — count of living opponents
  - `uint8_t result` — 0=ongoing, 1=win, 2=loss, 3=tie
  - `int32_t honorReward` — honor earned (0 if AFK/no damage)

### Files
- Create: `game/shared/arena_manager.h` — queue, matchmaking, match state, AFK tracking
- Modify: `game/shared/game_types.h` — Add `DeathSource::Arena = 5`
- Modify: `engine/net/protocol.h` or `game_messages.h` — CmdArena, SvArenaUpdate
- Modify: `engine/net/packet.h` — packet types (`CmdArena = 0x23`, `SvArenaUpdate = 0xAD`)
- Modify: `server/server_app.cpp` — ArenaManager integration, handle CmdArena, hook combat/movement for action tracking, arena disconnect cleanup in `onClientDisconnected`
- Modify: `engine/net/net_client.h/cpp` — client callback
- Note: Uses existing `PartyManager::onPartyChanged` — no modification to party_manager.h needed

---

## 4. Honor Ranking with Badges

### Problem
Honor field exists on `CharacterStats` but there's no ranking system or visual badge display. Players have no way to see their PvP standing relative to others.

### Design

**Honor ranks (derived from honor value, not a separate stat):**

| Rank | Enum Value | Honor Required |
|---|---|---|
| Recruit | 0 | 0 |
| Scout | 1 | 100 |
| Combat Soldier | 2 | 500 |
| Veteran Soldier | 3 | 2,000 |
| Apprentice Knight | 4 | 5,000 |
| Fighter | 5 | 10,000 |
| Elite Fighter | 6 | 25,000 |
| Field Commander | 7 | 50,000 |
| Commander | 8 | 75,000 |
| General | 9 | 99,999+ |

General is the highest rank — all honor values 99,999 and above display as General.

**Implementation:**
- `HonorRank` enum (uint8_t, 0-9)
- `HonorRank getHonorRank(int honor)` — pure lookup function, no state, in `game/shared/honor_system.h` (already exists)
- `const char* getHonorRankName(HonorRank rank)` — returns display string for UI/nameplate
- No separate stat — rank is computed from honor whenever needed

**Replication to clients:**
- Add `uint8_t honorRank` to `SvPlayerStateMsg` — local player sees their own rank
- Add `uint8_t honorRank` to `SvEntityEnterMsg` (entityType == 0 block, same as pkStatus) — remote players see each other's rank badges
- Add `honorRank` as bit 15 in `SvEntityUpdateMsg` — delta-synced when honor changes
- Note: bit 15 is the last available bit in the `uint16_t fieldMask`. If more fields are needed after this, expand to `uint32_t`.

**Server wiring:**
- `buildEnterMessage()`: populate `honorRank` from `getHonorRank(charStats->stats.honor)`
- `buildCurrentState()`: same
- `sendDiffs()`: delta check on `honorRank` (bit 15)

**Client display:**
- Badge icon rendered next to nameplate (same position for local and remote players)
- `getHonorRank()` shared between client and server

### Files
- Modify: `game/shared/honor_system.h` — Add `HonorRank` enum and `getHonorRank()` function
- Modify: `engine/net/protocol.h` — Add `honorRank` to `SvPlayerStateMsg`, `SvEntityEnterMsg`, bit 15 to `SvEntityUpdateMsg`
- Modify: `engine/net/replication.cpp` — Populate honorRank in enter/update/diff
- Modify: `server/server_app.cpp` — Populate honorRank in `sendPlayerState()`
- Client: apply badge to nameplate based on honorRank

---

## Testing Plan

| Test | Validates |
|---|---|
| EventScheduler transitions Idle → Signup → Active → Idle | State machine |
| EventScheduler fires callbacks at correct transitions | Callback timing |
| EventScheduler respects interval/signup/active durations | Timer accuracy |
| Multiple events can run on different schedules | Multi-event |
| Battlefield registration accepted during signup phase | Signup validation |
| Battlefield registration rejected during active/idle phase | Phase guard |
| Cannot register for battlefield while in arena queue | Event lock |
| Battlefield kill increments correct faction counter | Kill tracking |
| Battlefield winner determination (more kills) | Win logic |
| Battlefield tie gives both sides loser rewards | Tie handling |
| Battlefield players teleported back after end | Return teleport |
| Arena solo registration: no party needed | 1v1 mode |
| Arena duo registration: requires party of 2 | 2v2 mode |
| Arena team registration: requires party of 3 | 3v3 mode |
| Arena wrong party size rejected | Party validation |
| Arena cross-faction matching only | Faction check |
| Arena level range: within 5 levels | Level validation |
| Arena party member leaves → group auto-unregistered | Auto-unregister |
| Arena cannot join while in gauntlet | Event lock |
| Arena 3-minute timer → tie if both alive | Timer + tie |
| Arena all opponents dead → instant win | Win condition |
| Arena dead players stay dead (no respawn) | Death persistence |
| Arena AFK: 30s no action (living) → auto-forfeit | AFK detection |
| Arena AFK: dead player NOT flagged AFK | Dead exempt |
| Arena 0 damage dealt = 0 rewards | Damage gate |
| Arena queue timeout after 5 minutes | Queue expiry |
| HonorRank correctly maps honor thresholds | Rank lookup |
| Honor 0 = Recruit, 99999 = General | Boundary values |
| honorRank in SvEntityEnterMsg for remote players | Replication |
| honorRank bit 15 delta update | Delta sync |
| Battlefield cancelled if < 2 factions at start | Minimum players |
| Battlefield disconnect: kills still count, player removed | Disconnect handling |
| Arena disconnect: player treated as dead, 0 rewards | Arena disconnect |
| Arena simultaneous last kills = tie | Simultaneous death |
| PlayerEventLock prevents joining arena while in battlefield | Cross-event lock |
| DeathSource::Battlefield = no XP/honor loss | Death penalty bypass |
| DeathSource::Arena = permanent death in match | Arena death |
| CmdBattlefield/SvBattlefieldUpdate round-trip | Protocol |
| CmdArena/SvArenaUpdate round-trip | Protocol |
