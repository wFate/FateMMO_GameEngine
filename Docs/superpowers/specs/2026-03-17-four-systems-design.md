# Design Spec: Faction System, Pet System, Mage Double-Cast, Stat Enchant Scrolls

**Date:** 2026-03-17
**Branch:** feat/reflection-serialization (or new branch TBD)
**Status:** Approved design

---

## 1. Faction System

### Overview

4 permanent factions chosen at character creation. All factions are hostile to each other. Cross-faction public chat is garbled.

### Faction Definitions

```cpp
enum class Faction : uint8_t {
    None   = 0,
    Xyros  = 1,
    Fenor  = 2,
    Zethos = 3,
    Solis  = 4
};
```

Each faction has a `FactionDefinition` struct:
- `Faction faction` — enum value
- `std::string displayName` — e.g., "Xyros"
- `Color color` — nameplate/UI color
- `std::string homeVillageId` — starting village zone ID
- `std::string factionMerchantNPCId` — faction-restricted merchant

### Core Rules

1. **Cannot damage same-faction players.** The combat layer checks attacker/victim factions before any combat processing. Same-faction attacks are rejected before `PKSystem` is ever invoked. `PKSystem` remains pure status-transition logic with no faction awareness.
2. **Cross-faction chat is garbled.** All Chat and proximity chat from other factions is replaced with deterministic gibberish (seeded from message hash for consistency). Party, Guild, Whisper, and System channels are readable regardless of faction. `ChatMessage` gains a `Faction senderFaction` field. Garbling happens server-side: the server sends garbled payloads to cross-faction recipients, readable payloads to same-faction recipients.
3. **Faction is permanent.** Set once at character creation, stored on a dedicated `FactionComponent` on the player entity (not on `CharacterStats` — keeps the ECS clean, consistent with the dedicated `PetComponent` pattern). Never changes.
4. **Home village PvP exception.** Enemy faction players inside your faction's village can be attacked freely without PK penalty. The combat layer checks the attacker's current zone ID against their faction's `homeVillageId` before calling `PKSystem::processPvPKill()`. If in home village and target is enemy faction, `attackerPenalized` is overridden to false. Zone detection uses the existing `ZoneComponent` — each zone has an ID that can be matched against `FactionDefinition::homeVillageId`.

### Chat Garbling

Each character in the original message maps to a random character from a fixed garble alphabet. The substitution is deterministic per-message — seeded from a hash of the message content — so the same message always produces the same garbled output. Note: garbled output length matches the original, so message length is visible to cross-faction readers.

### Integration Points

- **Combat layer** (e.g., `CombatActionSystem` or equivalent) — faction check before any PvP processing; same faction = attack rejected. Home village zone check before calling `processPvPKill()` to override penalty. `PKSystem` and `CombatSystem` are NOT modified — they remain pure logic.
- `ChatManager` — garble pass using `ChatMessage::senderFaction` for cross-faction messages in public channels
- `FactionComponent` — new ECS component holding `Faction faction` field
- Nameplate rendering — color-coded by faction
- Faction merchants — NPCs restricted to selling only to their faction members
- `EntityFactory::createPlayer()` — faction parameter at creation time

---

## 2. Pet System

### Overview

Tradable pets with their own leveling system. Rarity-tiered base stats and growth rates. Provide stat bonuses (HP, Crit Rate, XP bonus) and auto-loot capability.

### Pet Data Model

```cpp
struct PetDefinition {
    std::string petId;
    std::string displayName;
    ItemRarity rarity;            // Common through Legendary

    // Base stats at pet level 1
    int baseHP           = 0;
    float baseCritRate   = 0.0f;
    float baseExpBonus   = 0.0f;  // percentage, e.g., 0.02 = +2% XP

    // Per-level growth
    float hpPerLevel       = 0.0f;
    float critPerLevel     = 0.0f;
    float expBonusPerLevel = 0.0f;

    // Higher rarity = higher base stats AND higher per-level growth
};

struct PetInstance {
    std::string instanceId;         // UUID
    std::string petDefinitionId;    // references PetDefinition
    std::string petName;            // player-renamable
    int level            = 1;       // max level = player level cap (50)
    int64_t currentXP    = 0;
    int64_t xpToNextLevel = 100;
    bool autoLootEnabled = false;
    bool isSoulbound     = false;   // false by default — pets are tradable
};
// Note: rarity is NOT stored on PetInstance — always looked up from PetDefinition
// via petDefinitionId to avoid data divergence.
```

### Pet Leveling

- Pets gain XP when the player gains XP (pet receives a percentage of player XP, e.g., 50%).
- Pet level cap tied to player level — pet cannot outlevel its owner.
- No separate feeding mechanic. Passive XP sharing only.
- Higher rarity pets have higher base `expBonus` and `expBonusPerLevel`, making them more valuable over time.

### Stat Bonuses

At any given pet level, the effective stats are:
- `effectiveHP = baseHP + hpPerLevel * (petLevel - 1)`
- `effectiveCritRate = baseCritRate + critPerLevel * (petLevel - 1)`
- `effectiveExpBonus = baseExpBonus + expBonusPerLevel * (petLevel - 1)`

