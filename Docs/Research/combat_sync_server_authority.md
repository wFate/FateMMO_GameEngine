# Server-authoritative combat sync for a TWOM-style 2D MMO

**The client should run zero combat math.** Every production MMO that has survived at scale — WoW, FFXIV, Ragnarok Online — treats the client as a display terminal for combat results, sending only player intent ("use skill X on target Y") and receiving authoritative outcomes from the server. MapleStory's history of client-trusted damage calculations leading to catastrophic x400 damage multiplier exploits and GM-skill injection proves this conclusively. For a stat-check MMO at 20 tick/sec with 100–300ms mobile latency, the 150–350ms delay between attack animation and damage number display is imperceptible in the combat rhythm — Ragnarok Online has operated this way for over two decades. The current architecture of running full parallel damage pipelines on both client and server with independent RNG, divergent status effect timing, and no reconciliation protocol is the single highest-risk design flaw in the engine and should be replaced with a clean intent→result model.

This report provides concrete architectural recommendations across all eight combat synchronization domains, grounded in TrinityCore, rAthena, and FFXIV emulator implementations, GDC talks from Blizzard and Bungie, and the canonical networking literature from Gabriel Gambetta and Glenn Fiedler.

---

## 1. The client should be a display terminal, not a combat simulator

The core architectural question — where this game sits on the "dumb client" vs "smart client" spectrum — has a clear answer backed by every major MMO's production history.

**WoW's packet-level evidence is definitive.** The client sends `CMSG_CAST_SPELL` (intent + target ID). The server processes the entire spell pipeline, then replies with `SMSG_SPELL_GO` containing explicit hit/miss target lists and `SMSG_SPELLNONMELEEDAMAGELOG` carrying final damage, crit flags, absorb amounts, and resist values. The client displays floating combat text and updates health bars **only upon receiving these server packets**. TrinityCore's reverse-engineering confirms that WoW's client has no independent damage calculation — it is purely a renderer of server-determined outcomes. The one exception is movement, which WoW makes client-authoritative (leading to well-known speed and teleport hacks).

**FFXIV takes this even further** with a ~3.3 Hz server tick for combat snapshots, creating the notorious "ghost hit" problem where players visually dodge AoE telegraphs but are hit because the server's position snapshot still had them inside. FFXIV proves that even a 300ms combat resolution delay is tolerable when the game design accommodates it. **Ragnarok Online** uses identical server-only combat resolution — rAthena's `battle_calc_attack()` computes all damage server-side, sends results via `clif_skill_damage()`, and the client simply renders them.

Three options exist for your architecture, with dramatically different tradeoff profiles:

**Option A — No client combat math (recommended).** Client sends intent, plays attack animation immediately for responsiveness, waits 150–350ms for server results, then displays damage numbers and health changes. Implementation complexity is lowest, anti-cheat protection is strongest, and no reconciliation code is needed. Every major stat-check MMO uses this model. The 150–350ms gap between swing animation and damage number feels natural in auto-attack/skill-based combat where players click targets and wait.

