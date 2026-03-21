# FateMMO Engine — Research Prompt

## Context: What You Are Researching For

You are researching for a 2D MMO game engine called **FateMMO**, written in C++23 with a custom reliable UDP networking layer, PostgreSQL persistence via libpqxx, an archetype-based ECS, and a fiber-based job system. The engine is currently rated 8.2/10 on a production readiness scale and is suitable for open beta. The three research topics below target the biggest remaining gaps: multi-zone scalability, network protocol security, and database connection optimization.

All research output should be **specific and actionable** — not generic overviews. Every recommendation should reference the concrete files, classes, and patterns described below. Prefer battle-tested, production-proven approaches over theoretical ones. When citing external projects or papers, explain how they map to FateMMO's existing architecture. Code examples should be in C++20/23 or pseudocode that maps to the existing codebase style.

---

## Current Architecture Summary (Read This First)

### Server Application (`server/server_app.h`, `server/server_app.cpp` — 3,758 lines)

The server is a single-process, single-zone monolith:

- **Tick rate:** 20 Hz (50 ms per tick), fixed timestep
- **Networking:** Custom reliable UDP on port 7777, protocol ID `0xFA7E`, 16-byte packet headers, 1200-byte max packet size, three channels (Unreliable, ReliableOrdered, ReliableUnordered)
- **Authentication:** Separate TLS auth server on port 7778, bcrypt passwords, session tokens (`uint32_t`)
- **Packet types:** 13 client→server types (`0x10`–`0x1D`), 17 server→client types (`0x90`–`0xA6`), 5 system types
- **ECS:** Archetype-based with SoA storage, compile-time `CompId`, entity replication via `ReplicationManager` with AOI (Area of Interest) visibility sets, delta compression, and hysteresis (320 px enter / 384 px exit)
- **Serialization:** `ByteReader`/`ByteWriter` with fixed-size buffers, overflow flag pattern (no exceptions), NaN/Inf rejection on floats, enum range clamping via `readEnum(maxValid)`
- **Rate limiting:** Per-client token-bucket array (256 slots), escalation: Ok → Dropped → Disconnect

### Database Layer

- **Single connection (`gameDbConn_`):** All 12 repositories (Character, Inventory, Skill, Guild, Social, Market, Trade, Bounty, Quest, Bank, Pet, ZoneMobState) use this single `pqxx::connection` for synchronous operations on the game thread
- **Connection pool (`DbPool`):** Min 5, max 50 connections, `std::mutex`-guarded idle list, RAII `Guard` for auto-release, `DbCircuitBreaker` integrated (5-failure threshold, 30 s cooldown)
- **Async dispatcher (`DbDispatcher`):** Dispatches work lambdas to fiber worker threads via `JobSystem`, acquires pooled connection per job, enqueues completion callbacks for main-thread drain next tick
- **Currently only `savePlayerToDBAsync()` uses the pool/dispatcher** — all reads and all other writes go through `gameDbConn_`
- **Auto-save:** Every 300 s per player, staggered so not all players save simultaneously
- **WAL:** Write-Ahead Log for crash recovery (GoldChange, ItemAdd, ItemRemove, XPGain, LevelUp), CRC32 checksums, 16 MB cap — currently logs entries on recovery but does not replay them
- **Maintenance timers:** Market expiry (60 s), Bounty expiry (60 s), Trade cleanup (30 s)

### Zone / Scene System

- `SceneManager` — singleton, one active scene at a time, `loadScene()` / `switchScene()` / `unloadScene()`
- `CmdZoneTransition` — client sends target scene name, server validates (level requirement, combat check), looks up spawn position from `SceneCache`, sends `SvZoneTransitionMsg` back
- **Currently single-zone:** The server loads one world, all players share one `World` ECS instance, one `ReplicationManager`, one `NetServer`
- `activeAccountSessions_` — in-memory `unordered_map<account_id, clientId>` prevents duplicate logins but only within this one process

### Session / Per-Client State

