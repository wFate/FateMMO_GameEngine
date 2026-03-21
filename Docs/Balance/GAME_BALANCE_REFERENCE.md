# FateMMO Game Balance Reference

> Authoritative balance constants and formulas for the C++ engine. Verified against Unity prototype docs (January 2026) and current `game/shared/` source code (March 2026). All formulas confirmed matching between engines.

---

## 1. XP & Leveling

### XP to Next Level
**Source:** `game/shared/game_types.h:395-398`
```
xpToNextLevel = max(100, round(0.35 × level^5.1))
```

| Level | XP Required | Est. Hours | Cumulative |
|------:|------------:|-----------:|-----------:|
| 1→2 | 100 | ~1 min | ~1 min |
| 10→11 | 44,063 | ~42 min | ~3 hrs |
| 20→21 | 1,515,833 | ~7 hrs | ~30 hrs |
| 30→31 | 11,903,075 | ~28 hrs | ~200 hrs |
| 40→41 | 51,663,378 | ~80 hrs | ~650 hrs |
| 50→51 | 161,119,702 | ~179 hrs | ~1,600 hrs |
| 60→61 | 410,823,027 | ~326 hrs | ~3,400 hrs |
| 69→70 | 836,875,639 | ~517 hrs | ~5,800 hrs |

**Total to max level:** ~5,800 hours (~242 days continuous)

### XP Reward Multiplier (mob-to-player level diff)
**Source:** `game/shared/xp_calculator.h`

| Level Diff | Color | Multiplier |
|------------|-------|-----------|
| ≤ -3 | Gray | 0.00x |
| -2 | Green | 0.25x |
| -1 | Green | 0.50x |
| 0 | White | 1.00x |
| +1 | Blue | 1.10x |
| +2 | Purple | 1.15x |
| +3 | Orange | 1.20x |
| ≥ +4 | Red | 1.30x |

### XP Modifiers
| Modifier | Value | Source |
|----------|-------|--------|
| Party bonus | +10% per additional member | `xp_calculator.h` |
| Guild bonus | +10% for all guild members | `guild_manager.cpp` |

### PvE Death XP Loss
**Source:** `game/shared/character_stats.cpp:294-302`

| Player Level | XP Loss % |
|-------------|-----------|
| < 10 | 10% |
| 10-19 | 5% |
| 20-29 | 1% |
| 30-39 | 0.5% |
| 40-49 | 0.25% |
| 50-59 | 0.1% |
| ≥ 60 | 0.05% |

---

## 2. Class Definitions & Stat Growth

### Base Stats by Class
**Source:** `game/shared/game_types.h:26-66`

| Stat | Warrior | Mage | Archer |
|------|--------:|-----:|-------:|
| Base HP | 120 | 120 | 120 |
| HP/Level | 8.0 | 8.0 | 8.0 |
| Base MP | 40 | 40 | 40 |
| MP/Level | 3.0 | 3.0 | 3.0 |
| Base STR | 12 | 8 | 8 |
| STR/Level | 1.5 | 0.5 | 0.5 |
| Base INT | 8 | 12 | 8 |
| INT/Level | 0.5 | 1.5 | 0.5 |
| Base DEX | 4 | 4 | 12 |
| DEX/Level | 0.8 | 0.8 | 1.5 |
| Base VIT | 12 | 8 | 8 |
| VIT/Level | 1.5 | 0.5 | 0.5 |
| Base WIS | 8 | 12 | 8 |
| WIS/Level | 0.5 | 1.5 | 0.5 |
| Base Hit Rate | 4.0 | 4.0 | 4.0 |
| Primary Resource | Fury | Mana | Fury |
| Fury Base | 3 | 3 | 3 |
| Fury +1 every | 10 levels | 10 levels | 10 levels |

### Derived Stat Formulas
**Source:** `game/shared/character_stats.cpp:23-108`

**HP:**
```
baseHP = round(baseMaxHP + hpPerLevel × (level - 1))
vitalityMultiplier = 1.0 + (bonusVitality × 0.01)
maxHP = round(baseHP × vitalityMultiplier) + equipBonusHP + passiveHPBonus
```

**Armor:**
```
armor = round(bonusVitality × 0.25) + equipBonusArmor
```

