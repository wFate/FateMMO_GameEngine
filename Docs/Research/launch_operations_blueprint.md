# Shipping a 2-person chibi MMO: from hardened engine to live game

A solo C++ developer with a pixel-artist wife can ship and sustain a TWOM-inspired 2D MMORPG by following a data-driven content pipeline, layered sprite compositing with palette swaps, zone-sharded PostgreSQL architecture on Hetzner, mobile-first SDL2 builds targeting 30 FPS, cosmetics-plus-subscription monetization, lightweight TimescaleDB telemetry, TWOM-style battlefield events, and a soft-launch-to-global mobile release strategy. The critical path from hardened engine to live game is **18–24 months**, with infrastructure costs starting at **~€80/month** and revenue sustainability achievable around **1,000 DAU**. What follows is an exhaustive operational blueprint across all eight domains.

---

## 1 — Content pipeline and world building

### Google Sheets to engine in one click

The fastest way for a 2-person team to author mobs, items, quests, and loot tables is a **Google Sheets → CSV → C++ parser pipeline**. The artist edits mob names, descriptions, and visual references in the same spreadsheets the developer uses for stats, eliminating any need to touch C++ code for content changes. A lightweight parser like rapidcsv reads CSVs at engine startup or hot-reloads on file change. CastleDB — the structured JSON database used by the Dead Cells team — adds typed columns, cross-sheet references, and a built-in level editor that outputs Git-friendly JSON. For the scripting layer that powers quest logic, NPC dialogue, and event triggers, **Lua via sol2** is the standard recommendation: Elias Daler's Re:creation project demonstrated live entity behavior changes without recompilation, and the Legend of Grimrock developers called out the ability to "change AI for monsters right in front of you in-game" as a productivity multiplier.

Dear ImGui, which has a native SDL2 + OpenGL 3.3 backend, serves as the in-engine editor toolkit. Priority tools include an entity inspector for live ECS component editing, a loot table editor with probability visualization using the imnodes library for quest graph visualization, and a spawn zone painter. The key rule: **C++ for systems (rendering, physics, networking); data files for content (mobs, items, quests, zones).**

### Zone design with Tiled and LDtk

For tile-based world authoring, both Tiled (.tmx) and LDtk export JSON/XML that a custom C++ engine can parse with TinyXML2 or pugixml. LDtk, created by the Dead Cells director Sébastien Bénard, offers a world view showing how multiple levels connect spatially — ideal for designing a TWOM-scale world with **11 regions and 200+ monster types**. Each zone is a separate map file with custom properties (`zone_id`, `level_range`, `faction`, `music_track`), and entity spawn data lives on object layers.

The world graph follows a hub-and-spoke pattern per region: central town → surrounding field zones → dungeon entrances. Zone connections are defined in a simple CSV (`from_zone, to_zone, connection_type, required_level, required_quest`). Multi-floor dungeons use stairway connections between zone files. Instanced content is flagged in zone metadata so the server creates per-party copies. To scale to 200+ monster types, define monsters in a `mobs.csv` with columns for sprite, stats, behavior script, and loot table reference, then use per-zone spawn tables with weighted random selection.

### Scaling quests from 6 to 500+

Start with **CSV-driven quest tables** — data rows the engine interprets without any scripting. A quest row contains: objectives as structured strings (`kill:wolf:10|collect:wolf_pelt:5`), rewards (`xp:500|gold:100|item:leather_armor`), prerequisite quest IDs, chain pointers, and repeat/reset metadata. This covers 80% of MMO quests. For the remaining 20% needing custom logic (boss encounters, branching dialogue, puzzles), Lua scripts fire at key quest events (`on_accept`, `on_objective_complete`, `on_turn_in`).

Research by Doran and Parberry (2011) analyzed **750+ quests from four MMORPGs** and found they share a common structure classifiable into motivation types (Knowledge, Comfort, Reputation, Conquest, Wealth, Equipment). Use this taxonomy as a quest variety checklist. To reach 500+ quests, use parametric generation: templates with variable slots filled from mob/item databases ("Kill {count} {mob_name} in {zone_name}") plus daily/repeatable quests with parameterized objectives that add effectively infinite perceived content.

### Loot tables and economy simulation

Loot tables use hierarchical, interlinking weighted tables: each entry has a weight, and probability equals weight divided by sum of all weights. Nested table references let a "Trash Loot" table be shared across many mobs — change once, update everywhere. For economy simulation, Machinations.io provides browser-based visual modeling of resource flows and can simulate thousands of player journeys; it was featured in the GDC talk "Building Sustainable Game Economies." More practically, Mike Stout's ratio-based distribution model assigns each mob type a relative loot share, and the system auto-distributes a zone's total gold budget based on ratios. The developer sets high-level knobs (gold per hour per zone tier) and lets the system distribute — ideal for a solo dev. The target faucet-to-sink ratio is **60–70% sink consumption** to prevent inflation.

