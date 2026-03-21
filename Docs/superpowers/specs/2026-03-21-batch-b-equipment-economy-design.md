# Batch B: Equipment & Economy — Design Spec

**Date:** 2026-03-21
**Scope:** Enchanting tuning to +15 with break mechanic, core extraction, combine book crafting.

---

## 1. Enchanting Tuning (+1 to +15)

### Problem
`enchant_system.h` supports +1 to +12 with 100% success for +1-8. No break mechanic exists. No safe thresholds per equipment type. No network messages — enchanting isn't wired to the server. Need TWOM-style risk/reward with a hybrid max of +15.

### Design

**Safe threshold: +8 for all enchantable equipment (100% success, no break risk).**

No weapon/armor distinction — all enchantable slots (weapons and armor) are safe to +8. Break risk begins at +9.

**Success rates and gold costs:**

| Target Level | Success Rate | Gold Cost | Break Risk |
|---|---|---|---|
| +1 | 100% | 100 | No |
| +2 | 100% | 100 | No |
| +3 | 100% | 100 | No |
| +4 | 100% | 500 | No |
| +5 | 100% | 500 | No |
| +6 | 100% | 500 | No |
| +7 | 100% | 2,000 | No |
| +8 | 100% | 2,000 | No |
| +9 | 50% | 10,000 | Yes |
| +10 | 40% | 25,000 | Yes |
| +11 | 30% | 50,000 | Yes |
| +12 | 20% | 100,000 | Yes |
| +13 | 10% | 500,000 | Yes |
| +14 | 5% | 1,000,000 | Yes |
| +15 | 2% | 2,000,000 | Yes |

`SAFE_ENCHANT_LEVEL = 8` — constant, same for all equipment types.

Implementation: `getSuccessRate(targetLevel)` returns 1.0f if `targetLevel <= SAFE_ENCHANT_LEVEL`, otherwise looks up the rate table. `getGoldCost(targetLevel)` indexed directly from the table. `hasBreakRisk(targetLevel)` returns `targetLevel > SAFE_ENCHANT_LEVEL`.

**Break mechanic:**
- Failure above safe threshold WITHOUT protection stone → item breaks:
  - `isBroken = true` on the ItemInstance
  - Item becomes unusable (cannot equip, cannot trade, cannot extract)
  - Item becomes soulbound (`isSoulbound = true`)
- Failure above safe threshold WITH protection stone → stone consumed, item stays at current level, no break
- Failure at or below safe threshold → impossible (100% success)

**Protection stone behavior:**
- Protection stone is **always consumed** when used, regardless of outcome:
  - On success: stone consumed, item enchant level increases by 1
  - On failure: stone consumed, item stays at current level (no break)
- Server finds `mat_protect_stone` in inventory via `findItemById()` and consumes the first match
- Protection stone item ID: `mat_protect_stone` (already exists)

**Repair mechanic:**
- Requires a `CmdRepair` message (separate from enchant)
- Repair Scroll item (`item_repair_scroll`) + 50,000 gold
- Server validates: item exists, `isBroken == true`, player has scroll, player has gold
- Repaired item's enchant level is set to a random level between +1 and +8 (the safe threshold)
- Repair removes `isBroken` flag but item remains soulbound
- Scroll consumed, gold deducted via `setGold(currentGold - 50000)`
- WAL entry logged before mutation

**Network messages:**

`CmdRepair` (client → server):
- `uint8_t inventorySlot` — slot of the broken item

`SvRepairResult` (server → client):
- `uint8_t success`
- `uint8_t newLevel` — resulting enchant level after repair
- `std::string message`

**Enhancement stones:** Server finds the correct tier stone via `findItemById()` based on the item's level bracket. First match consumed. Unchanged tiering (`mat_enhance_stone_basic` through `mat_enhance_stone_legendary`).

**Per-item maxEnchant:** The `CachedItemDefinition.maxEnchant` field (from DB `item_definitions.max_enchant`) still applies. Actual max for any item is `min(itemDef.maxEnchant, MAX_ENCHANT_LEVEL)`. Some items may cap below +15.

**Equipped items cannot be enchanted.** `CmdEnchant.inventorySlot` refers to inventory slots only (not equipment slots). Players must unequip first.

**Weapon damage bonuses (extended to +15):**
- Base multiplier: `1.0 + (enchantLevel × 0.125)`
- +11 secret bonus: additional 5% multiplier
- +12 secret bonus: additional 10% multiplier (stacks)
- +12 max damage bonus (30%): still applies at +13-15
- +15 secret bonus: additional 15% multiplier (stacks with +11, +12)
- +15 is therefore: 2.875 base × 1.05 × 1.10 × 1.30 × 1.15 = ~4.97× weapon damage