All per-client state is in-memory `unordered_map`s keyed by `uint16_t clientId`:
- `lastValidPositions_`, `lastMoveTime_`, `moveCountThisTick_`, `skillCommandsThisTick_`
- `needsFirstMoveSync_`, `nextAutoSaveTime_`, `lastAutoAttackTime_`, `skillCooldowns_`
- `rateLimiters_` (ClientRateLimiter per client)
- `playerLocks_` (PlayerLockMap — per-character mutex for async DB safety)

### Build / Test

- CMake, C++23, targets: `fate_engine` (static lib), `FateEngine` (client), `FateServer` (server), `fate_tests`
- Dependencies: SDL2, nlohmann/json, spdlog, ImGui, doctest, Tracy profiler, OpenSSL, libpqxx 7.9.2, bcrypt
- CI: GitHub Actions — Windows MSVC, Linux GCC-13, Linux Clang-17
- Test suite: 507 test cases, 1,653 assertions, 71 test files

---

## Research Topic 1: Multi-Zone MMO Server Architecture

### What We Need to Learn

The engine currently runs as a single process hosting one zone. We need to scale to multiple zones (think: overworld, dungeons, cities, instanced content) where each zone is a separate `ServerApp` process. Research the following **specific** design decisions:

### Questions to Answer

1. **Zone-process topology:** Should each zone be a standalone `ServerApp` process (shared-nothing), or should we use a single process with multiple `World` instances? What are the tradeoffs for a 2D MMO with our 20 Hz tick rate and expected player counts of 100–500 per zone, 2,000–10,000 total?

2. **Cross-zone shared services:** Our Market, Guild, Social (friends/blocks), Bounty, and Chat systems currently live inside `ServerApp`. These need to work across zones. Research the tradeoffs between:
   - **Message bus approach:** Each zone process publishes/subscribes to a message broker (Redis Pub/Sub, NATS, ZeroMQ). Shared state lives in the database; cache invalidation via pub/sub.
   - **Dedicated service processes:** Separate `MarketService`, `GuildService`, `ChatService` processes that zones RPC into.
   - **Shared Redis/Valkey layer:** All cross-zone state lives in Redis; zone processes are stateless for shared data.
   - Which approach works best given that we already have PostgreSQL with 65+ tables and a connection pool?

3. **Zone transition handoff protocol:** When a player moves from Zone A to Zone B:
   - How do we atomically transfer the player entity, inventory, buffs, cooldowns, and in-progress state?
   - Do we save-to-DB-then-load, or do we do a direct process-to-process handoff?
   - How do we handle the "gap" where the player exists in neither zone (loading screen)?
   - How do we prevent duplication exploits (player sends items in Zone A, immediately transitions to Zone B before server processes the trade)?
   - Reference our existing `CmdZoneTransition` handler which currently just validates and sends `SvZoneTransitionMsg` — what needs to change?

4. **Session management across processes:** Our `activeAccountSessions_` is in-memory. How should we track active sessions across zone processes to prevent:
   - Dual login (same account in two zones simultaneously outside of a handoff)
   - Session hijacking during zone transitions
   - Stale sessions from crashed zone processes
   - Research Redis-based session stores, distributed locks (Redlock), and lease-based approaches.

5. **Gauntlet / Instanced content:** Our `GauntletManager` runs timed competitive events. How do instanced PvP/PvE zones work in a multi-zone architecture? Does the gauntlet get its own process, or does a zone process spin up a temporary instance?

6. **Entity replication across zone boundaries:** Our `ReplicationManager` uses AOI with hysteresis. For zone-edge entities (players near a zone boundary), how do other MMOs handle visibility across zone boundaries? Do they use an overlap region, a proxy entity system, or just hard-cut at the boundary?

### Specific Projects / Approaches to Research

- Amazon's **Project Caldera** / **O3DE** multiplayer gems — how they handle zone sharding
- **SpatialOS** (Improbable) — their worker-based spatial partitioning model and why some studios abandoned it
- **Photon Server** — their room/lobby/master architecture
- **EVE Online's** "solar system as process" model with CREST/ESI for cross-system services
- **Albion Online** — their cluster-based zone architecture (one of the few successful indie MMOs)
- Any GDC talks on MMO zone architecture from 2018–2025
- Redis Cluster vs. Redis Sentinel for session management in game backends

