# FateMMO Damage Progression Analysis

Expected player damage output for Warrior, Mage, and Archer from **Level 1 to 70**.
Damage uses a linear growth curve tuned for 30-minute Raid Boss fights.

> **Data Sources:**
> - Weapon Damage: `item_definitions` table (fate_engine_dev, 86 weapons verified March 2026)
> - Stat Scaling: `game/shared/character_stats.cpp`
> - Combat Config: `game/shared/combat_system.h`

---

## Damage Formula (Linear Scaling)

```
Final Damage = (WeaponDamage + BaseDamage) × StatMultiplier × CritMultiplier

Where:
  WeaponDamage   = Random between weapon's damage_min and damage_max
  BaseDamage     = Min: 2 + floor((Level-1) × 0.5)
                   Max: 4 + floor((Level-1) × 1.0)
  PrimaryStat    = 15 + floor(Level × 3.3)
  StatMultiplier = 1.0 + (PrimaryStat × 0.02)
  CritMultiplier = 1.95x (base, plus equipment bonus)
```

**Source:** `character_stats.cpp:157-198`

### Stat Bonus (class-specific, added to base damage before multiplier)
- **Warrior:** `floor(STR × 0.3)`
- **Mage:** `floor(INT × 0.3)`
- **Archer:** `floor(DEX × 0.4)`

**Source:** `character_stats.cpp:165-175`

---

## Weapon Progression (from DB)

Best available weapon at each level tier. "Standard" = Common/Rare/Epic drops. "Ultra-Rare" = Legendary drops (0.1-6% rates).

### Swords (Warrior)

| Level | Weapon | Rarity | Damage | Source |
|------:|--------|--------|-------:|--------|
| 1 | Rusty Dagger | Common | 4-8 | Starter |
| 5 | Novice Blade | Common | 10-16 | Zone 1 |
| 8 | Ironbark Blade | Common | 15-25 | Zone 1 |
| 14 | Bloodfang Cleaver | Legendary | 88-128 | Tidepool Miniboss (ultra-rare) |
| 15 | Mariner's Cutlass | Common | 55-75 | Zone 2 |
| 20 | Fang of the Serpent | Rare | 70-90 | Zone 2 Boss |
| 22 | Resonating Blade | Common | 140-180 | Zone 3 |
| 24 | Quartz Executioner | Legendary | 285-365 | Crystal Cave Boss (ultra-rare) |
| 26 | Miner's Cleaver | Common | 160-200 | Zone 3 |
| 30 | Guardian's Edge | Rare | 180-220 | Zone 4 Boss |
| 30 | Core Breaker | Legendary | 365-455 | Core Guardian (ultra-rare) |
| 32 | Sun Cleaver | Common | 280-340 | Zone 4 |
| 35 | Scorpion's Sting | Common | 300-360 | Zone 4 |
| 40 | Pharaoh's Blade | Epic | 350-450 | Pyramid Boss |
| 41 | Pharaoh's Wrath | Legendary | 710-910 | Pyramid Boss (ultra-rare) |
| 42 | Plague Cleaver | Common | 450-550 | Zone 5 |
| 46 | Toxic Edge | Common | 600-750 | Zone 5 |
| 47 | Blightbane | Legendary | 1220-1520 | Lich Dungeon (ultra-rare) |
| 50 | Soul Reaver | Epic | 850-1050 | Lich Boss |
| 52 | Thunderstrike Blade | Common | 1100-1300 | Zone 6 |
| 55 | Tempest Cleaver | Legendary | 2220-2720 | Celestial Boss (ultra-rare) |
| 57 | Blade of Retribution | Common | 1350-1600 | Zone 6 |
| 60 | Judge's Verdict | Epic | 1650-2000 | Zone 7 Boss |
| 62 | Void Cleaver | Common | 1900-2300 | Zone 7 |
| 64 | Voidrend | Legendary | 3850-4750 | Void Realm Boss (ultra-rare) |
| 66 | Null Edge | Common | 2300-2700 | Zone 7 |
| 70 | Fate's Edge | Legendary | 2800-3500 | End-game (standard legendary) |

### Wands (Mage)

