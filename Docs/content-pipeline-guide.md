# Content Pipeline Guide

## How the System Works

The Content Browser is an editor panel that lets you create, edit, and delete game content definitions (mobs, items, loot tables, spawn zones) while the game is running. Changes push to the server instantly -- no restart, no recompile.

### Architecture

```
Aseprite (art) --> FileWatcher (hot-reload textures) --> Editor (assign to definitions)
                                                             |
                                                             v
Editor Content Browser --> Admin Protocol Message --> Server Handler
                                                             |
                                                     Parameterized SQL
                                                             |
                                                     Cache Reload
                                                             |
                                                     Response --> Editor Toast
```

All admin messages require `AdminRole::Admin` on the connected account. The server validates every request, executes parameterized SQL (no injection risk), reloads the affected cache, and responds with success or failure. The editor shows a toast notification.

### What Changed vs. Before

| Before | After |
|--------|-------|
| Hand-write SQL in pgAdmin | Click fields in the Content Browser |
| Restart server after every change | Server reloads cache instantly |
| No validation until runtime | Startup validation + on-demand validation tab |
| No way to check broken references | 11 cross-reference validation rules |
| Minutes per content iteration | Seconds per content iteration |

### New Components

| File | Purpose |
|------|---------|
| `engine/net/admin_messages.h` | 8 protocol message types |
| `engine/editor/content_browser_panel.h/.cpp` | The 5-tab editor UI |
| `engine/net/net_client.h/.cpp` | Client send/receive methods |
| `server/handlers/admin_handler.cpp` | SQL execution + cache reload |
| `server/validation/content_validator.h/.cpp` | 11 validation rules |
| 7 cache headers | Added `reload()` methods |

---

## How to Use the Content Browser

### Opening the Panel

1. Start the server with `DATABASE_URL` set
2. Start FateEngine (editor build)
3. Connect with an **Admin** account
4. **Window > Content Browser**

### Creating a New Mob

1. Go to the **Mobs** tab
2. Click **New Mob**
3. Fill in:
   - **ID**: unique string like `forest_wolf_alpha` (cannot change after save)
   - **Display Name**: what players see: `Forest Wolf Alpha`
   - **Base HP, Damage, Armor**: core combat stats
   - **Loot Table ID**: link to a loot table (create one in the Loot tab first)
   - **Attack Style**: Melee, Ranged, or Magic
4. Click **Save**
5. Green toast = success, red toast = error with details

### Creating a New Item

1. Go to the **Items** tab
2. Click **New Item**
3. Fill in:
   - **ID**: unique string like `sword_iron_02`
   - **Name**: display name `Iron Sword +2`
   - **Type**: Weapon, Armor, Accessory, Consumable, Material, QuestItem, Scroll, Bag, Pet
   - **Rarity**: Common through Legendary
   - **Class Req**: All, Warrior, Mage, Ranger, Healer
   - Combat stats, economy values, icon path
4. Click **Save**

### Creating a Loot Table

1. Go to the **Loot Tables** tab
2. Click **Add Entry** (creates a new entry with a blank table ID)
3. Fill in the **loot_table_id** (e.g., `loot_forest_wolf`), **item_id**, **drop chance** (0.0-1.0)
4. Click **Save** on each entry
5. Link the table to a mob by setting its **Loot Table ID** in the Mobs tab

### Creating a Spawn Zone

1. Go to the **Spawn Zones** tab
2. Click **New Spawn Zone**
3. Fill in:
   - **Scene ID**: which scene/map (e.g., `WhisperingWoods`)
   - **Mob Def ID**: which mob to spawn (must exist in mob_definitions)
   - **Center X/Y**: world position
   - **Radius**: spawn area size in pixels
   - **Target Count**: how many mobs to maintain
4. Click **Save**

### Running Validation

1. Go to the **Validation** tab
2. Click **Run Validation**
3. Results show:
   - **[ERROR]** (red): Broken references -- a loot table references a missing item, a spawn zone references a missing mob
   - **[WARN]** (yellow): Suspicious data -- mobs with 0 HP, items with no loot table, invalid drop chances
   - **[INFO]** (blue): Completeness gaps -- missing descriptions, inverted level ranges
4. Use filter checkboxes to focus on specific severity levels
5. Fix issues using the other tabs, then re-run validation

### Live Edit Workflow

The intended workflow for rapid content iteration:

1. **Art**: Draw sprite in Aseprite, save to `assets/` -- FileWatcher hot-reloads it
2. **Definition**: Open Content Browser, create/edit mob, set sprite reference, save
3. **Loot**: Create loot table entries, link to mob
4. **Spawn**: Create spawn zone in the target scene
5. **Test**: Walk to the spawn area in-game, see the mob, kill it, check drops
6. **Tune**: Adjust HP/damage/drop rates in Content Browser, save -- changes are live
7. **Validate**: Run validation to catch broken references

---

## How to Extend the System

### Adding a New Content Type (e.g., Skills, Recipes)

1. **Add a content type constant** in `engine/net/admin_messages.h`:
   ```cpp
   namespace AdminContentType {
       constexpr uint8_t Skill = 4;  // new
   }
   ```

2. **Add insert/update helpers** in `server/handlers/admin_handler.cpp`:
   - `insertSkill(pqxx::work& txn, const nlohmann::json& j)` with parameterized SQL
   - `updateSkill(pqxx::work& txn, const nlohmann::json& j)`
   - Add cases to `processAdminSaveContent` and `processAdminDeleteContent` switch statements