### Output Format Requested

For each of the 6 questions above, provide:
- A recommended approach with justification specific to our architecture
- A migration path from the current single-zone `ServerApp` (what changes first, what can wait)
- Estimated complexity (files touched, new dependencies, risk level)
- Known pitfalls from real-world implementations

---

## Research Topic 2: Network Packet Fuzzing for Custom Game Protocols

### What We Need to Learn

Our custom UDP protocol has 30+ message types, each with its own `read(ByteReader&)` deserialization path. A malformed packet that causes a crash in `onPacketReceived` takes down the entire server for all players. We need to set up systematic fuzz testing.

### Current Deserialization Architecture

`ByteReader` (139 lines, `engine/net/byte_stream.h`) is the sole deserialization primitive:
- Fixed-size buffer with position tracking and an `overflow_` flag
- On overflow: sets flag, returns zero-filled data, all subsequent reads also return zero
- `readFloat()` rejects NaN/Inf (sets overflow)
- `readString(maxLen=4096)` validates length prefix against remaining buffer
- `readEnum(maxValid)` clamps enum values
- **No exceptions thrown** — everything is flag-based

`onPacketReceived()` dispatches on `PacketType` (uint8_t), calls the appropriate `read(ByteReader&)` static method, then processes the result. Example:
```cpp
case PacketType::CmdChat: {
    auto chat = CmdChat::read(payload);
    if (chat.message.empty() || chat.message.size() > 200) return;
    auto filterResult = ProfanityFilter::filterChatMessage(chat.message, FilterMode::Censor);
    // ... process
}
```

### Questions to Answer

1. **Which fuzzer is best for our use case?** Research AFL++, libFuzzer, and Honggfuzz for fuzzing C++ network parsers. Consider:
   - We need to fuzz `ByteReader`-based deserializers, not full network stacks
   - Our parsers are small (10–50 lines each) but numerous (30+ types)
   - We already use CMake with MSVC/GCC/Clang — which fuzzer integrates best?
   - We want to run fuzzing in CI (GitHub Actions) — what's the setup?

2. **Harness design:** How do we write fuzz harnesses for our message types? Specifically:
   - Should we fuzz each `PacketType::*` handler individually, or write one harness that fuzzes the full `onPacketReceived` dispatch?
   - How do we handle the state dependency (some handlers check `client->playerEntityId`, `replication_` state, etc.)?
   - Should we create a minimal `ServerApp` stub for fuzzing, or extract the parsing layer into pure functions?
   - Provide example harness code for `CmdMove::read()`, `CmdAction::read()`, and `CmdChat::read()`

3. **Coverage of the `ByteReader` primitive itself:** Our `ByteReader` handles overflow gracefully, but:
   - Are there edge cases where overflow flag + zero-fill could lead to valid-looking but semantically wrong data downstream?
   - `readString()` returns an empty string on overflow — could a handler interpret empty string as a valid command?
   - `readI64` (in `protocol.h` detail namespace) reads two U32s — are there endianness or sign-extension issues a fuzzer might find?

4. **Packet-level fuzzing (wire format):** Beyond individual message parsers, how do we fuzz:
   - Malformed `PacketHeader` (wrong `protocolId`, impossible `payloadSize`, invalid `Channel` enum)
   - Sequence number attacks (replayed sequences, massive gaps)
   - Fragmentation / reassembly if we ever add it
   - Rate limiter interaction (does the rate limiter correctly drop fuzzed packets before they reach the parser?)

5. **Integration with CI:** How do we run fuzzing in GitHub Actions?
   - Corpus management (initial seeds from recorded game sessions?)
   - Time-boxed fuzzing runs (e.g., 10 minutes per PR)
   - Crash artifact collection and reproduction
   - Coverage-guided fuzzing vs. dumb fuzzing for our use case