**Armor bonuses (extended to +15):**
- +1 to +8: +1 flat armor per level (total: +8)
- +9 to +15: +3 flat armor per level (total: +8 + 21 = +29 at +15)

**Secret stat bonuses at +15 (doubled from +12):**
- Hat: +20 Magic Resist
- Shoes: +20% Move Speed
- Armor/Chest: +20 Critical Chance
- SubWeapon/Shield: +20 Armor
- Gloves: no secret bonus (intentional, matches TWOM)

**Non-enchantable slots:** Ring, Necklace, Cloak, Belt (use stat enchant system instead — unchanged).

**Network messages:**
- `CmdEnchant` (client → server):
  - `uint8_t inventorySlot` — inventory slot of the item to enchant (not equipment slot)
  - `uint8_t useProtectionStone` — 1 if protection stone should be consumed
- `SvEnchantResult` (server → client):
  - `uint8_t success` — 1 if enchant succeeded
  - `uint8_t newLevel` — resulting enchant level
  - `uint8_t broke` — 1 if item broke
  - `std::string message` — result text for chat/UI

**Server handler flow:**
1. Validate: item exists in inventory (not equipment), is enchantable, is not broken, is not at `min(itemDef.maxEnchant, MAX_ENCHANT_LEVEL)`
2. Validate: player has required enhancement stone (by item level tier, found via `findItemById()`)
3. Validate: player has enough gold
4. If `useProtectionStone`: validate player has `mat_protect_stone` in inventory
5. WAL: log enchant attempt (item ID, slot, current level, gold cost)
6. Consume enhancement stone + gold via `setGold(currentGold - cost)` + protection stone (if used)
7. Roll success based on target level's rate
8. If success: increment `enchantLevel`, recalculate equipment stats
9. If failure above safe threshold:
   - With protection: item stays at current level
   - Without protection: item breaks (`isBroken = true`, `isSoulbound = true`)
10. Send `SvEnchantResult` + `sendPlayerState()` + `SvInventorySync`

### Data pipeline for `isBroken`

**ItemInstance** (`game/shared/item_instance.h`): Add `bool isBroken = false` field.

**DB persistence** (`server/db/inventory_repository.h`): Add `is_broken` to `InventorySlotRecord`. Requires DB migration: `ALTER TABLE character_inventory ADD COLUMN is_broken BOOLEAN DEFAULT FALSE`.

**Client sync** (`engine/net/protocol.h`): Add `isBroken` to `InventorySyncSlot` and `InventorySyncEquip` structs. Add to write/read serialization.

**Inventory serialization** (`game/shared/inventory.h`): `isBroken` included in item serialization to/from JSON and DB records.

### Files to modify
- `game/shared/enchant_system.h` — Rewrite rates, add safe thresholds, break logic, repair logic
- `game/shared/item_instance.h` — Add `isBroken` field
- `game/shared/game_types.h` — Update `MAX_ENCHANT_LEVEL` to 15
- `engine/net/protocol.h` — Add `CmdEnchantMsg`, `SvEnchantResultMsg`, `CmdRepairMsg`, `SvRepairResultMsg`, update `InventorySyncSlot`/`InventorySyncEquip` with `isBroken`
- `engine/net/packet.h` — Add packet type constants
- `server/server_app.cpp` — Handle `CmdEnchant` and `CmdRepair`
- `server/db/inventory_repository.h/.cpp` — Add `is_broken` to slot record, SELECT, INSERT/UPDATE
- `engine/net/net_client.h/cpp` — Handle `SvEnchantResult` and `SvRepairResult` callbacks
- DB migration: `ALTER TABLE character_inventory ADD COLUMN is_broken BOOLEAN DEFAULT FALSE`

---

## 2. Core Extraction System

### Problem
No way to break down unwanted equipment into crafting currency. Need TWOM-style core extraction: destroy a colored-name item using a Magic Extraction Scroll to obtain Cores.

### Design

**Extraction rules:**
- Requires: Magic Extraction Scroll (`item_extraction_scroll`) + a non-Common rarity item
- Common (black name) items: cannot be extracted
- Broken items: cannot be extracted
- Equipped items: cannot be extracted (must unequip first)
- Extraction scroll is consumed
- Original item is destroyed (removed from inventory)
- Core item(s) added to player's inventory

**Core tier determination:**

