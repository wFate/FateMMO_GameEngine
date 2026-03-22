# Persistence Infrastructure Wiring — Design Spec

## Goal

Wire the existing `PersistenceQueue` and `PlayerDirtyFlags` infrastructure into ServerApp's handler methods and save pipeline so that (a) critical economic data reaches the DB within one tick, and (b) unchanged data sections are skipped during saves.

## Approach: Hybrid

The existing `tickAutoSave()` 5-minute staggered timer is kept as a safety-net catch-all. The `PersistenceQueue` adds an urgent fast-path for IMMEDIATE/HIGH priority data. `PlayerDirtyFlags` gates each section of `savePlayerToDB()` so unchanged data is skipped.

**Write-amplification tradeoff:** The 5-minute auto-save writes ALL sections unconditionally, regardless of whether the PersistenceQueue already saved them seconds ago. This is acceptable — the auto-save is a safety net, not an optimization target.

## System 1: PlayerDirtyFlags Wiring

### Dirty flag → savePlayerToDB section mapping

`savePlayerToDB()` does NOT call `saveInventoryForClient()` — it only saves `rec.gold` (from InventoryComponent). Full inventory is saved separately by handlers that call `saveInventoryForClient()` directly.

| Flag | Set by | Guards in savePlayerToDB |
|------|--------|--------------------------|
| `position` | CmdMove accepted (both first-move and validated paths) | `rec.position_x/y` and `rec.current_scene` assignment |
| `vitals` | HP/MP changes (combat, regen, consumable, equip clamp, DoT ticks, death/respawn) | `rec.current_hp/mp/fury` and `rec.is_dead` fields |
| `stats` | `addXP()`, level-up, honor gain/loss, PK status, pvpKills/pvpDeaths | `rec.level/xp/honor/pvp_kills/pvp_deaths/pk_status/faction` fields |
| `inventory` | `addItem/removeItem/setGold/addGold/removeGold`, equip, enchant, repair, craft, socket, core extraction, market buy/sell, trade execute, loot pickup, bounty refund, guild creation gold, pet auto-loot pickup, quest turn-in rewards, Phoenix Down | `rec.gold` field in savePlayerToDB, AND gates whether `saveInventoryForClient()` should be called by the PersistenceQueue flush |
| `skills` | `learnSkill()` during load (no runtime skill trainer handler exists yet) | Skill save block |
| `quests` | `acceptQuest()`, `completeQuest()`, quest progress update, quest turn-in | Quest save block |
| `bank` | Bank deposit/withdraw (gold or items) | Bank save block |
| `pet` | Pet equip/unequip, pet XP gain, auto-loot toggle | Pet save block |
| `social` | Not saved in savePlayerToDB — no gate needed | N/A |
| `guild` | Not saved in savePlayerToDB — no gate needed | N/A |

### Behavior on disconnect and auto-save

Both `onClientDisconnected()` and `tickAutoSave()` save unconditionally. Add a `bool forceSaveAll` parameter to `savePlayerToDB()` (default `false`). Disconnect and auto-save pass `true`, which skips all dirty-flag checks. Normal priority-queue-triggered saves pass `false`, which checks flags and skips clean sections.

### Flag lifecycle

1. Handler mutates player state → sets `playerDirty_[clientId].inventory = true`
2. `savePlayerToDB(clientId, false)` checks flag → if false, skips that section
3. After successful save, `playerDirty_[clientId].clearAll()`

### Mutation sites (where to set flags)

**position:**
- CmdMove first-move unconditional accept path
- CmdMove validated-move path (after rubber-band check)
- Zone transition (currentScene changes)

**vitals:**
- Combat damage (processAction auto-attack HP reduction)
- Skill damage (processUseSkill HP/MP changes)
- HP/MP regen tick
- `processUseConsumable` (HP/MP potions) — also sets `inventory`
- Death state transition (isDead = true)
- Respawn state (isDead = false, HP/MP restored)
- DoT tick damage (onDoTTick callback)
- Equip/unequip HP/MP clamp after recalcEquipmentBonuses

**stats:**
- `addXP()` calls (mob kill XP, quest turn-in XP)
- Level-up (inside addXP when level increases)
- Honor gain/loss (arena results, battlefield results, PK honor)
- PK status transitions
- pvpKills++ / pvpDeaths++ on PvP kills
- Quest turn-in XP (via QuestManager::turnInQuest calling stats.addXP)

**inventory:**
- Gold pickup (`addGold`/`setGold` in processAction loot)
- `setGold` calls (enchant gold cost, repair gold cost)
- Item add/remove (market buy, market list/remove, trade execute, craft result, loot pickup)
- Equip/unequip (processEquip)
- Enchant result (processEnchant — item replaced in slot)
- Repair result (processRepair)
- Core extraction (processExtractCore — item consumed, cores added)
- Craft result (processCraft — ingredients consumed, result added)
- Socket item (processSocketItem)
- Stat enchant (processStatEnchant — equipment modified)
- Consumable use (processUseConsumable — item removed)
- Bounty cancel gold refund
- Guild creation gold deduction
- Pet auto-loot gold/item pickup (tickPetAutoLoot)
- Quest turn-in gold reward (via QuestManager::turnInQuest calling inventory.addGold)
- Phoenix Down removal on respawn