**Stat application order:** When `recalculateStats()` runs: (1) `clearEquipmentBonuses()` zeros all `equipBonus*` fields, (2) equipment system writes gear bonuses to `equipBonus*`, (3) pet system writes pet bonuses to the same `equipBonus*` fields (additive), (4) `recalculateStats()` computes derived stats from the totals.

**XP bonus application:** The caller multiplies XP by the pet's `effectiveExpBonus` *before* calling `CharacterStats::addXP()`. This avoids adding a pet dependency to CharacterStats. Example: `stats.addXP(static_cast<int64_t>(baseXP * (1.0f + petExpBonus)));`

### Auto-Loot

- When `autoLootEnabled = true` and a pet is equipped, items dropped within a configurable radius of the player are automatically picked up into inventory.
- If inventory is full, auto-loot silently stops — items remain on the ground.
- Without a pet (or with auto-loot disabled), players must manually walk over and interact with dropped items.

### Pet Slot

One dedicated pet slot on the character. Options:
- New `EquipmentSlot::Pet` value, or
- A dedicated `PetComponent` on the player entity holding a `PetInstance`

Recommendation: Dedicated `PetComponent` since `PetInstance` has a different shape than `ItemInstance` and doesn't fit the equipment pipeline directly.

### Acquisition

- **Boss/mini-boss drops:** Low drop rate, rarity weighted (Common most likely, Legendary extremely rare).
- **Premium shop:** Guaranteed rarity tiers (e.g., premium egg guarantees Rare or better).

### Trading

Pets are tradable via the existing trade system. Pet level and XP transfer with the pet. `isSoulbound` can be set to true for event/promotional pets that shouldn't be traded.

---

## 3. Mage Double-Cast Mechanic

### Overview

A hidden spell chaining system. Certain mage spells open a brief window where the next spell cast is instant. No UI indicators — players discover this through experimentation.

### Data Model

Addition to `SkillDefinition`:
```cpp
float castTime          = 0.0f;    // seconds to cast (0 = instant). NEW FIELD — required prerequisite.
bool enablesDoubleCast  = false;   // casting this opens the window
float doubleCastWindow  = 2.0f;    // seconds before window expires
```

Note: `castTime` does not currently exist on `SkillDefinition`. It must be added as a prerequisite for this mechanic to work — double-cast sets `castTime` to 0 for the follow-up. Skills with `castTime = 0` already (instant casts) are unaffected by the double-cast window since they are already instant.

Caster state on `SkillManager` (transient server-only state, excluded from serialization/persistence):
```cpp
bool doubleCastReady          = false;
float doubleCastExpireTime    = 0.0f;
std::string doubleCastSourceSkillId;  // which skill opened the window
```

### Flow

1. Mage casts a double-cast-enabled spell (e.g., Flare). Spell executes normally — damage, mana cost, and cooldown all apply.
2. After the spell lands, `doubleCastReady = true` and the expire timer starts (`doubleCastExpireTime = currentTime + doubleCastWindow`).
3. If the mage casts any other spell within the window, that spell is **instant cast** (zero cast time). Mana cost and cooldown still apply normally to the follow-up spell.
4. After the follow-up fires (or the window expires unused), `doubleCastReady = false`.
5. Only one follow-up per trigger — no chaining double-casts into infinite combos.

### No UI Indicators

This is intentionally hidden. No glowing buttons, no "double cast ready!" text, no visual cue. The only feedback is that the follow-up spell fires noticeably faster. Players discover it through experimentation and share knowledge socially.

### Balance Levers

- **Which spells get the flag** — per-skill data, not code changes. Easy to add/remove.
- **Window duration** — per-skill, so some openers can give tighter/wider windows.
- **Follow-up still costs mana and triggers its own cooldown** — it's a tempo advantage, not free damage.

### Integration

- Skill execution logic checks `doubleCastReady` before applying cast time. If ready, cast time = 0.
- `SkillManager::tick()` checks `doubleCastExpireTime` and resets the flag on expiry.
- **CC interaction:** If the mage is stunned/rooted/silenced during the double-cast window, the window continues ticking down normally. CC does not freeze or extend the window — it simply expires if the mage cannot act within the timeframe. The existing `CrowdControlSystem` blocks spell casts during CC as usual.
- Class restriction: only `ClassType::Mage` skills should have `enablesDoubleCast = true` in data, but the system code is class-agnostic (future-proof if another class ever needs it).

---

## 4. Stat Enchant Scrolls

### Overview

Accessory-only enchanting system. Consumable scrolls with random outcome tiers (+0 through +5). New enchant replaces previous. No break risk. Drops from mobs and bosses.

### Scroll Types

7 scroll item types, each targeting one stat:
- `scroll_enchant_str`, `scroll_enchant_int`, `scroll_enchant_dex`, `scroll_enchant_vit`, `scroll_enchant_wis`
- `scroll_enchant_hp`, `scroll_enchant_mp`

These are regular `ItemInstance` entries. Stackable, tradable, consumed on use.

### Eligible Equipment Slots

