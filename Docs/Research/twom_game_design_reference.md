# IMO: The World of Magic — Complete Game Design Reference

**TWOM is a 2D mobile MMORPG by Com2uS featuring four classes, a two-faction PvP system, and a classic grind-based progression loop.** This document covers every major game system in enough detail to serve as an implementation reference for a C++ game engine. The game's core loop centers on monster grinding for XP and loot, party-based dungeon crawling, and faction-vs-faction PvP across an interconnected world of overworld zones, multi-floor dungeons, and instanced content. All 61 stat points, 49 skill points, 4 classes, 11 world regions, 40+ bosses, and hundreds of items are documented below.

---

## 1. Character classes and the stat system

### 1.1 The five core stats

Every character has exactly **61 total stat points** (base + distributable). The stat cap is **18** for any single stat. Equipment, enchants, and buffs can push stats beyond 18.

| Stat | Abbr | Primary Effect | Secondary Effects |
|------|------|----------------|-------------------|
| Strength | STR | Increases melee attack damage | Every 3 STR reduces incoming damage; boosts max HP slightly |
| Intelligence | INT | Increases magic skill damage (every 3 INT) | Every 2 INT adds +1 to healing; increases hit probability; affects debuff duration |
| Dexterity | DEX | Increases ranged physical damage | Every 3 DEX ≈ +1 critical chance; increases evasion (every ~5 DEX); boosts accuracy |
| Constitution | CON | Increases total HP, HP per level, HP regen | Cannot be replicated by equipment (equipment adds only flat HP); compounds per level |
| Wisdom | WIS | Increases total MP, MP per level, MP regen | Identical scaling to CON but for mana |

### 1.2 Derived stats

| Derived Stat | Formula / Source | Notes |
|---|---|---|
| HP | CON × level scaling | ~10–15 HP/level at 18 CON; ~8–12 HP/level at 15 CON. Retroactively recalculated on stat change. |
| MP | WIS × level scaling | Same mechanics as HP but for mana |
| Melee Attack | STR + weapon damage range | Flat weapon damage min–max plus STR modifier |
| Ranged Attack | DEX + weapon damage range | For Rangers using bows |
| Magic Attack | INT + skill base damage | Every 3 INT increases magic damage by ~1 |
| Healing Power | INT + skill base heal | Every 2 INT = +1 HP healed |
| Critical Rate | DEX + equipment CRIT + skills | 1 CRIT point = 1% chance; 3 DEX ≈ 1 CRIT |
| Critical Damage | Fixed | **200%** of normal damage |
| Deadly Strike Rate | Equipment/pets only | Cannot come from stats; decimal percentage (e.g., 5.2 = 5.2%) |
| Deadly Strike Damage | Fixed | **300%** of normal damage |
| Evasion | DEX + Swift skill | Every ~5 DEX increases dodge; Rangers excel here |
| Block Rate | Shield stat (Warriors only) | Percentage chance to negate incoming attack entirely |
| Armor/Defense | Equipment only | Flat damage subtraction per hit (e.g., 5 armor ≈ −5 damage per hit) |
| Damage Reduction | STR (every 3 STR) + Toughness skill | Percentage-based passive reduction |

### 1.3 Class base stats and builds

All classes total **61 stat points**. Classes with higher bases get fewer distributable points.

| | Warrior | Magician | Ranger | Breaker |
|---|---|---|---|---|
| STR | 12 | 8 | 10 | 15 |
| INT | 9 | 12 | 8 | 15 |
| DEX | 11 | 9 | 13 | 10 |
| CON | 14 | 8 | 11 | 13 |
| WIS | 9 | 12 | 10 | 5 |
| **Free Points** | **6** | **12** | **9** | **3** |

**Warrior builds:** CON Warrior (18 CON, 14 STR — recommended tank), STR Warrior (18 STR, 14 CON — damage dealer), Balanced (16/16). CON Warriors dominate at high levels because CON compounds per level and cannot be replicated by equipment.

**Magician builds:** INT/WIS Mage (18 INT, 18 WIS — max damage and mana, extremely low HP), INT/CON Mage (18 INT, 14 CON — better PvP survivability, sufficient MP at level 40+).

**Ranger builds:** DEX Ranger (18 DEX, 15 CON — max damage/crit/evasion/accuracy), CON Ranger (18 CON, 15 DEX — survival-focused).

**Breaker builds:** STR 15, INT 15, DEX 10, CON 16, WIS 5 (all 3 free points into CON). Least flexible class.

### 1.4 Class mechanics

**Warriors** use **Rage Crystals** (not mana). Gain 0.5 crystal per normal hit, 1 per crit/deadly, **0.2 per hit received** (unique to Warriors). Max crystals: 2 at level 1, +1 per 10 levels (7 at level 50, 8 with Instinct Stimulus). Can equip shields (block stat). Melee only. Highest HP and armor.

