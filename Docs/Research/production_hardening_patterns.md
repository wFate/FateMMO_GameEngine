# Production hardening a custom 2D MMORPG engine

**Your engine's biggest risks are not performance — they're atomicity, state authority, and crash resilience.** Across all six categories, a single pattern dominates: every client packet is a lie until the server validates it, every concurrent mutation is a dupe vector until serialized, and every second between saves is data you can lose. This report provides concrete C++20 implementation patterns for each gap, drawn from Glenn Fiedler's game networking canon, Valve's Source Engine documentation, Gabriel Gambetta's client-server architecture series, production MMO postmortems, and academic game security research — all tailored to a solo-developer chibi-style 2D MMORPG on custom reliable UDP at 20 tick/sec.

---

## Category 1: Network security and anti-abuse for your custom UDP protocol

### Token bucket rate limiting gives combat its burst budget

Per-command rate limiting is the first line of defense against packet flooding. The **token bucket algorithm** is the standard for game servers because it naturally models burst allowances — exactly what combat skills need. For a skill with a 3-use burst but a 10-per-10-seconds sustained limit, configure `max_tokens = 3` and `refill_rate = 1.0` tokens/sec. Each limiter needs only two floats (current tokens, last refill timestamp), so memory cost is negligible even with per-client, per-message-type instances.

```cpp
struct RateLimiter {
    float tokens;
    float maxTokens;      // burst capacity
    float refillRate;     // tokens per second
    double lastRefill;

    bool tryConsume(double now) {
        float elapsed = static_cast<float>(now - lastRefill);
        tokens = std::min(maxTokens, tokens + elapsed * refillRate);
        lastRefill = now;
        if (tokens >= 1.0f) { tokens -= 1.0f; return true; }
        return false;
    }
};
```

Use **token bucket for combat actions** (burst-tolerant) and **leaky bucket for chat** (enforces steady rate, prevents spam bursts). Store the configuration as a data table mapping `MessageType → {maxTokens, refillRate}` loaded from JSON, so tuning doesn't require recompilation. On rate limit failure, increment a per-client suspicion score that decays over time rather than kicking immediately — this absorbs lag spikes while catching sustained abuse.

### Glenn Fiedler's serialize pattern is the packet fuzzing defense

For binary protocol validation, Glenn Fiedler's unified serialize pattern (from Gaffer On Games) is the gold standard used in AAA titles. The core idea: every field is serialized through a single templated function that enforces range checks automatically on read. Integers use `serialize_int(stream, value, min, max)`, which rejects out-of-range values before they touch game logic. Strings are length-prefixed (never null-terminated scanned), with the length field itself range-checked against `MaxStringLength`. Floats are post-validated with `std::isnan()` and `std::isinf()` checks.

Your 16-byte header should include a **32-bit protocol ID** (hash of version + game identifier). Packets that fail this check are discarded immediately with zero processing cost — this alone filters out the vast majority of random UDP noise and port scanners. After deserialization, verify all bits were consumed (no trailing garbage data). For sub-action byte enums, serialize with explicit range: `serialize_int(stream, action, 0, MAX_ACTION_TYPE - 1)`.

Run a mutation fuzzer against your packet handlers compiled with MSVC's `/fsanitize=address` and `/fsanitize=undefined`. Truncate packets at random offsets, set length fields to 0/INT_MAX/negative, inject NaN bytes (`0x7FC00000`), and flip random bits. Every crash the fuzzer finds is a vulnerability an attacker would exploit.

### Replay prevention comes free from AEAD encryption with sequence nonces

Your reliability layer already uses per-channel sequence numbers and ACK bitfields. These same sequence numbers can serve as AEAD nonces if you encrypt every post-handshake packet with **XChaCha20-Poly1305** (libsodium) or AES-GCM, using per-session symmetric keys established during TLS authentication. The AEAD authentication tag (16 bytes) makes packets unforgeable, and the monotonic sequence-as-nonce makes each packet cryptographically unique. Replayed packets fail authentication because the receiver tracks a sliding replay window (typically 256–1024 entries) of already-seen sequence numbers.

