# FateMMO engine: scaling, hardening, and persistence

FateMMO's single-zone monolith rates 8.2/10 for production readiness — getting to 9+ requires three architectural upgrades: multi-zone process topology, systematic protocol fuzzing, and database pool migration from the blocking `gameDbConn_` bottleneck. This report provides battle-tested approaches for each, drawn from shipped MMOs (Albion Online, EVE Online, Path of Exile, New World) and mapped directly to FateMMO's existing `ServerApp`, `DbPool`, `ByteReader`, and `ReplicationManager` classes. **Total estimated effort across all three topics is 110–160 developer-days**, with the database work delivering the fastest ROI and multi-zone architecture carrying the highest risk.

---

## TOPIC 1: Multi-zone MMO server architecture

### Shared-nothing zone processes win on every axis that matters

The central topology decision — one process per zone vs. multiple `World` instances in a single process — has a clear answer from production systems. **EVE Online runs 5,000+ solar systems as separate processes** ("SOL nodes"), multiplexing low-activity systems onto shared blades and dedicating hardware to hot zones. Albion Online runs ~600 clusters where each cluster's game logic runs on a **single thread** within a Game Server process, with multiple clusters per machine. New World uses 7 "hub" instances per world behind 4 public-facing proxy REPs.

For FateMMO's scale (100–500 players/zone, 2,000–10,000 total), the recommended path is **multi-zone-per-process initially** (Albion model), migrating to zone-per-process later if crash isolation demands it. Run 4–8 zone `World` instances per `ServerApp` process, each on its own thread with its own ECS, `ReplicationManager`, and `SceneManager`. This minimizes IPC overhead while enabling horizontal scaling by adding processes across machines.

The migration starts with refactoring `SceneManager` from a singleton to support multiple concurrent `World` instances. Each `World` gets its own tick loop on a dedicated thread (keeping the 20 Hz cadence). A new `ZoneRouter`/`Gateway` process sits in front, accepting client UDP connections and forwarding traffic to the correct zone process. `ServerApp::run()` spawns N zone threads, each running a `ZoneWorker` with its own `World` and `ReplicationManager`.

**Estimated effort: 15–25 days.** Files touched: `ServerApp.cpp/h`, `SceneManager.cpp/h`, `World.cpp/h`, `ReplicationManager.cpp/h`, `NetworkManager.cpp/h`, plus new `ZoneRouter`/`Gateway` class. High regression risk — requires full integration testing of every game system per-zone.

### Cross-zone services need a message bus, not microservices

Market, Guild, Social, Bounty, and Chat currently live inside `ServerApp`. The temptation is to extract each into a dedicated microservice, but Albion Online's architecture provides a more pragmatic model: a dedicated **World Server** handles guilds, parties, and GvG coordination; separate **Marketplace** and **GoldMarket** servers handle economy; inter-server communication uses TCP connections.

For FateMMO, the recommended architecture is **Redis Pub/Sub as the message bus with PostgreSQL remaining as source of truth**. Zones publish events (trade requests, guild actions, chat messages) to Redis channels (`zone.{id}.events`, `market.updates`, `guild.{id}.events`). Dedicated service processes subscribe, process, and publish results back. This avoids introducing NATS (Redis is already needed for sessions — see below) while leveraging the existing 65+ table PostgreSQL schema. At FateMMO's scale, both Redis Pub/Sub and NATS deliver sub-millisecond latency; Redis wins by avoiding a new dependency.

Extract services incrementally: `MarketManager` first (highest cross-zone traffic), then `GuildManager`, `ChatManager`, `SocialManager`, `BountyManager`. Each extraction is ~4–5 days plus 3–5 days for message bus infrastructure and 5–7 days for cross-zone integration testing. **Total: 20–30 days.**

### Zone transitions must go through the database to prevent duplication exploits