**Hit Rate (class-specific):**
- Warrior: `baseHitRate + (STR × 0.05) + equipBonusAccuracy`
- Archer: `baseHitRate + (DEX × 0.1) + equipBonusAccuracy`
- Mage: `0.0` (no hit rate — uses spell resist system instead)

**Evasion:**
- Archer: `bonusDexterity × 0.5 + equipBonusEvasion`
- Others: `equipBonusEvasion` only

**Crit Rate:**
```
baseCrit = 0.05 (5%)
archerBonus = bonusDexterity × 0.005 (Archer only)
totalCrit = baseCrit + archerBonus + equipBonus + passiveBonus
```

**Class Damage Multiplier:**
- Warrior: `1.0 + (bonusSTR × 0.02)`
- Mage: `1.0 + (bonusINT × 0.02)`
- Archer: `1.0 + (bonusDEX × 0.02)`

**Speed:**
```
speed = (1.0 + equipBonusMoveSpeed) × (1.0 + passiveSpeedBonus)
```

---

## 3. Damage Calculation

### Player Damage Formula
**Source:** `game/shared/character_stats.cpp:157-198`

```
levelBaseMin = 2 + floor((level - 1) × 0.5)
levelBaseMax = 4 + floor((level - 1) × 1.0)

statBonus (class-specific):
  Warrior: floor(STR × 0.3)
  Archer:  floor(DEX × 0.4)
  Mage:    floor(INT × 0.3)

totalMin = levelBaseMin + statBonus + weaponDamageMin
totalMax = levelBaseMax + statBonus + weaponDamageMax

damage = randomRange(totalMin, totalMax)
damage = round(damage × classDamageMultiplier)

if (crit):
  damage = round(damage × (1.95 + equipBonusCritDamage))

return max(1, damage)
```

### Armor Reduction
**Source:** `game/shared/character_stats.cpp:206-208`
```
reductionPercent = min(75, armor × 0.5) / 100.0
actualDamage = max(1, round(damage × (1.0 - reductionPercent)))
```

### Weapon Progression (from DB)
| Level | Tier | Damage Range | Avg Crit Burst |
|------:|------|------------:|--------------:|
| 1 | Basic | 4-8 | 12-15 |
| 10 | Early | 25-35 | 79-144 |
| 20 | Common | 90-130 | 264-400 |
| 30 | Uncommon | 350-450 | 1,200-1,584 |
| 40 | Rare | 700-900 | 2,888-3,776 |
| 50 | Epic | 1,200-1,500 | 5,639-7,143 |
| 60 | Legendary | 2,200-2,700 | 11,735-14,533 |
| 70 | Mythic | 3,800-4,700 | 22,709-28,256 |

