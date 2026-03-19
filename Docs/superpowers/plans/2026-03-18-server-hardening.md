# Server Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the combat damage off-by-one bug, prevent item duplication on market/trade, add trade target chat notification, populate gauntlet config data, and move DB saves to the async fiber dispatcher.

**Architecture:** Five independent fixes that each improve server correctness or performance. No new systems — all changes are to existing files. The combat fix is a one-line change. Inventory saves after mutations prevent duplication. Async saves move all blocking DB I/O off the game thread.

**Tech Stack:** C++20, libpqxx 7.9.2, PostgreSQL, existing fiber job system + DbPool + DbDispatcher

---

## File Map

| File | Change |
|---|---|
| `game/shared/combat_system.cpp` | Fix off-by-one in `calculateDamageMultiplier` |
| `server/server_app.cpp` | Add inventory save after market/trade mutations, trade notification via chat, async save wrappers, gauntlet init from seed data |
| `server/server_app.h` | Add `saveInventoryToDB` helper, `savePlayerToDBAsync` |
| `Docs/migrations/003_gauntlet_seed_data.sql` | Gauntlet division configs, waves, rewards |
| `tests/test_combat_system.cpp` | Test for damage reduction boundary (existing file) |

---

### Task 1: Fix Combat Damage Reduction Off-By-One

**Files:**
- Modify: `game/shared/combat_system.cpp:93-97`
- Modify: `tests/test_combat_system.cpp` (add boundary test)

- [ ] **Step 1: Add failing test for the boundary case**

In `tests/test_combat_system.cpp`, add:

```cpp
TEST_CASE("CombatSystem: damage reduction starts at correct level diff") {
    // damageReductionStartsAt = 2
    // Unity: levelDiff <= 2 means NO reduction. levelDiff 3 = first reduction.
    // At levelDiff=2: should be 1.0 (no penalty)
    float mult2 = CombatSystem::calculateDamageMultiplier(10, 12); // diff=2
    CHECK(mult2 == doctest::Approx(1.0f));

    // At levelDiff=3: should be 0.88 (12% reduction, 1 effective level)
    float mult3 = CombatSystem::calculateDamageMultiplier(10, 13); // diff=3
    CHECK(mult3 == doctest::Approx(0.88f));
}
```

- [ ] **Step 2: Run test — expect FAIL (mult2 will be 0.88 instead of 1.0)**

Run: `./out/build/x64-Debug/fate_tests.exe -tc="CombatSystem: damage reduction*"`

- [ ] **Step 3: Fix the off-by-one**

In `game/shared/combat_system.cpp`, change lines 93-97:

```cpp
// BEFORE (bug):
if (levelDiff < cfg.damageReductionStartsAt) {
    return 1.0f;
}
int effectiveLevels = levelDiff - cfg.damageReductionStartsAt + 1;

// AFTER (matches Unity):
if (levelDiff <= cfg.damageReductionStartsAt) {
    return 1.0f;
}
int effectiveLevels = levelDiff - cfg.damageReductionStartsAt;
```

- [ ] **Step 4: Run test — expect PASS**

- [ ] **Step 5: Run full test suite to check no regressions**

Run: `./out/build/x64-Debug/fate_tests.exe`
Expected: All 196+ tests pass

- [ ] **Step 6: Commit**

```
fix: combat damage reduction off-by-one (match Unity prototype)
```

---

### Task 2: Inventory Save After Market/Trade Mutations

**Files:**
- Modify: `server/server_app.h` (add `saveInventoryForClient` helper)
- Modify: `server/server_app.cpp` (call after market list/buy/cancel, after trade execute)

- [ ] **Step 1: Add saveInventoryForClient helper to header**

In `server/server_app.h`, add to private section:

```cpp
void saveInventoryForClient(uint16_t clientId);
```

- [ ] **Step 2: Implement the helper**

In `server/server_app.cpp`, add:

```cpp
void ServerApp::saveInventoryForClient(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = replication_.getEntityHandle(pid);
    Entity* e = world_.getEntity(h);
    if (!e) return;

    auto* inv = e->getComponent<InventoryComponent>();
    if (!inv) return;

    // Build slot records from in-memory inventory
    std::vector<InventorySlotRecord> slots;
    const auto& items = inv->inventory.getSlots();
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (!items[i].isValid()) continue;
        InventorySlotRecord s;
        s.instance_id   = items[i].instanceId;
        s.character_id  = client->character_id;
        s.item_id       = items[i].itemId;
        s.slot_index    = i;
        s.rolled_stats  = ItemStatRoller::rolledStatsToJson(items[i].rolledStats);
        s.enchant_level = items[i].enchantLevel;
        s.is_protected  = items[i].isProtected;
        s.is_soulbound  = items[i].isSoulbound;
        s.quantity       = items[i].quantity;
        slots.push_back(std::move(s));
    }

    inventoryRepo_->saveInventory(client->character_id, slots);
}
```

- [ ] **Step 3: Call after Market ListItem (after `inv->inventory.removeItem(slot)`)**

After the `sendPlayerState(clientId)` in MarketAction::ListItem, add:

```cpp
saveInventoryForClient(clientId);
```

- [ ] **Step 4: Call after Market BuyItem (after `inv->inventory.addItem(boughtItem)`)**

After the `sendPlayerState(clientId)` in MarketAction::BuyItem, add:

```cpp
saveInventoryForClient(clientId);
```

- [ ] **Step 5: Call after Trade Confirm execution (after `txn.commit()`)**

After the trade `LOG_INFO` in TradeAction::Confirm, add:

```cpp
// Save both players' inventories to prevent duplication on crash
saveInventoryForClient(clientId);
// Find other player's clientId and save theirs too
std::string otherCharId = (client->character_id == session->playerACharacterId)
    ? session->playerBCharacterId : session->playerACharacterId;
server_.connections().forEach([&](ClientConnection& c) {
    if (c.character_id == otherCharId) {
        saveInventoryForClient(c.clientId);
    }
});
```

- [ ] **Step 6: Build and test**

Expected: Zero compile errors, 196+ tests pass

- [ ] **Step 7: Commit**

```
fix: save inventory to DB after market/trade mutations to prevent item duplication
```

---

### Task 3: Trade Target Notification via System Chat

**Files:**
- Modify: `server/server_app.cpp` (TradeAction::Initiate handler)

- [ ] **Step 1: After session creation, find target player and send chat notification**

In TradeAction::Initiate, after `sendTradeResult(1, 0, "Trade session started")`, add:

```cpp
// Notify target player via system chat (UI chat pending — use system message)
server_.connections().forEach([&](ClientConnection& c) {
    if (c.character_id == targetCharId) {
        SvChatMessageMsg chatMsg;
        chatMsg.channel    = static_cast<uint8_t>(ChatChannel::System);
        chatMsg.senderName = "[Trade]";
        chatMsg.message    = charStats->stats.characterName + " wants to trade with you.";
        chatMsg.faction    = 0;
        uint8_t chatBuf[512]; ByteWriter cw(chatBuf, sizeof(chatBuf));
        chatMsg.write(cw);
        server_.sendTo(c.clientId, Channel::ReliableOrdered,
                       PacketType::SvChatMessage, chatBuf, cw.size());

        // Also send trade invite update
        SvTradeUpdateMsg invite;
        invite.updateType = 0; // invited
        invite.sessionId  = sessionId;
        invite.otherPlayerName = charStats->stats.characterName;
        uint8_t tbuf[256]; ByteWriter tw(tbuf, sizeof(tbuf));
        invite.write(tw);
        server_.sendTo(c.clientId, Channel::ReliableOrdered,
                       PacketType::SvTradeUpdate, tbuf, tw.size());
    }
});
```

Note: Need to get `charStats` before this — it's already available since we have entity `e`.