Every shipped MMO that handles zone transitions safely uses a **save-to-DB-then-load** pattern. Albion Online's CTO confirmed that zone transitions always go through the database, even when source and destination are on the same Game Server process. This is the single most important architectural decision for preventing item duplication exploits.

The handoff protocol for FateMMO works as follows. When `CmdZoneTransition` fires on Zone A: freeze the player entity, serialize full state (inventory, buffs, cooldowns, in-progress quests), write to a new `zone_transfers` table with `status='pending'` and a one-time UUID transfer token, remove the entity from the `World`, and send `SvZoneTransitionMsg` with the token and destination address. The client disconnects from Zone A, connects to Zone B with the token. Zone B verifies the token, loads player state from `zone_transfers`, spawns the entity, and atomically marks the transfer `status='complete'`.

The "limbo" period (player exists in neither zone) is handled by the DB transfer record: the player's authoritative state lives in `zone_transfers` with `status='pending'`. A cleanup job detects stale transfers (>30s) and returns the player to Zone A's spawn. **The transfer token is single-use** — Zone B's `UPDATE status='complete' WHERE status='pending' AND token=$1` returns zero rows if already claimed, preventing any duplication.

Changes to existing code: add `transfer_token` generation and full entity serialization to `CmdZoneTransition`, add the `zone_transfers` table migration, modify `SvZoneTransitionMsg` to include `transfer_token` and `dest_zone_address`. **Estimated effort: 10–15 days.** Medium regression risk.

### Sessions must move to Redis with lease-based expiry

`activeAccountSessions_` as an in-memory `unordered_map<account_id, clientId>` cannot survive multi-zone. The proven solution is **Redis with TTL-based session leases and Sentinel failover**. Each session is a Redis key `session:{account_id}` with a JSON value containing `client_id`, `zone_id`, `session_token`, and `last_heartbeat`, set with a 30-second TTL.

Dual login prevention uses `SET session:{account_id} ... NX PX 30000` (set-if-not-exists with TTL) — atomic and race-free. Zone transitions update the session atomically via a Lua script: `SET session:{account_id} {"zone_id":"transitioning"} XX PX 30000`. Zone B claims with a Lua script that checks the current zone matches the expected source. Crashed zone processes auto-expire within 30 seconds via TTL — no manual cleanup needed.

**Redlock is overkill for game sessions.** Martin Kleppmann's analysis confirms that for efficiency-based coordination (not safety-critical mutual exclusion), a single Redis master with Sentinel failover suffices. Redis Cluster is also unnecessary at FateMMO's scale — 10K sessions is trivial for a single Redis instance.

The `DistributedSessionManager` class wraps Redis operations: `tryClaimSession()`, `renewLease()` (called every 100 ticks = 5s at 20 Hz), `releaseSession()`, and `transferSession()`. **Estimated effort: 8–12 days.** New dependency: `hiredis` or `redis-plus-plus` C++ client. This is the **P0 prerequisite** for all other multi-zone work.

### Instanced content uses pre-warmed process pools

For `GauntletManager` and instanced PvP/PvE, the Path of Exile model is the gold standard. GGG's "prespawner" pattern **pre-loads game data into a process, then `fork()`s** — the child inherits all pre-loaded data via copy-on-write, then initializes the specific instance in ~200ms. Each instance uses only 5–20MB of unique memory, allowing ~500 instances per machine.

For FateMMO, use the same `ServerApp` binary with a `--mode=instance` flag. A `GauntletOrchestrator` service maintains a pool of pre-warmed instance processes. When `GauntletManager` needs a match, it claims a warm instance, sends configuration (player list, difficulty, timer), and the instance runs independently. On completion, it reports results, saves rewards to DB, and returns to the pool. **Estimated effort: 12–18 days.**

### Zone boundaries should use hard-cut transitions, not seamless meshing

