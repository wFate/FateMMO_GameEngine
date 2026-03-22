# Persistence Infrastructure Wiring — Design Spec

## Goal

Wire the existing `PersistenceQueue` and `PlayerDirtyFlags` infrastructure into ServerApp's handler methods and save pipeline so that (a) critical economic data reaches the DB within one tick, and (b) unchanged data sections are skipped during saves.

## Approach: Hybrid

The existing `tickAutoSave()` 5-minute staggered timer is kept as a safety-net catch-all. The `PersistenceQueue` adds an urgent fast-path for IMMEDIATE/HIGH priority data. `PlayerDirtyFlags` gates each section of `savePlayerToDB()` so unchanged data is skipped.

## System 1: PlayerDirtyFlags Wiring

### Dirty flag → savePlayerToDB section mapping

| Flag | Set by | Guards in savePlayerToDB |
|------|--------|--------------------------|
| `position` | CmdMove accepted (after validation) | `rec.position_x/y` assignment block |
| `vitals` | HP/MP change (combat damage, regen tick, consumable use) | `rec.current_hp/mp/fury` fields |
| `stats` | `addXP()`, level-up, honor gain/loss, PK status change | `rec.level/xp/honor/pvp_kills/pvp_deaths/pk_status` fields |
| `inventory` | `addItem/removeItem/setGold/addGold/removeGold`, equip, enchant, repair, craft, socket, core extraction, market buy/sell, trade execute, loot pickup | `saveInventoryForClient()` call |
| `skills` | `learnSkill()`, skill bar rearrange, skill point spend | Skill save block (lines 1503-1528) |
| `quests` | `acceptQuest()`, `completeQuest()`, quest progress update | Quest save block (lines 1532-1543) |
| `bank` | Bank deposit/withdraw (gold or items) | Bank save block (lines 1546-1553) |
| `pet` | Pet equip/unequip, pet XP gain, auto-loot toggle | Pet save block (lines 1556-1569) |
| `social` | Not saved in savePlayerToDB — no gate needed | N/A |
| `guild` | Not saved in savePlayerToDB — no gate needed | N/A |

### Behavior on disconnect

`onClientDisconnected()` calls `savePlayerToDB()`. On disconnect, ALL sections must save regardless of dirty flags. The simplest approach: set all flags dirty before the save call in `onClientDisconnected()`, or pass a `forceSaveAll` parameter.

### Behavior on tickAutoSave

The 5-minute auto-save also saves unconditionally — it should mark all flags dirty before saving, or save without checking flags. This ensures the catch-all timer doesn't skip data that was dirtied but never explicitly enqueued.

### Flag lifecycle

1. Handler mutates player state → sets `playerDirty_[clientId].inventory = true`
2. `savePlayerToDB()` checks `playerDirty_[clientId].inventory` → if false, skips `saveInventoryForClient()`
3. After successful save, `playerDirty_[clientId].clearAll()`

### Mutation sites (where to set flags)

**position:**
- `CmdMove` handler (line ~1861) — after position accepted

**vitals:**
- Combat damage (in `processAction` auto-attack and `processUseSkill` — wherever `currentHP` is modified)
- HP/MP regen tick
- `processUseConsumable` (HP/MP potions)
- Death/respawn state changes

**stats:**
- `addXP()` calls (lines 3227, 3534 — mob kill XP)
- Level-up (inside addXP when level increases)
- Honor gain/loss (arena/battlefield/PK)
- PK status transitions

**inventory:**
- Gold pickup (`addGold` in processAction loot, line 3859)
- `setGold` calls (enchant gold cost line 4695, repair gold cost line 4791)
- Item add/remove (market buy line 2145, trade execute, craft result, loot pickup line 3880)
- Equip/unequip (processEquip)
- Enchant result (processEnchant — item replaced in slot)
- Repair result (processRepair)
- Core extraction (processExtractCore)
- Craft result (processCraft)
- Socket item (processSocketItem)
- Stat enchant (processStatEnchant)
- Consumable use (processUseConsumable — removes item)