Only the 4 accessory slots: **Belt, Ring, Necklace, Cloak.**

This complements the existing `EnchantSystem` which explicitly excludes these slots. Weapon/armor enchanting uses `EnchantSystem`; accessory enchanting uses `StatEnchantSystem`.

### Outcome Tiers & Probabilities

| Outcome | Primary Stats (STR/INT/DEX/VIT/WIS) | HP/MP Scrolls | Probability |
|---------|--------------------------------------|---------------|-------------|
| Fail    | +0 (enchant removed)                 | +0            | 25%         |
| Tier 1  | +1                                   | +10           | 30%         |
| Tier 2  | +2                                   | +20           | 25%         |
| Tier 3  | +3                                   | +30           | 12%         |
| Tier 4  | +4                                   | +40           | 6%          |
| Tier 5  | +5                                   | +50           | 2%          |

### Rules

1. **New enchant replaces previous.** Only one stat enchant per accessory at a time. No stacking.
2. **No break risk.** The item is never destroyed or damaged.
3. **Fail = enchant removed.** Rolling a fail resets the accessory to +0 (no stat enchant). This creates tension since you can lose a good existing roll.
4. **Scroll consumed on use** regardless of outcome.

### Stat Mapping

Scroll types map to `StatType` enum values:
- `scroll_enchant_str` → `StatType::Strength`
- `scroll_enchant_int` → `StatType::Intelligence`
- `scroll_enchant_dex` → `StatType::Dexterity`
- `scroll_enchant_vit` → `StatType::Vitality`
- `scroll_enchant_wis` → `StatType::Wisdom`
- `scroll_enchant_hp` → `StatType::MaxHealth` (feeds into `equipBonusHP` — flat additive after vitality multiplier)
- `scroll_enchant_mp` → `StatType::MaxMana` (feeds into `equipBonusMP`)

### Gold Cost

No gold cost to use. The scroll itself is the cost (consumed on use). Scrolls drop from mobs/bosses and are tradable, so their value is market-driven.

### Data Model

Addition to `ItemInstance`:
```cpp
StatType statEnchantType  = StatType::Strength;  // which stat is enchanted
int statEnchantValue      = 0;                     // 0 = no enchant active
```

### StatEnchantSystem

New static utility class (mirrors `EnchantSystem` pattern):

```cpp
class StatEnchantSystem {
public:
    StatEnchantSystem() = delete;

    static bool canStatEnchant(EquipmentSlot slot);
    // Returns true only for Belt, Ring, Necklace, Cloak

    static int rollStatEnchant();
    // Returns outcome tier 0-5 based on probability table

    static int getStatValue(StatType scrollType, int tier);
    // Primary stats: returns tier directly (0-5)
    // HP/MP: returns tier * 10 (0-50)

    static void applyStatEnchant(ItemInstance& item, StatType type, int tier);
    // Sets statEnchantType and statEnchantValue on the item
};
```

### Integration

- `CharacterStats::recalculateStats()` — reads `statEnchantType` and `statEnchantValue` from equipped accessories and adds to the corresponding `equipBonus*` fields.
- Loot tables — scrolls added to existing drop tables via `lootTableId` on `EnemyStats`.
- Inventory UI — shows current stat enchant on accessory tooltips.

---

## File Impact Summary

### New Files
- `game/shared/faction.h` — Faction enum, FactionDefinition, faction utility functions, chat garbler
- `game/shared/pet_system.h` / `.cpp` — PetDefinition, PetInstance, pet leveling, stat calculation
- `game/shared/stat_enchant_system.h` — StatEnchantSystem static utility
- `game/components/pet_component.h` — PetComponent for ECS (holds PetInstance)
- `game/components/faction_component.h` — FactionComponent for ECS (holds Faction enum)
- `tests/test_faction.cpp` — Faction system tests
- `tests/test_pet_system.cpp` — Pet system tests
- `tests/test_double_cast.cpp` — Double-cast mechanic tests
- `tests/test_stat_enchant.cpp` — Stat enchant scroll tests

### Modified Files
- `game/shared/game_types.h` — Faction enum import or forward declaration
- `game/shared/character_stats.h/.cpp` — pet stat integration in recalculateStats() (pet writes to equipBonus* fields)
- `game/shared/skill_manager.h/.cpp` — double-cast state fields, castTime field on SkillDefinition, enablesDoubleCast/doubleCastWindow fields, tick() expiry logic
- `game/shared/item_instance.h` — statEnchantType/statEnchantValue fields
- `game/shared/chat_manager.h/.cpp` — senderFaction field on ChatMessage, cross-faction garble pass
- `game/components/game_components.h` — register PetComponent, FactionComponent
- `game/entity_factory.h` — faction parameter on createPlayer, pet attachment helpers
- `game/register_components.h` — register new components with meta registry

### NOT Modified (intentionally)
- `game/shared/pk_system.h/.cpp` — remains pure status-transition logic. Faction/zone checks happen at the combat layer before PKSystem is called.
- `game/shared/combat_system.h/.cpp` — remains a stateless math utility. Faction-based attack validation happens at a higher layer (combat action system).