6. **Game-specific fuzzing concerns:**
   - State machine fuzzing: our Gauntlet system has states (Signup → Active → Tiebreaker → Completed). Can we fuzz state transitions?
   - Trade protocol fuzzing: trade has a lock/confirm/execute flow with two players. Can we fuzz interleaved actions?
   - What about fuzzing the `ReplicationManager` delta decoding on the client side?

### Specific Tools / Resources to Research

- **libFuzzer** with `-fsanitize=fuzzer,address` for LLVM-based builds
- **AFL++** with persistent mode for high throughput
- **Honggfuzz** — comparison with the above for network protocol fuzzing
- **Preeny** or **desock** for redirecting socket I/O to stdin for fuzzing
- Any GDC talks or whitepapers on fuzzing game network protocols
- Google's **OSS-Fuzz** integration patterns for C++ projects
- **Protocol Buffers fuzzing** patterns (even though we don't use protobuf, the patterns apply)

### Output Format Requested

For each question, provide:
- Recommended approach with rationale
- Example code (C++ or CMake) that maps to our existing build system
- Estimated setup time and ongoing maintenance cost
- Priority order (what to fuzz first for maximum security impact)

---

## Research Topic 3: Connection Pool Optimization Under Game Server Workloads

### What We Need to Learn

Our `DbPool` (5–50 connections) and `DbDispatcher` (fiber-based async) are built and working, but only `savePlayerToDBAsync()` uses them. All 12 repositories still call `gameDbConn_` (a single synchronous connection) directly on the game thread. We need to migrate to the pool without introducing latency spikes or data races.

### Current Database Access Patterns

**Synchronous (game thread, single connection — `gameDbConn_`):**
- Player login: `characterRepo_->loadByAccountId()`, `inventoryRepo_->loadForCharacter()`, `skillRepo_->loadForCharacter()`, `questRepo_->loadForCharacter()`, `petRepo_->loadForCharacter()`, `bankRepo_->loadForCharacter()`
- Market operations: `marketRepo_->listItem()`, `buyItem()`, `cancelListing()`, `getActiveListings()`, `getListingsForSeller()`
- Trade execution: `tradeRepo_->recordTrade()` (atomic gold + item transfer)
- Guild operations: `guildRepo_->createGuild()`, `addMember()`, `removeMember()`, `getGuildInfo()`
- Social: `socialRepo_->addFriend()`, `blockPlayer()`, `getFriends()`, `getBlocked()`
- Bounty: `bountyRepo_->placeBounty()`, `claimBounty()`, `getActiveBounties()`
- Quest: `questRepo_->saveQuestProgress()`, `completeQuest()`
- Mob state: `mobStateRepo_->saveMobState()`, `loadMobState()`

**Asynchronous (fiber workers, pooled connections — `dbDispatcher_`):**
- `savePlayerToDBAsync()` — dispatches character save + inventory save to fiber, completion callback on game thread

**Periodic maintenance (game thread, single connection):**
- Market expiry check every 60 s
- Bounty expiry check every 60 s
- Trade cleanup every 30 s
- Auto-save per player every 300 s (staggered)

### Questions to Answer

1. **Migration strategy:** How do we move repositories from `gameDbConn_` to `dbDispatcher_` without breaking the synchronous assumptions in command handlers? Many handlers do:
   ```cpp
   case PacketType::CmdMarket: {
       auto listings = marketRepo_->getActiveListings(); // sync DB call
       // ... immediately use listings to send response
   }
   ```
   This assumes the DB result is available in the same tick. With async dispatch, the result arrives next tick at earliest. Research patterns for:
   - Converting synchronous request-response handlers to async (callback-based or coroutine-based)
   - Which operations MUST stay synchronous (e.g., trade execution for atomicity)?
   - Which operations can tolerate 1-tick latency (e.g., market listing queries)?

2. **Pool sizing under game workloads:** Our pool is configured for 5–50 connections. Research:
   - How to determine optimal min/max for a 20 Hz tick server with 100–500 concurrent players
   - What happens when all 50 connections are checked out? Our `acquire()` currently blocks — should it fail-fast, queue, or grow?
   - Connection creation cost for PostgreSQL (cold connect vs. pooled) and how it impacts tick time
   - Should we use PgBouncer or Pgpool-II in front of PostgreSQL instead of application-level pooling?

3. **Query batching for auto-saves:** When 100 players are online with staggered 300 s auto-saves, worst case is ~20 saves hitting within a single second (if stagger entropy is low). Research:
   - Batching multiple player saves into single transactions
   - Using `COPY` or multi-row `INSERT ... ON CONFLICT` instead of individual queries
   - Write coalescing: if a player's data changes multiple times between saves, only the final state matters
   - How to size the fiber job queue to absorb save bursts without starving game-thread completions

4. **Prepared statement caching with libpqxx:** Our repositories likely create `pqxx::work` transactions per call. Research:
   - Does libpqxx 7.9.2 support prepared statements that survive across transactions?
   - Connection-level prepared statement caching (`pqxx::connection::prepare()`)
   - How does this interact with connection pooling? (prepared statements are per-connection)
   - Performance difference between `exec_params()` (parameterized but not prepared) vs. `exec_prepared()` for our query patterns

5. **Circuit breaker interaction with pool:** Our `DbCircuitBreaker` opens after 5 consecutive failures. Research:
   - When the circuit breaker is open, what happens to in-flight async jobs that are waiting for a connection?
   - Should the dispatcher queue jobs during the open state and replay them when half-open succeeds?
   - How to handle partial failures (1 of 50 connections is bad, others are fine) — should we evict bad connections individually?
   - Health check patterns: should the pool periodically `SELECT 1` on idle connections?

6. **Read replicas and read/write splitting:** Most of our reads (market listings, guild info, leaderboards) are tolerant of slight staleness. Research:
   - Setting up a PostgreSQL read replica and routing read-only queries to it
   - How to configure libpqxx to use different connection strings for reads vs. writes
   - Integration with our `DbDispatcher` — can we have two pools (read pool, write pool)?
   - What consistency guarantees do we need? (e.g., "player just listed an item, then queries listings — must see own listing")

### Specific Technologies / Patterns to Research

- **libpqxx 7.9.2** prepared statement API and connection lifecycle
- **PgBouncer** transaction-mode pooling vs. session-mode pooling for game servers
- **PostgreSQL LISTEN/NOTIFY** for cache invalidation across zone processes (ties into Topic 1)
- **Citus** or **pg_partman** for horizontal scaling of large tables (market_listings, trade_history)
- **pgbench** for simulating game workload patterns
- **HikariCP** (Java) and **pgxpool** (Go) — proven game-backend pool implementations to learn patterns from
- Any postmortems from game studios on PostgreSQL scaling (Albion Online uses PostgreSQL)

### Output Format Requested

For each question, provide:
- Recommended approach with justification specific to our `DbPool`/`DbDispatcher`/`pqxx` stack
- Migration steps (what to change in `server_app.cpp` and the repositories)
- Performance expectations (latency, throughput, connection count)
- Risks and rollback plan if the migration causes issues

---

## General Instructions for the Research Agent

1. **Be specific to our codebase.** Don't give generic advice. Reference our actual class names (`ServerApp`, `DbPool`, `DbDispatcher`, `ByteReader`, `ReplicationManager`, `ClientRateLimiter`, etc.) and file paths.

2. **Prioritize production-proven patterns.** We prefer approaches that shipped in real MMOs or large-scale game backends over academic solutions.

3. **Include code examples.** When recommending a pattern, show how it would look in our C++23 codebase with our existing types and conventions.

4. **Flag dependencies.** If a recommendation requires a new library (Redis client, message broker, fuzzer), specify the exact library name, version, and CMake integration.

5. **Estimate effort.** For each recommendation, give a rough estimate: days of work, files touched, risk of regression.

6. **Order by impact.** Within each topic, order recommendations from highest impact to lowest.