- [ ] **Step 2: Add CharacterStatsComponent lookup before the trade switch**

Before the trade `switch (subAction)` block, add:

```cpp
auto* charStats = e->getComponent<CharacterStatsComponent>();
```

- [ ] **Step 3: Build and test**

- [ ] **Step 4: Commit**

```
feat: notify trade target via system chat + trade invite message
```

---

### Task 4: Gauntlet Seed Data Migration

**Files:**
- Create: `Docs/migrations/003_gauntlet_seed_data.sql`

- [ ] **Step 1: Write the migration SQL**

Three divisions matching the Unity prototype level ranges. 4 basic waves + 1 boss wave per division. Winner/loser rewards.

```sql
-- Migration 003: Gauntlet seed data
-- Applied: March 18, 2026

-- Division 1: Low Level (1-20)
INSERT INTO gauntlet_config (division_name, min_level, max_level, arena_scene_name,
    wave_count, seconds_between_waves, respawn_seconds,
    team_spawn_a_x, team_spawn_a_y, team_spawn_b_x, team_spawn_b_y,
    min_players_to_start, max_players_per_team)
VALUES ('Novice Arena', 1, 20, 'Gauntlet_Div1', 5, 10, 10,
    -5.0, 0.0, 5.0, 0.0, 2, 10);

-- Division 2: Mid Level (21-40)
INSERT INTO gauntlet_config (division_name, min_level, max_level, arena_scene_name,
    wave_count, seconds_between_waves, respawn_seconds,
    team_spawn_a_x, team_spawn_a_y, team_spawn_b_x, team_spawn_b_y,
    min_players_to_start, max_players_per_team)
VALUES ('Veteran Arena', 21, 40, 'Gauntlet_Div2', 5, 10, 10,
    -5.0, 0.0, 5.0, 0.0, 2, 10);

-- Division 3: High Level (41-99)
INSERT INTO gauntlet_config (division_name, min_level, max_level, arena_scene_name,
    wave_count, seconds_between_waves, respawn_seconds,
    team_spawn_a_x, team_spawn_a_y, team_spawn_b_x, team_spawn_b_y,
    min_players_to_start, max_players_per_team)
VALUES ('Champion Arena', 41, 99, 'Gauntlet_Div3', 5, 10, 10,
    -5.0, 0.0, 5.0, 0.0, 2, 10);

-- Waves for Division 1 (use low-level mobs from mob_definitions)
INSERT INTO gauntlet_waves (division_id, wave_number, mob_def_id, mob_count, spawn_delay_seconds, is_boss, bonus_points) VALUES
    (1, 1, 'mob_slime', 10, 3.0, false, 0),
    (1, 2, 'mob_goblin', 12, 2.5, false, 0),
    (1, 3, 'mob_wolf', 15, 2.0, false, 0),
    (1, 4, 'mob_skeleton', 18, 1.5, false, 0),
    (1, 5, 'mob_forest_golem', 1, 0.0, true, 50);

-- Waves for Division 2
INSERT INTO gauntlet_waves (division_id, wave_number, mob_def_id, mob_count, spawn_delay_seconds, is_boss, bonus_points) VALUES
    (2, 1, 'mob_orc', 10, 3.0, false, 0),
    (2, 2, 'mob_dark_mage', 12, 2.5, false, 0),
    (2, 3, 'mob_troll', 15, 2.0, false, 0),
    (2, 4, 'mob_wyvern', 18, 1.5, false, 0),
    (2, 5, 'mob_dragon', 1, 0.0, true, 50);

-- Waves for Division 3
INSERT INTO gauntlet_waves (division_id, wave_number, mob_def_id, mob_count, spawn_delay_seconds, is_boss, bonus_points) VALUES
    (3, 1, 'mob_demon', 10, 3.0, false, 0),
    (3, 2, 'mob_lich', 12, 2.5, false, 0),
    (3, 3, 'mob_golem_ancient', 15, 2.0, false, 0),
    (3, 4, 'mob_shadow_drake', 18, 1.5, false, 0),
    (3, 5, 'mob_world_boss', 1, 0.0, true, 100);

-- Rewards: Winners (per division)
INSERT INTO gauntlet_rewards (division_id, is_winner, reward_type, reward_value, quantity) VALUES
    (1, true, 'Gold', '5000', 1),
    (1, true, 'Honor', '50', 1),
    (1, true, 'Token', 'gauntlet_token', 3),
    (2, true, 'Gold', '15000', 1),
    (2, true, 'Honor', '100', 1),
    (2, true, 'Token', 'gauntlet_token', 5),
    (3, true, 'Gold', '50000', 1),
    (3, true, 'Honor', '200', 1),
    (3, true, 'Token', 'gauntlet_token', 10);

-- Rewards: Losers (participation)
INSERT INTO gauntlet_rewards (division_id, is_winner, reward_type, reward_value, quantity) VALUES
    (1, false, 'Gold', '1000', 1),
    (1, false, 'Token', 'gauntlet_token', 1),
    (2, false, 'Gold', '3000', 1),
    (2, false, 'Token', 'gauntlet_token', 2),
    (3, false, 'Gold', '10000', 1),
    (3, false, 'Token', 'gauntlet_token', 3);

-- Performance rewards
INSERT INTO gauntlet_performance_rewards (division_id, category, reward_type, reward_value, quantity) VALUES
    (1, 'top_mob_killer', 'Token', 'gauntlet_token', 2),
    (1, 'top_pvp_killer', 'Token', 'gauntlet_token', 2),
    (2, 'top_mob_killer', 'Token', 'gauntlet_token', 3),
    (2, 'top_pvp_killer', 'Token', 'gauntlet_token', 3),
    (3, 'top_mob_killer', 'Token', 'gauntlet_token', 5),
    (3, 'top_pvp_killer', 'Token', 'gauntlet_token', 5);
```