**skills:**
- `learnSkill()` (in skill trainer handler)
- Skill bar rearrange (if there's a skill bar swap handler)

**quests:**
- `acceptQuest()` (line 2658)
- `completeQuest()` (line 2688)
- Quest objective progress updates

**bank:**
- `processBank()` — deposit/withdraw handlers

**pet:**
- `processPetCommand()` — equip/unequip/rename/toggle auto-loot
- Pet XP gain (lines 3240, 3547)

## System 2: PersistenceQueue Wiring

### New method: tickPersistQueue()

Called every tick from `tick()`. Dequeues up to 10 requests per tick. For each dequeued request, calls `savePlayerToDBAsync()` (or a targeted save depending on PersistType).

```
void ServerApp::tickPersistQueue() {
    auto batch = persistQueue_.dequeue(10, gameTime_);
    for (auto& req : batch) {
        // Deduplicate: if this client already has a save in-flight, skip
        savePlayerToDBAsync(req.clientId);
    }
}
```

### Deduplication

Add a `std::unordered_map<uint64_t, float>` called `pendingPersist_` keyed on `(clientId << 8) | PersistType`. Before enqueuing, check if a request for this client+type was enqueued within the last N seconds (based on priority tier). Skip if duplicate.

Helper method:
```
void ServerApp::enqueuePersist(uint16_t clientId, PersistPriority priority, PersistType type) {
    uint64_t key = (static_cast<uint64_t>(clientId) << 8) | static_cast<uint64_t>(type);
    auto it = pendingPersist_.find(key);
    if (it != pendingPersist_.end()) {
        // Already pending — only re-enqueue if upgrading priority
        // (e.g., NORMAL position → IMMEDIATE after gold pickup)
        float elapsed = gameTime_ - it->second;
        if (elapsed < 1.0f) return; // within dedup window
    }
    pendingPersist_[key] = gameTime_;
    persistQueue_.enqueue(clientId, priority, type, gameTime_);
}
```

Clean `pendingPersist_` entries older than 60s in `tickMaintenance()`.

### Enqueue sites

| Priority | Site | PersistType |
|----------|------|-------------|
| IMMEDIATE | Trade execute (both players) | Inventory |
| IMMEDIATE | Market buy (buyer) | Inventory |
| IMMEDIATE | Market sell/list (seller) | Inventory |
| IMMEDIATE | Gold pickup (loot) | Inventory |
| IMMEDIATE | Enchant (gold + item mutation) | Inventory |
| IMMEDIATE | Repair (gold + item mutation) | Inventory |
| IMMEDIATE | Core extraction (item consumed) | Inventory |
| IMMEDIATE | Craft (ingredients consumed, result added) | Inventory |
| IMMEDIATE | Socket item | Inventory |
| HIGH | Level-up (inside addXP) | Character |
| HIGH | Quest complete | Quests |
| HIGH | Skill learn | Skills |
| NORMAL | CmdMove (throttled: only if no pending position request within 30s) | Position |
| LOW | Pet state change | Pet |
| LOW | Bank deposit/withdraw | Bank |

### tick() integration

In `ServerApp::tick()`, call `tickPersistQueue()` after `tickAutoSave()`:
```
tickAutoSave(dt);
tickPersistQueue();
```

### Cleanup on disconnect

In `onClientDisconnected()`, remove the client's entries from `pendingPersist_`.

## Files Modified

| File | Changes |
|------|---------|
| `server/server_app.h` | Add `tickPersistQueue()` and `enqueuePersist()` declarations. Add `pendingPersist_` map |
| `server/server_app.cpp` | (1) Add dirty flag sets at ~20 mutation sites. (2) Add dirty flag checks in `savePlayerToDB()`. (3) Add `enqueuePersist()` calls at ~15 handler sites. (4) Implement `tickPersistQueue()`. (5) Wire into `tick()`. (6) Cleanup in `onClientDisconnected()` and `tickMaintenance()` |
| `tests/test_persistence_priority.cpp` | Add tests for deduplication logic |

## Testing

- Existing 6 PersistenceQueue tests continue to pass
- New tests for deduplication (enqueue same client+type twice within window → only one in queue)
- New tests for `enqueuePersist` priority upgrade behavior
- Full test suite regression (824 tests)

## What is NOT changed

- `tickAutoSave()` — stays as-is, 5-minute staggered catch-all
- `savePlayerToDBAsync()` — stays as-is
- `saveInventoryForClient()` — stays as-is
- WAL append sites — stay as-is (orthogonal crash recovery)
