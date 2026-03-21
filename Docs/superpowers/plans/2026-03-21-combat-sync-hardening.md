# Combat Sync Hardening & Equipment Wiring

Session date: 2026-03-21
Test count: 478 (all passing, 0 failures)

## What was done

Audited the engine against a server-authoritative combat sync research document covering 8 domains.
Fixed all identified gaps, wired equipment stats, and resolved edge-case bugs found during audit.

### Protocol enrichment

- **SvSkillResultMsg** — replaced isCrit/isKill/wasMiss with: `hitFlags` bitmask (HIT|CRIT|MISS|DODGE|BLOCKED|ABSORBED|KILLED), `overkill`, `targetNewHP`, `resourceCost`, `cooldownMs`, `casterNewMP`
- **SvPlayerStateMsg** — added derived stat snapshot: `armor`, `magicResist`, `critRate`, `hitRate`, `evasion`, `speed`, `damageMult`, `pkStatus`
- **SvLevelUpMsg** (0xA6) — new packet sent on level-up with full stat snapshot; wired via `onLevelUp` callback
- **CmdEquipMsg** (0x1D) — new client→server equip/unequip command
- **HitFlags** namespace added to `game_types.h`

### Status effects & replication

- Fixed `statusEffectMask` in `replication.cpp` — was hardcoded to 0, now reads live bitmask via `StatusEffectManager::getActiveEffectMask()`
- Fixed `deathState` in `replication.cpp` — was hardcoded to 0, now reads from `isDead`/`isAlive`
- Wired `StatusEffectManager::tick()` into server tick loop (DoT ticking was dead code)
- Wired `CrowdControlSystem::tick()` into server tick loop
- Wired `onDoTTick` callback — applies damage, broadcasts SvCombatEvent, triggers death if HP <= 0
- Wired `onDied` callback — clears all status effects and CC on death

### PK system

- PK transitions: White→Purple (attacking innocent), Purple/White→Red (killing non-flagged)
- Decay timers from PKCooldowns constants (Purple=60s, Red=30min)
- Combat timer (5s, refreshed on PvP damage, blocks equipment changes)
- Wired on BOTH skill path and auto-attack path (auto-attacks were missing PK logic)

### Equipment system (fully wired)

- `processEquip()` — validates not dead, not in combat, delegates to `Inventory::equipItem()`/`unequipItem()`
- `recalcEquipmentBonuses()` — clears + applies all equipped item bonuses + recalculates derived stats + clamps HP/MP
- `applyItemBonuses()` on `CharacterStats` — maps every rollable StatType to the correct `equipBonus*` field, including sockets and stat enchants
- DB load fix — equipped items now placed into actual equipment slots (was dumping into inventory bag)
- HP/MP clamp moved to AFTER equipment bonuses are applied on connect (was clamping to naked maxHP)
- `onEquipmentChanged` callback wired — any equip change triggers stat recalc + player state sync
- Execute threshold/damage bonus from equipment wired into skill execution (stacks with per-skill values)

### Edge-case bugs fixed

| Bug | Severity | Fix |
|---|---|---|
| DoT death → `removeAllEffects()` invalidates iterator during `tick()` | Critical | Deferred clear via `ticking_`/`pendingRemoveAll_` flags |
| PvP auto-attacks skipped PK transitions + combat timer | Critical | Added same PK logic as skill path |
| Lifesteal used stale pre-damage HP snapshot for cap | High | Reconstructed pre-hit HP from post-damage state |
| `passiveStatBonus` applied after derived stats consumed it | High | Moved to top of `recalculateStats()` |
| HP/MP clamped before equipment bonuses loaded on connect | High | Moved clamp after `recalcEquipmentBonuses()` |
| HP could exceed maxHP after unequipping gear | Medium | Added HP/MP clamp in `recalcEquipmentBonuses()` |
| `auth_protocol.h` missing `protocol.h` include | Low | Added include |

### Misc

- Player timer ticking added to server loop (PK decay, combat timer, respawn countdown)
- Respawn invulnerability explicitly NOT added (user wants spawn camping to be possible)

---

## Dead equipment bonus fields (do not use)

These `equipBonus*` fields exist in `CharacterStats` but have NO item stat roller mapping — items cannot roll these stats.
Tagged with `// DEAD` comments in `character_stats.h`.

- `equipBonusPhysAttack` — no "phys_attack" in ItemStatRoller::statNameMap
- `equipBonusMagAttack` — no "mag_attack" in ItemStatRoller::statNameMap
- `equipBonusPhysDef` — no "phys_def" in ItemStatRoller::statNameMap; origin unknown

If these are ever needed, add mappings to `ItemStatRoller::statNameMap()` and `buildReverseStatMap()` first.

## Equipment bonuses not yet wired (items CAN roll them, but no consuming system exists)

| Field | Rolls via | Needs |
|---|---|---|
| `equipBonusHPRegen` | "hp_regen" | HP regen tick system (passive heal per second) |
| `equipBonusMPRegen` | "mp_regen" | MP regen tick system (passive mana per second) |
| `equipBonusAttackSpeed` | N/A (no mapping) | Stat roller mapping + auto-attack cooldown integration |
| `equipResistFire` | N/A (no mapping) | Elemental damage type reduction in skill execution |
| `equipResistWater` | N/A (no mapping) | Same |
| `equipResistPoison` | N/A (no mapping) | Same |
| `equipResistLightning` | N/A (no mapping) | Same |
| `equipResistVoid` | N/A (no mapping) | Same |
| `equipResistMagic` | "magic_resist" via equipBonusMagDef | Already wired (magic_resist → equipBonusMagDef → _magicResist) |

## Files modified (17 files)

- `engine/net/auth_protocol.h` — added missing protocol.h include
- `engine/net/game_messages.h` — enriched SvSkillResultMsg, added CmdEquipMsg, SvLevelUpMsg
- `engine/net/net_client.cpp` — added SvLevelUp dispatch
- `engine/net/net_client.h` — added onLevelUp callback
- `engine/net/packet.h` — added SvLevelUp (0xA6), CmdEquip (0x1D)
- `engine/net/protocol.h` — added derived stats to SvPlayerStateMsg
- `engine/net/replication.cpp` — fixed statusEffectMask and deathState from components
- `game/game_app.cpp` — updated skill result handler for hitFlags
- `game/shared/character_stats.cpp` — applyItemBonuses, passive stat ordering fix, PK methods, timer ticking
- `game/shared/character_stats.h` — PK/combat/timer fields, applyItemBonuses, dead field tags
- `game/shared/game_types.h` — HitFlags namespace
- `game/shared/skill_manager.cpp` — lifesteal HP cap fix, execute threshold equipment wiring
- `game/shared/status_effects.cpp` — getActiveEffectMask(), iterator-safe tick with deferred removeAll
- `game/shared/status_effects.h` — getActiveEffectMask(), ticking_/pendingRemoveAll_ fields
- `server/server_app.cpp` — equipment wiring, DoT/CC/timer ticking, PK transitions, callbacks, DB equip load fix
- `server/server_app.h` — processEquip, recalcEquipmentBonuses declarations
- `server/target_validator.h` — isAttackerAlive, canAttackPlayer
- `tests/test_network_robustness.cpp` — updated SvSkillResultMsg round-trip for new fields
- `tests/test_target_validator.cpp` — removed old 2-arg isInAOI test
