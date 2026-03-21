# Batch B: Equipment & Economy — Design Spec

**Date:** 2026-03-21
**Scope:** Enchanting tuning to +15 with break mechanic, core extraction, combine book crafting.

---

## 1. Enchanting Tuning (+1 to +15)

### Problem
`enchant_system.h` supports +1 to +12 with 100% success for +1-8. No break mechanic exists. No safe thresholds per equipment type. No network messages — enchanting isn't wired to the server. Need TWOM-style risk/reward with a hybrid max of +15.

### Design

**Safe thresholds (100% success, no break risk):**
- Weapons (Sword, Bow, Wand): safe to **+6**
- Armor (Head, Armor, Gloves, Boots, Shield/SubWeapon): safe to **+4**

**Success rates above safe threshold:**

| Target Level | Success Rate | Gold Cost |
|---|---|---|
| +1 to safe | 100% | 100-500 (existing) |
| +7 | 70% | 5,000 |
| +8 | 50% | 10,000 |
| +9 | 30% | 25,000 |
| +10 | 15% | 50,000 |
| +11 | 8% | 100,000 |
| +12 | 5% | 200,000 |
| +13 | 2% | 500,000 |
| +14 | 0.5% | 1,000,000 |
| +15 | 0.1% | 2,000,000 |

For armor, the same rate table applies but indexed relative to the armor safe threshold (+4). So armor +5 uses the +7 row rate (70%), armor +6 uses the +8 row rate (50%), etc. In other words: rate is determined by `(targetLevel - safeThreshold)` steps above safe, both equipment types use the same curve.

**Wait — clarification needed on rate indexing:** Actually, the simpler approach: use a single rate table indexed by target enchant level. Both weapons and armor use the same rates at the same +level. The safe threshold only determines when break risk kicks in, not the success rate. So +7 weapon is 70% with break risk, +7 armor is also 70% with break risk (armor safe is +4 so break risk starts at +5). Rates below safe are overridden to 100%.

**Break mechanic:**
- Failure above safe threshold WITHOUT protection stone → item breaks:
  - `isBroken = true` on the ItemInstance
  - Item becomes unusable (cannot equip, cannot trade)
  - Item becomes soulbound (`isSoulbound = true`)
- Failure above safe threshold WITH protection stone → stone consumed, item stays at current level, no break
- Failure at or below safe threshold → impossible (100% success)

**Protection stone behavior:**
- Player marks an item with a protection stone before attempting enchant
- Protection stone is **always consumed** when used, regardless of outcome:
  - On success: stone consumed, item enchant level increases by 1
  - On failure: stone consumed, item stays at current level (no break)
- Protection stone item ID: `mat_protect_stone` (already exists)

**Repair mechanic:**
- Repair Scroll item (`item_repair_scroll`) + gold cost repairs a broken item
- Repaired item's enchant level is set to a random level between +1 and the safe threshold for its type (+6 weapons, +4 armor)
- Repair removes `isBroken` flag but item remains soulbound
- Repair gold cost: 50,000

**Enhancement stones:** Unchanged — tiered by item level as existing (`mat_enhance_stone_basic` through `mat_enhance_stone_legendary`).

**Weapon damage bonuses (extended to +15):**
- Base multiplier: `1.0 + (enchantLevel × 0.125)`
- +11 secret bonus: additional 5% multiplier
- +12 secret bonus: additional 10% multiplier (stacks)
- +15 secret bonus: additional 15% multiplier (stacks with +11, +12)
- +15 is therefore: 1.0 + 1.875 = 2.875 base × 1.05 × 1.10 × 1.15 = ~3.82× weapon damage

**Armor bonuses (extended to +15):**
- +1 to +8: +1 flat armor per level (total: +8)
- +9 to +15: +3 flat armor per level (total: +8 + 21 = +29 at +15)