| Level | Weapon | Rarity | Damage | Source |
|------:|--------|--------|-------:|--------|
| 1 | Gnarled Stick | Common | 3-6 | Starter |
| 5 | Root-Knotted Wand | Common | 8-14 | Zone 1 |
| 8 | Wisp-Touched Wand | Common | 14-24 | Zone 1 |
| 10 | HeartFate | Rare | 25-35 | Zone 1 Boss |
| 11 | Tidecaller Staff | Common | 42-62 | Zone 2 |
| 14 | Tidecaller's Heart | Legendary | 92-132 | Tidepool Miniboss (ultra-rare) |
| 15 | Siren's Song Focus | Common | 52-72 | Zone 2 |
| 20 | Serpent Eye Orb | Rare | 68-88 | Zone 2 Boss |
| 22 | Shard Caster | Common | 135-175 | Zone 3 |
| 24 | Prismatic Singularity | Legendary | 278-358 | Crystal Cave Boss (ultra-rare) |
| 26 | Prismatic Focus | Common | 155-195 | Zone 3 |
| 30 | Core Reactor | Rare | 175-215 | Zone 4 Boss |
| 30 | Heart of the Mountain | Legendary | 358-448 | Core Guardian (ultra-rare) |
| 32 | Sandstorm Rod | Common | 270-330 | Zone 4 |
| 35 | Obelisk Focus | Common | 290-350 | Zone 4 |
| 40 | Wand of the Sands | Epic | 330-430 | Pyramid Boss |
| 41 | Sands of Eternity | Legendary | 695-895 | Pyramid Boss (ultra-rare) |
| 42 | Swamp Hex Staff | Common | 420-520 | Zone 5 |
| 46 | Blight Focus | Common | 580-720 | Zone 5 |
| 47 | Lichbane Focus | Legendary | 1195-1495 | Lich Dungeon (ultra-rare) |
| 50 | Lich's Phylactery | Epic | 800-1000 | Lich Boss |
| 52 | Tempest Rod | Common | 1050-1250 | Zone 6 |
| 55 | Seraph's Blessing | Legendary | 2180-2680 | Celestial Boss (ultra-rare) |
| 57 | Seraph Focus | Common | 1300-1550 | Zone 6 |
| 60 | Dragon's Breath | Epic | 1600-1950 | Zone 7 Boss |
| 62 | Entropy Staff | Common | 1850-2250 | Zone 7 |
| 64 | Entropy Incarnate | Legendary | 3780-4680 | Void Realm Boss (ultra-rare) |
| 66 | Mind-Rot Scepter | Common | 2250-2650 | Zone 7 |
| 70 | Reality Shatter | Legendary | 2700-3400 | End-game (standard legendary) |

### Bows (Archer)

| Level | Weapon | Rarity | Damage | Source |
|------:|--------|--------|-------:|--------|
| 1 | Makeshift Bow | Common | 4-7 | Starter |
| 5 | Hunter's Shortbow | Common | 9-15 | Zone 1 |
| 8 | Composite Shortbow | Common | 15-26 | Zone 1 |
| 10 | Rootbound Bow | Rare | 26-36 | Zone 1 Boss |
| 12 | Reef Striker | Common | 41-61 | Zone 2 |
| 14 | Spine Shell Longbow | Legendary | 90-130 | Tidepool Miniboss (ultra-rare) |
| 16 | Harpoon Launcher | Common | 54-74 | Zone 2 |
| 20 | Serpent Fang Bow | Rare | 72-92 | Zone 2 Boss |
| 22 | Stalactite Bow | Common | 138-178 | Zone 3 |
| 24 | Echo Stalker | Legendary | 282-362 | Crystal Cave Boss (ultra-rare) |
| 26 | Cavern Stalker Bow | Common | 158-198 | Zone 3 |
| 30 | Light Refractor | Rare | 178-218 | Zone 4 Boss |
| 30 | Whisperwind | Legendary | 362-452 | Core Guardian (ultra-rare) |
| 33 | Rib-Cage Recurve | Common | 275-335 | Zone 4 |
| 36 | Desert Wind Bow | Common | 295-355 | Zone 4 |
| 40 | Tomb Raider's Bow | Epic | 340-440 | Pyramid Boss |
| 41 | Tomb Raider's Legacy | Legendary | 705-905 | Pyramid Boss (ultra-rare) |
| 43 | Vine Snare Bow | Common | 440-540 | Zone 5 |
| 45 | Blight-Bringer | Common | 590-740 | Zone 5 |
| 47 | Plague Piercer | Legendary | 1210-1510 | Lich Dungeon (ultra-rare) |
| 50 | Deathwhisper | Legendary | 820-1020 | Lich Boss |
| 54 | Storm Caller | Common | 1080-1280 | Zone 6 |
| 55 | Griffon's Talon | Legendary | 2200-2700 | Celestial Boss (ultra-rare) |
| 58 | Angel's Aim | Common | 1320-1580 | Zone 6 |
| 60 | Heaven's Wrath | Epic | 1620-1980 | Zone 7 Boss |
| 63 | Rift Piercer | Common | 1880-2280 | Zone 7 |
| 64 | Reality Splitter | Legendary | 3820-4720 | Void Realm Boss (ultra-rare) |
| 67 | Reality Bender | Common | 2280-2680 | Zone 7 |
| 70 | Fate's Arrow | Legendary | 2750-3450 | End-game (standard legendary) |

