# FateMMO engine: complete security and polish audit

**Every gap in your 56,100-line engine has a concrete fix.** This audit covers 33 specific items across network security, protocol hardening, database integrity, game feel, engine robustness, and anti-cheat — prioritized and scoped for a solo developer. The most urgent findings: the `CmdRespawn` handler allows free healing and teleportation on demand, `addItemToSlot()` silently overwrites occupied slots enabling item destruction, and the `system()` call in the editor is a command injection vector. Total estimated effort across all items is **60–80 developer-days**, but the 10 critical/high items that block alpha testing can be addressed in roughly **20–25 days**.

---

## Category 1: Network security and anti-abuse

### 1.1 Token bucket rate limiting at O(1) per packet

**Problem.** No per-command rate limiting exists. A modified client can flood the server with `CmdUseSkill`, `CmdAction`, or market commands at arbitrary rates, overwhelming the game loop or exploiting race conditions. At 20 ticks/sec, hundreds of queued commands per tick can slip through before game logic processes them.

**Solution.** A token bucket per message type per client gives both burst tolerance and sustained rate control. The bucket has a **capacity** (burst) and **refill rate** (sustained). Each incoming command consumes one token; empty bucket means the packet is dropped.

```cpp
struct RateLimitConfig {
    float burstCapacity;     // max tokens (e.g., 3 for skills)
    float sustainedRate;     // tokens/sec (e.g., 1.0 = 10 per 10s)
    uint32_t disconnectAfter; // violations before kick
};

struct TokenBucket {
    float tokens;
    std::chrono::steady_clock::time_point lastRefill;

    bool tryConsume(float rate, float capacity) {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastRefill).count();
        tokens = std::min(capacity, tokens + elapsed * rate);
        lastRefill = now;
        if (tokens >= 1.0f) { tokens -= 1.0f; return true; }
        return false;
    }
};
```

A `ClientRateLimiter` holds a `std::array<TokenBucket, 64>` indexed by message type — **O(1) lookup, no heap allocation, ~1 KB per client**. It hooks as the very first check in `onPacketReceived`, before any payload parsing. This is critical: malformed packets should be rate-limited before the server wastes cycles deserializing them.

**Response policy:** First violations are **shadow-ignored** (silent drop — never reveal rate limits to probing attackers). After 50% of the disconnect threshold, send `SMSG_RATE_LIMIT_WARNING` so legitimate laggy clients know to back off. At threshold, disconnect with logging. Example configs: skills get burst=3, sustained=1/sec; movement gets burst=25, sustained=20/sec; market operations get burst=2, sustained=0.5/sec. WoW's TrinityCore processes a max packet count per update tick as an additional backstop; RuneScape's 600ms game tick inherently caps throughput.

**Priority:** Critical | **Effort:** 1–2 days

### 1.2 SafeByteReader eliminates fuzzing crashes

**Problem.** The raw `ByteReader` with `readU8()`, `readI32()`, `readString()`, `readFloat()` is vulnerable to truncated packets (buffer overread → crash), oversized strings (uncontrolled allocation → OOM), NaN/Inf floats (poison propagation through combat math), and out-of-range enum bytes (array out-of-bounds). TrinityCore's `ByteBuffer` had exactly this class of vulnerability — `ByteBufferPositionException` crashes from malformed `CMSG_AUTH_SESSION` packets.

**Solution.** A **sticky error flag** wrapper, the pattern recommended by Glenn Fiedler (Gaffer on Games). Once any read fails, the reader enters an error state and all subsequent reads return safe defaults (zero). Handlers read all fields, then check `reader.ok()` once at the end — zero per-read boilerplate:

```cpp
class SafeByteReader {
    std::span<const uint8_t> data_;
    size_t pos_ = 0;
    ReadError error_ = ReadError::Ok;

    bool checkAvailable(size_t n) {
        if (error_ != ReadError::Ok) return false;
        if (pos_ + n > data_.size()) { error_ = ReadError::BufferUnderflow; return false; }
        return true;
    }
public:
    float readFloat() {
        if (!checkAvailable(4)) return 0.0f;
        float val; std::memcpy(&val, &data_[pos_], 4); pos_ += 4;
        if (std::isnan(val) || std::isinf(val)) { error_ = ReadError::InvalidFloat; return 0.0f; }
        return val;
    }
    std::string readString(uint16_t maxLen = 256) {
        uint16_t len = readU16();
        if (!ok() || len > maxLen) { error_ = ReadError::StringTooLong; return {}; }
        if (!checkAvailable(len)) return {};
        std::string r(reinterpret_cast<const char*>(&data_[pos_]), len);
        pos_ += len; return r;
    }
    template<typename E> E readEnum(E maxValid) {
        auto raw = readU8();
        if (!ok() || raw > static_cast<uint8_t>(maxValid))
            { error_ = ReadError::EnumOutOfRange; return static_cast<E>(0); }
        return static_cast<E>(raw);
    }
    [[nodiscard]] bool ok() const { return error_ == ReadError::Ok; }
};
```

A `REGISTER_HANDLER` macro wraps every handler with automatic error checking and trailing-bytes detection. Handlers become clean, linear read sequences with no error-handling code.

