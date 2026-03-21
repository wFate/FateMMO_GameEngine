# Phase 9: Engine Hardening, Mobile Platform, & Infrastructure

Session date: 2026-03-21
Test count: 573 (all passing, 0 failures) — up from 478

## What was done

Cross-referenced 9 research documents against the full engine codebase to identify gaps, then implemented 16 systems across 4 domains: combat integrity, network security, mobile platform, and engine infrastructure.

---

### Combat & PvP Integrity (4 items)

#### Two-tick death lifecycle
- Added `LifeState` enum (`Alive → Dying → Dead`) to `character_stats.h`
- `takeDamage()` now transitions to `Dying` (not straight to `Dead`), allowing on-death procs (DoT kill credit, thorns) to fire during the Dying tick
- `advanceDeathTick()` called once per server tick transitions `Dying → Dead`
- `isDead` bool kept for backward compat, synced from `lifeState`
- Dying and Dead entities reject further damage
- `respawn()` resets to `Alive`
- Replication sends actual `lifeState` value (0/1/2) instead of binary
- **Files:** `character_stats.h/cpp`, `server_app.cpp`, `replication.cpp`, `game_app.cpp`, `editor.cpp`
- **Tests:** 11 new in `test_death_lifecycle.cpp`

#### Full PvP target validation
- Replaced `canAttackPlayer()` stub (always returned true) with real validation
- New 4-parameter signature: `canAttackPlayer(attacker, target, inSameParty, inSafeZone)`
- Validation order: safe zone → party member → dead/dying target → same-faction innocents → different factions
- Same-faction players can only attack Red/Black (PK-flagged) targets
- Added `Faction faction` field to `CharacterStats` for direct access
- Server passes real party membership and PvP zone state from components
- Updated both `processAction()` and `processUseSkill()` PvP paths
- **Files:** `target_validator.h`, `server_app.cpp`, `character_stats.h`
- **Tests:** 10 new in `test_pvp_validation.cpp`

#### Inventory slot safety
- Audited `addItemToSlot()` — found it ALREADY had all 3 safety checks (bounds, trade-lock, occupancy via `isValid()`)
- Added 5 regression tests confirming the existing behavior
- **Files:** `test_inventory_safety.cpp` (new)
- **Tests:** 5 new

#### Optimistic combat feedback
- Client now plays attack animation IMMEDIATELY on input (no waiting for server round-trip)
- Added `CombatPredictionBuffer` ring buffer (32 slots) tracking pending attacks
- Attack flash: brief sprite tint on hit (warm yellow for melee, cool blue for spells), decays over 0.12s
- `triggerAttackFlash(bool isSpell)` exposed so skill bar can trigger the same effect
- Server response reconciles: resolves oldest prediction, shows final damage numbers
- Prediction buffer cleared on zone transition and disconnect
- **Files:** `combat_prediction.h` (new), `combat_action_system.h`, `game_app.h/cpp`

---

### Network Security (4 items)

#### AEAD packet encryption (XChaCha20-Poly1305)
- Added libsodium via vcpkg (`libsodium:x64-windows`)
- Created `PacketCrypto` class wrapping AEAD encrypt/decrypt
- Server generates session keys after `ConnectAccept`, sends via `KeyExchange` (0x82) packet
- Client stores keys, encrypts all non-system outgoing packets
- Both sides decrypt incoming non-system packets; tampered packets silently dropped
- System packets (Connect, Disconnect, Heartbeat, ConnectAccept, ConnectReject, KeyExchange) remain unencrypted
- Sequence number used as 24-byte XChaCha20 nonce (zero-padded)
- Separate tx/rx keys prevent reflection attacks
- Compiles and runs in plaintext mode if libsodium unavailable (`FATE_HAS_SODIUM` guard)
- **Files:** `packet_crypto.h/cpp` (new), `packet.h`, `connection.h`, `net_client.h/cpp`, `net_server.cpp`, `server_app.cpp`, `CMakeLists.txt`
- **Tests:** 11 new in `test_packet_crypto.cpp`

#### IPv6 dual-stack socket
- Refactored `NetAddress` from `uint32_t ip + uint16_t port` to `sockaddr_storage + addrLen`
- Socket creation: tries `AF_INET6` with `IPV6_V6ONLY=0` (dual-stack), falls back to `AF_INET`
- `resolve()` uses `AF_UNSPEC` via `getaddrinfo` — accepts both IPv4 and IPv6
- `sendTo()` auto-converts IPv4 `sockaddr_in` to IPv4-mapped IPv6 when socket is IPv6
- `recvFrom()` normalizes IPv4-mapped IPv6 back to plain IPv4 for clean comparison
- Added `toString()` for logging, `makeIPv4()` for test construction
- Updated all test files that constructed `NetAddress{ip, port}` to use `makeIPv4()`
- **iOS App Store compliance:** mandatory since June 2016
- **Files:** `socket.h`, `socket_win32.cpp`, `socket_posix.cpp`, `net_server.cpp`, 7 test files
- **Tests:** 7 new IPv6 tests