**skills:**
- `learnSkill()` only during load currently — no runtime mutation site yet (future: skill trainer handler)

**quests:**
- `acceptQuest()`
- `completeQuest()` / quest turn-in (note: turn-in also sets stats + inventory flags)
- Quest objective progress updates

**bank:**
- `processBank()` — deposit/withdraw handlers

**pet:**
- `processPetCommand()` — equip/unequip/rename/toggle auto-loot
- Pet XP gain (mob kill XP sharing)

## System 2: PersistenceQueue Wiring

### New method: tickPersistQueue()

Called every tick from `tick()`. Dequeues up to 10 requests per tick. For each dequeued request, calls `savePlayerToDBAsync()`.

```
void ServerApp::tickPersistQueue() {
    auto batch = persistQueue_.dequeue(10, gameTime_);
    for (auto& req : batch) {
        savePlayerToDBAsync(req.clientId);
    }
}
```

### Deduplication

`std::unordered_map<uint64_t, float>` called `pendingPersist_` keyed on `(clientId << 8) | PersistType`. Before enqueuing, check if a request for this client+type was enqueued within a 1-second window. Skip if duplicate.

```
void ServerApp::enqueuePersist(uint16_t clientId, PersistPriority priority, PersistType type) {
    uint64_t key = (static_cast<uint64_t>(clientId) << 8) | static_cast<uint64_t>(type);
    auto it = pendingPersist_.find(key);
    if (it != pendingPersist_.end()) {
        float elapsed = gameTime_ - it->second;
        if (elapsed < 1.0f) return;
    }
    pendingPersist_[key] = gameTime_;
    persistQueue_.enqueue(clientId, priority, type, gameTime_);
}
```

Clean stale `pendingPersist_` entries (>60s) in `tickMaintenance()`.

### Enqueue sites

| Priority | Site | PersistType |
|----------|------|-------------|
| IMMEDIATE | Trade execute (both players) | Inventory |
| IMMEDIATE | Market buy (buyer) | Inventory |
| IMMEDIATE | Market sell/list (seller — item removed) | Inventory |
| IMMEDIATE | Gold pickup (loot) | Inventory |
| IMMEDIATE | Enchant (gold + item mutation) | Inventory |
| IMMEDIATE | Repair (gold + item mutation) | Inventory |
| IMMEDIATE | Core extraction (item consumed) | Inventory |
| IMMEDIATE | Craft (ingredients consumed, result added) | Inventory |
| IMMEDIATE | Socket item | Inventory |
| IMMEDIATE | Bounty cancel gold refund | Inventory |
| IMMEDIATE | Guild creation gold deduction | Inventory |
| HIGH | Level-up (inside addXP) | Character |
| HIGH | Quest complete / turn-in | Quests |
| NORMAL | CmdMove (throttled: only if no pending within 30s) | Position |
| LOW | Pet state change | Pet |
| LOW | Bank deposit/withdraw | Bank |

**Note on handlers that already call saveInventoryForClient() directly:** Trade execute, socket, stat enchant, and market list already save inventory inline. The PersistenceQueue enqueue for these is additive — the direct save handles the inventory, while the queue ensures the full character record (gold, stats) is also persisted promptly. No existing direct saves are removed.

### tick() integration

```
tickAutoSave(dt);
tickPersistQueue();
```

### Cleanup on disconnect

In `onClientDisconnected()`, iterate `pendingPersist_` and erase all entries where the clientId portion of the key matches (up to 9 PersistType values per client). Also erase `playerDirty_[clientId]`.

## Dirty flags in savePlayerToDBAsync

`savePlayerToDBAsync()` snapshots player data on the game thread, then dispatches to fiber. The dirty-flag check happens during the snapshot phase (game thread), so no threading concerns. The `forceSaveAll` parameter propagates through: `savePlayerToDBAsync` calls `savePlayerToDB` internally.

## Files Modified

| File | Changes |
|------|---------|
| `server/server_app.h` | Add `tickPersistQueue()`, `enqueuePersist()` declarations. Add `pendingPersist_` map. Add `forceSaveAll` param to `savePlayerToDB` |
| `server/server_app.cpp` | (1) Dirty flag sets at ~25 mutation sites. (2) Dirty flag checks in `savePlayerToDB()` gated by `forceSaveAll`. (3) `enqueuePersist()` calls at ~16 handler sites. (4) `tickPersistQueue()` implementation. (5) Wire into `tick()`. (6) Cleanup in `onClientDisconnected()` and `tickMaintenance()`. (7) `forceSaveAll=true` in disconnect and auto-save paths |
| `tests/test_persistence_priority.cpp` | Add tests for deduplication logic |

## Testing

- Existing 6 PersistenceQueue tests continue to pass
- New tests for deduplication (enqueue same client+type twice within 1s window → only one in queue)
- Full test suite regression (824 tests)

## What is NOT changed

- `tickAutoSave()` — stays as-is, 5-minute staggered catch-all (passes `forceSaveAll=true`)
- `savePlayerToDBAsync()` — stays as-is (propagates forceSaveAll)
- `saveInventoryForClient()` — stays as-is (called by handlers directly, not gated by dirty flags)
- WAL append sites — stay as-is (orthogonal crash recovery)
- Existing direct `saveInventoryForClient()` calls in handlers — stay as-is