| Core Item | Source Rarity | Source Level | Quantity |
|---|---|---|---|
| `mat_core_1st` | Uncommon (Green) | 1-9 | 1 |
| `mat_core_2nd` | Uncommon (Green) | 10-19 | 1 |
| `mat_core_3rd` | Uncommon (Green) | 20-29 | 1 |
| `mat_core_4th` | Uncommon (Green) | 30-39 | 1 |
| `mat_core_5th` | Uncommon (Green) | 40-50 | 1 |
| `mat_core_6th` | Rare (Blue) | any | 1 |
| `mat_core_7th` | Epic (Purple) | any | 1 |

- Legendary/Unique items: also yield 7th Core (same as Epic)
- Cores are stackable Material items (max stack: 99)
- Enchanted items (+1 or higher) yield bonus cores: +1 additional core per 3 enchant levels (so +3 = 2 cores, +6 = 3 cores, +9 = 4 cores, etc.)
- Bonus cores are the same tier as the base core (a +9 Green Lv5 item yields 4× `mat_core_1st`)

**Network messages:**
- `CmdExtractCore` (client → server):
  - `uint8_t itemSlot` — inventory slot of the item to extract
  - `uint8_t scrollSlot` — inventory slot of the extraction scroll
- `SvExtractResult` (server → client):
  - `uint8_t success`
  - `std::string coreItemId` — which core was produced
  - `uint8_t coreQuantity`
  - `std::string message`

**Server handler flow:**
1. Validate: item exists in inventory, is not Common rarity, is not broken
2. Validate: scroll exists in inventory and is `item_extraction_scroll`
3. Validate: player has inventory space for cores
4. Determine core tier from item rarity + level
5. Calculate bonus cores from enchant level: `1 + (enchantLevel / 3)`
6. WAL: log extraction (item ID, slot, core result)
7. Remove item from inventory
8. Remove scroll from inventory (consume)
9. Add core item(s) to inventory
10. Send `SvExtractResult` + `SvInventorySync`

### Files to modify
- Create: `game/shared/core_extraction.h` — Static `determineCoreResult(rarity, level, enchantLevel)` → core ID + quantity
- `engine/net/protocol.h` — Add `CmdExtractCoreMsg`, `SvExtractResultMsg`
- `engine/net/packet.h` — Add packet types
- `server/server_app.cpp` — Handle `CmdExtractCore`
- `engine/net/net_client.h/cpp` — Handle `SvExtractResult` callback

---

## 3. Combine Book Crafting

### Problem
No crafting system. DB tables (`crafting_recipes`, `crafting_ingredients`) exist but are empty and have no loader. Need TWOM-style Combine Book crafting using cores and materials.

### Design

**DB schema adjustment:** The existing `crafting_recipes` table has `recipe_type VARCHAR(32)` but no `book_tier` column. Add migration: `ALTER TABLE crafting_recipes ADD COLUMN book_tier INTEGER DEFAULT 0`. The `recipe_type` field is kept for future use but crafting uses `book_tier` for tier filtering. The existing `class_req` column is validated during crafting (player class must match or `class_req` is null/empty). `crafting_time` is unused for now (instant crafting).

**Combine Book tiers:**

| Book Item | Purchase Cost | Level Req | Recipe Tier |
|---|---|---|---|
| `item_combine_novice` | Free (quest) | 1 | 0 |
| `item_combine_book_1` | 1,100g | 5 | 1 |
| `item_combine_book_2` | 110,000g | 20 | 2 |
| `item_combine_book_3` | 550,000g | 30 | 3 |

**Combine Books are permanent items** — not consumed on use. They unlock recipes of their tier and below. Validation: player must have a book in inventory where `bookTier >= recipe.bookTier`. A Book III unlocks all recipes (tier 0-3).

**Recipe structure:**
- `crafting_recipes`: recipe_id, recipe_name, book_tier (0-3), result_item_id, result_quantity, level_req, class_req, gold_cost
- `crafting_ingredients`: recipe_id, item_id (ingredient), quantity

**Example recipes (loaded from DB, not hardcoded):**
- Novice: 2× `mat_core_1st` + 1× `mat_leather` → Leather Shoes
- Book I: 3× `mat_core_2nd` + 1× `mat_iron_ore` → Iron Sword
- Book II: 5× `mat_core_4th` + 2× `mat_core_3rd` → Viking Sword
- Book III: 3× `mat_core_7th` + 5× `mat_core_5th` → Darksteel Sword