### Lv64 Ultra-Rare Exception (by design)
Lv64 ultra-rares are intentionally STRONGER than Lv70 standard weapons (0.1% drop rate):
- Voidrend Sword: 3850-4750 (vs Fate's Edge: 2800-3500)
- Entropy Incarnate Wand: 3780-4680 (vs Reality Shatter: 2700-3400)
- Reality Splitter Bow: 3820-4720 (vs Fate's Arrow: 2750-3450)

---

## 4. Combat System Config

### Hit Rate System
**Source:** `game/shared/combat_system.h:15-60`

| Constant | Value |
|----------|-------|
| hitRatePerLevelRequired | 2.0 |
| hitChanceLowerLevel | 0.97 (97%) |
| hitChanceSameLevel | 0.95 (95%) |
| penaltyPerLevelWithinCoverage | 0.05 (5%) |
| beyondCoverageHitChances | [50%, 30%, 15%, 5%, 0%] |
| maxEvasionReduction | 0.15 (15%) |
| evasionCounteredPerHitRate | 2.0 |

### Damage Reduction (level difference)
| Constant | Value |
|----------|-------|
| damageReductionPerLevel | 0.12 (12%) |
| maxDamageReduction | 0.90 (90%) |
| damageReductionStartsAt | 2 (starts at +3 level diff) |

### Spell Resist (Mage targeting)
| Constant | Value |
|----------|-------|
| spellResistFreeLevels | 3 |
| baseIntForResist | 16 |
| intPerLevelCovered | 9.0 |
| resistChanceAtExactCoverage | 0.05 (5%) |
| resistPenaltyPerMissingInt | 0.03 (3%) |
| maxSpellResistChance | 0.95 (95%) |
| minSpellResistChance | 0.01 (1%) |

### Magic Resist
| Constant | Value |
|----------|-------|
| playerMagicResistPerPoint | 0.005 (0.5%) |
| mobMagicResistPerPoint | 0.001 (0.1%) |
| maxPlayerMagicResist | 0.75 (75%) |
| maxMobMagicResist | 0.50 (50%) |

### PvP
| Constant | Value |
|----------|-------|
| pvpDamageMultiplier | 0.05 (5% of PvE damage) |
| mageVsWarriorArmorBypass | 0.75 (75% armor bypass) |

### Block System
- Uses higher of attacker's STR/DEX as counter stat
- blockCounterPerStat: 0.005 (each 200 STR/DEX = 1% block reduction)
- Mages bypass block entirely

---

## 5. Honor System

### Honor Constants
**Source:** `game/shared/honor_system.h/cpp`

| Constant | Value |
|----------|-------|
| MAX_HONOR | 1,000,000 |
| HONOR_LOSS_ON_DEATH | 30 |
| MAX_KILLS_PER_HOUR | 5 (per victim) |

### Honor Gain Table
| Attacker \ Victim | White | Purple | Red | Black |
|-------------------|------:|-------:|----:|------:|
| White/Purple | 10 | 15 | 30 | 20 |
| Red | 5 | 10 | 40 | 20 |
| Black | 0 | 0 | 0 | 0 |

### PK Status Durations
**Source:** `game/shared/game_types.h:378-382`

| Status | Duration | Trigger |
|--------|----------|---------|
| White | Permanent | Default innocent |
| Purple | 1 minute | Attacked another player |
| Red | 30 minutes | Killed another player |
| Black | 10 minutes | Excessive PKing |

---

## 6. Item & Loot System

### Stat Rolling
**Source:** `game/shared/item_stat_roller.cpp`

- **Weighted decay factor:** 0.65 (exponential bias toward minimum values)
- All stats always roll (no random stat count)
- Each stat rolls independently within DB-defined min/max range

**Rarity-based weighting:**
| Rarity | Weighted Stats | Chase Stats (crit, hitrate, move_speed) |
|--------|----------------|----------------------------------------|
| Common | ALL weighted | N/A |
| Rare | ALL weighted | N/A |
| Epic | Only chase stats weighted | Yes |
| Legendary | Only chase stats weighted | Yes |

### Socket Gem Value Distribution
**Source:** `game/shared/item_stat_roller.cpp:76-107`

| Value | Probability |
|------:|----------:|
| +1 | 25% |
| +2 | 20% |
| +3 | 17% |
| +4 | 13% |
| +5 | 10% |
| +6 | 7% |
| +7 | 4% |
| +8 | 2.5% |
| +9 | 1% |
| +10 | 0.5% |

### Primary Stat Caps
| Stat | Normal Cap | Legendary Boot Exception |
|------|-----------|------------------------|
| STR/INT/DEX/VIT | 12 | 18 |

### Drop Rate Guidelines (DB-driven)

**Boss weapon drops:**
| Boss Tier | Drop Rate |
|-----------|----------|
| End-game (Lv50+) | 0.5% |
| Late (Lv45) | 1% |
| Mid-Late (Lv40) | 2% |
| Mid (Lv30-35) | 3% |
| Early-Mid (Lv20) | 4% |
| Early (Lv10) | 5% |
| Miniboss | 6% |
| Regular mob | 0.5% |

**Skill book drops:**
| Rank | Regular Mob | Miniboss | Boss |
|------|-----------|----------|------|
| I | 1% | 5% | — |
| II | 0.5% | 2% | — |
| III | — | — | 0.5-1% |

---

## 7. Economy Constants

### Inventory & Trade
**Source:** `game/shared/game_types.h:326-340`

| Constant | Value |
|----------|-------|
| BASE_INVENTORY_SLOTS | 15 |
| MAX_BAG_SLOTS | 20 |
| MAX_GOLD | 999,999,999 |
| MAX_STACK_SIZE | 9,999 |
| MAX_TRADE_SLOTS | 8 |
| TRADE_INVITE_TIMEOUT | 30 seconds |
| MAX_TRADE_DISTANCE | 5.0 units |

### Market
| Constant | Value |
|----------|-------|
| MAX_LISTINGS_PER_PLAYER | 7 |
| LISTING_DURATION | 48 hours |
| TAX_RATE | 2% |
| JACKPOT_INTERVAL | 2 hours |
| MAX_LISTING_PRICE | 999,999,999 gold |

### Enhancement
| Constant | Value |
|----------|-------|
| MAX_ENCHANT_LEVEL | 12 |
| RISKY_ENCHANT_START | 9 (above +8) |

---

## 8. Guild System

### Constants
**Source:** `game/shared/game_types.h:366-373`

| Constant | Value |
|----------|-------|
| Creation cost | 100,000 gold |
| Max members | 20 (expandable) |
| Max officers | 5 |
| Symbol size | 16×16 pixels |
| Symbol storage | 256 bytes |
| Palette colors | 16 (indices 0-15) |
| XP bonus | +10% |

### Guild XP Formula
```
XP for Level N = 50,000 × 1.15^(N-1)
```

### Guild Level Milestones
| Level | XP Required | Cumulative | Est. Time* |
|------:|------------|-----------|-----------|
| 5 | 87,450 | 337,000 | 2 days |
| 10 | 175,800 | 1,017,000 | 1 week |
| 25 | 1,627,000 | 11,820,000 | 2 months |
| 50 | 53,940,000 | 378,000,000 | 2 years |
| 75 | 1,788,000,000 | 12,700,000,000 | 60+ years |
| 100 | 59,300,000,000 | 424,000,000,000 | Legendary |

*Based on 20 active members earning ~100k XP/day each

### Symbol Palette
| Index | Color | Hex |
|------:|-------|-----|
| 0 | Transparent | — |
| 1 | White | #FFFFFF |
| 2 | Black | #000000 |
| 3 | Red | #FF0000 |
| 4 | Dark Red | #8B0000 |
| 5 | Orange | #FF8C00 |
| 6 | Yellow | #FFD700 |
| 7 | Green | #00FF00 |
| 8 | Dark Green | #006400 |
| 9 | Blue | #0000FF |
| 10 | Dark Blue | #00008B |
| 11 | Cyan | #00FFFF |
| 12 | Purple | #800080 |
| 13 | Pink | #FF69B4 |
| 14 | Brown | #8B4513 |
| 15 | Gray | #808080 |

---

## 9. Chat System

| Constant | Value |
|----------|-------|
| MAX_MESSAGE_LENGTH | 200 chars |
| RATE_LIMIT | 0.5 seconds |
| MAX_HISTORY | 100 messages |
| PROXIMITY_RANGE | 15.0 units |
| Channels | Map, Global, Trade, Party, Guild, Private |

---

## Verification Status

All formulas verified matching between Unity prototype (C#/Mirror) and C++ engine (March 2026):

| System | Status | Notes |
|--------|--------|-------|
| XP formula | **Match** | `0.35 × level^5.1`, 100 floor |
| Level diff XP scaling | **Match** | Identical multiplier table |
| Class base stats | **Match** | All three classes identical |
| Damage formula | **Match** | Base damage, stat bonus, class multiplier, crit |
| Armor reduction | **Match** | 0.5% per point, 75% cap |
| Hit rate system | **Match** | All 5 config values identical |
| Spell resist | **Match** | INT-based, 7 config values |
| PvP damage | **Match** | 5% multiplier |
| Honor system | **Match** | Gain table, loss, kill tracking |
| Crit system | **Match** | 5% base, 1.95x, DEX scaling |
| Item stat rolling | **Match** | 0.65 decay, rarity weighting |
| Guild constants | **Match** | 100k creation, 20 members, 16×16 symbol |
| Economy constants | **Match** | All inventory/trade/market values |
| Enchantment | **Match** | Max +12, risky at +9 |

**Drop rates** are DB-driven (not hardcoded) — the percentages in this doc are design guidelines for `loot_tables` DB entries.