For entity replication at zone edges, the research is unambiguous: **every studio that attempted general-purpose seamless zone meshing (SpatialOS, Star Citizen) struggled for years or failed.** SpatialOS (Improbable) is the cautionary tale — developers reported "almost unusable" tooling, poor performance from "clunky architecture with lots of memory copying," and $81M+ operating losses. Every game shipped on SpatialOS either shut down or pivoted away.

Albion Online uses hard-cut transitions with loading screens and is hugely successful. For a 2D MMO, this is the correct approach. Zone boundaries are explicit map edges; walking to the edge triggers `CmdZoneTransition`. No cross-zone entity visibility needed.

If seamless feel is later desired, an **optional overlap region** (512px wide, ~1.5× the AOI exit radius of 384px) can provide cosmetic awareness: Zone B creates read-only `ProxyEntity` instances for entities near the border, receiving position updates from Zone A via Redis Pub/Sub. These are visual-only — no collision, no interaction. This is Phase 2 work at **15–20 days** with high regression risk.

### Multi-zone migration roadmap

| Priority | Task | Days | Risk | Prerequisite |
|----------|------|------|------|-------------|
| P0 | Redis session management | 8–12 | Low-Med | None |
| P1 | Zone transition handoff protocol | 10–15 | Medium | P0 |
| P2 | Multi-zone process support | 15–25 | High | P1 |
| P3 | Cross-zone shared services | 20–30 | Medium | P2 |
| P4 | Gauntlet instancing | 12–18 | Medium | P2 |
| P5 | Border entity replication | 15–20 | High | P2 + P3 |

---

## TOPIC 2: Network packet fuzzing for the custom game protocol

### libFuzzer is the right tool for ByteReader-based parsers

Comparing AFL++, libFuzzer, and Honggfuzz for FateMMO's use case, **libFuzzer wins decisively**. The 30+ `read(ByteReader&)` methods are 10–50 line pure functions — ideal in-process fuzzing targets that can execute **millions of iterations per second** with zero fork overhead. CMake integration is trivial (`-fsanitize=fuzzer,address`), GitHub Actions integration is turnkey via ClusterFuzzLite, and the `LLVMFuzzerTestOneInput` harness format is universally compatible — the same harnesses run on AFL++ (via `aflpp_driver`) and Honggfuzz if needed.

LLVM's `FuzzedDataProvider.h` maps perfectly to ByteReader's `readFloat`/`readString`/`readEnum` pattern for structured input generation. For MSVC builds where libFuzzer isn't available, harnesses double as regression tests by replaying the crash corpus through doctest.

One caveat: libFuzzer's original authors have shifted to Centipede, but libFuzzer remains fully maintained for bug fixes. If FateMMO outgrows it, `libafl_libfuzzer` is a drop-in replacement with identical harness compatibility.

### Two-tier harness architecture covers both depth and breadth