**Recipe cache:**
- `RecipeCache` loaded at server startup from `crafting_recipes` + `crafting_ingredients` tables
- Provides: `getRecipe(recipeId)`, `getRecipesForTier(tier)`, `getAllRecipes()`
- Each cached recipe holds: id, name, bookTier, resultItemId, resultQuantity, levelReq, classReq, goldCost, ingredients[]

**Network messages:**
- `CmdCraft` (client → server):
  - `uint16_t recipeId`
- `SvCraftResult` (server → client):
  - `uint8_t success`
  - `std::string resultItemId`
  - `uint8_t resultQuantity`
  - `std::string message`

**Server handler flow:**
1. Look up recipe from cache; reject if not found
2. Validate: player has a Combine Book in inventory with `bookTier >= recipe.bookTier`
3. Validate: player meets level requirement
4. Validate: player class matches `recipe.classReq` (if set)
5. Validate: player has all required ingredients (check quantities per ingredient)
6. Validate: player has enough gold
7. Validate: player has inventory space for result
8. WAL: log craft attempt (recipe ID, gold cost)
9. Consume all ingredients from inventory
10. Deduct gold via `setGold(currentGold - recipe.goldCost)`
11. Create result item (roll stats if equipment via ItemDefinitionCache) and add to inventory
12. Send `SvCraftResult` + `SvInventorySync`

### Files to modify
- Create: `server/cache/recipe_cache.h` — Load recipes from DB at startup
- `engine/net/protocol.h` — Add `CmdCraftMsg`, `SvCraftResultMsg`
- `engine/net/packet.h` — Add packet types
- `server/server_app.cpp` — Handle `CmdCraft`, initialize RecipeCache
- `engine/net/net_client.h/cpp` — Handle `SvCraftResult` callback
- DB migration: `ALTER TABLE crafting_recipes ADD COLUMN book_tier INTEGER DEFAULT 0`

---

## DB Migrations Required

```sql
ALTER TABLE character_inventory ADD COLUMN is_broken BOOLEAN DEFAULT FALSE;
ALTER TABLE crafting_recipes ADD COLUMN book_tier INTEGER DEFAULT 0;
```

---

## Testing Plan

| Test | Validates |
|---|---|
| Enchant +1 to +8: 100% success, no break | Safe threshold |
| Enchant +9 fails without protection: item breaks | Break mechanic |
| Enchant +9 fails with protection: item stays at +8, stone consumed | Protection stone failure |
| Enchant +9 succeeds with protection: item +9, stone consumed | Protection stone success |
| Enchant +9 succeeds without protection: item +9, no break | Normal success |
| Broken item cannot be equipped | Broken state |
| Broken item cannot be extracted | Broken extraction guard |
| Repair scroll restores broken item to random +1-8 | Repair mechanic |
| Repair consumes scroll + 50k gold | Repair resource consumption |
| +15 enchant has 0.1% success rate | Max level rate |
| Per-item maxEnchant < 15 enforced | Per-item cap |
| MAX_ENCHANT_LEVEL = 15 enforced | Global cap |
| Equipped item cannot be enchanted | Equip guard |
| Weapon damage multiplier at +15 correct (~4.97×) | Damage formula |
| Armor bonus at +15 = +29 | Armor formula |
| Gold deducted via setGold pattern | Server authority |
| WAL entry logged before enchant mutation | Crash recovery |
| Extract Common item: rejected | Extraction rarity guard |
| Extract Green Lv5 item: yields 1st Core | Core tier by level |
| Extract Blue item: yields 6th Core | Core tier by rarity |
| Extract +9 Green item: yields base + 3 bonus cores | Enchant bonus cores |
| Extraction scroll consumed, item destroyed | Resource consumption |
| Extract equipped item: rejected | Equip guard |
| Craft with insufficient ingredients: rejected | Ingredient validation |
| Craft with wrong book tier: rejected | Book tier check |
| Craft with Book III unlocks tier 0-2 recipes | Book tier >= recipe tier |
| Craft validates class_req if set | Class requirement |
| Craft success: ingredients consumed, gold deducted, result created | Full craft flow |
| Recipe cache loads from DB correctly | Cache initialization |
| CmdEnchant/SvEnchantResult round-trip serialization | Protocol |
| CmdRepair/SvRepairResult round-trip serialization | Protocol |
| CmdExtractCore/SvExtractResult round-trip serialization | Protocol |
| CmdCraft/SvCraftResult round-trip serialization | Protocol |
| isBroken persisted to DB and loaded back | DB persistence |
| isBroken synced to client via InventorySyncSlot | Client sync |