3. **Add a `*ToJson` serializer** in admin_handler.cpp for `processAdminRequestContentList`

4. **Add a cache type** to `AdminCacheType` if not already present (Skills already has `SkillDefs = 4`)

5. **Add a tab** in `content_browser_panel.cpp`:
   - Add per-tab state members to the header (list, selected index, editing buffer, filter)
   - Add a `drawSkillsTab()` method following the Mobs tab pattern
   - Add the tab to the `ImGui::BeginTabBar` in `draw()`

### Adding a New Validation Rule

In `server/validation/content_validator.h`, add a method declaration:
```cpp
std::vector<ValidationIssue> checkNewRule() const;
```

Implement it in `content_validator.cpp`, then add it to `runAll()`.

### Adding Fields to an Existing Content Type

1. Add the field to the DB (ALTER TABLE or migration)
2. Add the field to the cache struct (e.g., `CachedMobDef`)
3. Add the field to the cache's `initialize()` SELECT query
4. Add the field to `mobsToJson()` in admin_handler.cpp
5. Add the field to `insertMob()`/`updateMob()` SQL
6. Add a field editor call in `drawMobsTab()` (e.g., `drawIntField("New Field", editingMob_, "new_field")`)

---

## Testing Checklist

### Prerequisites

- [ ] Server running with `DATABASE_URL` set
- [ ] Editor connected with an Admin account
- [ ] Content Browser open (Window > Content Browser)
- [ ] Server console visible to see logs

### Smoke Tests

- [ ] **Mobs tab loads**: Click Mobs tab, verify list populates with existing mobs (73 expected)
- [ ] **Items tab loads**: Click Items tab, verify list populates (748 expected)
- [ ] **Loot tab loads**: Click Loot Tables tab, verify table IDs appear in left pane
- [ ] **Spawns tab loads**: Click Spawn Zones tab, verify zones populate
- [ ] **Validation runs**: Click Validation tab > Run Validation, verify results appear

### CRUD - Mobs

- [ ] **Create mob**: Click New Mob, set ID to `test_mob_001`, display name `Test Mob`, HP=100, Save. Verify green toast.
- [ ] **Read mob**: The new mob should appear in the list. Click it. Verify all fields populated correctly.
- [ ] **Update mob**: Change base_hp to 200, click Save. Verify green toast. Click another mob, click back to `test_mob_001`, verify HP shows 200.
- [ ] **Delete mob**: Select `test_mob_001`, click Delete. Verify green toast. Verify mob disappears from list.
- [ ] **Duplicate ID error**: Create two mobs with the same ID. Second save should show red toast with "duplicate key" error.

### CRUD - Items

- [ ] **Create item**: Click New Item, set ID to `test_sword_001`, name `Test Sword`, type Weapon, rarity Rare, damage_min=10, damage_max=20. Save.
- [ ] **Edit item**: Change gold_value to 500, save. Verify change persists by reselecting.
- [ ] **Delete item**: Delete the test item. Verify removed from list.

### CRUD - Loot Tables

- [ ] **Add loot entry**: Select an existing loot table (or create a new mob first with a loot_table_id). Click Add Entry. Set item_id to an existing item, drop_chance to 0.5. Save.
- [ ] **Modify drop chance**: Change a drop_chance slider, save. Verify change persists.
- [ ] **Delete loot entry**: Click X on an entry. Verify the entry is removed and toast shows success.

### CRUD - Spawn Zones

- [ ] **Create spawn zone**: Click New Spawn Zone. Set scene_id (e.g., `WhisperingWoods`), mob_def_id to an existing mob, center_x/y, radius=200, target_count=3. Save.
- [ ] **Delete spawn zone**: Delete the test zone.

### Cross-Reference Validation

- [ ] **Create a broken reference**: Create a mob with loot_table_id set to `nonexistent_table`. Run Validation. Verify an ERROR appears for the broken loot table reference.
- [ ] **Fix the reference**: Either create the loot table or clear the mob's loot_table_id. Re-run validation. Verify the error is gone.
- [ ] **Orphaned item warning**: Create an item that is not in any loot table. Run Validation. Verify a WARNING about the orphaned item.

### Live Reload

- [ ] **Mob stat change takes effect immediately**: Find a mob in-game that you can fight. In Content Browser, change its `base_hp` to something extreme (e.g., 1). Save. Kill the mob. Verify the next spawn has the new HP (1 HP = instant kill).
- [ ] **Loot change takes effect immediately**: Change a drop_chance to 1.0 (guaranteed). Kill the mob. Verify the item drops every time.

### Edge Cases

- [ ] **Empty required field**: Try to create a mob with empty mob_def_id. Verify error toast.
- [ ] **Filter works**: Type a partial name in the filter box. Verify the list filters correctly.
- [ ] **Revert button**: Edit a mob, change some fields, click Revert. Verify fields reset to saved values.
- [ ] **Delete with references**: Try to delete a mob that has spawn zones. Verify error toast saying "Cannot delete: mob is referenced by N spawn zones".
- [ ] **Server console logs**: Check the server console shows validation results at startup and logs admin operations.
- [ ] **Non-admin rejection**: Connect with a non-admin account. Open Content Browser. Try to save. Verify "Permission denied" error.

### Cleanup

- [ ] Delete all test entries created during testing (`test_mob_001`, `test_sword_001`, test spawn zones)
- [ ] Run Validation one final time to confirm no broken references remain