### Procedural generation supplements hand-authored content

Wave Function Collapse (WFC), first used commercially in Caves of Qud, generates tilemap output "locally similar" to small input examples. The Simple Tiled Model works best for dungeon floor generation: define adjacency rules per tile, pre-constrain entrance positions, and run WFC within BSP-partitioned room chunks. Rule-based decoration placement uses Perlin noise density curves per biome — paint biome overlay in Tiled, define rules (`forest → trees(density:0.3), bushes(density:0.15)`), and place at pre-calculated grid points filtered by noise threshold. The critical insight: **procedural generation should supplement, not replace, hand-authored content**. Players in a TWOM-style game expect recognizable zones; use proc-gen for variety within zones and for repeatable dungeon instances, not for the world structure itself.

---

## 2 — Art pipeline and visual identity

### Aseprite CLI drives the entire sprite pipeline

The canonical export workflow uses Aseprite's CLI to batch-process `.aseprite` files into spritesheet PNGs and JSON animation metadata:

```bash
aseprite -b character.aseprite --sheet character.png --format json-array \
  --list-tags --split-layers --sheet-pack --trim --data character.json
```

The `--list-tags` flag exports animation tags (idle, walk_down, attack_right) as named frame ranges. The `--split-layers` flag exports equipment layers separately — critical for the paper-doll system. Each character needs **idle (4-dir × 2–4 frames), walk (4-dir × 4–6 frames), attack (4-dir × 4–6 frames), cast, hit reaction, death, and sit animations**. Since movement is cardinal-only, left/right sprites mirror with `SDL_FLIP_HORIZONTAL`, reducing directional art from 4 to 3 directions — a **25% art workload reduction**.

Stardew Valley's `FarmerRenderer` composites layers at runtime: head/torso base → pants → shirt → hair → accessories → hat. Each component sits on a **separate spritesheet with identical 16×32 frame grids**. Ragnarok Online's `.spr/.act` format composites equipment sprites at runtime over the base job sprite, though the exponential workload per job severely limited visual equipment variety. For this project, the recommended approach is **pre-bake composited layers into a cached FBO per character on equipment change**, then draw from that cached texture each frame. This gives single-draw-call rendering with unlimited combinations.

### 4-bit autotiling matches cardinal-only movement

For a cardinal-only game, the **4-bit bitmask** approach is ideal: check 4 cardinal neighbors, each contributing a bit (North=1, West=2, East=4, South=8), producing an index into a 16-tile tileset per terrain transition. The artist draws 16 variants per terrain pair (grass→dirt, dirt→water); the algorithm is trivial in C++. If smoother corners are later desired, upgrading to the 47-tile 8-bit blob approach is straightforward. Animated tiles (water, lava) store a frame count and duration in the tileset atlas, driven by a global timer. All terrain tiles should fit in a **single 2048×2048 atlas** for one-draw-call terrain rendering.

LDtk's auto-layer rules assistant auto-generates all transition rules from a template layout, and there's a C++ import library (`ldtkimport`) that runs auto-layer rules at runtime for procedural generation. LDtk natively loads `.aseprite` files, making it ideal for this art pipeline.

### VFX through particles and palette tricks at 480×270

At the target resolution, a hybrid approach works best. Hand-animated sprite VFX cover high-impact moments (fireball impact, heal aura), while a simple data-driven 2D particle emitter system handles ambient and procedural effects. The particle system needs emitter properties (position, rate, angle range, speed, lifetime), particle properties (velocity, color interpolation, alpha fade, scale curve), and two blend modes: **additive blending** (`GL_SRC_ALPHA, GL_ONE`) for magic/fire glow and normal alpha for smoke/dust. Zone transitions use an iris-wipe fragment shader (`if (distance(fragCoord, center) > radius) discard;`) for a classic JRPG feel.

Day/night cycle lighting works best as an ambient color multiply: render the scene to an FBO, draw a fullscreen quad with multiply blending using the time-of-day color, then add point-light sprites at torch/lamp positions using additive blending. The Gleaner Heights developer documented that **Hard Light blending mode** works best for 2D day/night because it darkens dark areas while preserving highlights. Hyper Light Drifter, which uses the same **480×270 native resolution**, achieves its luminous look through carefully hand-drawn bright pixel clusters and additive glow — not true bloom.

### Palette swapping creates exponential variety from minimal art

A GPU palette swap shader is the single most powerful tool for a 2-person art team. The artist draws sprites using indexed grayscale values, and the fragment shader maps these to a 1D palette texture row:

```glsl
vec4 indexColor = texture(u_sprite, v_texCoord);
vec4 finalColor = texture(u_palette, vec2(indexColor.r, u_paletteRow));
```

One armor spritesheet × 4 faction palettes × 5 rarity tiers = **20 visual variants from a single art asset**. The total art scope becomes manageable: 3 classes × 5 equipment tiers × 4 visual slots = 60 base equipment spritesheets. With palette swapping providing 4+ variants each, this yields **240+ visually distinct equipment appearances**.

### Touch-friendly mobile UI at native resolution

For 748+ item icons, the artist creates a **16×16 master icon template** in Aseprite, draws ~50 base silhouettes by category, then produces color/detail variants using palette swaps to reach the full icon count. The UI renders at native 480×270 to an FBO, then upscales to display resolution with nearest-neighbor filtering. All interactive elements need at least 16×16 pixels at native resolution for touch targets. The layout follows mobile MMO conventions: virtual joystick bottom-left, 4–6 skill buttons in an arc bottom-right (largest for primary attack), collapsible panels sliding from edges. 9-slice rendering allows a single 24×24 panel border texture to scale to any panel size.

---

## 3 — Server scaling and infrastructure

### One C++ process handles 300–1,000 concurrent players per zone

For a custom C++20 server with archetype ECS, arena memory, AOI-based replication, and 20 tick/sec, realistic capacity is **300–500 CCU per single-threaded zone process** (conservative) or **500–1,000** with well-optimized AOI. Albion Online, the closest comparable architecture (zone-based, single-threaded per zone), targets ~25–100 players per zone in normal play across ~600 zones on 8× 10-core Xeon machines. Bandwidth per player for a 2D MMO with AOI runs **2–10 KB/s** (16–80 kbps) outbound; at 1,000 CCU, that's ~5–20 MB/s or 40–160 Mbps, well within a 1 Gbps connection.

Academic research on MMORPG traffic (Sinica, Taiwan) found average bandwidth of ~10 kbps per client, with Ragnarok Online consuming approximately 3.7 Gbps at 370K peak players. Custom UDP eliminates TCP header overhead, which research shows comprises **73% of transmitted bytes** in MMORPGs — roughly halving wire overhead. The CPU is almost always the bottleneck, not bandwidth.

### Zone-based sharding is the simplest scaling path

Albion Online's architecture provides the blueprint: each zone ("cluster") runs as a single-threaded process with all incoming commands processed through a synchronized event queue. Database operations, pathfinding, and logging offload to separate thread pools. When a player changes zones, the client connects to a different game server process through a loading screen.

For this project, the scaling path is: **Phase 1** — single process handling all zones as logical memory partitions. **Phase 2** — zone processes plus a gateway process, with player state serialized to PostgreSQL on zone exit and loaded on zone entry. **Phase 3** — dedicated processes for zones, world features (guilds, parties), login/auth, and chat, communicating through shared PostgreSQL for low-frequency operations and direct TCP for real-time cross-server needs. The scene-string-based ECS maps naturally: each scene string routes to exactly one zone server process via an in-memory routing table.

### Deploy with Docker on Hetzner, monitor with Prometheus

A multi-stage Dockerfile builds the C++ game server in a builder stage and copies only the binary and runtime dependencies to a minimal final image. Use `--net=host` for the game server container to avoid Docker NAT overhead on UDP. The full stack runs in Docker Compose: game-server, PostgreSQL, PgBouncer, Prometheus, and Grafana. Custom Prometheus metrics exposed via an HTTP `/metrics` endpoint include `game_tick_duration_seconds`, `game_connected_players`, `game_db_query_duration_seconds`, `game_packet_loss_ratio`, and `game_memory_usage_bytes`.

Hetzner Cloud pricing starts at **€3.49/month** for 2 vCPU/4GB RAM, with dedicated servers from ~€39/month at auction. Compared to DigitalOcean ($24/month for comparable specs), Hetzner offers **2–3× better value**. For zero-downtime restarts, the pragmatic approach is weekly scheduled maintenance: announce with 15–30 minute warning, drain players, serialize state, bring up new version.

### PostgreSQL handles MMO workloads up to ~5,000 CCU before needing replicas

Regnum Online ran PostgreSQL for **9+ years** as its primary game database, growing from thousands to hundreds of millions of rows, and concluded they "would definitely use PostgreSQL again for an MMO." With PgBouncer in transaction pooling mode (`default_pool_size = 20–50` actual connections, `max_client_conn = 1000`), PostgreSQL handles **5,000–10,000 writes/second** on moderate hardware. For 1,000 CCU saving character state every 30 seconds, that's ~33 writes/second — trivial.