**Secret stat bonuses at +15 (doubled from +12):**
- Hat: +20 Magic Resist
- Shoes: +20% Move Speed
- Armor/Chest: +20 Critical Chance
- SubWeapon/Shield: +20 Armor

**Non-enchantable slots:** Ring, Necklace, Cloak, Belt (use stat enchant system instead — unchanged).

**Network messages:**
- `CmdEnchant` (client → server):
  - `uint8_t inventorySlot` — slot of the item to enchant
  - `uint8_t useProtectionStone` — 1 if protection stone should be consumed
- `SvEnchantResult` (server → client):
  - `uint8_t success` — 1 if enchant succeeded
  - `uint8_t newLevel` — resulting enchant level
  - `uint8_t broke` — 1 if item broke
  - `std::string message` — result text for chat/UI

**Server handler flow:**
1. Validate: item exists, is enchantable, is not broken, is not at max level
2. Validate: player has required enhancement stone (by item level tier)
3. Validate: player has enough gold
4. If `useProtectionStone`: validate player has `mat_protect_stone` in inventory
5. Consume enhancement stone + gold + protection stone (if used)
6. Roll success based on target level's rate
7. If success: increment `enchantLevel`, recalculate equipment stats
8. If failure above safe threshold:
   - With protection: item stays at current level
   - Without protection: item breaks (`isBroken = true`, `isSoulbound = true`)
9. Send `SvEnchantResult` + `sendPlayerState()` + `SvInventorySync`

### Files to modify
- `game/shared/enchant_system.h` — Rewrite rates, add safe thresholds, break logic, repair logic
- `game/shared/item_instance.h` — Add `isBroken` field
- `game/shared/game_types.h` — Update `MAX_ENCHANT_LEVEL` to 15
- `engine/net/protocol.h` — Add `CmdEnchantMsg`, `SvEnchantResultMsg`
- `engine/net/packet.h` — Add packet type constants
- `server/server_app.cpp` — Handle `CmdEnchant`
- `engine/net/net_client.h/cpp` — Handle `SvEnchantResult` callback

---

## 2. Core Extraction System

### Problem
No way to break down unwanted equipment into crafting currency. Need TWOM-style core extraction: destroy a colored-name item using a Magic Extraction Scroll to obtain Cores.

### Design

**Extraction rules:**
- Requires: Magic Extraction Scroll (`item_extraction_scroll`) + a non-Common rarity item
- Common (black name) items: cannot be extracted
- Broken items: cannot be extracted
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

**Network messages:**
- `CmdExtractCore` (client → server):
  - `uint8_t itemSlot` — slot of the item to extract
  - `uint8_t scrollSlot` — slot of the extraction scroll
- `SvExtractResult` (server → client):
  - `uint8_t success`
  - `std::string coreItemId` — which core was produced
  - `uint8_t coreQuantity`
  - `std::string message`

**Server handler flow:**
1. Validate: item exists, is not Common rarity, is not broken
2. Validate: scroll exists and is `item_extraction_scroll`
3. Determine core tier from item rarity + level
4. Calculate bonus cores from enchant level
5. Remove item from inventory
6. Remove scroll from inventory (consume)
7. Add core item(s) to inventory
8. Send `SvExtractResult` + `SvInventorySync`

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

**Combine Book tiers:**

| Book Item | Purchase Cost | Level Req | Recipe Tier |
|---|---|---|---|
| `item_combine_novice` | Free (quest) | 1 | 0 |
| `item_combine_book_1` | 1,100g | 5 | 1 |
| `item_combine_book_2` | 110,000g | 20 | 2 |
| `item_combine_book_3` | 550,000g | 30 | 3 |

**Combine Books are permanent items** — not consumed on use. They unlock recipes of their tier and below.

**Recipe structure (DB schema already exists):**
- `crafting_recipes`: recipe_id, recipe_name, book_tier (0-3), result_item_id, result_quantity, level_req, gold_cost
- `crafting_ingredients`: recipe_id, item_id (ingredient), quantity