**Magicians** use **Mana (MP)**. Highest burst damage via skills. Only class with healing (Light Healing, Shield, Prayer of Protection, Sara's Blessing). Fire/Ice elemental attacks. Carnivalize converts 20 HP → ~20–28% of max MP. Double-casting mechanic (Flare/Ice Lance enable instant follow-up spells). Lowest HP and armor. No Rage Crystals.

**Rangers** use **Rage Crystals** (same generation as Warriors except no crystals from damage taken). Fastest attack speed, highest DPS class. Bows are primary weapon (swords suboptimal). Four benefits from DEX: damage, crit, avoid, accuracy. Only AoE is Multi Shot. Clarity skill recharges 5 crystals over 5 seconds.

**Breakers** use **Crystal Points** (similar to Rage Crystals). Hybrid physical + magical damage (ice and lightning). Melee martial arts with Claws/Knuckles/Tonfas. High mobility and burst. Added after the original three classes; least documented.

**Class triangle in PvP:** Warrior beats Ranger (stun + block negates crits) → Ranger beats Mage (fast attacks break shield, high sustained DPS) → Mage beats Warrior (range + freeze + burst damage).

---

## 2. Leveling and experience system

### 2.1 XP curve and max level

**Max level is 50**, requiring approximately **50 million+ total EXP**. The XP curve is exponential, with levels 40–50 requiring roughly 10 million XP each. Reaching level 50 takes an estimated **600–1,000+ hours**.

| Level Range | XP Speed | Notes |
|---|---|---|
| 1–10 | Very fast | Minutes per level |
| 10–20 | Moderate | ~1 day of play to level 15–20 |
| 20–30 | Slow | "Pro" milestone at level 30 |
| 30–40 | Very slow | Steep grind |
| 40–50 | Extremely slow | Ancient Coins become primary XP source |

### 2.2 Monster-level XP rules

| Level Difference | Monster Name Color | XP Yield |
|---|---|---|
| 4+ levels above you | Red | High (but dangerous) |
| 3–4 above | Mahogany | Good |
| 1–2 above | Orange | Optimal |
| Same level | White | Normal |
| 1–2 below | Blue | Reduced |
| 3–4 below | Green | Very little |
| 5+ below | — | **0 XP** |

### 2.3 XP sources

**Monster kills** are the primary source. **Party XP sharing** splits evenly among members within ~3 levels. **Ancient Coins** (3 coins → Level × 50 EXP) become the primary leveling method at level 42+. **Pirate Coins** (10 coins → 300 EXP, level 11+ only). **EXP Boosts** (10%/20%) stack additively. **Battlefield victory** grants a 20% EXP boost for 1 hour.

### 2.4 Death penalty

**1% of current level's total XP lost per death** (PvE and PvP). Mitigated by **Protection of Lanos** or **Guardianship of Siras** (5 for 1,100 gold from faction merchant — automatically consumed on death to prevent XP loss). **Phoenix Heart** (Premium item, 5 for 99 Platinum) allows on-the-spot revival with full HP/MP.

### 2.5 No prestige/rebirth system

TWOM has no level reset or prestige mechanic. "Rebirth point" refers only to the respawn location set at an Innkeeper.

---

## 3. Skill system

### 3.1 Skill point allocation

Players receive **1 skill point per level starting from level 2**, for a total of **49 SP at level 50**. Skills are learned by reading skillbooks (purchased from NPC vendors or dropped by bosses/monsters). Each skill volume costs 1 SP to learn. **Oblivion Potion** (11,000 gold) resets all skill points without requiring re-reading skillbooks. **Skill Point Potion** (Guild Shop, 5,000 Guild Coins) grants +1 bonus SP.

### 3.2 Warrior skills

| Skill | Lvl | Type | Vols | Crystal Cost | CD | Target | Key Effect |
|---|---|---|---|---|---|---|---|
| Wild Swing | 2 | Active | I–IV | 0 | ~8s | Single | 140%/160%/180%/200% damage |
| Hemorrhage | 5 | Active DoT | I–III | 0 | Short | Single | 100% hit rate; DoT damage |
| Taunting Blow | 5 | Active | I–III | 0 | — | Single | 100% hit rate; low damage, aggro |
| Parry | 5 | Passive | I–IV | — | — | Self | Increases block rate |
| Counterattack | 10 | Active | I–IV | 0 | ~8s | Single | Triggers after block; 140–200% damage |
| Bull Rush | 10 | Buff | I–IV | 0 | 20s | Self | Speed +24%/36%/48%/60% for 8s |
| Wild Charge | 10 | Active | I–IV | Crystals | 15–12s | Single | Charge + 2s stun |
| Drowsiness | 10 | Debuff | I–IV | 2 | — | Single | Slows target 5/6/7/8s; 50% damage |
| Rage | 15 | Buff | I–IV | 3 | — | Self | Fixed critical rate for duration |
| Slam | 15 | Active | I–III | 2 | Instant | Single/AoE | High damage + stun; spammable; works with Sweeping Strikes |
| Toughness | 15 | Passive | I–IV | — | — | Self | Damage reduction 2%/4%/6%/8% |
| Berserk | 20 | Buff | I–IV | 0 | 22s | Self | Instantly fills 2/3/4/5 crystals |
| Sweeping Strikes | 20 | Buff | I–IV | 2 | 20s | Self | Adds +1/+2/+3/+4 extra targets for 5s |
| Last Resistance | 20 | Buff | I–IV | 0 | 300s | Self | Takes only 10% damage for 5/6/7/8s |
| Fatal Attack | 30 | Active | I–II | 3 | — | Single | +400% damage; works with Sweeping Strikes |
| Stampede | 35 | Party Buff | I–IV | 1 | 180–120s | Party | Movement speed increase |
| Heavy Strike | 40 | Active | I–II | All crystals | 30–15s | Single | Consumes all crystals; strong hit + Silence |

**Warrior combo:** Rage → Berserk → Sweeping Strikes → Wild Swing → Slam → Slam → Slam

### 3.3 Magician skills

| Skill | Lvl | Type | Vols | MP Cost | CD | Target | Key Effect |
|---|---|---|---|---|---|---|---|
| Flame Shock | 2 | Active | I–IV | Low | ~3s | Single | Main early attack; fast CD |
| Freezing Trap | 5 | CC | I–III | Med | Med | Single | Freezes 4/5/6s |
| Light Healing | 5 | Heal | I–III | Med | ~3s | Single | Base heal + (INT/2) bonus; double-castable |
| Shield | 5 | Buff | I–IV | Med | Med–Long | Single | Absorbs 20/40/60/80 damage |
| Ice Prison | 10 | CC | I–III | Med | Med | **AoE** | Freezes multiple targets; farming/escape |
| Firebolt | 10 | Active (Cast) | I–III | Med | ~5s | Single | Higher damage than Flame Shock |
| Teleport | 10 | Utility | I–III | Med | Long | Self | Instant blink |
| Flare | 15 | Active (Instant) | I–III | High | ~8s | Single | Same damage as Firebolt; instant cast; enables double-cast |
| Carnivalize | 15 | Utility | I–III | 20 HP | 30s | Self | Converts HP → 18%/23%/28% max MP |
| Ice Lance | 20 | Active | I–II | Med | Fast | Single | Damage + freeze chance; double-castable |
| Flame of Kataru | 20 | Active | I–III | Med | Med | **AoE** | Mage farming skill; area fire damage |
| Curse of Doom | 20 | Debuff | I–II | Med | Med | Single | **Doubles next damage** on target |
| Prayer of Protection | 20 | Buff | I–II | High | Long | Single | Second shield; heals + absorbs |
| Stun | 25 | CC | I–II | Med | Med | Single | Paralyzes target |
| Firestorm | 30 | Active | I | High | Long | **AoE** | Best AoE damage |
| Silence | 30 | Debuff | I–II | Med | Med | Single | Prevents skill use |
| Sara's Blessing | 35 | Heal | I–II | High | Long | **AoE** | Party heal |
| Fear | 35 | CC (Cast) | I | High | Long | Single | Forces flee; most powerful CC |
| Hellfire | 40 | Active | I–II | Very High | Long | Single | **Strongest single-target spell** |
| Neutralize | 40 | Debuff | I–II | Med | Med | Single | **Removes all Rage Crystals** from target |

**Mage DPS combo:** Curse of Doom → Flare → (double-cast) Ice Lance → Firebolt → Flame Shock

### 3.4 Ranger skills

| Skill | Lvl | Type | Vols | Crystal Cost | CD | Target | Key Effect |
|---|---|---|---|---|---|---|---|
| Double Shot | 2 | Active | I–IV | 2 | Fast | Single | Main attack; fastest DPS skill |
| Concentrate | 5 | Buff | I–III | 0 | — | Self | Increases hit rate |
| Thorns | 5 | Buff | I–III | 0 | — | Self | Damage reflect |
| Shift | 10 | Utility | I–III | 0 | Long | Self | Instant blink |
| Ensnare | 10 | Debuff | I–IV | 0 | Med | Single | Slows target; essential for kiting |
| Impact Shot | 10 | Active | I–IV | Crystals | ~5s | Single | Stun + damage; only ranger stun |
| Weakness | 10 | Debuff | I–IV | 0 | — | Single | Strips armor: −2/−4/−8/−16 |
| Sharp Eye | 15 | **Passive** | I–IV | — | — | Self | Increases critical rate |
| Swift | 15 | **Passive** | I–IV | — | — | Self | Increases evasion |
| Multi Shot | 20 | Active | I–II | Crystals | Med | **AoE** | Hits up to 4 targets; reduced per-target damage |
| Stoneskin | 20 | **Passive** | I–IV | — | — | Self | Increases armor |
| Amplify Sense | 25 | **Passive** | I–IV | — | — | Self | Increases critical damage |
| Instinct Stimulus | 25 | Buff | I–III | Crystals | — | Self | Increases max crystal capacity |
| Soul Drain Shot | 30 | Active | I–IV | Crystals | Med | Single | HP lifesteal on hit |
| Clarity | 35 | Buff | I | 0 | Med | Self | Recharges 5 crystals over 5 seconds |
| Power Shot | 40 | Active | I–IV | High | Long | Single | **250% damage** single-target nuke |

### 3.5 Skillbook acquisition

| Source | Skills Available |
|---|---|
| Reader Lal (starting village) | Level 2–10 skills, Volumes I–III |
| Reader Mal (Damas Village) | Level 10–20 skills, Volume I |
| Reader Hal (faction castle) | Level 25–40 skills, Volume I only (11K–58K gold each) |
| Boss/monster drops | Volumes II–IV of most skills |
| Battlefield (60 Pendants of Honor) | Wild Charge III, Thorns III, Firebolt III |
| Quest rewards | Double Shot IV (Bull's-Eye quest), select others |
| Anguish Altar/Lunarf Runes | Volume IV of select skills (3 Runes + 1M gold at Researcher NPC) |

---

## 4. Combat system

### 4.1 Combat flow

Players tap a target to auto-attack. Attack speed varies by class (Rangers fastest, Mages slowest at ~5s basic attack interval). Skills are activated manually from a skill bar, consuming Rage Crystals or MP. Combat indicators: **yellow numbers** = damage dealt, **red numbers** = damage taken, **green +** = healing, **"BLOCK!"** = shield block, **"MISS!"** = attack missed.

### 4.2 Damage calculation

No official formula is published. Community-derived understanding:

**Physical damage** = weapon damage range (min–max) + STR/DEX modifier. Each weapon enchant level adds approximately +1 to max damage. **Armor subtracts flat damage** per hit (e.g., 15 armor ≈ −15 damage; confirmed by Overload monster: "200 armor = −40 damage for Warriors and Rangers," suggesting a partial ratio rather than 1:1).

**Magical damage** = skill base damage + INT modifier (every 3 INT ≈ +1 magic damage). Magic damage is less affected by physical armor.

**Healing formula:** Skill base heal + (INT ÷ 2). Example: Light Healing III base 40 + 24 INT = 40 + 12 = **52 HP healed**.

### 4.3 Critical and deadly strikes

| Type | Damage Multiplier | Crystal Gain | Source |
|---|---|---|---|
| Normal Hit | 100% | 0.5 crystal | Default |
| Critical Hit | **200%** | 1.0 crystal | DEX, equipment CRIT, Sharp Eye skill |
| Deadly Strike | **300%** | 1.0 crystal | Equipment and pets only (Cloak of Will, Cloak of Death, pet enchants) |

Critical on healing: Light Healing deadly = 300% of base heal. Deadly Strike can also proc on Mage healing spells.

### 4.4 Hit, miss, block, and dodge

**Hit rate** comes from equipment stats, INT (for Mages), and DEX (for Rangers). The Concentrate skill boosts Ranger accuracy. Hemorrhage and Taunting Blow have **100% hit rate** against any monster. **Block** is a Warrior-only mechanic from shields; each point of block stat is a percentage chance to fully negate an attack. **Evasion** scales with DEX and the Swift passive. **Level difference** significantly affects hit/miss rates — attacking monsters 5+ levels above results in frequent misses.

### 4.5 Elemental damage and resistances

| Resistance | Effect per Point | Max Obtainable |
|---|---|---|
| Fire Resist (FR) | −1 fire damage; at low levels ~1% chance to fully resist | 78 |
| Magic Resist (MR) | −1 magic damage; reduces debuff duration (Stun, Fear, Doom) | 60 |
| Ice Resist (IR) | +1% resist chance to ice attacks; reduces freeze duration | 31 |
| Poison Resist (PR) | +1% resist chance to poison | 22 |

Mage spells are categorized as **Fire** (Flame Shock, Firebolt, Flare, Flame of Kataru, Hellfire, Firestorm) or **Ice** (Ice Prison, Ice Lance, Freezing Trap). Resisting a spell = zero damage.

### 4.6 Aggro and threat

TWOM has **no formal threat table**. Aggressive monsters auto-attack nearby players. Neutral monsters only attack when provoked. **Boss loot goes to whoever dealt the most total damage**, creating a de facto damage competition system. Warriors "lure" monsters by walking into spawn areas and drawing aggro physically. Taunting Blow functionally draws monster attention. Mini-bosses become **invulnerable** (100% miss, regen to full HP at ~50 HP/sec) after 5 seconds of receiving no damage.

### 4.7 Rage Crystal generation reference

| Source | Crystals |
|---|---|
| Normal hit | +0.5 |
| Critical or Deadly hit | +1.0 |
| Hit received (Warriors only) | +0.2 |
| Berserk skill | Instantly +2/+3/+4/+5 |
| Clarity skill (Ranger) | +5 over 5 seconds |
| Neutralize (Mage, on target) | Removes ALL crystals |

**Crystal capacity by level:** Lv1=2, Lv10=3, Lv20=4, Lv30=5, Lv40=6, Lv50=7, +1 from Instinct Stimulus = 8 max.

---

## 5. Equipment and item system

### 5.1 Equipment slots

| Slot | Enchantable (Weapon/Armor Scroll) | Enchantable (Stat Scroll) |
|---|---|---|
| Weapon (main hand) | Yes (Weapon Enchant) | No |
| Shield / Sub-Weapon | Yes (Armor Enchant) | No |
| Helmet / Hat | Yes (Armor Enchant) | No |
| Body Armor | Yes (Armor Enchant) | No |
| Gloves | Yes (Armor Enchant) | No |
| Boots / Shoes | Yes (Armor Enchant) | No |
| Belt | No | Yes (Stat Scrolls) |
| Ring | No | Yes (Stat Scrolls) |
| Necklace | No | Yes (Stat Scrolls) |
| Cloak | No | Yes (Stat Scrolls) |
| Pet (dedicated slot) | No | Pet Enchant Stones |
| Costume (7 slots) | No | Costume Gems (3 gem slots) |

### 5.2 Item rarity tiers

| Name Color | Grade | Source | Core on Extraction |
|---|---|---|---|
| Black | Normal/Common | No bonus stats | Cannot extract |
| Green | Unique | Any monster drop; crafted | 1st–5th Core (by item level) |
| Blue | Hero | Mini-boss/boss drops; crafted | 6th Core |
| Purple | Rare | Boss drops (very rare); Combine Book III | 7th Core |

### 5.3 Enhancement system

**Weapon Enchant Scrolls** and **Armor Enchant Scrolls** come in 5 grades based on the target item's level:

| Grade | Item Level | Source |
|---|---|---|
| D Class | 1–9 | Monster drops Lv1–9; NPC Damas (2,200g); crafted |
| C Class | 10–19 | Monster drops Lv10–19; Combine Book I |
| B Class | 20–29 | Monster drops Lv20–29; Combine Book II |
| A Class | 30–39 | Mini-boss/boss drops Lv30–50; Combine Book II |
| S Class | 40–49 | High-level boss drops; Combine Book III |

**Safe enchant limits:** Weapons safe to **+6** (break risk at +7+). Armor safe to **+4** (break risk at +5+). Approximate success rates: +7 weapon ≈ 30%, +8 ≈ 10%, +9 to +15 ≈ 5%. Max observed: **+10 weapons**, **+8 armor**. Breaking makes the item unusable and soulbound until repaired with a Repair Scroll + Origin Stones (restores to random +1 to +6 for weapons, +1 to +4 for armor).

**Holy Water** (crafted from Red + Blue Turtle Shells) creates **Blessed Enchant Scrolls** — on failure, item resets to +0 instead of breaking.

**Stat Enchant Scrolls** (for accessories: Belt, Ring, Necklace, Cloak) add stat bonuses with four outcome tiers: Fail (0), Low (+1), High (+2), Ultra High (+3) for STR/INT/DEX scrolls; or +10/+20/+30 for MP scrolls. Cannot break items; new enchant replaces old one.

### 5.4 Core system (Magic Extraction)

Using a Magic Extraction Scroll on a colored-name item destroys it and yields Cores:

| Core | Source Item | BT Price |
|---|---|---|
| 1st Core | Green items Lv 1–9 | 400g |
| 2nd Core | Green items Lv 10–20 | 500–4Kg |
| 3rd Core | Green items Lv 20–29 | 2K–8Kg |
| 4th Core | Green items Lv 31–40 | 3K–10Kg |
| 5th Core | Green items Lv 41–50 | 18K–42Kg |
| 6th Core | Blue items (any level) | 30K–90Kg |
| 7th Core | Purple items (any level) | 300K–500Kg |

Cores stack up to 99 and are the primary crafting currency.

### 5.5 Crafting system

**Combine Books** are portable crafting tools purchased from faction merchants:

| Book | NPC Price | Typical Recipes |
|---|---|---|
| Novice | Free | Leather Shoes, Cured Leather, Small Bag |
| Combine Book I | 1,100g | Blunt Shortsword, Longsword, Skull Shield, Enchant Scroll C |
| Combine Book II | 110,000g | Viking Sword, Gladius, Rapier, Archangel Shield, Enchant Scroll B→A |
| Combine Book III | 550,000g | Darksteel Sword, War Sword, Golden Necklace, Enchant Scroll S |

**NPC-specific crafting** exists at various vendors (Kulin, Amy, Poscar, Golemor/Brainless, Libya) for quest-related items and high-level equipment.

**Grade Symbols** (soulbound, daily-limited crafting currency) obtained from Supply Manager: D-Grade from Ruined Leather(5), C-Grade from Small Coral(5), B-Grade from Piece of Bone(5), A-Grade from Cotton Cloth(5), S-Grade from Special Cotton Cloth(5). **Double output on weekends.**

### 5.6 Equipment binding

**Soulbound** items cannot be traded. Triggered by: quest rewards, starter items, premium purchases, and **items that break during enchanting** (become soulbound until repaired). Over 320 soulbound items exist.

### 5.7 Inventory and storage

Inventory has a limited grid expanded by **Bags** (Small Bag = 4 slots; Spider Silk Bag = more; Random Bag from Premium Shop yields Kooii Bag, Mushroom Bag, Kooiivuitton, Koocci, Barslaf Bag, or Koorada at varying rarities). Stackable items (Cores, Recall Scrolls, coins) stack up to **99–250 per slot**.

**Vault** (bank) accessed via **Vault Fairy Pher** NPC. Requires Ticket: Vault Usage Pass (one-time unlock). Expandable with Vault Expansion Tickets (+4 slots each). **Shared between characters** on the same server/account. **Pher's Bell** allows remote vault access (requires Magic Fish Bread consumable).

---

## 6. World architecture and map design

### 6.1 World regions overview

The world consists of **11 major regions** with distinct level ranges and purposes:

| Region | Level Range | Type | Access Method |
|---|---|---|---|
| Wingfril Island | 1–18 | Starting overworld + dungeons | Default starting area |
| Inotia Continent | 14–44 | Faction territories + shared zones | Airship from Beach (777g) |
| Sky Castle | 30–40+ | Time-rotating faction dungeon | Ancient Palace entrance |
| Maze of Forest | 19–46 | End-game overworld | Overworld connection |
| Crown Rock | 30–50 | Guild siege + dungeons | Village NPC teleport (2,500g) |
| Anguish Altar | 45–50 | End-game dungeon | Crown Rock → Bruno NPC (10,000g) |
| Lunarf | 45–50 | End-game dungeon | Dorn NPC in forest (10,000g) |
| Morphosis | 35–50 | End-game content chain | Pledger NPC quest chain |
| Instant Dungeons | 20–40 | Instanced party dungeons | Party leader request |
| Guild Dungeon | 30+ | Guild-only instanced content | Guild system |
| Koowaii Island | 10+ | Limited event map | NPC Quiess |

### 6.2 Wingfril Island zones

**Kooii Training Ground** (Lv 1–3, tutorial, safe). **Woody-Weedy Village** (Siras town hub) and **Woody-Wordy Village** (Lanos town hub) — both contain all essential NPCs: Innkeeper, Skill Vendors, Leather Craftsman, Black Trader, Dungeon Keeper, Crown Rock Magician, Supply Manager. **Woody-Weedy/Wordy Forests** (Lv 1–5, non-PK). **Mushroom Marshland** (Lv 5–9, neutral). **Mushroom Spore Cave** (Lv 9–11, mini-dungeon). **Wingfril Island Beach** (Lv 10–13, **PvP enabled**, airship to continent). **Island with the Lighthouse** (Lv 10–14, PvP). **Lighthouse Dungeon** (5 floors, Lv 14–30+, PvP, quest-locked). **Pirate Ship** (Lv 20–23, quest-locked dungeon). **Temple of Wingfril** (Lv 25–35, PvP in second half, requires Joma's Pendant).

### 6.3 Inotia Continent zones

**Lanos territory:** Lanos Plains (Lv 14–18), Forest with Ruins (Lv 19–24), Arid Grassland (Lv 25–36). **Siras territory:** Forest of Grave (Lv 14–18), Kataru Mountains (Lv 19–24), Desert Valley (Lv 25–36). **Shared:** Hot Sand Plains (Lv 37–44, most valuable farming zone for A-Class enchant scrolls). **Faction castles:** Siras Castle and Lanos Palace (Lv 25+ skill vendors, 777g airship toll). All Inotia zones are PvP-enabled.

### 6.4 Sky Castle time-rotation system

Six maps with **hourly faction rotation**. Ancient Palace splits into Eastern/Western Sky Castle based on faction and current hour. Faction currently teleported receives an attack damage buff. After the buff hour expires, paths swap. Cannot return to previous maps. Final zone (Fallen Temple) is shared. Death respawns at recall location, not within Sky Castle. **CALIGO** (Lv 50 raid boss) resides here.

### 6.5 Navigation tree

```
Village → Forest → Mushroom Marshland → Beach → Airship (777g) → Inotia Continent
                                          ↓
                              Lighthouse Dungeon (quest req.)
                              Pirate Ship (quest + 5,500g key)

Village → Crown Rock Magician (2,500g) → Crown Rock Basecamp
                                          → Syphnel Road → Ancient Passage
                                            → Anguish Altar (10,000g)
                                            → Islot's Temple (9,999g)

Village Forest → Dorn NPC (10,000g) → Lunarf

Village → Pledger NPCs → Morphosis Root → Garden → Forbidden Library
```

---

## 7. Monster and mob system

### 7.1 Monster hierarchy

| Tier | Count | Traits |
|---|---|---|
| Regular Monsters | 200+ | Common spawns, fast respawn (~minutes) |
| Mini-Bosses | 71 | Higher stats, 15–30 min respawn, better drops; become invulnerable after 5s of no damage |
| Bosses | 40+ | Strongest named per map, 12–60 min respawn, rare drops |
| Raid Bosses | 9 | Multi-player endgame, Lv 50–52, located in end-game zones |

**Behavior types:** Neutral (won't attack unless provoked — Kooii, Leaf Boar) and Aggressive (attacks on sight — Continental Bulldozer, Woopa). Attack styles: Melee or Ranged.

### 7.2 Sample monster data

| Monster | Level | HP | Zone | Aggressive | Respawn |
|---|---|---|---|---|---|
| Kooii | 1–2 | ~20–30 | Training Ground | No | Fast |
| BULLDOZER (Boss) | 7 | 212 | Woody Forest | No | 12 min |
| BULLDOZER'S BROTHER (Boss) | 8 | 262 | Woody Forest | Yes | 15 min |
| Continental Bulldozer | 12 | 178 | Various | Yes | 3 min |
| Sleepy Kooii (Mini-Boss) | 20 | 713 | Lighthouse 2F | Yes | ~15 min |
| DARKJUNO (ID Boss) | 38 | 34,500 | Impassable Cave | Yes | N/A (instanced) |
| Flower Bulldozer | 42 | 3,800 | Morphosis | Yes | 1 hour |
| Overload | 50 | Very high | Morphosis | Yes | ~30 min |

### 7.3 Boss and raid boss roster

**Raid bosses (all Lv 50–52):** BIGMAMA (Hot Sand Plains), CALIGO (Sky Castle), Ukpana (Marsh of Death), Darlene the Witch (Maze Forest), Barslaf (Islot's Temple), Illust (Anguish Altar), Sephia (Lunarf), Aiyo's Protector (Morphosis Root, Lv 52), Platanista (Morphosis Garden, Lv 52).

Notable bosses by zone: FUNGUS KING (Lv 12, Marshland), CHIEF WOOPAROOPA (Lv 14, Beach), TURTLE Z (Lv 18, Beach), RECLUSE (Lv 15, LH 1F), BLACKSKULL (Lv 23, LH 1F), DEVILANG (Lv 34, LH 5F), BLACKJUNO (Lv 25, Temple), BLACKSKY (Lv 30, Temple), WHITE CROW (Lv 19, Lanos Plains), 777TAILFOX (Lv 20, Forest of Grave), GHOSTSNAKE (Lv 27, Forest with Ruins), CHIEF MAGIEF (Lv 28, Kataru Mtns), SHAAACK/BSSSZSSS (Lv 40, desert zones).

### 7.4 Drop competition

**Boss loot goes to whoever dealt the most total damage.** This is the core boss-farming competition mechanic. Mini-bosses have a **5-second no-damage invulnerability** trigger that regenerates them to full HP, preventing idle camping.

---

## 8. PK and PvP system

### 8.1 Name color (PK status) system

| Color | Trigger | Duration | Penalties |
|---|---|---|---|
| **White** | Default; attacking red/black names | N/A | None; full NPC access |
| **Purple** | Attacking another player (must deal damage); healing a PKer; using debuffs on players | ~1 min after combat ends | Combat flag; killing a purple does NOT cause red |
| **Red** | Killing a white-named player (directly or indirectly) | ~3 hours online (pauses offline) | 2× fame loss on death; NPC access restricted |
| **Black** | Dying while red-named | 1 hour hard timer (pauses offline) | Cannot gain PvP fame; 2× fame loss; NPC access restricted |

**Key rule:** Using a Recall Scroll while purple prevents going red, even if the attacked player subsequently dies. Killing an enemy faction player in your own village does NOT give red name.

### 8.2 Fame and honor system

Fame is the primary PvP ranking metric displayed as badge icons next to character names. **Max fame: 99,999.** Gained from PvP kills (scaled by target level, equipment, fame), Battlefield participation (fame = your level), Arena, boss kills, and Honor Manager NPC (10 fame per Lanos/Siras Coin, capped at Fighter rank). **Lost on PvP death** (proportional to level difference; **2× loss if red/black named**). Cannot gain fame from the same person more than **3 times per hour**.

Fame ranks: Recruit → Scout → Combat Soldier → Veteran Soldier → Apprentice Knight → Fighter → Elite Fighter → Field Commander → Commander → General. Yellow icons for Siras, blue for Lanos.

### 8.3 Faction system (Siras vs. Lanos)

**Empire of Siras** (leader: Emperor Kanos, allied with Secret Phantom wizards, guarded by Elite Knights). **Kingdom of Lanos** (leader: King Roberto, allied with Earl Christin and Captain Crymson Saver, guarded by Elite Mercenaries). Cannot damage same-faction players. Cross-faction chat is encoded into gibberish. In enemy villages, defenders can attack freely without going red.

### 8.4 Battlefield (RvR event)

Occurs every **2 hours**, lasts **15–20 minutes**. Objective: destroy enemy faction's Treasure Egg (3 eggs to win). **Level brackets:** BF1 (10–19), BF2 (20–29), BF3 (30–39), BF4 (40–46), BF5 (47+). Max balanced at 1.5:1 ratio. **Rewards:** Winning faction gets 3 Pendants of Honor + 20% EXP boost for 1 hour. Losing faction gets 1 Pendant. Stars: +2 per kill, −2 per death (max 30). No name color changes from Battlefield combat.

### 8.5 Arena (2v2 PvP)

Held every **10 minutes**. Enter with a partner within 5 levels. Fight 2 opposing-faction players within 5-level range. Winners get 3–5 Scrolls of Arena + fame; losers get 1–2 scrolls. Arena Score starts at 1000; tracked on leaderboard.

---

## 9. Economy and trading

### 9.1 Currencies

**Gold** (primary, max 999,999,999 in inventory). **Platinum** (premium, 100 Plat ≈ $1 USD ≈ 70K–100K gold in player market). **Pendants of Honor** (Battlefield reward currency). **Dungeon Tokens** (Ancient Dungeon). **Guild Coins** (guild activities). **Inotia Tokens** (Inotia War events).

### 9.2 Trading systems

**Black Trader** (cross-faction marketplace, located in villages/beach/castles): Items listed for 24 hours. Fee structure: **~5% total** (1.5% deposit upfront + 3.5% commission on sale). Requires Premium Member ticket (200 Platinum) to sell; buying is free. Starting 5 slots, expandable to 20 via Trader Expand tickets (99–200 Plat each, sequential).

**Trader** (faction-only marketplace): Same mechanics, fees are **20% of Black Trader's fees** (much cheaper). Fewer items due to faction restriction.

**Direct player-to-player trading** also available via trade window. High-value trades often use middlemen.

### 9.3 Key gold sinks

Combine Books (up to 550K gold), enchant scroll crafting (up to 800K gold per recipe), Black Trader 5% tax, NPC supplies, Recall Scrolls (110g each), Airship tolls (777g), Crown Rock teleport (2,500g), dungeon entry fees (2,500–10,000g), guild creation (10K gold), union creation (100K gold), Oblivion Potion (11K gold).

### 9.4 Money-making progression

Levels 1–14: Sell NPC drops (Practice Swords for 250g at Secret Vendors). Levels 5–19: Farm mini-boss skill books (Wild Charge II ≈ 1M gold). Levels 15–20: Farm Testing Woopas for Sturdy Leather Gloves and enchant scrolls. Levels 20–30: Farm Woopas/Roopas/desert mobs for crafting materials. Levels 30+: Desert monster farming, boss hunting. Levels 40+: HSP parties for A-Class enchant scroll drops (400K–700K gold each).

---

## 10. Party and social systems

### 10.1 Party mechanics

**Party size: 4 players max.** XP splits evenly among members within ~3 levels of each other. Large level gaps prevent XP sharing. Common composition: Warrior (tank/lure) + Mage (healer) + Rangers (DPS). Loot drops on ground; boss loot goes to highest damage dealer. **Pet Looting** (auto-loot, introduced v2.5.0) activated by Perr Berry Seed (2h) or Perr Berry (30 days).

### 10.2 Chat channels

All Chat (nearby same-faction), Party Chat, Guild Chat, Union Chat (Crown Rock only), Whisper/PM. Cross-faction chat is garbled; only emoticons transmit across factions.

### 10.3 Friend system

Standard friend list via in-game menu. **Whirl of Summoning** (99 Platinum for 5) teleports a party member to your location.

---

## 11. Guild system

### 11.1 Guild creation and management

**Cost: 10,000 gold** at Guild Manager NPC. Cannot create with red/black name. Creator becomes Guild Master. **Three ranks:** Guild Master (all permissions), Guild Officer (add members, change mark), Guild Member (standard). Guild Master can delegate leadership to an Officer. **Symbol of Guildmark** (150 Platinum or 50K–150K gold on BT) required to set guild logo.

### 11.2 Guild battles (Crown Rock)

Formal guild siege system at Crown Rock. Entry: 2,500 gold teleport. Two battle areas: Syphnel Road (30 flags max) and Serain's Altar (10 flags max). Attack team destroys flag post; defense uses summoned Warriors/Rangers. **No EXP loss** and **no name color change** during guild battles. Season rewards include Glorious Costume set, cloaks, Ancient Coins, and Phoenix Hearts.

### 11.3 Union system

Alliance of **up to 3 guilds**, created for 100,000 gold at Crown Rock NPC. Union chat available during guild fights. Optional Symbol of Unionmark (1,000 Platinum).

### 11.4 Guild Dungeon

**Almighty Crown Lieti** (Level 30 required). Party of up to 4. **10-minute time limit.** Drops Proeta Pieces (crafted into Paradox of Omnipotence, Arrogance of Omniscience, or Ancient Coins). **Guild Shop** uses Guild Coins for Equipment Stamps, Skill Point Potions, and other items.

---

## 12. Quest system

### 12.1 Main quest lines

**Lighthouse Quest** (Lv 15+): Multi-step quest involving Clovers, Wild Boar Meat, Innkeeper's Recommendation, and Kulin's tasks. Unlocks Lighthouse Dungeon access. **Pirate Ship Quest:** Collect 3 Old Cloth pieces from Beach bottles → get Old Cloth Map → buy Rusty Key (5,500g) → enter hidden dungeon. **Edvant's Quest:** Collect Old Female Ring + Female Bone → multi-step investigation → rewards Ring of Virgin (+2 INT) and Ganoderma Potion (best potion, 5-sec cooldown). **Seruang's Tear Quest:** High-level quest rewarding a key endgame crafting ingredient.

### 12.2 Repeatable and daily quests

**Supply Manager dailies:** Reliable Adventurer (free Awaken Kooii Doll), Proof of Bravery (1 Pendant → Donguri Card Box), Supply Request (materials → Grade Symbol, doubled on weekends). **Pirate Coin turn-in** (10 coins → 300 EXP + 800/1,000g, Lv 11+). **Ancient Coin turn-in** (3 coins → Level×50 EXP, primary late-game leveling).

### 12.3 Collection quests

**Donguri Card Set Quest:** Collect 5 cards (10, J, Q, K, A) → Donguri Hat (6 color variants with different stat bonuses, Red being rarest at 500K+ gold). **Kooii Card Set Quest** and **Pirate Hat Quest** follow similar patterns. **Collection System** (introduced May 2021): Register items across General/Costume/Pet/Monster categories for permanent stat bonuses shared across all characters on same server.

---

## 13. Pet system

### 13.1 Pet mechanics

Pets occupy a dedicated slot, providing stat bonuses (HP, Deadly Strike, Critical, sometimes EXP boost). Only one pet active at a time. **Common pets** from event boxes; **Rare pets** (always +1.0 Deadly Strike more) from Pet Eggs (500 Platinum, ~15% chance to get a pet).

### 13.2 Pet enchantment

Via NPC Dingo using three stones: **Stone of Life** (+1 HP per enchant, max 117), **Stone of Death** (+0.1 Deadly Strike per enchant, max 6.5), **Stone of Mystery** (+1 random stat: DEX/STR/INT or +10 HP/MP, can re-roll). Pet Crystals (from Pet Eggs and boss drops) are a key ingredient.

### 13.3 Pet auto-loot

Activated by Perr Berry Seed (2h) or Perr Berry (30 days). Character auto-picks up nearby loot. Timer counts down even when feature is toggled off.

---

## 14. Mount system (recently added)

**Azugar** (released April 2025): Movement Speed 105, +7 HP. **Ganesha** (released June 2025): Movement Speed 115, +10 HP. Obtained through Soul Fragment → Soul Shard → Soul Stone crafting chain.

---

## 15. Additional systems

### 15.1 Instant Dungeons

Six instanced party dungeons (up to 4 players, 1-hour time limit): Ghost Ship (Lv 20), Forgotten Cave (Lv 25), Silent Altar (Lv 30), Impassable Cave (Lv 35), Desert Dungeon (Lv 40), Endless Darkness (Lv 40). Weekly try reset; can reset early with Gemstones. Ranking system tracks scores.

### 15.2 Ancient Dungeon (scheduled event)

Every **60 minutes** (30 min offset from Battlefield). 8 randomly selected players. 5-minute boss battle (DEATHBONE, BREMEN, or MEDUSA by level). Rewards Dungeon Tokens exchangeable for items (Ring of Ancient = 150 tokens).

### 15.3 Costume system

Cosmetic overlays over equipment (7 costume slots + 3 costume gem slots). From Random Costume Boxes (300 Platinum), crafting, or events. **Costume Gems** provide stat bonuses (STR/INT/DEX 1–4, HP/MP 10–40) plus special effects. **Closet System** converts costumes to "Dresses" shared across all characters on same server.

### 15.4 Recall and respawn

**Recall Scroll** (110g, stackable to 250): Teleports to registered respawn point. Respawn points set at Innkeepers. Death offers: respawn at village, respawn at registered location, or Phoenix Heart (on-spot revival). Faction-specific Recall Scrolls (2,310g) teleport to specific castles.

### 15.5 Seasonal events

Monthly events with themed Event Boxes dropped by all monsters (containing pets, Ancient Coins, potions, enchant scrolls). Events include Valentine's Day, Halloween, Christmas, Anniversary, Lunar New Year. **Hot Time Events** provide scheduled EXP+ATK 10% boosts. **Inotia War** cross-game collaboration events with special token shops.

### 15.6 Server architecture

**8 global English servers** (TurtleZ, Devilang, Caligo, Ganesha, etc.). Up to **6 characters per server** per account. Cross-server play not supported. PC Beta available via withhive.com.

---

## Implementation notes for C++ engine

### Key data structures needed

**`CharacterStats`**: 5 base stats (STR/INT/DEX/CON/WIS, cap 18), derived stats (HP/MP/atk/def/crit/deadly/evasion/block/hitrate), current level, current XP, fame, faction enum, class enum. HP/MP must recalculate fully on stat changes (retroactive scaling by level × CON/WIS).

**`RageCrystalSystem`**: float currentCrystals, int maxCrystals (2 + level/10), generation rates per hit type (0.5 normal, 1.0 crit/deadly, 0.2 per damage received for Warriors only). Consumed by skills.

**`DamageCalculation`**: Physical = weapon_range(min,max) + stat_modifier − target_armor. Magic = skill_base + INT_modifier, partially ignoring armor. Critical = 2.0× multiplier. Deadly = 3.0× multiplier. Doom debuff = 2.0× next hit. Armor = flat subtraction (approximate ratio, not 1:1 at high values).

**`PKStatusSystem`**: Enum {WHITE, PURPLE, RED, BLACK}. Purple after dealing player damage (1 min timer). Red on killing white-named (3h online timer). Black on dying while red (1h hard timer). Fame multipliers: 2× loss/gain for red/black. Same-target kill cap: 3/hour.

**`EnchantSystem`**: Safe thresholds (weapon +6, armor +4). Above threshold: success_rate decreases per level (30%/10%/5%). On failure above threshold: item breaks (soulbound, unusable). Holy Water: failure resets to +0 instead of breaking. Each +1 adds approximately +1 damage (weapons) or +1 armor (defensive).

**`WorldMap`**: 11 regions, each containing multiple zones. Zone properties: level_range, pvp_enabled, faction_alignment, connected_zones[], npc_list[], monster_spawn_table[]. Sky Castle requires hourly rotation logic with faction-based path selection.

**`MonsterAI`**: Two behavior modes: NEUTRAL (attacks only when provoked) and AGGRESSIVE (attacks on sight within aggro range). Mini-boss invulnerability trigger after 5 seconds of no incoming damage. Boss loot allocation to highest-damage player.

**`SkillSystem`**: 49 total skill points (1 per level from 2–50). Skills require skillbook items to unlock. Each volume costs 1 SP. Oblivion Potion resets all SP. Skills have: mp_cost OR crystal_cost, cooldown_seconds, target_type (SELF/SINGLE/AOE/PARTY), damage_multiplier, effect_type (DAMAGE/HEAL/BUFF/DEBUFF/CC), duration_seconds for buffs.

This document provides the foundational reference for all major TWOM systems. Where exact formulas are unavailable (particularly damage calculation coefficients and XP-per-level tables), the qualitative relationships and boundary conditions documented here should guide reverse-engineering through playtesting or further wiki mining of the 4,400+ individual pages.