#### One-time nonces for economic actions
- Created `NonceManager` class in `server/nonce_manager.h`
- `issue(clientId, gameTime)` generates random `uint64_t` nonce
- `consume(clientId, nonce, gameTime)` validates: single-use, correct client, not expired (60s max)
- `expireAll()` called in `tickMaintenance()` to clean stale nonces
- `removeClient()` called on disconnect
- Infrastructure ready — protocol message fields for trade/market nonce echo are the next step
- **Files:** `nonce_manager.h` (new), `server_app.h/cpp`
- **Tests:** 8 new in `test_economic_nonces.cpp`

#### Auto-reconnect state machine
- Added `ReconnectPhase` enum (`None → Reconnecting → Failed`) to `NetClient`
- Heartbeat timeout (5s without server packets) triggers auto-reconnect
- Exponential backoff: 1s → 2s → 4s → 8s → ... → 30s max
- 60s total timeout before giving up (fires `onDisconnected`)
- Reconnects using stored `authToken_`, `lastHost_`, `lastPort_`
- Anonymous connections (no auth token) skip auto-reconnect
- `ConnectAccept` during reconnect clears reconnect state
- `disconnect()` resets all reconnect state
- **Files:** `net_client.h/cpp`
- **Tests:** 4 new reconnect state tests

---

### Mobile Platform (5 items)

#### GLES 3.0 shader preamble injection
- `loadFromSource()` and `loadFromFile()` now prepend platform-specific preamble:
  - Desktop: `#version 330 core`
  - GLES vertex: `#version 300 es`
  - GLES fragment: `#version 300 es` + `precision highp float;` + `precision highp sampler2D;`
- Added `hasVersionDirective()` helper to skip injection if shader already has `#version`
- Removed hardcoded `#version 330 core` from SpriteBatch embedded fallback shaders
- **Files:** `shader.cpp`, `sprite_batch.cpp`

#### SDL lifecycle events
- Already implemented in a previous session via `App::lifecycleEventFilter()` in `engine/app.h/cpp`
- Verified: handles `SDL_APP_WILLENTERBACKGROUND`, `SDL_APP_DIDENTERFOREGROUND`, `SDL_APP_LOWMEMORY`
- Virtual overrides `onEnterBackground()`, `onEnterForeground()`, `onLowMemory()`
- 4 tests already passing in `test_lifecycle.cpp`

#### Mobile memory tiers and thermal throttling
- Created `DeviceInfo` struct in `engine/platform/device_info.h/cpp`
- `getPhysicalRAM_MB()`: Windows (`GlobalMemoryStatusEx`), macOS/iOS (`sysctlbyname`), Linux/Android (`/proc/meminfo`)
- `getDeviceTier()`: Low (≤3GB, 200MB VRAM), Medium (4-6GB, 350MB), High (8GB+, 512MB)
- `getThermalState()`: 0-3 scale (stubs for iOS/Android with TODO markers for native bridge)
- `recommendedFPS()`: 60 for nominal/fair, 30 for serious/critical thermal states
- **Files:** `device_info.h/cpp` (new)
- **Tests:** 5 new in `test_device_info.cpp`

#### iOS build pipeline
- Updated `ios/Info.plist.in` with CMake variable substitution, landscape-only, fullscreen
- Simplified `ios/LaunchScreen.storyboard` to minimal black background
- Created `ios/build.sh` — accepts `[debug|release] [build|device|testflight]`
  - `build`: generates Xcode project via CMake, compiles
  - `device`: deploys to connected iPhone via `ios-deploy`
  - `testflight`: archives, exports IPA, uploads via `xcrun altool`
- Created `ios/ExportOptions.plist` for TestFlight export (team ID placeholder)
- Removed old `ios/build-ios.sh`
- **Files:** `Info.plist.in`, `LaunchScreen.storyboard`, `build.sh` (new), `ExportOptions.plist` (new)

#### Android Gradle project
- Updated `android/app/build.gradle.kts`: C++23, NDK r27, flexible page sizes, corrected assets path
- Updated `AndroidManifest.xml`: fullscreen theme, landscape, INTERNET permission, GLES 3.0 requirement
- Created `FateMMOActivity.java` extending SDLActivity (replaced old `FateActivity.java`)
- Simplified `android/app/src/main/jni/CMakeLists.txt` to thin wrapper delegating to root CMake
- Updated `gradle.properties`: 4GB JVM heap, nonTransitiveRClass
- Created `android/build.sh` convenience script
- **Files:** 9 files across `android/` directory

---

### Engine Infrastructure (3 items)

#### PhysicsFS virtual filesystem
- Added PhysicsFS via FetchContent (static build, no tests/docs)
- Created `VirtualFS` wrapper class in `engine/vfs/virtual_fs.h/cpp`
- API: `init()`, `mount()`, `readFile()`, `readText()`, `exists()`, `listDir()`, `shutdown()`
- Supports overlay mounting (later mounts take priority) for patching and mod support
- Added `FATE_SOURCE_DIR` define to test target for correct asset path resolution
- **Files:** `virtual_fs.h/cpp` (new), `CMakeLists.txt`
- **Tests:** 9 new in `test_vfs.cpp`