**Example recipes (loaded from DB, not hardcoded):**
- Novice: 2× `mat_core_1st` + 1× `mat_leather` → Leather Shoes
- Book I: 3× `mat_core_2nd` + 1× `mat_iron_ore` → Iron Sword
- Book II: 5× `mat_core_4th` + 2× `mat_core_3rd` → Viking Sword
- Book III: 3× `mat_core_7th` + 5× `mat_core_5th` → Darksteel Sword

**Recipe cache:**
- `RecipeCache` loaded at server startup from `crafting_recipes` + `crafting_ingredients` tables
- Provides: `getRecipe(recipeId)`, `getRecipesForTier(tier)`, `getAllRecipes()`
- Each cached recipe holds: id, name, tier, resultItemId, resultQuantity, levelReq, goldCost, ingredients[]

**Network messages:**
- `CmdCraft` (client → server):
  - `uint16_t recipeId`
- `SvCraftResult` (server → client):
  - `uint8_t success`
  - `std::string resultItemId`
  - `uint8_t resultQuantity`
  - `std::string message`

**Server handler flow:**
1. Look up recipe from cache
2. Validate: player has a Combine Book of sufficient tier in inventory
3. Validate: player meets level requirement
4. Validate: player has all required ingredients (check quantities)
5. Validate: player has enough gold
6. Validate: player has inventory space for result
7. Consume all ingredients from inventory
8. Deduct gold (server-authoritative via `setGold`)
9. Create result item (roll stats if equipment) and add to inventory
10. Send `SvCraftResult` + `SvInventorySync`

### Files to modify
- Create: `server/cache/recipe_cache.h` — Load recipes from DB at startup
- `engine/net/protocol.h` — Add `CmdCraftMsg`, `SvCraftResultMsg`
- `engine/net/packet.h` — Add packet types
- `server/server_app.cpp` — Handle `CmdCraft`, initialize RecipeCache
- `engine/net/net_client.h/cpp` — Handle `SvCraftResult` callback

---

## Testing Plan

| Test | Validates |
|---|---|
| Enchant +1 to +6 weapon: 100% success, no break | Safe threshold weapons |
| Enchant +1 to +4 armor: 100% success, no break | Safe threshold armor |
| Enchant +7 weapon fails without protection: item breaks | Break mechanic |
| Enchant +7 weapon fails with protection: item stays, stone consumed | Protection stone failure |
| Enchant +7 weapon succeeds with protection: item +8, stone consumed | Protection stone success |
| Enchant +7 weapon succeeds without protection: item +8, no break | Normal success |
| Broken item cannot be equipped | Broken state |
| Repair scroll restores broken weapon to random +1-6 | Repair mechanic |
| Repair scroll restores broken armor to random +1-4 | Repair armor |
| +15 enchant has 0.1% success rate | Max level rate |
| MAX_ENCHANT_LEVEL = 15 enforced | Level cap |
| Weapon damage multiplier at +15 correct (3.82×) | Damage formula |
| Armor bonus at +15 = +29 | Armor formula |
| Extract Common item: rejected | Extraction rarity guard |
| Extract Green Lv5 item: yields 1st Core | Core tier by level |
| Extract Blue item: yields 6th Core | Core tier by rarity |
| Extract +9 Green item: yields base + 3 bonus cores | Enchant bonus cores |
| Extraction scroll consumed, item destroyed | Resource consumption |
| Craft with insufficient ingredients: rejected | Ingredient validation |
| Craft with wrong book tier: rejected | Book tier check |
| Craft success: ingredients consumed, gold deducted, result created | Full craft flow |
| Recipe cache loads from DB correctly | Cache initialization |
| CmdEnchant/SvEnchantResult round-trip serialization | Protocol |
| CmdExtractCore/SvExtractResult round-trip serialization | Protocol |
| CmdCraft/SvCraftResult round-trip serialization | Protocol |