---

## Warrior Damage Progression (using best standard weapon per level)

Uses the highest-damage weapon available at each level that isn't an ultra-rare legendary. Ultra-rares shown in separate column for comparison.

| Level | Standard Weapon | Weapon Dmg | Base Dmg | STR | Mult | Normal Min-Max | Crit Min-Max |
|------:|:----------------|----------:|---------:|----:|-----:|---------------:|-------------:|
| 1 | Rusty Dagger | 4-8 | 2-4 | 18 | 1.36x | **8-16** | **15-31** |
| 5 | Novice Blade | 10-16 | 4-8 | 31 | 1.62x | **22-38** | **42-74** |
| 8 | Ironbark Blade | 15-25 | 5-11 | 41 | 1.82x | **36-65** | **70-126** |
| 10 | Ironbark Blade | 15-25 | 6-13 | 48 | 1.96x | **41-74** | **79-144** |
| 15 | Mariner's Cutlass | 55-75 | 9-18 | 64 | 2.28x | **145-212** | **282-413** |
| 20 | Fang of Serpent | 70-90 | 11-23 | 81 | 2.62x | **212-296** | **413-577** |
| 22 | Resonating Blade | 140-180 | 12-25 | 87 | 2.74x | **416-561** | **811-1093** |
| 26 | Miner's Cleaver | 160-200 | 14-29 | 100 | 3.00x | **522-687** | **1017-1339** |
| 30 | Guardian's Edge | 180-220 | 16-33 | 114 | 3.28x | **642-830** | **1251-1618** |
| 32 | Sun Cleaver | 280-340 | 17-35 | 120 | 3.40x | **1009-1275** | **1967-2486** |
| 35 | Scorpion's Sting | 300-360 | 19-38 | 130 | 3.60x | **1148-1432** | **2238-2792** |
| 40 | Pharaoh's Blade | 350-450 | 21-43 | 147 | 3.94x | **1461-1942** | **2848-3786** |
| 42 | Plague Cleaver | 450-550 | 22-45 | 153 | 4.06x | **1916-2415** | **3736-4709** |
| 46 | Toxic Edge | 600-750 | 24-49 | 166 | 4.32x | **2695-3451** | **5255-6729** |
| 50 | Soul Reaver | 850-1050 | 26-53 | 180 | 4.60x | **4029-5073** | **7856-9892** |
| 52 | Thunderstrike | 1100-1300 | 27-55 | 186 | 4.72x | **5319-6395** | **10372-12470** |
| 57 | Blade of Retribution | 1350-1600 | 30-60 | 203 | 5.06x | **6984-8393** | **13618-16366** |
| 60 | Judge's Verdict | 1650-2000 | 31-63 | 213 | 5.26x | **8840-10851** | **17238-21159** |
| 62 | Void Cleaver | 1900-2300 | 32-65 | 219 | 5.38x | **10398-12733** | **20276-24829** |
| 66 | Null Edge | 2300-2700 | 34-69 | 232 | 5.64x | **13167-15625** | **25676-30469** |
| 70 | *(see Lv70 legendaries below)* | | 36-73 | 246 | 5.92x | | |

---

## Mage Damage Progression (using best standard weapon per level)