The bottleneck arrives around **2,000–5,000 CCU** for read-heavy workloads like leaderboards and marketplace browsing. At that point, streaming read replicas (1–3ms lag) handle analytical queries while the primary handles writes. Table partitioning by time range for chat messages, audit logs, and transaction history enables efficient pruning and archival. Storage growth runs approximately **500 MB–1 GB per 1,000 active players per month**.

### OVH Game DDoS protection offers the best value

For DDoS protection on a budget, **OVH Game dedicated servers** include always-on, game-aware UDP filtering at no extra cost (~€60–120/month total). Hetzner includes basic L3/L4 volumetric protection but lacks game-specific filtering. Cloudflare Spectrum requires Enterprise pricing (~$30K/year) for custom UDP — far too expensive for indie. Path.net offers advanced game-focused protection at ~$150–400/month but has mixed reliability reviews. Application-level protections — rate limiting per IP, UDP challenge-response before resource allocation, and iptables hashlimit rules — provide a free first layer. Albion Online was hit with **>60 Gbps DDoS attacks for 4+ weeks** during beta, burning through their first cheap mitigation provider.

---

## 4 — Mobile build and optimization

### SDL2 ships on mobile with careful lifecycle management

SDL2 officially supports iOS and Android. On iOS, `SDL.xcodeproj` builds `SDL2.xcframework` supporting all CPU architectures; the developer removes default ApplicationDelegate files since SDL provides its own `UIApplicationDelegate`. On Android, SDL2 works through JNI — the game builds as a `.so` shared library interfacing with SDL2's Android Java layer, using Gradle with NDK integration. CMake cross-platform builds use platform detection (`if(ANDROID)`, `if(IOS)`) for conditional compilation.

The critical mobile difference is lifecycle management. **iOS gives 5 seconds** after `SDL_APP_DIDENTERBACKGROUND` to save all state before potential termination. The game loop runs on a separate thread from iOS/Android callbacks, requiring `SDL_AddEventWatch()` for reliable lifecycle event handling. Touch coordinates arrive normalized (0.0–1.0) via `SDL_FINGERDOWN`/`SDL_FINGERMOTION`/`SDL_FINGERUP` events. The virtual keyboard shows via `SDL_StartTextInput()`.

### OpenGL 3.3 maps cleanly to ES 3.0 with precision qualifiers

OpenGL ES 3.0 is functionally the mobile implementation of OpenGL 3.3's feature set. The conversion requires changing `#version 330 core` to `#version 300 es`, adding `precision mediump float;` qualifiers, and replacing S3TC/BPTC texture compression with **ETC2/EAC** (mandated by ES 3.0, providing 6:1 RGB compression). Features absent in ES 3.0 (geometry shaders, tessellation, compute) are irrelevant for a 2D sprite game. As of 2025+, **~99% of active Android devices** support ES 3.0. However, OpenGL ES was deprecated on iOS in favor of Metal — the runtime still works but Apple could remove it. MoltenVK or ANGLE provide future-proofing paths.

### Target 30 FPS locked with under 50 draw calls

**30 FPS is the standard target for mobile MMOs** — Genshin Impact, MapleStory M, and Ragnarok Mobile all default to 30 FPS. This halves the thermal load compared to 60 FPS. At 30 FPS, the frame budget is 33.33ms: allocate ≤5ms for rendering, ≤5ms for game logic, ≤2ms for networking, with headroom for OS overhead. Mobile GPUs (all tile-based: PowerVR, Adreno, Mali) penalize overdraw heavily, so batch opaque sprites front-to-back and transparent sprites back-to-front.

The draw call budget for mobile is **<50 draw calls** on budget devices, <200 on flagships. A properly sprite-batched 2D game at 480×270 should achieve **<20 draw calls**. State changes cost even more than draw calls: switching shaders costs 175% more than same-shader calls. Use a single texture atlas (2048×2048 max for mobile compatibility) and minimize shader/texture switches.

For battery optimization, use the **Android Frame Pacing Library (Swappy)** to match the display refresh rate to 30 Hz — critical because modern phones running 90/120Hz displays waste power when the game only outputs 30 FPS. The Android Thermal API (ADPF) enables proactive workload reduction before throttling occurs: step 1 drops from 60→30 FPS, step 2 disables particles and reduces draw distance, step 3 simplifies shaders and reduces entity draw count.

### App store requirements demand odds disclosure and privacy compliance

Both Apple ($99/year) and Google ($25 one-time) take **30% commission** on IAP, reduced to **15%** under their respective small business programs for developers earning under $1M/year. Apple requires loot box odds disclosure since December 2017: "Apps offering mechanisms that provide randomized virtual items for purchase must disclose the odds of receiving each type of item prior to purchase." Google has matching requirements since 2019.

