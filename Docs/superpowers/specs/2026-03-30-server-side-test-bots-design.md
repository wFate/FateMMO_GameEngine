# Server-Side Test Bots

**Date:** 2026-03-30
**Status:** Approved

## Overview

Interactive server-side bot system for testing multiplayer features (party, trade, guild, friends, PvP, buffs). Bots are real DB-backed player entities spawned via GM commands. They persist across server restarts, are fully editable in the inspector, and auto-accept interactions based on faction-aware rules.

## Approach

**Server-side entities (no network connection).** Bots are created entirely within the server process using the same entity creation path as real players. A virtual connection adapter routes handler calls that would normally go to a socket into the bot brain instead. This avoids crypto handshakes, UDP sockets, and heartbeat overhead while exercising the exact same game systems real players use.

## Architecture

### New Components

**`BotComponent`** — marker + config, attached to bot entities:
- `accountId` (int) — DB account row
- `characterId` (string) — DB character row
- `autoAcceptParty` (bool, default true)
- `autoAcceptTrade` (bool, default true)
- `autoAcceptGuild` (bool, default true)
- `autoAcceptFriend` (bool, default true)
- `autoAcceptDuel` (bool, default true)

Registered in the component registry. All fields editable in the inspector.

### New Systems

**`BotBrainSystem`** — server-only system ticking entities with `BotComponent`. Processes events routed from the virtual connection adapter:

- **Party invite** → auto-accept if `autoAcceptParty` and faction rules allow
- **Trade request** → auto-accept if `autoAcceptTrade`. After other player locks, bot auto-locks then auto-confirms (~500ms delay)
- **Guild invite** → auto-accept if `autoAcceptGuild` and same faction
- **Friend request** → auto-accept if `autoAcceptFriend`
- **Duel challenge** → auto-accept if `autoAcceptDuel` (bot stands still, takes hits)
- **Buffs/heals** → work naturally through existing `DamageableComponent`/`StatusEffectComponent`

No movement AI initially. Bots stand where spawned.

### Virtual Connection Adapter

Many server handlers route actions via `clientId`. Bots have no real network connection, so we create a `BotConnectionAdapter`:

- Each bot gets a virtual `clientId` in a reserved range (50000+)
- `ConnectionManager` maps virtual IDs to bot entity handles
- A stub `ClientConnection` is created with:
  - `playerEntityId` pointing to the bot entity
  - `sendReliable()`/`sendUnreliable()` that push packets into `BotBrainSystem`'s event queue instead of a socket
  - Trade state fields (`activeTradeSessionId`, `tradePartnerCharId`, `tradeNonce_`)
- `connections().mapEntity(pid, virtualClientId)` works identically to real players
- Existing handlers (trade, party, guild, friends, duel) work unchanged

## GM Commands

### `/spawnbot <class> <faction> <level> [name]`

- `class`: Warrior, Magician, Archer
- `faction`: Xyros, Fenor, Zethos, Solis (or 0-3)
- `level`: 1-99
- `name`: optional, auto-generated if omitted (e.g. "Bot_Warrior_01")
- `minRole`: GM

**Spawn flow:**

1. Create DB account: `INSERT INTO accounts` with username `bot_<name>`, placeholder password hash, placeholder email
2. Create DB character: `INSERT INTO characters` with specified class, faction, level. Override base stats for level.
3. Grant starter equipment via `grantStarterEquipment()`
4. Load `CharacterRecord` back via `characterRepo_->loadCharacter()`
5. Run same entity creation path as `onClientConnected` — all 27 components, equipment bonuses, HP/MP clamping
6. Attach `BotComponent`
7. Allocate virtual `clientId`, create stub `ClientConnection`, register in `ConnectionManager`
8. Register with `ReplicationManager` (visible to all nearby clients)
9. Position at spawning player's location + small offset
10. System message: `"Bot '<name>' (<class>, <faction>, Lv<level>) spawned"`

### `/despawnbot <name>`

1. Find bot entity by name
2. Unregister from `ReplicationManager` (entity-leave to nearby clients)
3. Remove virtual connection from `ConnectionManager`
4. Destroy entity
5. `DELETE FROM accounts WHERE username = 'bot_<name>'` (cascade deletes character, inventory, skills, etc.)

### `/despawnbots`

Remove all bots in the caller's current scene. Same flow as `/despawnbot` for each.

### `/botlist`

Lists all active bots: name, class, faction, level, scene.

## Bot Entity Components

Bots get the same 27 components as real players, plus `BotComponent`:

| # | Component | Notes |
|---|-----------|-------|
| 1 | Transform | Spawned at caller's position |
| 2 | SpriteComponent | Class-appropriate sprite |
| 3 | BoxCollider | Standard player collider |
| 4 | PlayerController | `isLocal = false` |
| 5 | CharacterStatsComponent | Level/stats set per spawn args |
| 6 | CombatControllerComponent | Standard combat params |
| 7 | DamageableComponent | Can receive damage (PvP/duel testing) |
| 8 | InventoryComponent | Starter gear from DB |
| 9 | SkillManagerComponent | Empty initially |
| 10 | StatusEffectComponent | Can receive buffs/debuffs |
| 11 | CrowdControlComponent | Can be stunned/rooted |
| 12 | TargetingComponent | No auto-targeting |
| 13 | ChatComponent | Unused but present for consistency |
| 14 | GuildComponent | Joinable by same-faction players |
| 15 | PartyComponent | Joinable by same-faction players |
| 16 | FriendsComponent | Accepts friend requests |
| 17 | MarketComponent | Present for consistency |
| 18 | TradeComponent | Full trade support |
| 19 | QuestComponent | Present for consistency |
| 20 | BankStorageComponent | Present for consistency |
| 21 | FactionComponent | Set per spawn args |
| 22 | EquipVisualsComponent | Reflects equipped gear |
| 23 | AppearanceComponent | Default gender/hairstyle |
| 24 | PetComponent | Empty initially |
| 25 | CostumeComponent | Empty initially |
| 26 | NameplateComponent | Shows bot name + level |
| 27 | CollectionComponent | Empty initially |
| 28 | **BotComponent** | Bot config + auto-accept flags |

## Server Startup: Auto-Spawn

1. Query `SELECT account_id FROM accounts WHERE username LIKE 'bot_%'`
2. For each bot account, load character(s) via existing repositories
3. Run entity creation (steps 4-8 of spawn flow) for each
4. Log: `"Auto-spawned N bot(s)"`

Bots persist across restarts with full state (inventory, guild membership, friends, etc.).

## DB Identification

Bot accounts use a `bot_` username prefix convention. No schema changes to the `accounts` table. Easy to query, easy to distinguish from real players.

## Faction Rules

Bots respect the same faction rules as real players:
- Same-faction bots can party, guild, trade, friend, buff each other and the player
- Cross-faction bots trigger PvP rules when attacked
- The `FactionComponent` is real — all existing faction checks apply naturally

## Future Enhancements (Not in Scope)

- Movement AI (follow player, patrol, roam)
- Combat AI (use skills, auto-attack targets)
- Level-appropriate gear generation beyond starter equipment
- Stress testing via bulk bot spawn