#### Telemetry metric collector
- Created `TelemetryCollector` class in `engine/telemetry/telemetry.h/cpp`
- `record(name, value)`: stores named float metrics with epoch timestamps
- `flushToJson()`: serializes to JSON with session ID + metric array, clears buffer
- `trySend()`: placeholder for HTTPS POST (logs payload size for now)
- **Files:** `telemetry.h/cpp` (new)
- **Tests:** 5 new in `test_telemetry.cpp`

#### Palette swap pipeline
- Created `PaletteRegistry` class in `engine/render/palette.h/cpp`
- Loads named 16-color palettes from JSON (hex color strings like `"#FF0000"`)
- Added `Color::fromHex(const std::string&)` overload to parse hex strings
- Created `assets/data/palettes.json` with 5 starter palettes:
  - `warrior_siras`, `warrior_lanos` (faction colors)
  - `leather_common`, `leather_rare`, `leather_epic` (rarity tints)
- Connects to existing shader renderType 5 and SpriteBatch `setPalette()`/`drawPaletteSwapped()`
- **Files:** `palette.h/cpp` (new), `palettes.json` (new), `types.h` (fromHex overload)
- **Tests:** 5 new in `test_palette.cpp`

---

## Verified as already complete

| Item | Evidence |
|------|----------|
| Profanity filter server wiring | `server_app.cpp:1392-1431` uses `ProfanityFilter::filterChatMessage(msg, FilterMode::Censor)` |
| system() command injection | No `system()` calls found in any engine/editor code |
| SDL lifecycle events | Implemented in `engine/app.h/cpp` with 4 passing tests |
| addItemToSlot safety | Already has bounds + trade-lock + occupancy checks |

---

## What's NOT done yet

| Gap | Reason |
|-----|--------|
| AEAD nonce wrapping | `uint16_t` sequence wraps at 65535; production should combine with session prefix |
| iOS thermal bridge | `getThermalState()` returns 0 on iOS; needs ObjC++ bridge to `NSProcessInfo.thermalState` |
| Android thermal bridge | Returns 0; needs JNI bridge to `AThermal_getCurrentThermalStatus` (API 30+) |
| Trade/market nonce protocol fields | NonceManager infrastructure ready; protocol message nonce fields are next step |
| Telemetry HTTPS POST | `trySend()` is a placeholder; needs cpp-httplib or platform-native HTTP |
| PhysicsFS asset packaging | VFS wrapper ready; actual ZIP/PAK bundling is future pipeline work |

---

## Files modified (69 files total, +2992 / -197 lines)

### New files created (19)
- `engine/net/packet_crypto.h/cpp` — AEAD encryption wrapper
- `engine/platform/device_info.h/cpp` — RAM detection, device tiers, thermal
- `engine/render/palette.h/cpp` — JSON-driven palette registry
- `engine/telemetry/telemetry.h/cpp` — metric collector
- `engine/vfs/virtual_fs.h/cpp` — PhysicsFS wrapper
- `game/combat_prediction.h` — optimistic attack prediction buffer
- `server/nonce_manager.h` — economic action replay prevention
- `ios/build.sh`, `ios/ExportOptions.plist` — iOS build pipeline
- `android/build.sh`, `android/.../FateMMOActivity.java` — Android build
- `assets/data/palettes.json` — 5 starter color palettes
- `tests/test_death_lifecycle.cpp` — 11 tests
- `tests/test_pvp_validation.cpp` — 10 tests
- `tests/test_inventory_safety.cpp` — 5 tests
- `tests/test_packet_crypto.cpp` — 11 tests
- `tests/test_economic_nonces.cpp` — 8 tests
- `tests/test_device_info.cpp` — 5 tests
- `tests/test_telemetry.cpp` — 5 tests
- `tests/test_vfs.cpp` — 9 tests
- `tests/test_palette.cpp` — 5 tests

### Key files modified
- `CMakeLists.txt` — libsodium + PhysicsFS deps
- `engine/net/socket.h` + `socket_win32.cpp` + `socket_posix.cpp` — IPv6 dual-stack
- `engine/net/net_client.h/cpp` — AEAD integration, auto-reconnect, key exchange handler
- `engine/net/net_server.cpp` — AEAD integration, IPv6 logging
- `engine/net/packet.h` — `KeyExchange` packet type
- `engine/net/protocol.h` — pkStatus in entity enter/update
- `engine/net/replication.cpp` — lifeState + pkStatus replication
- `engine/render/shader.cpp` — GLES preamble injection
- `engine/render/sprite_batch.cpp` — removed hardcoded #version
- `engine/core/types.h` — `Color::fromHex(string)` overload
- `server/server_app.h/cpp` — death lifecycle, PvP validation, nonces, AEAD keys, PK replication
- `server/target_validator.h` — full PvP validation rules
- `game/game_app.h/cpp` — optimistic combat, lifeState sync, prediction buffer
- `game/shared/character_stats.h/cpp` — LifeState enum, faction field, advanceDeathTick

### Test growth
- **Before:** 478 tests
- **After:** 573 tests (+95 new tests, +20%)