Privacy compliance spans three major regimes: GDPR (EU, requires consent/legitimate interest, data deletion rights, 72-hour breach notification), CCPA (California, right to know/delete/opt-out), and COPPA (under-13, requires verifiable parental consent). A chibi-style game may attract children, so implementing a **13+ age gate** and minimizing data collection is the safest approach. Both stores require Apple App Privacy Labels and Google Data Safety Section disclosures covering all collected data types.

### Mobile networking needs session-token-based reconnection

UDP over LTE shows **30–50ms typical round-trip** latency with 1–3% packet loss under normal conditions, spiking to 5–10% during handoffs. Carrier-grade NAT UDP mapping timeouts average **~60–65 seconds on cellular networks** (74% of CGNs expire idle UDP state within 1 minute), requiring keepalive packets every **15–30 seconds**. The most critical challenge is WiFi↔LTE handoff: the IP address changes completely, invalidating the tracked UDP connection. The solution is **session-token-based connection identity** (as Mosh uses): the client sends an authenticated re-hello packet with a session token after network change, and the server updates the address binding. Target reconnect time is **<2–3 seconds**. Bandwidth for a 2D MMO should stay under **20–50 MB/hour** — well within mobile data plans.

---

## 5 — Monetization without pay-to-win

### The ethical free-to-play playbook is proven by Path of Exile and OSRS

Path of Exile generated **$83.8M NZD revenue and $48.9M profit** in FY2022 on purely cosmetic MTX plus convenience stash tabs — zero pay-to-win. Tencent's $100M+ acquisition validated the model. Old School RuneScape mobile has earned **$93M all-time**, driven by membership subscriptions plus tradeable Bonds that reduced real-world trading by **61% within one week** of introduction. Albion Online's premium-plus-cosmetics model led to a **€120M acquisition** by Stillfront Group in 2021.

The cautionary tales are equally instructive. Diablo Immortal earned **$1B+ lifetime** but became the poster child for P2W backlash — one player spent $100,000 and couldn't find PvP matches due to inflated MMR. Belgium and Netherlands banned it outright. MapleStory's gacha practices resulted in a **$8.9M fine** in South Korea for undisclosed odds changes. RuneScape 3's Treasure Hunter loot boxes caused a mass exodus that directly birthed OSRS. The universal lesson: **hardcore PvP communities have zero tolerance for pay-to-win**. One mistake destroys a community overnight.

### Three-phase monetization starting with VIP and direct-purchase cosmetics