**Priority:** Critical | **Effort:** 2–3 days (wrapper + migrate all handlers)

### 1.3 Replay attacks are mostly prevented, with one gap

**Problem.** Can a captured packet be replayed to duplicate a market listing or trade confirmation?

**Analysis.** The existing session token + monotonic sequence number scheme prevents both cross-session replay (token changes per connection) and within-session replay (server rejects `seq <= last_accepted`). **However**, without per-packet authentication (HMAC or AEAD), an attacker on the network path can forge new packets with valid session tokens and future sequence numbers. Additionally, critical one-shot economic actions benefit from an extra layer.

**Solution.** Add **HMAC-SHA256 per packet** using a session-derived key (established during TLS auth handshake). For critical actions (trade confirmations, market listings, gold transfers), add **server-issued one-time nonces**: when the client opens a trade UI, the server sends a random `uint64_t` nonce. The client must echo this nonce in the confirmation packet. The server validates and consumes it — single-use, expires after 60 seconds. This prevents both transport-level replay and application-level re-submission.

**Priority:** High | **Effort:** 2 days

### 1.4 Speed hack detection via sliding window statistics

**Problem.** Simple per-move distance checks are trivially bypassed by many small, rapid position updates that individually pass validation but collectively exceed legal speed.

**Solution.** Maintain a **60-sample circular buffer** of timestamped positions per player (~1 KB per player). Compute both displacement (start→end, catches teleports) and **total path length** (sum of segments, catches speed hacks with zig-zagging). Use a weighted violation scoring system rather than hard thresholds: speed exceeded = +1, teleport = +5, acceleration spike = +2, path through wall = +3. Scores decay at ~1 point/sec. Score >10 triggers rubberband, >30 kicks, >100 temp-bans with admin review. Allow a **1.3× grace multiplier** over theoretical max speed to absorb legitimate lag spikes.

WoW's server-side validation compares reported position against expected position from acknowledged movement opcodes. MapleStory's NGS accumulates statistical evidence and bans in waves. The key insight: **lag tolerance through scoring rather than binary checks** prevents false positives while catching sustained cheating.

**Priority:** High | **Effort:** 2–3 days

### 1.5 Economy abuse: the addItemToSlot() overwrite is critical

**Problem.** `addItemToSlot()` silently overwrites occupied slots — this enables **item destruction** (overwriting a valuable item with a cheap one) and potentially **duplication** (racing two move requests for the same item). Concurrent trade + market + loot + auto-save operations on the same player can race: trade reads gold=1000, market credit reads gold=1000, trade writes 500, market writes 1200 → **500 gold lost** from a lost update.

**Solution.** Three layers of defense:

First, **fix `addItemToSlot()`** immediately — it must check slot occupancy and return false:
```cpp
bool addItemToSlot(Inventory& inv, size_t slot, Item item) {
    if (slot >= inv.slots.size() || inv.slots[slot].has_value()) return false;
    inv.slots[slot] = std::move(item);
    return true;
}
```

Second, add a **per-player mutex** guarding all inventory/gold mutations. Cross-player trades use `std::scoped_lock` with ordered locking (lower player ID first) to prevent deadlocks. Third, funnel all async mutations (market credits) through a **pending table** that the game thread polls and applies atomically, rather than allowing fiber workers to directly modify player state.