| Level | Standard Weapon | Weapon Dmg | Base Dmg | INT | Mult | Normal Min-Max | Crit Min-Max |
|------:|:----------------|----------:|---------:|----:|-----:|---------------:|-------------:|
| 1 | Gnarled Stick | 3-6 | 2-4 | 18 | 1.36x | **6-13** | **11-25** |
| 5 | Root-Knotted Wand | 8-14 | 4-8 | 31 | 1.62x | **19-35** | **37-68** |
| 8 | Wisp-Touched Wand | 14-24 | 5-11 | 41 | 1.82x | **34-63** | **66-122** |
| 10 | HeartFate | 25-35 | 6-13 | 48 | 1.96x | **60-94** | **117-183** |
| 15 | Siren's Song Focus | 52-72 | 9-18 | 64 | 2.28x | **139-205** | **271-399** |
| 20 | Serpent Eye Orb | 68-88 | 11-23 | 81 | 2.62x | **207-290** | **403-565** |
| 22 | Shard Caster | 135-175 | 12-25 | 87 | 2.74x | **402-548** | **783-1068** |
| 30 | Core Reactor | 175-215 | 16-33 | 114 | 3.28x | **626-813** | **1220-1585** |
| 40 | Wand of the Sands | 330-430 | 21-43 | 147 | 3.94x | **1382-1862** | **2694-3630** |
| 50 | Lich's Phylactery | 800-1000 | 26-53 | 180 | 4.60x | **3799-4843** | **7408-9443** |
| 60 | Dragon's Breath | 1600-1950 | 31-63 | 213 | 5.26x | **8577-10588** | **16725-20646** |
| 70 | *(see Lv70 legendaries below)* | | 36-73 | 246 | 5.92x | | |

---

## Archer Damage Progression (using best standard weapon per level)

| Level | Standard Weapon | Weapon Dmg | Base Dmg | DEX | Mult | Normal Min-Max | Crit Min-Max |
|------:|:----------------|----------:|---------:|----:|-----:|---------------:|-------------:|
| 1 | Makeshift Bow | 4-7 | 2-4 | 18 | 1.36x | **8-14** | **15-27** |
| 5 | Hunter's Shortbow | 9-15 | 4-8 | 31 | 1.62x | **21-37** | **40-72** |
| 8 | Composite Shortbow | 15-26 | 5-11 | 41 | 1.82x | **36-67** | **70-130** |
| 10 | Rootbound Bow | 26-36 | 6-13 | 48 | 1.96x | **62-96** | **120-187** |
| 16 | Harpoon Launcher | 54-74 | 9-19 | 67 | 2.34x | **147-217** | **286-423** |
| 20 | Serpent Fang Bow | 72-92 | 11-23 | 81 | 2.62x | **217-301** | **423-586** |
| 22 | Stalactite Bow | 138-178 | 12-25 | 87 | 2.74x | **411-556** | **801-1084** |
| 30 | Light Refractor | 178-218 | 16-33 | 114 | 3.28x | **636-823** | **1240-1604** |
| 40 | Tomb Raider's Bow | 340-440 | 21-43 | 147 | 3.94x | **1422-1903** | **2772-3710** |
| 50 | Deathwhisper | 820-1020 | 26-53 | 180 | 4.60x | **3891-4935** | **7587-9623** |
| 60 | Heaven's Wrath | 1620-1980 | 31-63 | 213 | 5.26x | **8682-10746** | **16930-20954** |
| 70 | *(see Lv70 legendaries below)* | | 36-73 | 246 | 5.92x | | |

---

## Level 70 Endgame Damage

### With Standard Legendary (Lv70)

| Class | Weapon | Damage | Normal Min-Max | Crit Min-Max |
|-------|--------|-------:|---------------:|-------------:|
| Warrior | Fate's Edge | 2800-3500 | **16,788-21,163** | **32,736-41,267** |
| Mage | Reality Shatter | 2700-3400 | **16,197-20,571** | **31,584-40,113** |
| Archer | Fate's Arrow | 2750-3450 | **16,492-20,867** | **32,160-40,690** |

### With Lv64 Ultra-Rare Legendary (best in game)

| Class | Weapon | Damage | Normal Min-Max | Crit Min-Max |
|-------|--------|-------:|---------------:|-------------:|
| Warrior | Voidrend | 3850-4750 | **23,005-28,556** | **44,860-55,684** |
| Mage | Entropy Incarnate | 3780-4680 | **22,591-28,142** | **44,052-54,876** |
| Archer | Reality Splitter | 3820-4720 | **22,828-28,379** | **44,514-55,339** |