The harness strategy uses two tiers. **Tier 1: individual handler harnesses** (one per `PacketType`'s `read()` method, ~30 total) test each deserializer in isolation with maximum throughput. **Tier 2: a dispatch harness** exercises the full `onPacketReceived` path, using the first byte to select PacketType and remaining bytes as payload. Research from OSS-Fuzz patterns confirms that splitting into per-functionality harnesses is superior because "mixing it would just confuse the fuzzer" — the corpus for `CmdMove` would pollute `CmdChat`'s coverage.

Since the `read(ByteReader&)` methods are already essentially pure functions, no `ServerApp` stub is needed for Tier 1. The CMake setup adds a `FATEMMO_FUZZ` option:

```cmake
# fuzz/CMakeLists.txt
option(FATEMMO_FUZZ "Build fuzz targets" OFF)
if(FATEMMO_FUZZ)
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined)
    add_link_options(-fsanitize=address,undefined)

    add_library(fatemmo_fuzzable STATIC
        ../src/ByteReader.cpp ../src/packets/CmdMove.cpp
        ../src/packets/CmdAction.cpp ../src/packets/CmdChat.cpp)
    target_compile_definitions(fatemmo_fuzzable PUBLIC
        FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION)

    function(add_fuzz_target name source)
        add_executable(fuzz_${name} ${source})
        target_link_libraries(fuzz_${name} PRIVATE fatemmo_fuzzable)
        target_link_options(fuzz_${name} PRIVATE -fsanitize=fuzzer)
    endfunction()

    add_fuzz_target(cmd_move fuzz_cmd_move.cpp)
    add_fuzz_target(cmd_chat fuzz_cmd_chat.cpp)
    add_fuzz_target(dispatch fuzz_dispatch.cpp)
endif()
```

A representative individual harness for `CmdMove`:

```cpp
// fuzz/fuzz_cmd_move.cpp
#include "ByteReader.h"
#include "packets/CmdMove.h"
#include <cassert>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 1184) return -1;  // max payload = 1200 - 16 header
    ByteReader reader(data, size);
    CmdMove cmd;
    cmd.read(reader);
    if (!reader.hasOverflow()) {
        assert(std::isfinite(cmd.x) && std::isfinite(cmd.y));
    }
    return 0;
}
```

A dictionary file (`fatemmo.dict`) with protocol magic bytes, boundary integer/float values, and packet type IDs dramatically improves fuzzer efficiency for the binary format.

### The overflow-flag zero-fill pattern is the highest-priority edge case

ByteReader's overflow behavior — setting a flag and returning zero-filled data — creates a subtle class of bugs where **truncated packets produce valid-looking but semantically wrong data**. If a `CmdMove` packet is truncated after the X coordinate, the Y coordinate reads as `0.0f` and the overflow flag is set. If any handler fails to check `hasOverflow()`, the player teleports to `(valid_x, 0.0)`.

Three specific patterns need fuzzer attention. First, inputs exactly 1 byte short of each field boundary, exercising the partial-read-then-zero-fill path. Second, `readString` returning an empty string on overflow — indistinguishable from a legitimate empty string, potentially matching a default case in command parsing. Third, `readI64` assembled from two `readU32` calls where sign extension on the OR operation corrupts the result if the low word is cast to `int64_t` before combining:

```cpp
// CORRECT pattern for readI64:
int64_t readI64() {
    uint32_t low = readU32();
    uint32_t high = readU32();
    return static_cast<int64_t>(
        static_cast<uint64_t>(high) << 32 | static_cast<uint64_t>(low));
}
```

**Recommendation: add `[[nodiscard]]` to `ByteReader::hasOverflow()`** and consider making `read()` return `bool` or `std::optional` so overflow cannot be silently ignored.

### Transport and application layers need separate harnesses

The packet-level wire format requires its own harness set, separate from application message parsing. A `PacketHeader` harness fuzzes protocolId validation (anything other than `0xFA7E`), impossible `payloadSize` values, invalid `Channel` enum values, and corrupt sequence numbers. A `ClientRateLimiter` harness interprets fuzz data as sequences of `(clientId, timestamp, packetSize)` tuples to stress the token-bucket array and escalation logic.

For the transport layer, **direct in-process harnesses are strongly preferred over socket-based desocketing** (Preeny/libdesock). Since FateMMO owns the source code and uses ByteReader/ByteWriter, bypassing the network stack entirely yields millions of executions per second instead of the ~1,200 exec/s that AFLNet achieves with actual network I/O. If integration testing of the real socket path is needed, add a `#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION` code path that reads raw bytes from a file instead of `recvfrom()`.

### CI integration uses ClusterFuzzLite with 10-minute PR gates

Google's ClusterFuzzLite provides turnkey GitHub Actions integration for coverage-guided fuzzing. The setup requires three files: `.clusterfuzzlite/project.yaml` (language: c++), a `Dockerfile` (base image `gcr.io/oss-fuzz-base/base-builder:v1`), and a `build.sh` that runs CMake with `-DFATEMMO_FUZZ=ON`.

The workflow configuration runs **10-minute time-boxed fuzzing on every PR** (catching regressions before merge) and **4-hour nightly batch fuzzing** (building corpus over time). Crash artifacts are uploaded as GitHub Actions artifacts with 30-day retention, and SARIF output enables inline annotations on PRs via GitHub Code Scanning.

```yaml
# .github/workflows/cflite_pr.yml
name: FateMMO Fuzz (PR)
on: [pull_request]
jobs:
  PR-Fuzzing:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        sanitizer: [address, undefined]
    steps:
      - uses: google/clusterfuzzlite/actions/build_fuzzers@v1
        with: { language: c++, sanitizer: "${{ matrix.sanitizer }}" }
      - uses: google/clusterfuzzlite/actions/run_fuzzers@v1
        with:
          fuzz-seconds: 600
          mode: code-change
          sanitizer: "${{ matrix.sanitizer }}"
```

**Seed corpus from recorded game sessions** is critical for initial coverage. Record actual gameplay as raw packet dumps, extract individual packets, and place them in `fuzz/corpus/<target_name>/`. A daily corpus pruning workflow removes redundant inputs.

### Stateful fuzzing for Gauntlet and trades uses SGFuzz-style enum tracking

Standard libFuzzer is stateless — one input, one execution. For `GauntletManager`'s state machine (Signup→Active→Tiebreaker→Completed), **SGFuzz** (USENIX Security 2022) is the best fit. It extends libFuzzer to automatically detect enum-type state variables and build a State Transition Tree for additional coverage signal, achieving **34× more state transitions than baseline libFuzzer** and finding 12 zero-day bugs in 23 hours.

The implementation uses `FuzzedDataProvider` to interpret fuzz input as a sequence of game actions:

```cpp
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    FuzzedDataProvider fdp(data, size);
    GauntletManager manager;
    int n = fdp.ConsumeIntegralInRange<int>(1, 50);
    for (int i = 0; i < n; i++) {
        switch (fdp.ConsumeIntegral<uint8_t>() % 8) {
            case 0: manager.signup(fdp.ConsumeIntegral<uint32_t>()); break;
            case 1: manager.startGauntlet(); break;
            case 2: manager.submitScore(fdp.ConsumeIntegral<uint32_t>(),
                     fdp.ConsumeIntegral<int32_t>()); break;
            case 3: manager.triggerTiebreaker(); break;
            // ... remaining actions
        }
    }
    return 0;
}
```

For the trade protocol, the harness simulates two players with interleaved actions and asserts invariants after each sequence: **no item duplication** (same item ID in both inventories) and **gold conservation** (total gold before == total gold after). This catches the double-spend and add-after-confirm exploits that are common in game trade systems.

### Fuzzing implementation roadmap

| Week | Task | Output |
|------|------|--------|
| 1 | CMake setup, first 6 harnesses (header, dispatch, move, chat, action, rate limiter), seed corpus, dictionary | Working fuzz pipeline |
| 2 | ClusterFuzzLite: Dockerfile, build.sh, PR + nightly workflows | CI-integrated fuzzing |
| 3 | Gauntlet state machine harness + SGFuzz instrumentation, trade invariant harness | Stateful fuzzing |
| 4 | Corpus pruning, coverage measurement, expand harnesses for remaining ~24 message types | Full protocol coverage |

**Total: ~12–18 hours of setup, then ongoing maintenance of ~1 hour/week.** New dependencies: none (libFuzzer ships with Clang-17, ClusterFuzzLite is GitHub Actions only).

---

## TOPIC 3: Connection pool optimization under game server workloads

### Fibers make async DB calls feel synchronous — migrate in four phases

The core problem is that all 12 repositories use `gameDbConn_` (a single synchronous connection) on the game thread, while `DbPool` and `DbDispatcher` exist but are only used by `savePlayerToDBAsync()`. The solution leverages FateMMO's existing fiber-based `JobSystem` to make DB calls non-blocking without rewriting calling code.

The pattern is **dispatch-and-yield**: the fiber dispatches a DB job to `DbDispatcher`, yields control (allowing the game tick to continue processing other work), and resumes when the result arrives. From the caller's perspective, it looks synchronous:

```cpp
// Before (blocks game thread for 2-10ms):
auto player = playerRepo.loadPlayer(id);
initEntity(player);

// After (fiber yields, game tick continues):
auto player = co_await dbDispatcher.dispatch([&](pqxx::connection& conn) {
    return playerRepo.loadPlayer(conn, id);
});
initEntity(player);
```

**Phase 1** (lowest risk, highest immediate ROI): Move all periodic maintenance — market/bounty expiry (60s), trade cleanup (30s) — to async fire-and-forget via `DbDispatcher`. These are write-only operations with no game-thread dependency on the result. Zero tick impact, zero regression risk.

**Phase 2**: Move save operations to batched async (extends existing `savePlayerToDBAsync()` pattern to all save operations).

**Phase 3**: Implement fiber-awaitable DB dispatch for read operations. The login sequence (6 repository loads) becomes parallel: dispatch all 6 simultaneously, fiber awaits `when_all()`, completing in `max(individual_load_times)` instead of their sum — **reducing login DB time from 12–30ms to 3–8ms**.

**Phase 4**: Eliminate `gameDbConn_` entirely. All repos use `DbPool` through `DbDispatcher`.

Operations that **must stay synchronous** (or fiber-await on the game thread): trade execution (atomicity between two inventories in a single transaction), player login (6 repo loads must complete before entity creation), and any read-modify-write on shared state. Operations that **tolerate latency**: all saves, quest/bounty logging, social updates, mob respawn state, chat history persistence.

### Your pool max of 50 is too high — target 10–15 connections

The PostgreSQL pool sizing formula, validated by HikariCP benchmarks and the PostgreSQL wiki, is **`connections = (core_count × 2) + effective_spindle_count`**. For a 4-core SSD database server: (4 × 2) + 1 = **9 connections**. For 8-core: **17 connections**. A famous Oracle performance demo showed that reducing a pool from 2,048 to ~10 connections improved response time from 100ms to 2ms — beyond CPU saturation, connections cause context switching and lock contention.

| Parameter | Current | Recommended | Rationale |
|-----------|---------|-------------|-----------|
| min_connections | 5 | 5 | Warm pool for steady state |
| max_connections | 50 | 10–15 | Match DB server core formula |
| checkout_timeout | — | 10ms | Fail fast within tick budget |
| idle_timeout | — | 60s | Reclaim unused connections |
| max_lifetime | — | 25min | Prevent stale connections |

Connection creation costs **50–200ms** with SSL (TCP handshake + TLS negotiation + PostgreSQL fork + auth), which would blow the 50ms tick budget. **Pre-warm the pool at startup and never create connections during gameplay.** When all connections are checked out, use a bounded queue with 10ms timeout — fail fast for fire-and-forget saves, queue with short timeout for reads.

**PgBouncer in transaction mode** is recommended as a future addition between `DbPool` and PostgreSQL. It allows 10–15 server connections to serve hundreds of logical connections. PgBouncer 1.21+ supports prepared statements in transaction mode via `max_prepared_statements = 100`, adding <0.1ms overhead.

### Batch saves using UNNEST pattern cut write overhead by 10×

With 100 players on staggered 300-second saves, worst case is ~20 saves/second, each touching 3–8 tables. The current approach of individual `INSERT`/`UPDATE` per player per table is wasteful. The **UNNEST + ON CONFLICT pattern** batches multiple player saves into a single parameterized query:

```cpp
void PlayerRepository::batchSave(pqxx::connection& conn,
                                  const std::vector<PlayerSaveData>& players) {
    pqxx::work txn(conn);
    // Build column arrays from players vector
    std::vector<int64_t> ids; std::vector<float> xs, ys;
    std::vector<int> healths, manas;
    for (auto& p : players) { /* populate arrays */ }

    txn.exec_params(
        "INSERT INTO players (id, x_pos, y_pos, health, mana) "
        "SELECT * FROM UNNEST($1::bigint[], $2::float4[], $3::float4[], "
        "$4::int[], $5::int[]) "
        "ON CONFLICT (id) DO UPDATE SET "
        "x_pos=EXCLUDED.x_pos, y_pos=EXCLUDED.y_pos, "
        "health=EXCLUDED.health, mana=EXCLUDED.mana",
        ids, xs, ys, healths, manas);
    txn.commit();
}
```

UNNEST uses a fixed number of parameters (one array per column) regardless of batch size, avoiding PostgreSQL's 32,767 parameter limit. It supports `ON CONFLICT` for upsert semantics, which `COPY` cannot do. For batch sizes ≤1,000 rows, **prepared UNNEST is competitive with or faster than COPY** (Tiger Data benchmarks).

**Write coalescing via dirty flags** is essential: track per-component dirty bits (`position`, `inventory`, `quests`, `stats`), and only save changed components. In a typical MMO, only 30–50% of players have changes each save cycle, and most changes are position-only. This reduces write volume by **60–80%**. Group dirty players by which tables need updating — position-only changes become a single batch UPDATE.

Expected performance: single player save drops from **5–15ms to 0.3–0.8ms per player** when batched in groups of 10.

### Prepared statements in libpqxx 7.9.2 deliver 20–50% speedup on OLTP queries

In libpqxx 7.9.2, prepared statements use `cx.prepare("stmt_name", "SQL with $1, $2...")` at the connection level and `tx.exec_prepared("stmt_name", arg1, arg2)` within transactions. Statements persist across transactions on the same connection until the connection closes. The newer `tx.exec(pqxx::prepped{"name"}, args...)` unified API was introduced in 7.9.3 — for 7.9.2, stick with `exec_prepared()`.

The critical challenge with connection pooling is that **prepared statements are per-connection**. Each connection in `DbPool` must independently prepare all statements. The recommended pattern is a `PreparedStatementRegistry` — a static registry of `{name, SQL}` pairs populated by each repository at startup. `DbPool` calls `registry.prepareAll(cx)` when creating each connection:

```cpp
class PreparedStatementRegistry {
    static inline std::vector<std::pair<std::string,std::string>> stmts_;
public:
    static void add(std::string name, std::string sql) {
        stmts_.emplace_back(std::move(name), std::move(sql));
    }
    static void prepareAll(pqxx::connection& cx) {
        for (auto& [name, sql] : stmts_) cx.prepare(name, sql);
    }
};
```

**Warning**: `cx.prepare()` with a name that's already prepared throws. Prepare all statements exactly once at connection creation time, not on checkout. Benchmark data from pgbench shows simple SELECT by PK drops from **0.058ms to 0.031ms** (~47% faster) with prepared statements. For FateMMO's ~60 repository queries executed across a 20 Hz tick, the aggregate savings are significant.

### The circuit breaker needs per-connection granularity, not just global

`DbCircuitBreaker` opening after 5 failures is too coarse for a connection pool. If 1 connection goes bad, 5 rapid attempts hitting that connection trip the global breaker, blocking all 50 connections. The fix is **layered circuit breakers**: per-connection health tracking plus pool-wide aggregation.

Each `PooledConnection` wrapper tracks `consecutiveFailures` and `markedBad` state. After 3 consecutive failures on one connection, mark it bad, remove it from the pool, and create a replacement. The global circuit breaker only opens if >50% of connections are failing (indicating a systemic issue like DB down).

Validation before checkout follows the HikariCP pattern: check `PQstatus()` for socket-level liveness (fast, no round-trip), then run a full `SELECT 1` validation only if the connection hasn't been validated within 30 seconds. A background health-check fiber runs every 30–60 seconds on idle connections, evicting those that fail or exceed `maxLifetime` (25 minutes).

**For in-flight async jobs when the circuit opens**: fail-fast for normal operations (position saves — game state will be re-sent next tick), queue with 5-second TTL for critical operations (trade execution, purchases), and silently drop best-effort operations (analytics, logging). Never let the fiber job queue fill with doomed requests.

### Read replicas offload 60–80% of database reads

PostgreSQL streaming replication creates a hot standby that accepts read-only queries with sub-second lag on the same network. For FateMMO, market listing browsing, guild info, leaderboards, and chat history are all read-heavy workloads that tolerate eventual consistency — perfect candidates for a read replica.

The implementation adds a `readPool_` to `DbDispatcher` alongside the existing `writePool_`, each with its own connection string. Repository methods are annotated with read/write intent, and `DbDispatcher` routes accordingly. If the read circuit breaker opens or replication lag exceeds a threshold, reads fall back to the primary.

**Read-your-own-writes consistency** matters for market listings (player posts a listing, immediately views "My Listings"). The simplest solution is **sticky-to-primary with TTL**: after a write, stick that player's reads to the primary for 3 seconds (exceeding typical replication lag), then resume routing to the replica.

```cpp
DbPool& poolFor(PlayerId player, bool isWrite) {
    if (isWrite) {
        stickyMap_[player].stickyUntil = now() + 3s;
        return writePool_;
    }
    if (auto it = stickyMap_.find(player);
        it != stickyMap_.end() && now() < it->second.stickyUntil)
        return writePool_;
    return readPool_;
}
```

**PostgreSQL LISTEN/NOTIFY** provides cache invalidation for in-memory market/guild caches. A trigger on `market_listings` fires `pg_notify('market_changed', ...)` on INSERT/UPDATE/DELETE. A `pqxx::notification_receiver` on a dedicated connection (must stay idle — notifications don't arrive inside transactions) invalidates specific cache entries. Notifications are not durable — use TTL-based cache expiry as fallback.

### Database optimization migration roadmap

| Phase | Task | Days | Risk | ROI |
|-------|------|------|------|-----|
| 1 | Move periodic maintenance to async fire-and-forget | 2–3 | Very low | Immediate tick relief |
| 2 | Implement dirty flags + batched UNNEST saves | 5–8 | Low | 60–80% write reduction |
| 3 | Add PreparedStatementRegistry + prepare-on-create | 3–5 | Low | 20–50% query speedup |
| 4 | Per-connection health tracking + layered circuit breaker | 3–5 | Medium | Resilience |
| 5 | Fiber-awaitable DB dispatch for login loads | 5–8 | Medium | Login time 12–30ms → 3–8ms |
| 6 | Read replica + read/write splitting | 5–8 | Medium | 60–80% primary offload |
| 7 | Eliminate gameDbConn_ entirely | 3–5 | High | Clean architecture |

---

## Conclusion

The three topics interconnect in important ways. **Database pool optimization (Topic 3) should come first** — moving repos off `gameDbConn_` and implementing batched saves directly improves single-zone production quality and is prerequisite for multi-zone (each zone process needs its own healthy connection pool). **Fuzzing (Topic 2) should run in parallel** with all other work — the 12–18 hours of setup produces a CI pipeline that catches protocol bugs continuously, and is especially critical before the multi-zone transition protocol introduces new packet types and handoff sequences. **Multi-zone (Topic 1) is the capstone** — it builds on the Redis session infrastructure, the async database patterns, and the hardened protocol parsing.

The single most impactful insight from production MMOs is Albion Online's principle: **zone transitions always go through the database, even on the same server.** This eliminates entire categories of duplication exploits at the cost of one extra round-trip of latency — a cost that players, behind a loading screen, never notice. The second most impactful insight is that FateMMO's pool max of 50 is actively harmful — reducing to 10–15 connections will improve both throughput and latency, contradicting the intuition that more connections means more capacity.