Additional mitigations: non-refundable listing fees to make rapid list/cancel cycling unprofitable (EVE's broker fee pattern), price computation based on completed transactions only (not listings), and a player state machine that prevents overlapping economic actions (trading ∣ market ∣ looting — only one at a time).

**Priority:** Critical | **Effort:** 3–4 days

### 1.6 Chat abuse: Unicode normalization before filtering

**Problem.** The profanity filter exists in `profanity_filter.h` but isn't wired server-side. Zero rate limiting. Homoglyphs (Cyrillic `а` for Latin `a`), zero-width characters, and RTL overrides bypass client-side filters trivially.

**Solution.** Server-side chat pipeline: rate limit (token bucket: burst=3, sustained=1 msg/3 sec) → length check → **Unicode sanitization** (strip zero-width chars U+200B–U+200F, U+202A–U+202E, U+2060–U+2064, variation selectors; limit combining marks to 2 per base; apply **NFKC normalization** to collapse homoglyphs) → profanity filter → spam detection (track last 5 messages per player, flag if Levenshtein similarity >80%) → broadcast. Integration point: the profanity filter runs on the server in `ServerChatHandler::processMessage()`, after normalization and before broadcast. For NFKC, use ICU's `Normalizer2::getNFKCInstance()` or a lightweight confusable-skeleton mapper.

**Priority:** Medium | **Effort:** 2 days

### 1.7 Replacing system() with ShellExecuteW / fork+execvp

**Problem.** `system("code " + filePath)` passes the string to the OS shell. A path containing `; rm -rf /` or `& del /s /q C:\*` executes arbitrary commands. This is CWE-78 (OS Command Injection).

**Solution.** On Windows, use `ShellExecuteW(nullptr, L"open", filePath.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL)` — it invokes the Shell directly without `cmd.exe`, so metacharacters have no special meaning. On Unix, use `fork()` + `execvp("xdg-open", argv)` (or `"open"` on macOS) — `execvp` passes arguments as a discrete array, no shell interpretation. The path is a single argument, immune to injection. Validate the path exists before invoking. Both approaches are well-established (CERT ENV33-C explicitly prohibits `system()`).

**Priority:** High | **Effort:** 0.5 days

---

## Category 2: Network protocol hardening

### 2.1 Per-entity sequence counters prevent stale HP overwrites

**Problem.** `SvEntityUpdate` goes unreliable with no per-entity ordering. A late-arriving packet can overwrite newer HP values — particularly damaging because HP changes are infrequent, so stale data persists visually.

**Solution.** Add a `uint8_t updateSeq` per entity, incremented each time that entity's state is packed into an outgoing update. Pack this single byte into each entity's delta block. Client-side, use wrapping comparison:

```cpp
inline bool seqNewer(uint8_t a, uint8_t b) {
    return static_cast<int8_t>(a - b) > 0;  // works for window of 128
}
```

Discard any update where the incoming sequence isn't newer than the last applied. **Bandwidth cost: 1 byte per entity per update** — with 30 entities in AOI at 20 Hz, that's 600 bytes/sec, negligible. Source Engine uses entity serial numbers in `edict_t` plus snapshot-level tick counters for similar ordering guarantees.

**Priority:** High | **Effort:** 1 day

### 2.2 Expanded delta compression from 4 to 16 fields

**Problem.** Only position, animFrame, flipX, and currentHP are delta-replicated. Status effects, death state, skill animations, equipment visuals, and casting state have no network representation.

**Solution.** Expand to a **16-bit field bitmask**: add velocity/moveDir (2B), animState (2B), maxHP (2B), statusEffects (4B bitmask for 32 effects), deathState (1B), castingState (3B: skillId + progress), faction (1B), level (1B), nameplateFlags (1B), equipVisuals (4B: weapon/armor/helm/accessory palette indices), targetEntityId (2B), auraEffects (2B), plus 1 reserved bit. Add a `schemaVersion` byte to the entity update header — old clients seeing version >0 can disconnect-and-update or skip unknown fields via a payload-length prefix. Delta compression means the expanded schema **adds bandwidth only when new fields actually change**. Status effects change ~0.5/sec, equipment changes very rarely. The common hot path (position + anim) is unchanged. Net average increase: **~10–15% over current**.

**Priority:** High | **Effort:** 3–4 days

### 2.3 Bandwidth scales to 2,000 players with tiered updates

**Problem.** At ~6.2 KB/sec/player, 2,000 concurrent players need ~100 Mbps server→client — feasible but tight during PvP spikes.

**Key bandwidth model:**

| Scenario | Entities in AOI | Avg bytes/entity/tick | KB/sec/player |
|---|---|---|---|
| Wilderness (sparse) | 10 | 10B | ~1.2 |
| Town (200 idle) | 200 (capped at 30 budget) | 6B | ~3.6 |
| Normal overworld | 30 | 12B | ~4.8 |
| Dense PvP (50 players) | 50 | 16B | ~11.2 |
| Boss fight (20 + effects) | 30 | 18B | ~9.0 |

**Solution.** Distance-based update frequency tiers: near (≤10 tiles) at every tick (20 Hz), mid (10–25) every 3rd tick (~7 Hz), far (25–50) every 5th tick (4 Hz), edge of AOI every 10th tick (2 Hz). Implement a **per-client priority queue** where each entity's score = `basePriority / distanceSq + timeSinceLastSent × urgencyWeight + (hasHPChange ? 100 : 0)`. Each tick, pop entities until the per-tick bandwidth budget (~500 bytes) is exhausted. Server-total at 2,000 concurrent: ~12 MB/sec (96 Mbps) normal, ~20 MB/sec (160 Mbps) during PvP peaks. A **200 Mbps link** covers worst-case.

**Priority:** Medium | **Effort:** 3–4 days

### 2.4 Client-side prediction eliminates movement lag

**Problem.** The client sends `CmdMove`, waits for server validation — at 100–300ms mobile RTT, movement feels sluggish.

**Solution.** The standard Quake/Gambetta prediction model adapted for 2D MMO:

The client maintains a **PredictionBuffer** (circular array of 128 frames, ~6.4 sec at 20 Hz). Each frame stores the input command, its sequence number, and the predicted local player state. On each client tick: gather input → predict locally via `simulateMovement()` → store frame → send input to server. When the server acknowledges an input sequence with authoritative state: accept server state → discard acknowledged frames → **re-predict by replaying all unacknowledged inputs** from the server's confirmed state. Visual smoothing handles small mismatches (exponential decay of offset over ~200ms); large mismatches (>N pixels) hard-snap.

Remote players use **entity interpolation**: buffer received snapshots, render 100–150ms behind real-time, linearly interpolating between bracketing snapshots. This replaces rubber-banding for most cases — rubber-banding becomes the fallback only for large teleport corrections.

**Priority:** High | **Effort:** 5–7 days

### 2.5 Connection cookies stop UDP request floods

**Problem.** Without a challenge-response handshake, an attacker can flood the server with spoofed-IP connection requests, forcing per-client state allocation and exhausting memory.

**Solution.** Adopt the **netcode.io pattern** (Glenn Fiedler). Connection requests are stateless: server generates a cookie via `HMAC-SHA256(serverSecret, clientIP:port || clientNonce || timestamp)` and sends it back. Only when the client echoes the correct cookie does the server allocate session state. This is the UDP equivalent of SYN cookies. Critical design rule: **response size ≤ request size** during handshake to prevent amplification. If the cookie is invalid, the server sends nothing (no denial packet to spoofed IPs).

Session tokens are bound to `IP:port` and **rotated every 5 minutes** — the server sends a new token via the reliable channel with a 10-second grace period accepting the old token. Under packet loss >20%, the server adapts: reduces effective update rate to 10 Hz and increases redundancy to 3× (each packet includes the previous 2 states), keeping net bandwidth at ~1.5× while dramatically improving delivery probability.

**Priority:** High | **Effort:** 2–3 days

---

## Category 3: Database and persistence integrity

### 3.1 A lightweight WAL prevents catastrophic data loss on crash

**Problem.** With 5-minute auto-save intervals, SIGKILL/power loss/OOM loses up to 5 minutes of player state. No destructors run. For an MMO, rollbacks mean duplicated items, lost rare drops, and broken quest state.

**Solution.** Implement a binary write-ahead log that **flushes every tick** (50ms) to a local file. Critical mutations (gold changes, item adds/removes, XP gains) are journaled immediately. The WAL uses a 32-byte entry header (sequence number, timestamp, player ID, entry type, payload size, CRC32) followed by a variable payload. **Batch + periodic `fdatasync()`** every 50ms (aligned to tick rate) gives ~20 syncs/sec — each batching all changes from that tick. At 100 concurrent players, expect ~200–2000 entries/sec, well within batch-fsync performance on any SSD.

On server restart, the recovery algorithm: read last CHECKPOINT LSN → for each entry after checkpoint, validate CRC32, skip if `entry.lsn <= player's last_save_lsn`, apply change to in-memory state → batch-commit recovered state to DB → truncate WAL. Use **16 MB segment files** (rotate when full, delete segments older than last checkpoint).

**Priority:** Critical | **Effort:** 3–4 days

### 3.2 Connection pool with circuit breaker replaces single-connection fragility

**Problem.** The single `gameDbConn_` is a single point of failure. If PostgreSQL restarts or a network blip drops the TCP connection, libpqxx throws `pqxx::broken_connection` and **all database operations fail** until manual server restart. libpqxx does not support automatic reconnection (confirmed by maintainer on GitHub issue #673).

**Solution.** Replace `gameDbConn_` with a proper `PgConnectionPool` using RAII leases. Each repository method acquires a `Lease` at the start, auto-releases via destructor. Wrap all DB calls with a **circuit breaker**: 5 consecutive failures → Open for 30 seconds → HalfOpen (probe with one query) → Closed on success. When the circuit is Open, the game server enters "degraded mode" — the WAL (Item 3.1) continues capturing mutations, DB writes queue up, and auto-save is disabled. Reconnection uses **exponential backoff** (100ms → 200ms → 400ms → ... capped at 10s). Each fiber in the async dispatcher acquires its own connection lease from the pool.

**Priority:** High | **Effort:** 2–3 days

### 3.3 Per-player mutex serializes concurrent mutations

**Problem.** Trade, market credit, loot pickup, and auto-save can race on the same player's inventory/gold. Without serialization, lost updates are inevitable (trade reads gold=1000, market credit reads gold=1000, both write independently → 500 gold vanishes).

**Solution.** A **striped lock map** with one mutex per player (not one global mutex, not per-field). The game thread should be the single writer for a given player's in-memory state. Async fibers (market sales, mail) write to a `pending_credits` table in the database; the game thread polls and applies these atomically. Auto-save reads a consistent snapshot under the player lock. Database transactions use **optimistic concurrency** with a `version` column: `UPDATE characters SET gold=$1, version=version+1 WHERE character_id=$2 AND version=$3` — if affected rows = 0, retry with fresh read.

Cross-player trades lock both mutexes in consistent order (`std::scoped_lock(min_id_mutex, max_id_mutex)`) to prevent deadlocks.

**Priority:** High | **Effort:** 2–3 days

### 3.4 Automated schema migration on server startup

**Problem.** Four migration files exist with no runner. Manual SQL execution is error-prone and unrepeatable.

**Solution.** A `MigrationRunner` that scans a `migrations/` directory for files named `NNN_description.sql`, queries a `schema_migrations` table for already-applied versions (with SHA-256 checksum verification to detect tampered files), and applies pending migrations in order. Each migration is wrapped in a PostgreSQL transaction (DDL is transactional in Postgres, unlike MySQL, so partial application is impossible). The runner executes **before pool initialization** on server startup. On failure, it throws and aborts startup — never start with an inconsistent schema. Forward-only migrations (no rollback) keeps it simple; write a new migration to undo if needed.

**Priority:** Medium | **Effort:** 1 day

### 3.5 pgBackRest gives near-zero RPO with minimal setup

**Problem.** No backup strategy means any data corruption, disk failure, or accidental DELETE destroys all player data permanently.

**Solution.** Enable PostgreSQL WAL archiving (`wal_level=replica`, `archive_mode=on`) and configure **pgBackRest** with weekly full + daily differential backups. This enables **point-in-time recovery** to any moment — RPO approaches near-zero (recover up to the last archived WAL segment, typically within seconds of failure). For a simpler alternative, a cron job running `pg_dump -Fc` every 4 hours with 14-day retention. Target RPO: **5 minutes** (continuous WAL archiving + game auto-save). Target RTO: **30 minutes** (pgBackRest delta restore + WAL replay). Copy backups off-machine (even rsync to another disk is better than nothing). **Test restores monthly.**

**Priority:** High | **Effort:** 0.5–1 day

---

## Category 4: Game feel and client polish

### 4.1 Optimistic combat feedback with graceful reconciliation

**Problem.** Attacks wait for server response before showing any visual feedback. At 100–300ms mobile RTT, combat feels unresponsive.

**Solution.** The key insight: **full combat state prediction is NOT needed** for a tab-target/action 2D MMO. Instead, use optimistic visual feedback with server reconciliation. On input: immediately play attack animation, hit VFX (short-lived particles, ~0.05–0.1s), and hit SFX. Optionally show a transparent predicted damage number. Store a `PendingCombatPrediction` in a ring buffer (max 32 entries) keyed by prediction ID.

When the server response arrives: confirmed hit → upgrade damage number to final value with a scale-bounce animation; miss vs predicted hit → the particles have already faded naturally (they're short-lived by design), show "MISS" text; wrong damage → quick interpolation from predicted to actual (±20% difference is barely noticeable); target already dead → extra particles add to death spectacle. **Keep hit VFX short-lived** so corrections never require visual "undo."

**Priority:** Critical | **Effort:** 4–5 days

### 4.2 SDL_mixer with an abstraction layer for future FMOD migration

**Problem.** Zero audio. Audio is 50%+ of game feel per industry consensus.

**Solution.** Start with **SDL_mixer** — already in the SDL2 ecosystem, zero licensing cost, supports OGG music streaming and WAV SFX playback. Design an `AudioManager` interface so FMOD can be swapped in later. Key systems: **24 SFX channels** with priority-based stealing (UI sounds always play, mob attacks are lowest priority and get culled first), per-zone ambient music with crossfade (use `Mix_FadeOutMusic()` + `Mix_FadeInMusic()` with brief overlap), and **2D spatial audio** (distance-based volume: `volume = 1.0 - clamp(dist / maxDist, 0, 1)`, panning based on listener-relative x-offset).

Memory strategy: SFX (<1s, WAV) preloaded per zone (~50 sounds × 50 KB = 2.5 MB); music (2–5 min, OGG) streamed via `Mix_LoadMUS()`, one at a time. Total audio budget: ~10–15 MB on mobile. When all channels are busy, steal the lowest-priority channel that's furthest from the listener.

**Priority:** High | **Effort:** 4–6 days

### 4.3 Screen shake, hit flash, and hit-stop in under 2 days

**Problem.** Combat has no visual feedback beyond animations and damage numbers. Without juice, the game feels flat.

**Solution.** Three systems with outsized impact-to-effort ratios:

**Screen shake:** Trauma-based system (per Squirrel Eiserloh's GDC talk). Trauma variable (0–1) accumulates on hits, decays at 0.8/sec. Shake amplitude = `trauma^2`. Use Perlin noise for smooth displacement. Critical for **480×270**: offsets must be **integer virtual pixels** to avoid sub-pixel shimmer (each virtual pixel maps to 4–8 physical pixels). Max shake ±3–4 pixels. Zero rotation (causes pixel artifacts with pixel art).

**Hit flash:** GLSL fragment shader: `finalColor = mix(texColor.rgb, vec3(1.0), u_flashAmount)`. Set `u_flashAmount = 1.0` on hit, decay to 0 over 2–3 frames (~0.05s). White flash for dealing damage, red tint for taking damage.

**Hit-stop:** Freeze game time for 0.02–0.06s on significant hits. **Non-additive** (take max, not sum — lesson from Vlambeer's LUFTRAUSERS). Cap at 80ms. Particles keep playing during freeze. Suggested trauma values: basic attack = 0.1, skill hit = 0.2, boss slam = 0.4, player death = 0.6.

**Priority:** High | **Effort:** 1–2 days

### 4.4 Virtual D-pad and skill buttons for mobile

**Problem.** No touch input means no mobile play. TWOM uses a fixed D-pad on lower-left, skill buttons on lower-right, with tap-to-target.

**Solution.** Unified `InputManager` that maps both keyboard and touch to a `GameAction` enum. The **VirtualDPad** uses SDL2 `SDL_FINGERDOWN`/`SDL_FINGERMOTION`/`SDL_FINGERUP` events with scaled radial dead zone (20–25% of outer radius). Fixed position lower-left, ~80×80 virtual pixels. Each finger is tracked independently by `SDL_FingerID` — the D-pad claims one finger, skill buttons claim others, enabling **simultaneous movement + skills**.

Layout at 480×270: D-pad lower-left, Attack button (48×48 vp, largest) lower-right, 5 skill buttons (36×36 vp) arranged in arc around attack button. At common phone DPIs, 36 virtual pixels = 72–108 physical pixels — comfortably above the 44pt minimum touch target. **Tap-to-target**: convert screen touch to world coordinates via camera inverse transform, expand sprite hitboxes by 50–100% for touch (16px sprite gets 32px touch target), use nearest-entity search within a touch radius.

**Priority:** Critical | **Effort:** 4–5 days

### 4.5 Keep ImGui for alpha, migrate to RmlUI for ship

**Problem.** Dear ImGui is mouse-centric with limited styling, no CSS-like theming, and poor touch support. It won't ship on mobile.

**Solution.** Phased approach. RmlUI is the standout choice: it has an SDL2+OpenGL3 backend matching FateMMO's stack exactly, uses HTML/CSS (familiar skills), supports native touch with inertial scrolling (added in v5.0), has been used in shipped MMORPGs (ROSE Online), and is MIT licensed. It generates vertices/indices/draw commands — you bring your own renderer, fitting the existing OpenGL pipeline.

Migration path: Phase 1 (alpha, now) keep ImGui for all UI. Phase 2 (pre-beta, 2–4 weeks work): integrate RmlUI alongside ImGui, port one complex screen (inventory with drag-and-drop) to validate. Phase 3 (beta): port remaining screens, keep ImGui only for the dev console and debug overlays. Nuklear is a lateral move from ImGui with the same limitations. Custom UI is months of solo dev work for inferior results.

**Priority:** Medium | **Effort:** 2–4 weeks spread over development

---

## Category 5: Engine robustness

### 5.1 Structured error handling with std::expected and circuit breakers

**Problem.** No global error recovery. DB failures get retry-once with DATA LOSS logging. A PostgreSQL outage causes immediate data loss for all in-flight writes.

**Solution.** Use C++23 `std::expected<T, EngineError>` as the return type for all fallible operations — zero dynamic allocation, monadic chaining with `.and_then()` and `.transform()`. Define error categories: `Transient` (retry likely succeeds), `Recoverable` (can queue and degrade), `Degraded` (subsystem offline), `Fatal` (unrecoverable). A global `ErrorManager` tracks per-subsystem circuit breaker state. When the DB circuit opens: queue writes to a bounded ring buffer (~10K entries), disable auto-save, enable in-memory dirty tracking. On circuit close: drain write queue with batch operations. Never crash the client — show "reconnecting..." UI during degraded state.

**Priority:** High | **Effort:** 3–4 days

### 5.2 LRU texture cache with VRAM budget

**Problem.** `TextureCache` holds `shared_ptr<Texture>` indefinitely with no eviction. Zone transitions accumulate textures without bound.

**Solution.** Replace with a size-capped LRU cache using an intrusive doubly-linked list for O(1) promotion/eviction and a hash map for O(1) lookup. Track `estimatedVRAM` per entry (`width × height × bpp × 1.33` for mipmaps). Set a configurable VRAM budget (512 MB default). Per-frame eviction pass: walk LRU tail, evict entries where `refCount == 0` and `lastAccessFrame < currentFrame - 300` until usage drops below 85% of budget. Zone transition triggers `onZoneUnload(zoneId)` for bulk eviction. Never evict textures with `refCount > 0` (currently rendering).

**Priority:** Medium | **Effort:** 2–3 days

### 5.3 Async asset loading via fiber workers with PBO upload

**Problem.** Main-thread-only asset loading causes hitches during zone transitions.

**Solution.** Three-stage pipeline: (1) **Decode on fiber workers** — texture decode (stb_image), JSON parse, tilemap chunks run on the existing fiber/job system via blocking I/O (fine on workers). (2) **Upload queue** — decoded assets flow through an SPSC queue to the main thread. (3) **PBO-based GPU upload** — use double-buffered Pixel Buffer Objects for DMA transfer; `glTexSubImage2D` with PBO returns immediately (GPU performs async DMA). Process max 2 uploads per frame to spread the cost over ~30 frames.

Zone transition job graph: request manifest → fan-out N parallel decode jobs → enqueue to upload queue → main thread PBO uploads (2/frame) → zone activation. Use placeholder textures (1×1 magenta) until real textures upload. Pre-load adjacent zone assets when the player is within N tiles of a boundary.

**Priority:** High | **Effort:** 5–7 days

### 5.4 GitHub Actions CI with headless SDL2 testing

**Problem.** No automated testing. Bugs only found during manual play sessions.

**Solution.** GitHub Actions workflow with a 3-compiler matrix (MSVC on Windows, GCC-13 and Clang-17 on Ubuntu). Cache FetchContent dependencies via `actions/cache@v4` keyed on `CMakeLists.txt` hash. Server tests (ECS logic, combat math, protocol parsing) run headlessly on all platforms. Client rendering tests (shader compilation, texture pipeline) run on Linux with **Xvfb + Mesa software renderer** (`LIBGL_ALWAYS_SOFTWARE=1`) providing an OpenGL 3.3 context. Add `clang-tidy` and `cppcheck` in a separate static-analysis job. Use `services: postgres` for database integration tests.

**Priority:** High | **Effort:** 2–3 days

### 5.5 Deterministic RNG is a cosmetic issue, not a gameplay one

**Problem.** Server and client both use `thread_local std::mt19937` for combat RNG.

**Analysis.** In a server-authoritative MMO, this is **not a critical desync problem**. The server is the source of truth for all combat outcomes. Client RNG is only used for visual prediction. The only visible issue: client predicts a crit (local mt19937 rolls crit), shows crit animation → server says no → jarring visual correction.

**Solution.** Replace server combat RNG with **SplitMix64** (5× faster than mt19937, 8 bytes state, passes PractRand) seeded per-action: `seed = hash(attackerID, targetID, serverTick, actionSeqNum)`. Include combat results (`{damage, isCrit, procEffects}`) in server response packets. Client removes local prediction for combat RNG — just displays server results. Keep mt19937 only for non-gameplay visuals (particle randomness, idle animations). Do **not** send seeds to clients (enables prediction of future rolls → exploit risk).

**Priority:** Low | **Effort:** 1–2 days

### 5.6 Arena watermark tracking plus ASan for non-arena leaks

**Problem.** Arena allocators make traditional leak detection harder. ASan sees arenas as single large allocations, missing per-object leaks within them.

**Solution.** Complementary approach: (1) **Arena watermark assertions** — track `highWaterMark_` across arena resets. If the watermark grows for >300 consecutive frames (5 seconds), log a warning with arena name and growth rate. This catches "logical leaks" (objects allocated but never logically freed). (2) **Debug-mode allocation tracker** — in debug builds, override global `operator new`/`operator delete` to track all heap allocations with size, source location, and frame number. Per-frame budget assertions catch unexpected growth. (3) **AddressSanitizer** (already in CMake presets) catches use-after-free, buffer overruns, and heap leaks at program exit via LeakSanitizer. (4) **Per-frame memory snapshots** — record arena usage, heap bytes, and VRAM estimate every N frames. Detect monotonic growth trends over a 60-second window.

**Priority:** Medium | **Effort:** 3–4 days

---

## Category 6: Gameplay integrity and anti-cheat

### 6.1 Per-tick command cap prevents cooldown bypass

**Problem.** `SkillManager::isOnCooldown()` is **not sufficient**. At 20 ticks/sec, multiple `CmdUseSkill` packets can arrive and be batched into a single tick. The first triggers the skill and starts cooldown, but subsequent packets in the same tick may pass the check before the cooldown is recorded. TrinityCore fixed exactly this (Issue #759) — clients bypassed the Global Cooldown entirely by modifying client-side data.

**Solution.** A `ServerCooldownTracker` using **timestamps** (not tick counts) for sub-tick precision, plus a **hard cap of 1 skill command per tick**:

```cpp
class ServerCooldownTracker {
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> skillExpiry_;
    std::chrono::steady_clock::time_point gcdExpiry_;
    uint32_t commandsThisTick_ = 0;
public:
    CooldownResult tryUseSkill(uint32_t skillId, const SkillData& data) {
        if (commandsThisTick_ >= 1) return CooldownResult::RATE_LIMITED;
        auto now = std::chrono::steady_clock::now();
        if (now < gcdExpiry_) return CooldownResult::GCD_ACTIVE;
        if (auto it = skillExpiry_.find(skillId); it != skillExpiry_.end() && now < it->second)
            return CooldownResult::ON_COOLDOWN;
        skillExpiry_[skillId] = now + data.cooldownDuration;
        if (data.triggersGCD) gcdExpiry_ = now + gcdDuration_;
        ++commandsThisTick_;
        return CooldownResult::OK;
    }
    void onTickEnd() { commandsThisTick_ = 0; }
};
```

This tracks GCD independently from per-skill cooldowns (matching WoW's architecture) and supports shared/category cooldowns for TWOM-style skill families.

**Priority:** Critical | **Effort:** 2–3 days

### 6.2 Atomic pickup lock prevents loot duplication

**Problem.** Without a pickup lock, two players (or the same player spamming `CmdAction(type=3)`) can race on the same dropped item: both pass the ownership check before the entity is destroyed → **item duplicated**. Classic TOCTOU bug.

**Solution.** Add a `bool pickupLock_` flag to `DroppedItem`. The pickup operation uses a **check-and-set** pattern: if `pickupLock_` is already true, return `ALREADY_PICKED`. Since the game loop is single-threaded, a simple bool flag (not atomic) suffices — the key is that it's set before any async work. Critically, destroy the entity **only after** successful inventory addition. If the inventory is full, release the lock so others can try. Add an ownership timer that transitions items to free-for-all after a configurable duration (Path of Exile uses ~2.6 seconds).

**Priority:** High | **Effort:** 1–2 days

### 6.3 Server-side target validation closes ghost entity exploits

**Problem.** A modified client can send `CmdAction` targeting entities outside their actual AOI, attack invisible targets by enumerating entity IDs, or target entities they shouldn't be able to see.

**Solution.** A `TargetValidator` that checks every `CmdAction` against the **server's authoritative AOI** (not the client's claimed state). Validation pipeline: (1) target entity must exist on server, (2) target must be in actor's server-side AOI list, (3) range check with latency tolerance: `maxRange + min(estimatedLatencyMs × 0.001 × maxMoveSpeed, 2.0)`, (4) target type validation (can't attack friendly NPCs), (5) target state validation (not dead, not invulnerable), (6) optionally, line-of-sight via Bresenham raycast on the tilemap. Entity IDs should be **randomized** (not sequential) to prevent enumeration.

**Priority:** Critical | **Effort:** 3–4 days

### 6.4 Config-driven PvP balance system

**Problem.** PvP multipliers (0.05× auto-attacks, 0.30× skills) are hardcoded in different code locations. Tuning requires recompilation.

**Solution.** A unified `DamageCalculator` that reads all PvP modifiers from a TOML config file, hot-reloadable without restart. Config includes: per-attack-type multipliers, level scaling (Ragnarok Online-style compression: higher damage is reduced more than lower damage, preventing one-shots), a class advantage matrix (warrior vs mage, mage vs ranger, etc.), zone-based PvP rules (safe zones, PvP zones, contested zones), and healing reduction. All PvP modifiers flow through a single code path — no more scattered `* 0.05` literals. The config also specifies minimum PvP level (prevent newbies from being griefed) and PvP zone definitions.

**Priority:** Medium | **Effort:** 3–4 days

### 6.5 Death state machine closes the free-heal-and-teleport exploit

**Problem.** `isDead` is never set server-side for PvE deaths. A client can send `CmdRespawn` at any time to get **healed to full HP and teleported to a safe spawn point on demand** — free healing + free teleportation.

**Solution.** A `PlayerLifeStateMachine` with validated transitions: `ALIVE → DEAD → RESPAWNING → ALIVE`. The respawn handler's first check: `if (state_ != LifeState::DEAD) return NOT_DEAD`. Add an **escalating respawn cooldown** (5s base + 5s per consecutive death, capped at 60s) to prevent spawn-camping exploitation. Respawn heals to 50% (not full). Respawn location is **server-determined** (bound respawn point from visiting a village), never from client data. Apply death penalties (XP loss scaling with level, optional gold drop in PvP zones).

Until full server-side mobs are implemented, validate PvE damage reports from the client against mob templates: if `claimedDamage > mobTemplate.maxPossibleDamage × 1.2`, reject the report. This is a transitional measure — the long-term fix is server-side mob simulation.

**Priority:** Critical | **Effort:** 2–3 days (state machine + validation). Server-side mobs: 2+ weeks (separate project)

---

## Prioritized implementation roadmap

The 33 items distill into a clear execution order. Items are grouped by wave, with dependencies respected and highest-impact items first.

**Wave 1 — Critical blockers (blocks alpha testing, ~12–15 days):**

| Item | Category | Effort |
|---|---|---|
| 6.5 Death state machine | Anti-cheat | 2–3 days |
| 1.5 addItemToSlot() fix + inventory locking | Economy | 3–4 days |
| 6.1 Skill cooldown enforcement | Anti-cheat | 2–3 days |
| 1.1 Rate limiting | Network security | 1–2 days |
| 1.2 SafeByteReader | Network security | 2–3 days |

**Wave 2 — High priority (before open beta, ~25–30 days):**

| Item | Category | Effort |
|---|---|---|
| 3.1 WAL for crash recovery | Database | 3–4 days |
| 3.2 Connection pool + circuit breaker | Database | 2–3 days |
| 3.3 Concurrent mutation safety | Database | 2–3 days |
| 2.4 Client-side prediction | Protocol | 5–7 days |
| 5.4 CI/CD pipeline | Engine | 2–3 days |
| 4.4 Mobile touch input | Client | 4–5 days |
| 4.1 Combat feel prediction | Client | 4–5 days |
| 4.3 Screen shake/juice | Client | 1–2 days |

**Wave 3 — Medium priority (production polish, ~20–25 days):**

| Item | Category | Effort |
|---|---|---|
| 4.2 Audio system | Client | 4–6 days |
| 2.1 Per-entity sequence counters | Protocol | 1 day |
| 2.2 Delta field expansion | Protocol | 3–4 days |
| 5.3 Async asset loading | Engine | 5–7 days |
| 5.1 Error handling system | Engine | 3–4 days |
| 3.5 Backup/DR | Database | 0.5–1 day |

Remaining items (6.3 ghost entity validation, 1.4 speed hack detection, 1.3 replay prevention, 2.5 connection hardening, 6.2 loot locks, 1.7 system() fix, 2.3 bandwidth budgeting, 3.4 schema migrations, 5.2 texture cache, 5.6 memory leak detection, 6.4 PvP config, 1.6 chat abuse, 4.5 UI migration, 5.5 deterministic RNG) fill out Wave 3 and beyond.

## Conclusion

The engine's server-authoritative architecture is a strong foundation — most MMOs that suffer catastrophic exploits lack this. The most dangerous gaps are logic errors, not architectural ones: the death state bypass, the slot overwrite, and the cooldown batch exploit are all fixable in days, not weeks. The protocol is fundamentally sound (session tokens + sequence numbers cover most replay scenarios), but needs the SafeByteReader to survive hostile input. For game feel, the combat prediction and screen shake systems will transform the player experience disproportionately to their implementation cost. The database layer needs the WAL and connection recovery before any public testing — silent data loss during a crash is the one failure players never forgive.