The netcode.io standard (Glenn Fiedler, used in Titanfall 2's reference implementation) defines the full connect token flow: web backend authenticates via HTTPS → issues AEAD-encrypted connect token containing per-session keys → client presents token to game server over UDP → server validates HMAC, extracts keys → all subsequent packets encrypted with per-client keys. This eliminates replay attacks, session hijacking, and packet injection in one layer.

### Movement validation needs statistical depth beyond distance checks

For a TWOM-style cardinal-only 2D MMO, the ideal architecture is **fully server-authoritative movement**: the client sends direction inputs (N/S/E/W + sequence number), the server simulates movement at the known walk speed, checks collision against the world map, and broadcasts authoritative positions. This eliminates speed hacks entirely for cardinal movement because the server never accepts position data from the client.

If you accept client-reported positions (for responsiveness), layer validation: instantaneous speed check (hard cap at `MAX_SPEED × 1.5` for lag tolerance), sliding-window average speed over 1 second (tighter cap at `MAX_SPEED × 1.1`), cardinal-direction enforcement (reject diagonal deltas), path continuity validation (every tile along the path must be walkable), and cumulative distance tracking over 5-minute windows. Track **rolling standard deviation** of per-tick speed — cheaters have anomalously low variance (constant high speed) versus legitimate players who naturally stop and start. Response to violations should use a decaying suspicion score: rubber-band on mild violations, freeze on severe teleports, auto-kick only after sustained scoring.

### Economy abuse is an atomicity problem, not a rate limiting problem

Item duplication in MMOs almost always stems from race conditions between concurrent operations on the same inventory state. The MU Online postmortem documents the canonical patterns: dual-login race (same account on two clients simultaneously), trade-cancel state rollback (cancel resets inventory but doesn't undo NPC operations), and server-transfer timing (map transition + trade + disconnect = old save overwrites new state).

The fix is a **per-player mutation queue** (actor-model pattern) that serializes all inventory/gold operations for a given player through a single logical thread. Trade, market, loot pickup, and auto-save all enqueue mutations rather than directly modifying state. For two-player operations (trades), lock both players' queues in consistent ID order to prevent deadlocks. At the database level, give every item a **UUID primary key** in its own row (not embedded in a binary blob) with a `UNIQUE(owner_id, slot_index)` constraint — the database rejects duplicate slot assignments even if application code has a bug. Use `WHERE owner_id = $old_owner AND item_id = $item` in transfer queries so the affected-rows count acts as a database-level compare-and-swap.

Market manipulation prevention requires rate-limiting list/cancel operations separately (e.g., 5 listings/sec burst, 1/sec sustained; 3 cancels/sec burst, 0.5/sec sustained) and adding a cooldown after cancellation before re-listing the same item.

### Unicode abuse in chat requires ICU-level normalization

Standard profanity filters fail against Cyrillic/Latin homoglyphs (Cyrillic "а" U+0430 vs Latin "a" U+0061). The fix is ICU's `SpoofChecker` with `uspoof_getSkeleton()`, which normalizes confusable characters to a canonical "skeleton" before filter comparison. Server-side chat processing should: apply NFC Unicode normalization, strip all zero-width characters (U+200B ZWSP, U+200C ZWNJ, U+200D ZWJ, U+2060 Word Joiner, U+FEFF BOM), strip all bidirectional override characters (U+202A–202E, U+2066–2069), generate the confusable skeleton, then apply word filters against the skeleton. **RTL override characters have no legitimate use in game chat** and should be unconditionally stripped — they're exclusively used for text-direction attacks (MITRE ATT&CK T1036.002).

For spam detection beyond rate limiting: hash recent messages per player and reject near-duplicates (Jaccard similarity on skeleton-normalized bigrams), limit consecutive repeated characters, and use escalating mute penalties (30s → 2min → 30min → 24h).

### Replace every system() call with CreateProcessW or fork+execve

The `system()` function invokes the shell, meaning any user-controllable string in the command enables arbitrary code execution. On Windows, use `CreateProcessW()` which takes arguments as separate parameters (no shell interpretation). For "open" actions (URLs, documents), use `ShellExecuteW()`. On Unix, use `fork()` + `execve()` with the full executable path — arguments are passed as an array, so metacharacters like `;`, `|`, and `&&` are treated as literal characters. Use `execve()` (not `execvp()`) to avoid PATH manipulation attacks. Wrap both in a cross-platform `safe_execute()` utility using `std::filesystem::path`.

---

## Category 2: Network protocol hardening for entity replication

### Per-entity sequence counters prevent stale state overwrites at near-zero cost

When entity state updates are sent unreliably, out-of-order arrival can overwrite current state with stale data — a player teleporting backward, HP flickering. Glenn Fiedler's State Synchronization article and the Source Engine both prescribe per-entity sequence counters. Each entity gets a monotonically increasing **16-bit sequence number** that increments with each state update. The receiver compares incoming sequence against the last applied sequence per entity; stale updates are silently discarded.

```cpp
inline bool isNewerSequence(uint16_t incoming, uint16_t current) {
    return static_cast<int16_t>(incoming - current) > 0; // handles wraparound
}
```

At 20 tick/sec, a 16-bit counter wraps in **~54 minutes** — far beyond any reasonable gameplay duration for a single entity. The bandwidth cost is 2 bytes per entity update. With ~30 entities in a typical AOI at 20 ticks/sec, that's 1,200 bytes/sec — negligible. This pattern is critical for unreliable-channel position/HP updates where reliability overhead (ACK tracking, retransmission) is too expensive.

### Delta compression needs 15–20 fields for convincing visual replication

Beyond position/animFrame/flipX/HP, a TWOM-style game needs these replicated fields for full visual fidelity: **facing direction** (2 bits, cardinal), **moveState** (3 bits: idle/walk/dead/sitting), **animId** (8 bits, current skill animation), **hpMax** (16 bits, for health bar percentage), **level** (8 bits, nameplate), **statusEffectMask** (16 bits, bitfield of active visual buffs/debuffs), **faction/guildId** (8–16 bits, nameplate coloring), **combatTarget** (16 bits, entity ID for combat indicator), **deathState** (2 bits: alive/dying/dead/ghost), **classId** (4 bits, visual appearance), and **equipmentVisuals** (compact sprite IDs for head/body/weapon).

Use a **32-bit bitmask** for the field mask — start with ~16 fields, reserve the upper bits for future expansion. Variable-length fields (playerName, chatBubble, status effect list) go at higher bit positions and are length-prefixed so unknown fields can be skipped by older clients. For backward compatibility, include a protocol version byte in the connection handshake; clients ignore bits beyond their supported range. A typical position-only delta (walking entity) is just **8 bytes** (4-byte bitmask + 2 bytes posX + 2 bytes posY). Full state on AOI enter is ~20–30 bytes including name.

### Bandwidth at scale: expect 25 Kbps/player average, 150+ Kbps in dense PvP

Academic traffic analysis of Ragnarok Online (a comparable 2D MMO) measured **~7 Kbps** average server-to-client bandwidth — much lower than the ~40 Kbps of FPS games due to the slower pace. With your engine's AOI-based delta compression at 20 tick/sec, modeling gives these per-player server-to-client estimates:

| Scenario | Entities in AOI | Active | Kbps/player |
|----------|:-:|:-:|:-:|
| Town idle | 40 | 10 moving | **15** |
| Field grinding | 20 | 15 active | **27** |
| Boss fight | 30 | 25 active | **51** |
| Dense PvP (50v50) | 100 | 80 active | **156** |

At **500 concurrent players** with mixed scenarios, expect ~15 Mbps total server bandwidth. At **2,000 players** during a PvP event, spikes can reach 160 Mbps. Mitigations for dense scenarios: reduce tick rate for distant AOI entities to 10 Hz, cap entity updates per packet at 50 (prioritize by distance), and dynamically shrink AOI radius in dense areas.

### Client prediction at 20 tick/sec uses Gabriel Gambetta's standard algorithm

Gabriel Gambetta's canonical 4-part article series defines the standard: client sends inputs with monotonic sequence numbers, immediately predicts locally, server processes authoritatively and responds with state + last-processed-input-number, client accepts server state, discards acknowledged inputs, and **re-applies all unacknowledged inputs** on top of the server state to arrive at the predicted present.

At 20 tick/sec (50ms between server updates), a player with 100ms RTT predicts ~2 inputs ahead; at 200ms RTT, ~4 inputs. For cardinal-only movement, re-simulation is extremely cheap — just re-apply direction vectors with collision checking. Keep a ring buffer of 64 pending inputs (~3.2 seconds at 20 tick/sec). For other players (remote entities), interpolate between two buffered snapshots with a **100ms interpolation delay** (Source Engine's default `cl_interp 0.1`), which survives one dropped packet at 20 tick/sec.

When prediction diverges from server state, use **visual smoothing**: maintain a position-error offset that decays exponentially each frame. Small errors (< 4 pixels) smooth at 0.95 decay; large errors (> 64 pixels) snap immediately. Apply smoothing only at the render level, never the simulation level — this is Glenn Fiedler's recommended approach from his State Synchronization article.

### UDP connection hardening follows the netcode.io challenge-response model

The netcode.io standard prevents SYN-flood equivalents through a challenge-response handshake where the server's challenge packet is deliberately **smaller than the client's request** — eliminating amplification attack viability. No server memory is allocated until the challenge-response completes successfully. Rate-limit connection attempts per source IP to 5/second.

For session hijacking prevention, all post-handshake packets use AEAD encryption with per-session keys and per-packet sequence nonces. For graceful degradation under packet loss, implement a quality tier system: 0–2% loss = normal delta compression; 2–10% = send critical fields (HP, position) redundantly in every packet; 10–20% = switch to keyframe-only mode (no deltas) and reduce tick rate to 10 Hz; >20% = keepalive-only with disconnect warning UI. Send keepalive packets every 1 second minimum to maintain NAT mappings (most NAT devices drop UDP mappings after 30–120 seconds of inactivity).

---

## Category 3: Database and persistence integrity

### A local write-ahead journal is your only defense against SIGKILL

SIGKILL cannot be caught — no signal handler, no graceful shutdown. The in-memory game state is the source of truth (confirmed by the McGill University MMORPG persistence paper), and the gap between the last DB write and a crash is the data loss window. The fix is an **application-level write-ahead journal**: before any critical mutation (inventory, gold, trade) is applied in memory, append a CRC-protected entry to a local file, then `fsync()` for critical operations. On crash recovery at startup, replay any journal entries with sequence numbers beyond the last committed DB sequence.

Implement a **tiered persistence strategy**: critical operations (inventory changes, gold mutations, trades) write to DB immediately within the same logical tick; important state (XP, level, quest progress) flushes every 5–15 seconds; low-priority state (position) saves every 30–60 seconds or on zone change. For the journal, use an append-only structure with per-entry CRC32 checksums. Memory-mapped files (`mmap` with `MAP_SHARED`) provide crash resilience since the OS flushes pages even on abnormal termination, though you must use `msync(MS_SYNC)` for critical operations.

Set the game server process's `oom_score_adj` to `-1000` to reduce OOM killer likelihood. Register handlers for SIGTERM and SIGINT to trigger emergency save-all before shutdown.

### libpqxx connection recovery requires catch-destroy-recreate

Modern libpqxx (7.x) does **not support automatic reconnection** — the maintainer confirmed this in GitHub issues #673 and #205. When a `pqxx::broken_connection` exception is thrown, the only recovery is destroying the connection object and creating a new one. Your connection pool should validate connections before use with a lightweight `SELECT 1` health check if the connection hasn't been used in 30+ seconds, and discard broken connections silently.

Wrap all DB operations in retry-with-exponential-backoff (100ms → 200ms → 400ms → ... capped at 30s, with random jitter to prevent thundering herd). Layer a **circuit breaker** on top: after 5 consecutive failures, open the breaker for 30 seconds, during which the game server queues writes to the local journal, disables auto-save, blocks marketplace/trading (requires transactional consistency), and shows players a "saving temporarily unavailable" system message. After the cooldown, allow one probe request; on success, close the breaker and replay queued writes. Include TCP keepalive parameters in the connection string: `keepalives=1 keepalives_idle=30 keepalives_interval=10 keepalives_count=3`.

### Per-player mutation queues eliminate inventory race conditions

When a player simultaneously trades, receives market sale credit, picks up loot, and auto-save triggers, you need serialized mutation access. The recommended pattern is a **per-player mutation queue** (actor-model-like): all inventory/gold operations enqueue into a per-player queue processed once per tick on the game thread. This eliminates lock contention without database-level serialization.

For database-level safety, use **PostgreSQL advisory locks** keyed on player ID: `SELECT pg_advisory_xact_lock($player_id)` at the start of a transaction serializes all concurrent DB writes for that player. For two-player trades, always acquire advisory locks in ascending player-ID order to prevent deadlocks. Back this with an **optimistic version counter** on the inventory table: `UPDATE player_inventory SET ... , version = version + 1 WHERE player_id = $1 AND slot_id = $2 AND version = $3`. If zero rows are affected, a concurrent modification occurred — retry or reject.

### Schema migrations run at startup with transactional DDL

Implement a `schema_migrations` table tracking `(version INTEGER PRIMARY KEY, name, checksum, applied_at, duration_ms)`. Store migration files as numbered SQL scripts (`001_initial_schema.sql`, `002_add_marketplace.sql`, ...). At server startup, compare applied versions against available files, then execute pending migrations in order. PostgreSQL uniquely supports **transactional DDL** — a failed migration automatically rolls back the entire DDL change, leaving the database in the pre-migration state.

Use forward-only migrations (no rollback scripts). Validate checksums of previously applied migrations to catch accidental edits. For a single-server MMO, run migrations at startup behind a `pg_advisory_lock(0)` to prevent parallel migration runs if multiple server instances start simultaneously. This pattern matches production frameworks like Flyway and the Metaplay game server.

### Three-tier backup with WAL archiving enables point-in-time player rollbacks

Configure WAL archiving (`wal_level = replica`, `archive_mode = on`) with a 5-minute `archive_timeout`. Run `pg_dump -Fc` every 6 hours for logical backups (portable, can restore individual tables), `pg_basebackup` weekly for physical backups, and sync both to off-site cloud storage (S3, Backblaze B2, or a second VPS via rsync). Test restores monthly by restoring to a temporary database and verifying row counts on critical tables.

For player item rollback requests, restore a backup to a **separate PostgreSQL instance** on a different port, query the player's inventory at the target timestamp, and apply corrections to production — avoiding full PITR on the production database. Better yet, maintain an `item_audit_log` table with before/after JSONB state for every item mutation, indexed on `(player_id, timestamp)`. This lets you investigate and resolve most rollback requests without touching backups at all.

---

## Category 4: Game feel and client polish

### Animation windup is TWOM-style combat's natural latency mask

For a hit-rate combat system on 100–300ms mobile connections, the **animation windup pattern** hides latency without requiring client-side damage prediction. On input, immediately play the attack animation's windup phase (100–150ms) and a "whoosh" swing sound effect. The server response (hit/miss/crit/target-dead) typically arrives by the time the animation reaches its hit frame. On confirmation, branch the animation: show damage number + impact particles + hit sound on hit; show "MISS" floating text + lighter whiff sound on miss; show large damage number + bigger screen shake on crit.

This works because **TWOM-style combat is inherently latency-tolerant** — it's cooldown-based and stat-driven, not twitch. The deliberate pace means a 200ms delay between pressing attack and seeing the damage number feels natural, not sluggish. If the server reports the target already dead, simply suppress the hit effect and let the death animation (already broadcast) play. The wasted swing animation feels natural — like the player swung at air. Never roll client-side damage independently; always use server-authoritative damage numbers (see Category 5 on RNG).

### SoLoud wins the audio engine comparison for this stack

Comparing SDL_mixer, SoLoud, FMOD, and raw SDL_audio: **SoLoud** is the optimal choice. It's free (zlib license, no revenue caps), provides up to **4,095 virtual voices** with automatic priority-based voice stealing (solving the "50 simultaneous mob attacks drowning player feedback" problem), has mixing buses for independent BGM/SFX/UI/ambient volume control, built-in faders for zone music crossfades (`soloud.fadeVolume(handle, 0.0f, 2.0f)`), and uses SDL audio as a backend — integrating directly with your existing SDL2 stack.

Architecture: preload all UI SFX, player combat sounds, and common mob sounds (~2–5MB as WAV). Stream background music as OGG via SoLoud's `WavStream` class. Lazy-load zone-specific ambient loops on zone entry, unload on exit. For 2D spatial audio, convert world-space X offset from camera center to stereo pan [-1, 1] with distance-based volume attenuation. Configure max active voices to ~32 and set player sounds at higher priority — SoLoud automatically virtualizes (silences but tracks) lower-priority voices when the cap is reached.

### Camera shake at 480×270 needs Perlin noise and pixel-snapping discipline

The GDC-standard approach (Squirrel Eiserloh, "Juicing Your Cameras With Math") uses a **trauma value** (0.0–1.0) that decays over time, with actual shake intensity equal to `trauma²` (quadratic makes transitions feel punchier). Use Perlin noise sampled at different offsets for X, Y, and rotation to produce smooth, organic displacement rather than random jitter.

At 480×270 virtual resolution, a single pixel is ~0.37% of screen width — already very noticeable. **Cap maximum shake offset at 3–5 pixels** for heavy hits and 1–2 pixels for light hits. Always `std::round()` the final camera position to integer pixels to avoid sub-pixel shimmer. Render to a 480×270 FBO with a few pixels of extra border, then offset the blit to screen — this avoids black border artifacts during shake.

For **hit-stop** (freeze frames), research shows 0.1 seconds of stillness between shaking **increases perceived impact by ~30%**. Light attacks: 2–3 frames (33–50ms); heavy attacks: 4–6 frames (67–100ms); crits: 6–8 frames. During freeze, pause gameplay simulation but keep particles and camera shake running using real delta time, not game delta time. Combine with a white-flash shader (mix sprite color toward white for 1–2 frames, then decay) for immediate visual feedback that the hit connected.

For the small viewport with chibi sprites (~16–24px tall), favor high-contrast low-count effects: **3–8 directional spark particles** per hit at 1–3px each, bright damage numbers in a readable bitmap font with upward float, and subtle screen shake. Avoid chromatic aberration (too subtle at this resolution) and large particle systems that clutter the tiny viewport.

### Touch controls follow the TWOM and Ragnarok Mobile playbook

For cardinal-only movement, a **fixed D-pad** on the bottom-left is more appropriate than a floating analog joystick — players develop muscle memory for 4 directions. Size it at ~100–120px physical diameter with a 25–30% dead zone in the center. Divide the touch area into 4 quadrants at 45° diagonals for cardinal snapping.

Skill buttons go in a bottom-right cluster (4–6 buttons in radial/grid layout) with a separate prominent auto-attack button. **Minimum touch target: 44×44px physical** (Apple HIG and Android guidelines). Add 8–12px spacing between buttons to prevent mis-taps. Skill cooldown feedback uses radial fill animation directly on the button.

For tap-to-target with small chibi sprites, **expand touch hit areas to 2–3× visual sprite size** (if sprite is 16×24px, hit area should be 48×48px minimum). When a tap doesn't directly hit any sprite, select the nearest entity within a generous radius (64–80px), prioritizing monsters over NPCs, lower-HP targets, and proximity to tap point. Implement an **auto-attack toggle** (tap once, character continuously attacks current/nearest target with automatic retargeting when current target dies) — this is essential for mobile MMO playability and matches both TWOM and Ragnarok Mobile patterns.

### RmlUi is the lowest-cost migration from ImGui to production mobile UI

Comparing heavily-styled Dear ImGui, RmlUi, Nuklear, custom retained-mode, and custom immediate-mode: **RmlUi** has the lowest migration cost for your specific feature set (drag-drop inventory, animated cooldowns, scrolling chat, touch-friendly targets). It's MIT-licensed, retained-mode (only redraws on changes — critical for mobile battery life), uses HTML/CSS-like layout (RCSS), supports data binding, CSS animations, proper scrollable containers, and has existing SDL2+OpenGL backends.

ImGui is not designed for production game UI per its creator (ocornut, GitHub issue #8376). It redraws every frame (wasteful on mobile), has no momentum scrolling or swipe gestures, no CSS-like layout engine, and limited animation support. Nuklear is even more limited. Custom retained-mode requires months of UI framework development.

The migration path: **Phase 1** (now through alpha) — keep ImGui for all UI and dev tools. **Phase 2** (alpha to beta) — integrate RmlUi alongside ImGui; RmlUi handles game UI (inventory, chat, HUD, menus), ImGui handles debug overlays and dev console. **Phase 3** (beta to ship) — strip ImGui from release builds with `#ifdef DEV_BUILD`. RmlUi's context should match your 480×270 virtual resolution, with touch input scaled from physical screen coordinates.

---

## Category 5: Engine robustness and production hardening

### Error handling needs three tiers, not one strategy

Following the BitSquid/Stingray engine philosophy (Niklas Frykholm): classify errors as **expected** (network timeouts, file-not-found — handle with return values), **unexpected bugs** (null pointers, invalid state — crash immediately in dev, log + attempt recovery in release), and **warnings** (deprecated calls, non-critical missing assets — log and continue).

Use `std::expected<T, EngineError>` (supported in MSVC since VS2022 17.6) for recoverable error paths in non-hot-path code. Define a structured `EngineError` enum with categorized codes (100s for assets, 200s for network/DB, 900s for fatal). For graceful degradation: DB down → disable auto-save + queue to journal + show player toast; texture load failure → return magenta checkerboard placeholder + log; network loss → buffer inputs + show reconnect overlay + replay on reconnection. Never use C++ exceptions on hot paths — Unreal and most commercial engines avoid them for performance reasons.

### Texture cache eviction uses three-state LRU with VRAM budget

Track every loaded texture in three states: **InUse** (referenced by active render commands this frame), **Cached** (in VRAM, no active references, recently used), and **Evictable** (unused for N frames). Use an intrusive doubly-linked LRU list (no heap allocation for list nodes) with a configurable VRAM budget (e.g., 256MB). When loading new textures would exceed budget, evict from the LRU tail, skipping InUse textures.

For zone transitions, **pre-load the next zone's texture manifest** when the player is within N tiles of a zone boundary. Keep previous zone's textures in Cached state for 30–60 seconds (players often backtrack). Evict/load at texture atlas granularity (e.g., 2048×2048 atlases), not individual sprites. Use `glTexStorage2D()` (immutable storage) instead of `glTexImage2D()` for reusable textures — enables driver optimizations.

### The three-phase async loading pipeline eliminates zone transition hitches

Zone transitions stall because texture decoding and GPU upload block the main thread. Split loading into three phases: **I/O** (dedicated thread reads raw file data), **Decode** (worker fibers decompress PNG/decode JSON — no GL calls), and **GPU Upload** (main thread only, time-boxed to 2ms per frame).

OpenGL contexts are thread-affine, so all `gl*` calls must happen on the main thread. Use a thread-safe queue between decode fibers and the main thread's upload phase. For reduced upload latency, use a PBO (Pixel Buffer Object) ring buffer: `glBufferData` into PBO N while GPU uploads from PBO N-1. Track loading progress with `std::atomic<uint32_t>` counters for loading screen display. A typical zone load sequence: player approaches boundary (frame N) → spawn preload job → I/O reads files (frames N+1 to N+4) → decoder fibers decompress in parallel (N+5 to N+7) → main thread uploads 2–3 textures per frame at 2ms budget (N+8 to N+20) → transition complete.

### CI/CD on GitHub Actions needs xvfb for headless OpenGL testing

For your CMake + MSVC build, use a GitHub Actions matrix with Windows MSVC, Linux GCC, and Linux Clang. Cache `build/_deps` (FetchContent download directory) keyed on a hash of CMakeLists.txt. On Linux, install Mesa's LLVMpipe software renderer and run tests under `xvfb-run` with `MESA_GL_VERSION_OVERRIDE=3.3` to force OpenGL 3.3 compatibility without a physical GPU.

For tests that don't need rendering (ECS logic, networking, combat math), compile with a `HEADLESS_TESTING` define that skips SDL window creation and GL context initialization entirely. Run doctest with `--reporters=junit --out=test-results.xml` for CI-compatible output. Enable MSVC code analysis (`/p:EnableMicrosoftCodeAnalysis=true`) in the Windows build step. For sanitizer coverage, add an ASan configuration (`/fsanitize=address`) to the test matrix — it catches use-after-free and buffer overflows at ~2× slowdown.

### Combat RNG must be server-authoritative, never independently rolled

When client and server both roll damage independently, the numbers will disagree — and the server's number is always canonical. This creates a jarring UX where the client shows "247 damage" but the server applies 189. The solution used by WoW, FFXIV, and most production MMOs: **server computes all combat outcomes** (damage, crit, hit/miss, status effects). Client sends only intent ("attack target X with skill Y"). Server validates, rolls, and broadcasts the result.

For optimistic client display: show an estimated damage number immediately (using known base stats without RNG variance), then replace it with the server's actual number when the response arrives 50–150ms later. Use separate PRNG streams per system (`std::mt19937` for loot, damage, and crit each seeded independently) to prevent "fishing" where manipulating one system affects another. Seeded per-interaction RNG (where client and server share a seed for identical results) is theoretically elegant but practically fragile — floating-point precision differences between compiler optimizations cause desync, and the client knowing the seed enables outcome prediction.

### Memory leak detection layers arena watermarks with MSVC ASan

With archetype ECS arena memory, traditional leak detectors don't understand arena semantics. Use a **multi-layer strategy**: arena high-water-mark assertions that log warnings when per-frame allocation exceeds a budget (e.g., 4MB warning, 8MB fatal), debug allocation tracking that records `{file, line, size, frame}` for each arena allocation (compiled out in release), MSVC AddressSanitizer (`/fsanitize=address`) in CI to catch use-after-free and buffer overflows in non-arena code, MSVC CRT debug heap (`_CrtDumpMemoryLeaks()`) for non-arena heap leak detection (since MSVC ASan doesn't include LeakSanitizer), and custom `TrackedAllocator<T>` wrappers for STL containers to monitor `std::vector`/`std::string` growth trends that bypass arenas.

---

## Category 6: Gameplay integrity and anti-cheat

### Skill cooldown enforcement needs hybrid timestamp-plus-tick dedup

At 20 tick/sec, the server processes commands in 50ms batches. A modified client can flood multiple `CmdUseSkill` packets within a single tick window. If `isOnCooldown()` processes all queued commands sequentially without updating cooldown state between them, a skill fires multiple times per tick. WoW private server research confirms this vector: zeroing the client-side GCD via memory editing allows spamming skill packets, but WoW's server-side enforcement limits it to ~1 cast/second regardless.

The fix uses **wall-clock timestamps** (`std::chrono::steady_clock`, monotonic) for cooldown expiry plus a **tick-based dedup** that rejects duplicate skill commands within the same server tick. Process at most one `CmdUseSkill` per skill per entity per tick. Track the `readyAt` timestamp and `lastUsedTick` for each skill. Log rejected skill commands for behavioral analysis — a player consistently hitting cooldown rejection at packet-flood rates is clearly using a modified client.

### Loot pickup atomicity requires a compare-and-swap claim

If two players send `CmdAction(type=3)` targeting the same dropped item simultaneously, both can pass the "item exists" check before either removes it — a classic TOCTOU (time-of-check-to-time-of-use) duplication bug. The fix is a single **`std::atomic<EntityID>` claim field** on each dropped item, resolved by `compare_exchange_strong`:

```cpp
bool DroppedItem::tryClaim(EntityID claimant) {
    EntityID expected = INVALID_ENTITY;
    return claimedBy_.compare_exchange_strong(expected, claimant,
        std::memory_order_acq_rel);
}
```

Exactly one caller wins the CAS; all others receive "item already taken." Validate range and inventory space after claiming but before adding to inventory. At the database level, `DELETE FROM dropped_items WHERE id = $1 AND claimed_by IS NULL` provides a secondary CAS. For personal loot protection in hardcore PvP, dropped items get a 30–60 second ownership window for the killer before becoming free-for-all.

### AOI validation gates every action through the server-maintained visibility set

A modified client can target entities it shouldn't know about — either obtained via memory scanning, packet sniffing, or manipulated position reports. The fix: the server maintains a per-player `std::unordered_set<EntityID>` representing their AOI, updated every tick based on **server-authoritative position** (never client-reported position). Every action that references a target entity must pass three checks: the entity exists in the world, the entity exists in the player's AOI set, and the distance between the server's authoritative positions of the player and target is within action range (melee=64px, ranged=128px, loot=48px, trade=96px).

The 320px/384px AOI hysteresis prevents rapid enter/leave flickering but doesn't affect security — the critical point is that movement validation uses server position and the AOI set is computed from server position. A client that reports a false position to expand their AOI first fails the movement validation check (speed/teleport detection), getting rubber-banded to their server-authoritative position, which then recomputes their correct AOI.

### PvP balance loads from hot-reloadable JSON with atomic config swap

Store damage multipliers, level scaling curves, and the 3×3 class advantage matrix in a version-numbered JSON file. Load on startup, then check file modification time every 5–10 seconds (or on admin command `/reload balance`). On change, parse the new config, validate all values (no negative multipliers, sane ranges), then atomically swap via `std::atomic_store` on a `std::shared_ptr<const BalanceConfig>`. Reader threads get the new config on their next `std::atomic_load` without any locking on the hot path.

The damage formula chains multiplicatively: `baseDamage × classMatrix[attacker][defender] × levelScaling(levelDiff) × globalPvPMultiplier × (1 - armorReduction)`. Level scaling uses a clamped `tanh` curve: `base + maxBonus × tanh(levelDiff × steepness)`, providing diminishing returns on level advantage (configurable steepness and max bonus, typically ±30%). This entire formula's coefficients live in the JSON file, enabling live tuning without recompilation or restart.

### Death state needs a server-authoritative state machine gating all actions

Without proper server-side `isDead` enforcement, `CmdRespawn` becomes a free full-heal and teleport. Implement a **four-state life cycle**: Alive → Dying (brief loot-drop window) → Dead (spectator mode, waiting for respawn) → Respawning (cooldown active, transitioning). Every incoming command passes through `canPerformAction(action, lifeState)` — dead players can only chat, spectate, and request respawn. Everything else (movement, attack, skill use, trade, pickup, inventory manipulation, marketplace) is rejected.

Anti-abuse measures: **escalating respawn cooldowns** (10s base + 5s per recent death, capped at 40s, with death count decaying by 1 per 60 seconds alive), respawn at **reduced HP (50–75%)** not full, respawn only at designated bind points (not arbitrary locations), and brief spawn protection (3s invulnerability during which the player also cannot attack, preventing abuse as an offensive mechanic).

### Inventory slot writes must check-before-write with a database-level unique constraint

The `addItemToSlot()` silent-overwrite bug permanently destroys existing items. The fix: always check `slots_[slot].has_value()` before writing. If occupied and items are stackable with the same template ID, merge stacks up to `maxStack`. If occupied and not stackable, return `SlotResult::SlotOccupied` — never silently replace. Provide a separate `replaceItemInSlot()` function with explicit semantics that returns the displaced item as `std::optional<Item>`, forcing the caller to handle it.

At the database level, `UNIQUE(owner_id, slot_index)` on the items table is the ultimate safeguard — even if application code has a bug, the database rejects a second item in the same slot with a constraint violation. **Log every item mutation** to an `item_audit_log` table with before/after state, event type (created, destroyed, transferred, stacked, traded, looted), and context. Run periodic integrity assertions: no duplicate item UUIDs within an inventory, all stack counts within bounds, all template IDs valid. The MU Online postmortem demonstrated that without per-item GUIDs and audit logging, duplication bugs are nearly impossible to detect or reverse after the fact.

---

## Conclusion

The overarching pattern across all six categories is that **a custom 2D MMORPG's hardest problems are not rendering or even networking — they're state consistency and trust boundaries**. Three architectural decisions deliver the highest impact for the lowest effort: first, a per-player mutation queue that serializes all inventory/gold operations eliminates the entire class of item-duplication race conditions that have plagued every MMO from MU Online to ESO. Second, AEAD encryption using your existing reliability-layer sequence numbers as nonces provides replay prevention, anti-tampering, and anti-hijacking in a single layer without protocol redesign. Third, a local write-ahead journal for critical state changes is the only viable defense against SIGKILL/OOM data loss, and it doubles as a circuit-breaker queue when the database is temporarily unreachable.

The engine's 20 tick/sec rate and cardinal-only movement are actually advantages: movement prediction is trivially deterministic (4 directions, fixed speed), re-simulation for reconciliation costs nearly nothing, and the deliberate combat pace hides network latency naturally through animation windup timing. The constraint that matters most is ensuring every client command passes through a validation pipeline — rate limit → life state check → AOI membership → range check → cooldown check → atomicity guarantee → audit log — before it touches game state. Build that pipeline once, route every command through it, and the vast majority of exploits described in this report become structurally impossible.