**Phase 1 (launch):** VIP subscription at **$4.99/month** granting marketplace access (TWOM's model), extra storage, cosmetic aura, and reduced marketplace fees. Direct-purchase cosmetics (10–15 items at $1.99–$9.99). A one-time $2.99 starter pack with massive perceived value to convert first-time buyers (starter packs convert **5–8% of players** vs. typical 1.6–2% IAP conversion).

**Phase 2 (6–12 months):** Battle pass at **$4.99/season** (60–90 days) with free and premium tracks, cosmetic-only rewards. Include enough premium currency in the premium track to buy the next pass — Fortnite's model proves this sustains engagement. Target 30–50 tiers per season. The artist creates 5–8 new sprites per season, generates 10–15 palette-swap variants, 5 particle effects, 3–5 titles, and 3 emotes.

**Phase 3 (12+ months, 5K+ DAU):** Bond/token system for legal gold exchange. Supporter packs ($15–$50) with exclusive cosmetics. No premium currency exchange until the player base supports market liquidity.

### Revenue projections scale from hobby to sustainable at 1,000 DAU

Conservative monthly revenue estimates: **500 DAU → $1,000–$2,500**, **1,000 DAU → $2,500–$5,000**, **5,000 DAU → $12,000–$25,000**, **10,000 DAU → $25,000–$50,000**. Minimum viable sustainability for a 2-person team is approximately **1,000–1,500 DAU** generating $3,000–$5,000/month after platform fees. Apple and Google reduce commission from 30% to **15% after 1 year** of continuous subscription, significantly improving margins for a subscription-focused model.

For anti-fraud, server-side receipt validation is mandatory — never trust the client. RevenueCat SDK manages cross-platform subscription logic, receipt validation, and analytics with a free tier covering up to $2,500/month revenue, which is ideal for bootstrapping.

---

## 6 — Live operations and analytics

### TimescaleDB extends the existing PostgreSQL stack for telemetry

Since the team already runs PostgreSQL, **TimescaleDB** (a PostgreSQL extension) is the most practical analytics solution — same SQL, same backup strategy, same tooling, with native time-series query optimization for retention curves and economy monitoring. The data pipeline uses a lock-free SPSC ring buffer: the game tick thread writes events without blocking, and a dedicated I/O thread drains the buffer with batched INSERTs (100–500 events per batch) every 5–10 seconds. For third-party backup, **GameAnalytics** has an official C++ SDK (C17+, static library) with pre-built retention and progression dashboards, free up to 100K MAU.

The essential dashboards track: **DAU/MAU ratio** (healthy MMO target: 20–40%), **D1/D7/D30 retention** (targets: >40%, >20%, >10%), session length distribution, total currency in circulation over time, faucet/sink ratio, marketplace price index, PvP win rates by class/bracket, and CCU by time of day.

### Economy monitoring follows the Alter Aeon feedback control model

The MUD Alter Aeon uses an automated system that tracks total in-game currency and **dynamically adjusts drop rates and shop prices** — peaks in total currency don't vary more than 10% over two-year periods, and players with >1M currency are taxed 2% on excess. This is the ideal model for a solo dev: set high-level targets, let the system self-correct.

Track every faucet (mob drops, quest rewards, vendor sales) and sink (marketplace tax, repairs, crafting costs, fast travel). Set Grafana alerts for: total money supply increasing >X% per day, any account earning >Y gold in 24 hours, marketplace basket price changes >Z% per week, and faucet/sink ratio exceeding 1.5:1 for 3+ consecutive days. For bot detection, flag accounts with gold/hour exceeding 3 standard deviations above mean, session lengths >16 hours without breaks, and trade graph hub accounts receiving gold from many sources and distributing to many targets.

### GM tools start with 10 chat commands and a Discord webhook

Essential launch commands: `/tp`, `/kick`, `/ban`, `/mute`, `/invisible`, `/spawn`, `/item`, `/setstat`, `/announce`, and `/inspect`. Every GM action writes to a `gm_audit_log` table (timestamp, gm_id, action_type, target_player, parameters, reason) before executing. Player reports via `/report` fire a Discord webhook to a private #mod-reports channel with player name, reporter, reason, timestamp, and location — eliminating the need for a separate ticketing system.

The web admin panel (Flask/FastAPI reading PostgreSQL) provides player lookup, ban management, server status, and economy overview. Permission levels gate access: level 1 (moderator) can mute/kick; level 2 (admin) can modify stats; level 3 (developer) has full access. Recruit **3–5 volunteer moderators** from trusted early players with in-game mute capability and Discord mod powers to multiply moderation capacity.

### Hot-reloadable data enables weekly balance patches without restarts

Store all game data as JSON files loaded into memory at startup. A `/reload <system>` GM command re-parses files and atomically swaps data using `std::shared_ptr` with atomic exchange or a double-buffer pattern. Hot-reloadable systems include item definitions, quest tables, balance configs, mob definitions, loot tables, NPC dialogue, shop inventories, and event schedules. Core ECS architecture, networking protocol, and database schema changes require restarts.

For client patching, host game files on Cloudflare R2 ($0 egress CDN). The client checks a `version.json` file on connect; version mismatches trigger a download of changed files. Mobile app store review takes 24–48 hours on Apple and same-day on Google. Minimize required client updates by keeping as much content server-side as possible. For database migrations, **pgroll** (by Xata) provides zero-downtime PostgreSQL migrations using the expand/contract pattern with virtual schemas via views. For a small indie MMO, brief 5–15 minute maintenance windows during low-traffic hours are simpler and entirely acceptable.

---

## 7 — Scheduled events and endgame systems

### TWOM's Battlefield provides the event scheduler blueprint

TWOM's Battlefield fires every 2 hours with a 10-minute signup period, 12v12 faction matchmaking, auto-teleport to a level-bracketed arena, 15–18 minutes of objective-based play (break 5 treasures), and rewards scaled by performance (3 Pendants of Honor for win, 1 for loss). This maps cleanly to a finite state machine: `DORMANT → SIGNUP → MATCHMAKING → INSTANCE_CREATION → ACTIVE → SCORING → REWARDS → COOLDOWN → DORMANT`. Store the event schedule in PostgreSQL as wall-clock trigger times; on each server tick, check against a priority queue of upcoming events.

Event state persistence follows a simple rule: if a crash happens during DORMANT or SIGNUP, recalculate the next trigger time on restart. If a crash happens during ACTIVE phase, **abort the event** and refund signup tokens — per IT Hare's guidance, disrupting a game event for more than 0.5–2 minutes means you won't get the same players back. Only persist meaningful checkpoints: results, reward distributions, and rating changes.

### Instances as separate ECS worlds with arena allocators

Each dungeon instance creates a completely separate `World` object with its own entity registry, component storage, and system execution. This provides clean isolation with no entity ID conflicts or cross-contamination. The lifecycle is: allocate ECS World → load tilemap template → serialize and transfer player entities from overworld → execute instance-specific systems (WaveSpawnSystem, BossAISystem, TimerSystem) → serialize results → transfer players back → bulk-free the arena allocator. Limit max concurrent instances (e.g., 50) to bound memory usage. Loot distribution uses **personal loot** (each player gets their own roll) — simplest to implement and fairest for PvP-oriented players.

### Simplified territory control uses king-of-the-hill with prime time windows

Rather than Albion Online's full fortification system (which took a 30+ person team years to iterate), implement **10–20 capturable open-world zones** with king-of-the-hill capture mechanics. Zones are only contestable during 2–3 hour prime time windows each evening. Benefits of ownership include passive **5–10% tax** on monster gold drops in the zone, zone-wide buffs for guild members, and exclusive resource spawns. Destructible tile entities (barricades, walls) use HP components; when HP hits 0, the tile sprite swaps to rubble and collision is removed. Defenders respawn at the territory tower; attackers respawn at the zone entrance.

### Boss design adapts CrossCode's HP-break phase system for cardinal movement

CrossCode's boss design — the gold standard for 2D top-down combat — uses HP break segments: when a segment depletes, the player regains HP and the boss transitions to a new phase with enhanced attack patterns. Adapt this for cardinal-only movement by designing AoE patterns that align with the grid: cross (+), X-shape, rows, columns, and expanding rings. A dedicated danger-zone tile overlay marks tiles 1–2 seconds before damage: red for immediate danger, yellow for incoming. The warning sequence is tile flash (0.5s) → tile solid color (0.5s) → damage applied → tiles clear.

For MMO adaptation, add a simple threat table (damage dealt = threat generated, healing = 50% threat), warrior taunt forcing boss target for N seconds, add/minion waves at phase transitions to force DPS splitting, and an enrage timer preventing infinite kiting strategies.

### Glicko-2 rating with PostgreSQL-cached leaderboards

Glicko-2 tracks three values per player: **rating** (μ, starting ~1500), **rating deviation** (φ, starting ~350, measures confidence), and **volatility** (σ, tracks consistency). RD increases over time when a player is inactive, naturally encoding uncertainty. The "instant" variant updates after every match rather than in batch rating periods. Guild Wars 2, Lichess, and CS:GO all use Glicko-2.

For leaderboards under 10K concurrent players, PostgreSQL alone with `RANK() OVER (ORDER BY score DESC)` and a 30–60 second application-level cache of the top-100 is sufficient. Add Redis Sorted Sets only when PostgreSQL becomes a bottleneck. Anti-boosting measures include flagging same-player pairs matching repeatedly (>3 times/day), suspicious game lengths (<60 seconds), and queue-snipe detection (two players queuing within 1–2 seconds at off-peak times). Seasonal soft resets use `new_rating = (old_rating + base_rating) / 2` with RD reset to 250.

---

## 8 — Launch strategy and growth

### The testing timeline spans 18–24 months from alpha to global launch

Based on Albion Online's 5-year development and Palia's rapid alpha-to-open-beta sequence, a realistic timeline for a 2-person team is: **closed alpha** (50–200 testers, 3–6 months testing core networking and combat), **closed beta** (500–2,000 testers via keys, 2–4 months testing economy and progression), **soft launch** on Android (3–6 months in test markets), then **global launch**. Albion Online involved large guilds at a very early stage, tested early builds in 2–4 week sessions, and offered special rewards (custom guild logos, founder certificates) — creating the seed of a community with "long-reaching loyalty."

Recruit alpha testers from TWOM fan Discord servers, Reddit's r/MMORPG and r/IndieGaming, and through devlog content that builds a pre-launch community. The "2-person team building an MMO from scratch" narrative is genuinely compelling — lean into the story.

### Soft launch Android first in Philippines, then Canada, then Nordics

**Phase 1 (technical testing):** Philippines and India — large English-speaking audiences, very low CPI ($0.10–0.50), great for stress-testing servers. **Phase 2 (retention testing):** Canada and Australia — behavioral patterns similar to US, moderate CPI ($2–5). **Phase 3 (monetization testing):** Nordic countries (Denmark, Norway, Sweden, Finland) — 85% English-speaking, exceptional monetization potential, high credit card usage. Always soft launch on **Android first** for faster review/update cycles.

The key metrics that determine global launch readiness: **D1 retention >35%**, **D7 >15%**, **D30 >8%** (GameAnalytics 2024–2025 data across 10,000+ projects shows median D1 at 22.9%, top 25% at 26–28%), **ARPDAU >$0.03**, **crash-free rate >99%**, and **session length >15 minutes**. Games spending >1 month in soft launch can increase retention by **20%** per deltaDNA data. Typical soft launch duration is 3–6 months.

### Marketing leverages pixel art showcases and the husband-wife team narrative

The wife's pixel art is a major marketing asset — sprite sheets, character reveals, environment art, and animation breakdowns perform exceptionally well on TikTok, Reddit r/PixelArt, and Twitter. Weekly devlogs on YouTube follow Legends of Idleon's model (solo developer built massive following through regular video updates). Google Play pre-registration (available 90 days before launch, >50% conversion from pre-reg to install) and pressing to MMORPG.com, Massively Overpowered, and indie game bloggers provide free discovery channels.

Budget-friendly marketing focuses on organic channels: TikTok/YouTube Shorts of pixel art and gameplay, genuine Reddit community participation, a Discord server built early, and cross-promotion with other indie devs. A minimum viable paid budget of **$500–2,000** in Facebook/Google Ads during soft launch provides acquisition data. Founder's packs ($15–$50 with exclusive cosmetics and beta access) generate both revenue and community investment, following Albion Online's proven model.

### Infrastructure costs start at €80/month and scale linearly

The starter stack for launch-ready infrastructure at 500–1,000 CCU costs approximately **€85–130/month**: one Hetzner dedicated server (€80–120), self-hosted PostgreSQL and monitoring (included), Hetzner/OVH DDoS protection (included), backups on Hetzner Object Storage (€5–10), and Cloudflare free tier for DNS/CDN. At 2,000–5,000 CCU, costs rise to **€300–640/month** with 2–4 game servers and a dedicated database server. Real-world references: City of Heroes private servers running 5K+ concurrent across 4 shards cost ~$4,700/month; a well-optimized 2D MMO at 5K CCU realistically costs ~$1,500/month.

**Infrastructure is not the main cost** — marketing, time/labor, and ongoing content development dwarf server expenses. The game needs approximately **500–1,000 DAU** with functioning monetization to cover infrastructure. At $0.03 ARPDAU (mediocre), 1,000 DAU generates ~$900/month, covering a Hetzner stack. Hetzner's server auction offers deeply discounted used servers at **50–70% below list price**, the single best cost optimization available.

### Legal requirements center on a clean EULA and age gating

The EULA must establish that players receive a limited, revocable license (not ownership) to virtual goods, explicitly prohibit RMT and botting, reserve the right to modify game mechanics and wipe characters during testing, and include an arbitration clause. A game-savvy lawyer review costs **$500–1,500 one-time**. Privacy policy generators (TermsFeed, Iubenda at $29/year) provide templates covering GDPR, CCPA, and COPPA requirements. IARC age ratings are free and instant through Google Play Console; Apple uses its own system in App Store Connect. A 13+ age gate avoids COPPA's strict parental consent requirements.

Being "inspired by" TWOM is legally fine — gameplay mechanics and genre conventions are not copyrightable. The critical boundaries: never copy TWOM's specific art assets, character names, item names, or story elements; never use "TWOM" or "World of Magic" in marketing. The phrase "inspired by classic mobile MMOs" is safe.

---

## Conclusion: the critical path forward

The research across all eight domains converges on five non-obvious insights. First, **the content pipeline is the true bottleneck**, not the engine — invest the first month in Google Sheets → CSV → hot-reload infrastructure before adding any more game features, because every hour of manual C++ content editing compounds into weeks of lost velocity. Second, **palette swapping is the art team's force multiplier**: 60 base equipment spritesheets × 4+ palette variants = 240+ distinct appearances, making a TWOM-scale equipment system achievable for one artist. Third, **PostgreSQL alone handles everything up to ~5,000 CCU** with proper PgBouncer pooling and TimescaleDB for analytics — resist the urge to add Redis, Cassandra, or other databases until the numbers demand it. Fourth, **$4.99/month VIP subscription gated on marketplace access** (TWOM's proven model) provides the most predictable revenue baseline for a 2-person team, supplemented by direct-purchase cosmetics and a seasonal battle pass. Fifth, **soft-launch on Android first** in the Philippines for stress testing, then Canada for retention validation, then Nordics for monetization — this three-phase sequence, lasting 3–6 months, is how the data-driven mobile industry de-risks launches, and it works equally well for a niche MMO.

The total investment from current state to global launch is approximately 18–24 months of development time, €85–130/month in infrastructure, $124/year in developer account fees, and $1,000–2,000 in legal costs. Revenue sustainability begins around 1,000 DAU. The hardened engine is the foundation; now the work is building everything that sits on top of it.