**Lv64 ultra-rares are ~37% stronger than Lv70 standard legendaries.** This is intentional — see design note below.

---

## Analysis Summary

1. **Linear Progression:** Damage grows steadily via weapon upgrades and stat scaling. No exponential blowup.
2. **Weapon Dominance:** Weapons are 60-80% of final damage at all levels. Stat multiplier provides a ~6x scaling factor at endgame.
3. **Raid Boss Balance:**
   - **Boss HP:** 500,000,000
   - **Lvl 70 Avg Hit:** ~19,000
   - **Lvl 70 Crit:** ~37,000
   - **Est. DPS:** ~15,000 per player
   - **Time to Kill (20 players):** (500M) / (15k × 20) = **~1,666 seconds (28 minutes)**
   - This meets the 30-minute design target.
4. **PvP:** All damage × 0.05 (5%) in PvP. A Lv70 crit of ~40,000 becomes ~2,000 in PvP. Source: `combat_system.h:pvpDamageMultiplier = 0.05f`

---

## Ultra-Rare Legendary Weapon Design

**Lv64 ultra-rares are intentionally stronger than Lv70 standard weapons. DO NOT nerf them.**

| Weapon Type | Lv64 Ultra-Rare | Lv70 Standard |
|-------------|----------------|---------------|
| Sword | Voidrend: 3850-4750 | Fate's Edge: 2800-3500 |
| Wand | Entropy Incarnate: 3780-4680 | Reality Shatter: 2700-3400 |
| Bow | Reality Splitter: 3820-4720 | Fate's Arrow: 2750-3450 |

**Reasoning:**
- 0.1% (1-in-1000) drop rate from Void Realm bosses
- Meant to be game-changing finds that excite players
- A Lv64 player with an ultra-rare will outperform a Lv70 with standard gear
- Creates aspirational chase items that drive engagement

### All Ultra-Rare Weapons by Level

| Level | Warrior | Mage | Archer | Source |
|------:|---------|------|--------|--------|
| 14 | Bloodfang Cleaver (88-128) | Tidecaller's Heart (92-132) | Spine Shell Longbow (90-130) | Tidepool Miniboss |
| 24 | Quartz Executioner (285-365) | Prismatic Singularity (278-358) | Echo Stalker (282-362) | Crystal Cave Boss |
| 30 | Core Breaker (365-455) | Heart of the Mountain (358-448) | Whisperwind (362-452) | Core Guardian |
| 41 | Pharaoh's Wrath (710-910) | Sands of Eternity (695-895) | Tomb Raider's Legacy (705-905) | Pyramid Boss |
| 47 | Blightbane (1220-1520) | Lichbane Focus (1195-1495) | Plague Piercer (1210-1510) | Lich Dungeon Boss |
| 55 | Tempest Cleaver (2220-2720) | Seraph's Blessing (2180-2680) | Griffon's Talon (2200-2700) | Celestial Boss |
| 64 | Voidrend (3850-4750) | Entropy Incarnate (3780-4680) | Reality Splitter (3820-4720) | Void Realm Boss |
| 70 | Fate's Edge (2800-3500) | Reality Shatter (2700-3400) | Fate's Arrow (2750-3450) | End-game |

---

## Crit System Reference

```
Total Crit % = 5% (base) + (DEX × 0.5%) + equipment crit stat (1 crit = 1%)
Crit Multiplier = 1.95x + equipment crit_dmg bonus
```

Source: `character_stats.cpp:95-100`, `combat_system.h:baseCritRate = 0.05f`

**Example:** Archer Lv70 with 246 DEX and +18 equipment crit:
- Base: 5% + (246 × 0.5%) = 128% → capped at engine max
- Warrior/Mage with 50 DEX: 5% + 25% + equipment = ~48% typical

---

## Armor & Damage Reduction

```
Armor Reduction % = min(75, armor × 0.5)
Actual Damage = max(1, damage × (1.0 - reduction%/100))
```

Source: `character_stats.cpp:206-208`, `combat_system.h:armorReductionFactor = 0.5f, maxArmorReductionPercent = 75`

150 armor = 75% reduction (cap). Always deals minimum 1 damage.