**IMPORTANT:** The `mob_def_id` values above are placeholders. Before running, verify which mob IDs actually exist in `mob_definitions`:

```sql
SELECT mob_def_id FROM mob_definitions ORDER BY min_spawn_level LIMIT 20;
```

Replace the placeholder IDs with real ones from the query results.

- [ ] **Step 2: Ask the user to run the migration against fate_engine_dev**

Provide the SQL and ask them to execute it. Then verify:

```sql
SELECT COUNT(*) FROM gauntlet_config;    -- expect 3
SELECT COUNT(*) FROM gauntlet_waves;     -- expect 15
SELECT COUNT(*) FROM gauntlet_rewards;   -- expect 15
SELECT COUNT(*) FROM gauntlet_performance_rewards; -- expect 6
```

- [ ] **Step 3: Commit the migration file**

```
feat: add gauntlet seed data (3 divisions, 15 waves, 21 rewards)
```

---

### Task 5: Async DB Saves via Fiber Dispatcher

**Files:**
- Modify: `server/server_app.h` (add `savePlayerToDBAsync`)
- Modify: `server/server_app.cpp` (implement async wrapper, update tickAutoSave)

- [ ] **Step 1: Add async save declaration**

In `server/server_app.h`, add to private section:

```cpp
void savePlayerToDBAsync(uint16_t clientId);
```

- [ ] **Step 2: Implement — snapshot data on game thread, dispatch DB work to fiber**

In `server/server_app.cpp`:

```cpp
void ServerApp::savePlayerToDBAsync(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = replication_.getEntityHandle(pid);
    Entity* e = world_.getEntity(h);
    if (!e) return;

    // ---- Snapshot all data on the game thread (safe to read components) ----
    CharacterRecord rec;
    rec.character_id = client->character_id;
    rec.account_id   = client->account_id;

    auto* charStatsComp = e->getComponent<CharacterStatsComponent>();
    if (charStatsComp) {
        const CharacterStats& s = charStatsComp->stats;
        rec.character_name   = s.characterName;
        rec.class_name       = s.className;
        rec.level            = s.level;
        rec.current_xp       = s.currentXP;
        rec.xp_to_next_level = static_cast<int>(s.xpToNextLevel);
        rec.current_hp       = s.currentHP;
        rec.max_hp           = s.maxHP;
        rec.current_mp       = s.currentMP;
        rec.max_mp           = s.maxMP;
        rec.current_fury     = s.currentFury;
        rec.honor            = s.honor;
        rec.pvp_kills        = s.pvpKills;
        rec.pvp_deaths       = s.pvpDeaths;
        rec.is_dead          = s.isDead;
    }

    auto* t = e->getComponent<Transform>();
    if (t) {
        Vec2 tilePos = Coords::toTile(t->position);
        rec.position_x = tilePos.x;
        rec.position_y = tilePos.y;
    }

    auto* sc = SceneManager::instance().currentScene();
    rec.current_scene = sc ? sc->name() : "Scene2";

    auto* inv = e->getComponent<InventoryComponent>();
    if (inv) rec.gold = inv->inventory.getGold();

    // Snapshot skills
    std::vector<CharacterSkillRecord> skillRecords;
    int skillEarned = 0, skillSpent = 0;
    std::vector<std::string> skillBar(20, "");
    auto* skillComp = e->getComponent<SkillManagerComponent>();
    if (skillComp) {
        for (const auto& learned : skillComp->skills.getLearnedSkills()) {
            CharacterSkillRecord sr;
            sr.skillId = learned.skillId;
            sr.unlockedRank = learned.unlockedRank;
            sr.activatedRank = learned.activatedRank;
            skillRecords.push_back(std::move(sr));
        }
        skillEarned = skillComp->skills.earnedPoints();
        skillSpent = skillEarned - skillComp->skills.availablePoints();
        for (int i = 0; i < 20; ++i)
            skillBar[i] = skillComp->skills.getSkillInSlot(i);
    }

    std::string charId = client->character_id;

    // ---- Dispatch to worker fiber (DB I/O off game thread) ----
    dbDispatcher_.dispatchVoid([this, rec, charId, skillRecords, skillEarned, skillSpent, skillBar]
                               (pqxx::connection& conn) {
        // Character save
        CharacterRepository charRepo(conn);
        charRepo.saveCharacter(rec);

        // Skills save
        SkillRepository skillRepo(conn);
        skillRepo.saveAllCharacterSkills(charId, skillRecords);
        skillRepo.saveSkillBar(charId, skillBar);
        skillRepo.saveSkillPoints(charId, skillEarned, skillSpent);

        // Last online
        SocialRepository socialRepo(conn);
        socialRepo.updateLastOnline(charId);
    });
}
```

- [ ] **Step 3: Update tickAutoSave to use async version**

Change the `savePlayerToDB(clientId)` call in `tickAutoSave` to:

```cpp
savePlayerToDBAsync(clientId);
```

- [ ] **Step 4: Keep synchronous savePlayerToDB for disconnect (must complete before entity destruction)**

The existing `savePlayerToDB()` stays synchronous for `onClientDisconnected()` — we can't destroy the entity until the save completes. Only auto-save uses the async path.

- [ ] **Step 5: Build and test**

Expected: Zero compile errors, 196+ tests pass

- [ ] **Step 6: Commit**

```
perf: move auto-save to async fiber dispatcher (game loop no longer blocks on DB)
```

---

### Task 6: Update Docs

**Files:**
- Modify: `Docs/ENGINE_STATE_AND_FEATURES.md`
- Modify: `Docs/DATABASE_REFERENCE.md`

- [ ] **Step 1: Add changelog entry**

- [ ] **Step 2: Update DATABASE_REFERENCE with gauntlet seed data counts**

- [ ] **Step 3: Commit**

```
docs: update for server hardening (combat fix, inventory saves, async dispatch, gauntlet data)
```