**Option B — Predicted combat math with reconciliation.** Client runs identical formulas, shows instant damage numbers, then corrects when server disagrees. This is catastrophic for MMO combat: RNG divergence is **unsolvable** when multiple players attack the same target (the client cannot predict what damage 4 other players will produce or whether they'll kill the target first). Reconciliation artifacts — damage numbers changing, false kills, health bar jumps from unobserved attacks — destroy player trust. No production MMO uses this for stat-check combat. FPS games like Overwatch use prediction for **spatial hit detection** (an entirely different problem), not damage math.

**Option C — Full combat math as cosmetic overlay.** Client runs formulas for instant visual feedback but only updates actual HP from server data. This creates the worst confusion: players see damage number "500" but the health bar drops by a different amount. It ships full combat formulas to the client (security liability) while providing no gameplay benefit over Option A.

The authority split should follow this model:

| Authority level | Systems |
|---|---|
| **Server-only** (no client prediction) | Damage resolution, hit/miss/crit rolls, status effect application/ticking/expiry, death determination, HP/MP/fury values, lifesteal/shield calculations, XP/gold/loot, PK status, item transactions, derived stats |
| **Both sides with server correction** | Player movement (client predicts, server validates), cast bar display (client starts, server confirms or cancels), cooldown timers (client displays, server enforces), visual effects (client renders server-confirmed events) |
| **Client-authoritative with server validation** | Input intent ("attack target X"), target selection, camera/viewport, facing direction |

**Reconciliation for the remaining prediction surfaces** follows established patterns. When the server rejects a cast (insufficient mana, out of range, target dead), it sends a failure packet and the client cancels the animation — this mirrors WoW's `SMSG_SPELL_CAST_FAILED`. Health bars should never be driven by client prediction; they animate smoothly toward the last server-confirmed value over 100–200ms. Death should **never** display on the client until the server confirms it — Gabriel Gambetta's articles explicitly advise against predicting kills even when local HP calculations show zero.

---

## 2. Stat synchronization needs full server snapshots, not dual computation

The current dual-computation model where both sides run `recalculateStats()` independently is a permanent divergence risk. If equipment bonuses or passive skill bonuses differ by even one point due to timing, every derived stat (armor from VIT, hit rate from STR/DEX, crit rate from DEX + equipment, damage from primary stat) diverges and stays diverged until a full correction arrives.

**The solution is server-only derived stats with periodic full snapshots.** The server computes all derived stats authoritatively and sends them in `SvPlayerState` at 1–2 Hz or on any stat-changing event. With ~20 derived stats at 2–4 bytes each, the full snapshot costs roughly **50–80 bytes** — negligible bandwidth at any send rate. The client never runs `recalculateStats()` for gameplay purposes; it only uses those formulas for tooltip previews ("if I equip this item, my armor would be...") without applying predicted values to actual state.

For HP/MP/fury reconciliation when server corrections arrive, a **tiered approach** handles all cases cleanly:

- **Ignore-if-close** (≤2% of max): Suppress micro-jitter from regen tick timing differences. Players cannot perceive a 1–2% HP difference.
- **Interpolate** (2–10% of max): Lerp toward server value over 150ms. Smooth enough to avoid jarring jumps while correcting meaningful drift.
- **Snap** (>10% of max): Something unexpected happened (unobserved damage from another player, server-side event). Immediately set to server value. The visual jump is acceptable because it corresponds to a real gameplay event the player needs to know about.

**Fury generation** presents a specific challenge: the client wants to show the fury bar filling on hit for UI responsiveness, but if the server says the hit was actually a miss, the client added fury that doesn't exist. The cleanest solution is making fury display purely cosmetic — show the bar filling based on server-confirmed hits only, and **never let predicted fury trigger a fury-spending ability**. Fury abilities should only enable when server-confirmed fury ≥ cost. The 150–350ms delay before the bar fills is hidden by hit animation timing.

**Level-up must be an explicit server event**, not client-detected XP overflow. If the client detects level-up before the server confirms the kill that granted the XP, it triggers `recalculateStats()`, restores HP/MP to max, and creates a massive desync across every stat. The server should send `SvLevelUp { newLevel, newMaxHP, newMaxMP, newCurrentHP, newCurrentMP, newBaseStats }` as a reliable message, and the client should hard-snap all values.

**Equipment changes should be server-authoritative with optimistic UI.** Client immediately shows the item in the equipment slot visually but does **not** recalculate stats. Stats display remains at old values (or shows "...") until the server sends `SvEquipConfirm` with a full stat block. Equipment changes are infrequent enough (seconds between equips at fastest) that the 150–350ms wait is imperceptible.

---

## 3. Status effects must be purely server-authoritative with absolute expiry timestamps

WoW and Ragnarok Online both treat status effects as entirely server-owned state. TrinityCore manages all aura application, duration, ticking, stacking, and removal server-side, sending `SMSG_AURA_UPDATE` packets with delta updates (`addedAuras`, `removedAuraInstanceIDs`, `isFullUpdate`). rAthena's `status_change_start()` handles all status logic on the server and broadcasts icon changes via `clif_status_change()`. Neither system involves the client in status effect computation.

**DoT damage (Bleed, Burn, Poison) should tick server-only.** DoT intervals are typically 1–3 seconds; a 200ms delay on seeing a tick is imperceptible relative to that interval. If both sides tick DoTs, timing drift means the client and server diverge on *when* ticks happen — after 10 seconds, they could be off by ±1 tick, showing wrong HP values. The server processes each DoT using absolute tick scheduling:

```cpp
struct DoTInstance {
    StatusEffectType type;
    int32_t damagePerTick;
    uint32_t nextTickServerTick;  // absolute server tick number
    uint32_t expiryServerTick;
    EntityId source;              // for kill credit attribution
};
```

The client receives `SvDamageEvent { targetId, amount, damageType: DOT_BURN, sourceId }` and renders the floating number. HP updates arrive in the next `SvPlayerState`.

**Crowd control (Stun, Freeze, Root, Taunt) should not be predicted client-side.** When the server applies a stun, the packet takes 100–300ms to reach the client. During this window, the player can still input commands, but the server silently drops them. Players experience this as "slight input loss" — the universal MMO experience that WoW players have accepted for 20 years. Predicting CC on the client would create the far worse experience of **false stuns** where the client locks the player out but the server says the stun was resisted or immuned. For break-free abilities, play a "breaking free" animation immediately on button press (cosmetic), but keep the player stunned until server confirms removal.

**Duration tracking must use absolute expiry timestamps, not countdowns.** The client ticks with variable deltaTime (frame-dependent), while the server ticks at fixed 50ms. Over 10 seconds, these drift ±200ms. WoW's `UnitAura` API returns `expirationTime` as an absolute value — the client computes remaining duration as `expirationTime - GetTime()`. Your system should send `expiryServerTick` for every effect, and the client converts using its synchronized server-tick estimate.

**With only 16 status effect types, full list synchronization is optimal.** At ~8 bytes per effect, the full list costs **128 bytes maximum** — trivial bandwidth. Include the full active effect list in `SvPlayerState` with an `effectListVersion` counter. When the version changes, the client replaces its local list entirely. This eliminates all stacking and priority conflicts: if the server says the list is [A, B, D] (because another player cleansed C), the client immediately adopts that list. Delta encoding saves negligible bandwidth for 16 effects but adds significant reconciliation complexity — not worth it.

---

## 4. Skill results need rich packets and cooldowns need drift tolerance

The current `SvSkillResult` sending only `{entityId, skillId, targetId, damage, isCrit=0}` is critically insufficient. WoW's `SMSG_SPELLNONMELEEDAMAGELOG` contains damage, overkill, school mask, absorb amount, resist amount, crit flag, and blocked amount. WoW's `SPELL_MISSED` event specifies the miss type (DODGE, PARRY, IMMUNE, RESIST, etc.). rAthena's `clif_skill_damage()` sends source, target, tick, animation delays, damage, hit count, skill ID, and damage type.

**The recommended `SvSkillResult` structure:**

```cpp
struct SvSkillResult {
    uint32_t serverTick;
    uint32_t casterId;
    uint16_t skillId;
    uint8_t  hitCount;          // number of targets (for AOE)
    struct TargetResult {
        uint32_t targetId;
        int32_t  damage;        // final post-mitigation damage
        int32_t  overkill;      // damage beyond lethal (0 if alive)
        uint16_t targetNewHP;
        uint8_t  hitFlags;      // bitmask: HIT|CRIT|MISS|DODGE|BLOCKED|ABSORBED
        uint8_t  killedTarget;
        uint8_t  effectsApplied[];  // status effects this skill applied
        uint8_t  effectsRemoved[];  // status effects broken by this skill
    } targets[];
    uint16_t resourceCost;      // mana actually consumed
    uint16_t cooldownMs;        // authoritative cooldown duration
    uint16_t casterNewResource; // caster's new mana value
};
```

**Cooldown drift** is a concrete problem at 20 tick/sec: the client's cooldown timer expires 50–100ms before the server's, the player sends a skill request, and the server rejects it as "on cooldown." TrinityCore checks cooldowns authoritatively and returns `SPELL_FAILED_NOT_READY`. The mitigation is a three-layer approach: the client adds a **+100ms buffer** to displayed cooldowns (showing "ready" slightly after the true local expiry), the server grants a **1-tick (50ms) forgiveness window** (queuing requests that arrive within 50ms of expiry rather than rejecting them), and the server sends `cooldownMs` in every skill result so the client can resync its timer.

**AOE targeting must be server-determined.** The client sends `{ skillId, targetPosition: {x, y} }` — never an entity list. The server runs `map_foreachinrange()` (rAthena's pattern) against its authoritative entity positions and returns per-target results. The client plays the AOE VFX immediately at the target position (cosmetic prediction) but does not deduct HP until server results arrive. When 5 targets are visible client-side but the server only hits 3 (because 2 moved), the server's word is final.

**The double-cast instant-cast window** (TWOM's mage chain-casting mechanic) should be tracked with a generous server-side window. If the design window is 500ms, use **550ms** server-side (design + 1 tick) to absorb RTT variance. The client mirrors the logic for animation prediction. If the server rejects the instant cast because the request arrived outside the window, it responds with a normal cast-time result, and the client snaps to a cast bar. Since TWOM's 2023 removal of chain casting on iOS was extremely controversial (it was "the only way mages could compete"), preserving this mechanic with latency tolerance is important.

**Skill interruption** (stun during cast) should follow the mobile-friendly pattern: mana deducted at **cast completion, not start**. At 100–300ms latency, "mana wasted on interrupted cast" feels terrible on mobile. Interrupted skills get **50% cooldown** — preventing spam-retry while not fully punishing the player. The server sends `SvSkillInterrupted { casterId, skillId, reason: STUNNED, cooldownMs }`.

---

## 5. PvP synchronization requires consolidated multipliers and snapshot-based resolution

The current split of PvP damage multipliers — **0.05x** for auto-attacks hardcoded in one file, **0.30x** for skills in another — is a maintenance and balance hazard. rAthena solves this with `skill_damage_db.txt`, a single data table specifying per-skill, per-context damage adjustments: `SkillName,Caster,Map,DmgVsPlayers,DmgVsMobs,DmgVsBosses`. All PvP multipliers should live in **one configuration file** as a skill-indexed table, never scattered across source files.

For PvP damage scaling design, the tiered formula `PvPDamage = PvEDamage × PvPSkillMultiplier × LevelDifferenceMultiplier × TargetPvPReduction` provides good control. Level difference scaling with diminishing returns (`levelMult = clamp(1.0 - (attackerLvl - defenderLvl) × 0.05, 0.3, 1.2)`) prevents high-level griefing. The Ragnarok X mobile approach of applying a **power-curve compression** (`finalDamage = rawDamage^0.7` in PvP) effectively compresses the gap between whale and F2P players without flattening it entirely.

**PK status transitions** should follow the Lineage 2 model: Innocent (White) → Aggressor (Purple) on attacking another Innocent → Criminal (Red) on killing a non-flagged player. The server is sole authority on transitions. The client can **optimistically display** the local player as Aggressor when they initiate an attack (since this is nearly always correct), but other clients see the color change only when the server broadcasts `SvPKStatusChanged`. For simultaneous attacks on the same tick, process in entity ID order (deterministic) — the first attacker becomes Aggressor, the second is "retaliating." Neither gets Criminal status from mutual combat; Criminal only triggers on killing a non-flagged target.

**Posthumous actions** (client sends attack after server-side death) should be **rejected for all packets arriving after the death tick**, but a **same-tick grace period** should exist. The recommended pattern: collect all pending actions for a tick, calculate all damage using snapshot state from tick start, apply all damage simultaneously, **then** resolve deaths. This means if Player A and Player B attack each other on the same tick, both attacks process against pre-tick HP — both kills can count. Actions from subsequent ticks after death are silently dropped.

**Simultaneous PvP resolution** uses the snapshot model to eliminate processing-order bias:

1. Snapshot all entity HP/stats at tick start
2. Calculate all damage against snapshot state (order-independent)
3. Apply results simultaneously: `A.hp = snapshot_A.hp - damageFromB + lifestealA`
4. Death check after all applications — mutual kills are allowed

**Reflected damage cannot trigger further reflects** — rAthena's `battle_calc_return_damage()` explicitly prevents loops by tagging reflected damage with an `isReflected` flag that bypasses all counter-attack checks.

The **PvP target validation checklist** should execute in this order, with silent rejection for race conditions and explicit error messages for client-checkable conditions:

- Attacker alive, not stunned/silenced, not in safe zone, sufficient mana, skill off cooldown
- Target exists (silent reject if disconnected), target alive (silent reject if dead), target on same map (silent reject if zoned), target not in safe zone (error message), target in range (error message), target not same faction/party/guild (error or silent depending on type)

---

## 6. Entity replication needs compact bitmasks and distance-based event culling

Beyond position, animation frame, facing, and current HP, combat state that needs replication to other players includes: **active status effect visuals** (encode as a compact uint32 bitmask covering all 16 types — 2 bytes if using uint16), **current target ID** (for target-line rendering — send on change only, 2 bytes), **combat state flag** (in-combat, casting — 1 byte bitmask), and **PK status** (2–3 bits for color state). Class-specific visual state (mage charging, warrior berserk) fits in a single uint8 "visual state" enum. Guild tags and party indicators are sent only on entity spawn, not per-tick.

Per-entity per-tick bandwidth at 20 Hz: **position** (4–6 bytes, delta-compressed to 2–3), **HP** (2 bytes, or 1 byte as percentage for non-targeted entities), **anim frame** (1 byte), **state flags** (2 bytes but often unchanged — 1 bit "no change"), **facing** (1 bit). Total: **~8–15 bytes per actively-changing entity per tick**. With 50 visible entities, typical bandwidth is ~6–10 KB/s, peaking at ~15–20 KB/s in dense 20v20 combat — well within mobile budgets.

**Combat event broadcasting** follows TrinityCore's model: damage numbers, healing, miss/dodge text are sent as **discrete unreliable events** (cosmetic — if one is lost, the next tick's state update corrects the displayed HP). State-changing events (death, buff application, CC) are sent **reliably**. Distance-based interest management culls cosmetic events in three rings: **0–10 tiles** (all events), **10–20 tiles** (only damage involving self/target, deaths, large AOE), **20+ tiles** (deaths and zone-wide announcements only). This follows Bungie's "I Shot You First" GDC talk principle of priority-based replication — the entity you're targeting gets the highest update frequency.

**Floating damage text in dense PvP** (20v20 generating hundreds of events per second) requires client-side priority culling: damage to self (always, full detail) > damage from self (always) > healing on self > kill feed > damage to current target from others > everything else (culled in dense fights). Maximum **8–12 simultaneous floating numbers** with oldest recycled when new ones arrive. Rapid hits within a 200ms window should aggregate into a single number with a hit-count indicator.

**Ghost entities** (last-known-state placeholders for out-of-range players) should carry last known position, HP as percentage (not exact — prevents stale data exploitation), status effect bitmask, and a staleness timestamp. Show "?" instead of exact HP on ghosts. On re-entering interest range, the server sends a full entity create snapshot (~50–100 bytes) as if the entity just spawned.

---

## 7. Death needs a two-tick lifecycle, and respawn invulnerability breaks on offense

TrinityCore's `Unit::Kill()` implementation reveals a critical insight documented in Issue #19927: death should be **deferred by one tick** to allow on-death procs to fire. If death clears all auras immediately, on-death triggers (damage reflect, thorns, kill-credit tracking) never execute. The recommended lifecycle:

- **Tick N**: Damage applied → HP ≤ 0 → set `DYING` flag (not `DEAD`) → process on-death triggers → clear DoTs → disengage combat
- **Tick N+1**: Set `DEAD` state → send death notification → start respawn timer → freeze entity

**Respawn invulnerability** of **3 seconds** (60 ticks) breaking on any offensive action (attack, hostile skill, targeting another player) is the industry standard. It should **not** break on movement (mobile players need to reposition), self-buff casting, or item use. The exploit vector of allies healing a respawned player to full during invulnerability can be mitigated by either preventing received heals during invuln or accepting it as intended (the 3-second window is narrow enough that coordinated exploitation is marginal).

**Simultaneous PvP death** should allow **mutual kills** using the snapshot model — both players die, neither gets kill credit for the other, both suffer death penalties. This is the fairest and simplest resolution. Kill credit across all scenarios uses **cumulative damage tracking**: the highest total damage dealer gets credit, with a minimum 3% of mob max HP threshold to qualify for any rewards. XP distributes proportionally by damage contribution.

**Death during active DoTs**: clear all effects immediately on death (rAthena's default behavior). Kill credit from DoT kills goes to the DoT source — the source entity ID must always be tracked. If Player A's Poison DoT kills Player B, Player A gets PK status.

**Disconnect while dead**: the death timer should **pause** (mobile players lose connection involuntarily). On reconnect, restore death state with remaining timer. Auto-respawn at save point after 10 minutes offline to prevent logging into a dead state days later.

---

## 8. Tick processing order determines every edge case outcome

The most critical implementation detail is the **per-tick processing order**, which resolves nearly every race condition and edge case:

```
1. Receive all client inputs queued since last tick
2. Process AI decisions (mob behavior)
3. Apply movement updates
4. Process skill casts (start/continue/complete)
5. Collect all damage events (do NOT apply yet)
6. Collect all healing events
7. Apply all damage simultaneously (snapshot model)
8. Apply all healing
9. Process shield absorption
10. Process DoT ticks
11. Single death check for all entities
12. Process death triggers (on-death procs, kill credit)
13. Finalize deaths (clear auras, disengage combat, set DYING)
14. Send state updates to clients
```

This order resolves the **damage + healing same tick** question: a player at 100 HP taking 80 damage and receiving 50 healing ends at 70 HP (100 - 80 + 50). The death check happens once after all effects, so clutch heals on the same tick save the player. It resolves the **multiple attackers same target** question: 5 players hitting a 100 HP mob for total 500 damage all have their damage credited (overkill counts for attribution), the mob dies on the single death check, and XP distributes proportionally.

**Target dying mid-animation**: the server rejects damage to dead targets. No cooldown consumed, no mana spent for auto-attacks. Client receives "target invalid," auto-retargets to nearest hostile. This is unavoidable at 100–300ms latency and should be accepted as normal.

**Equipment changes during combat should be locked.** WoW moved to full combat-lock on weapons in Patch 9.1 after years of macro-based gear-swap exploits. For a mobile game where fast gear swaps are impractical anyway, a 5-second "in combat" timer (refreshing on dealing/receiving damage) that prevents equipment changes is the cleanest solution.

**Zone transition during combat**: clear all mob-sourced DoTs (they belong to the zone context), **persist PK timer** (stored on player, not zone), reset combat flag, and implement a **5-second zone-lock** after taking PvP damage to prevent "zone running" escape exploits. Mob aggro fully resets — mob returns to spawn and heals to full.

**Overkill damage and lifesteal**: cap lifesteal at the **target's remaining HP**, not full damage dealt. A 200-damage hit with 10% lifesteal against a 50 HP target heals 5, not 20. This prevents lifesteal builds from becoming dominant in a stat-check meta. For damage attribution tracking, record the **full 200 damage** (not capped) to correctly reflect attacker contribution.

**Shield absorption order**: armor reduces damage first, then shields absorb the post-reduction remainder. Multiple shields consume in FIFO order (oldest first). A 100-point shield against 150 post-armor damage absorbs 100, remaining 50 hits HP. This makes shields more efficient (they see post-armor damage) and creates interesting itemization tradeoffs.

---

## Conclusion: the architectural pivot that solves everything

The single most impactful change is **removing combat_action_system.h's damage pipeline from the client entirely**. Replace it with a thin layer that sends intent packets and renders server results. This eliminates the entire class of RNG divergence, status effect timing, and multi-player interaction problems in one stroke. The server's pipeline in server_app.cpp becomes the sole source of truth, and the `SvSkillResult` packet — enriched with crit flags, hit/miss type, per-target results, status effect changes, resource costs, and cooldown durations — provides everything the client needs for responsive visual feedback.

The key insight from the Overwatch GDC talk applies directly: "We really can't trust the client with any simulation authority other than their input, because some clients are jerks." For a stat-check MMO where combat pacing is measured in seconds rather than milliseconds, the latency cost of server authority is invisible, but the security and consistency benefits are absolute. The recommended architecture — full server authority for all combat math, full stat snapshots at 1–2 Hz, absolute-timestamp status effects with full list sync, snapshot-based simultaneous damage resolution, and two-tick death processing — produces a system that is correct by construction rather than correct by reconciliation. The complexity budget saved on prediction and rollback code can be invested where it matters: responsive animations, smooth health bar interpolation, and the rich combat event broadcasting that makes the game feel alive.