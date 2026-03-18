# Phase 7: Player Persistence + Auth — Design Spec

## Overview

Add player persistence and authentication to the FateMMO C++ engine. Players create accounts, log in securely, and have their character data (stats, inventory, position) saved to PostgreSQL and restored on reconnect. This makes progress persist between sessions.

## Decisions

| Decision | Choice | Notes |
|----------|--------|-------|
| Account model | One character per account | Leave notes for future multi-character support |
| Character creation | All-in-one registration form | Notes for TWOM-style interactive class showcase later |
| Save frequency | Disconnect only | Notes for periodic auto-save; reference Unity project DB code |
| Duplicate login | Kick existing session | New login saves + disconnects old session |
| Password hashing | bcrypt | Via small C library (OpenBSD bcrypt or libbcrypt) |
| Database | PostgreSQL via libpqxx | 1:1 schema with Unity project |
| Auth transport | TLS-encrypted TCP (Approach B) | Credentials never touch UDP channel |
| Auth session | 128-bit random token, 30s expiry | TCP-only; sent in UDP Connect payload, not header |

## Section 1: Authentication Flow

### Input Validation

| Field | Min | Max | Allowed Characters |
|-------|-----|-----|--------------------|
| Username | 3 | 20 | Alphanumeric + underscore |
| Password | 8 | 128 | Any printable ASCII |
| Character name | 2 | 16 | Alphanumeric + spaces (no leading/trailing) |

### Registration

1. Client shows ImGui register screen (username, password, character name, class selection)
2. Client opens TLS-encrypted TCP connection to server (port 7778)
3. Sends `RegisterRequest` (username, password, character_name, class_name)
4. Server validates (username unique, name unique, input validation per table above)
5. Server bcrypt-hashes password, inserts into `accounts` + `characters` tables
6. Returns `AuthResponse` (success + auth token) or (failure + error reason)
7. TCP connection closes
8. Client auto-connects via UDP with auth token in Connect packet payload

### Login

1. Client shows ImGui login screen (username, password)
2. TLS TCP connection to server port 7778
3. Sends `LoginRequest` (username, password)
4. Server loads password_hash from `accounts`, verifies with bcrypt
5. If another session exists for this account: kick it (save data, disconnect existing UDP client, destroy entity)
6. Returns `AuthResponse` (success + session token + character preview: name, class, level)
7. TCP closes, client connects UDP with token

### Auth Token Lifecycle

The auth token is a 128-bit random value (`std::array<uint8_t, 16>`) used only to bridge the TLS TCP auth and the UDP game connection. It is distinct from the existing 32-bit UDP session token in the packet header (which remains unchanged).

- Generated server-side on successful auth, stored in pending session map
- Client sends it in the **payload** of the UDP `Connect` packet (not the header)
- Server validates, consumes (removes from map), and proceeds with normal UDP handshake
- Expires after 30 seconds if UDP connection not established
- The existing 32-bit `PacketHeader.sessionToken` continues to work as before (assigned by server on `ConnectAccept`)

## Section 2: Database Layer Architecture

### Connection Management

- **Two separate DB connections** (libpqxx connections are not thread-safe):
  - **Auth thread connection** — used by `AuthServer` for password verification and character lookups
  - **Game thread connection** — used by repositories for save/load during gameplay
- Connection string from environment variable (`DATABASE_URL`) or config file
- Reconnect logic on connection loss (retry with backoff)

### Repository Pattern (Mirroring Unity Project)

| Repository | Responsibility | Tables |
|------------|---------------|--------|
| `AccountRepository` | Create account, find by username, verify password, update last_login, ban/unban | `accounts` |
| `CharacterRepository` | Create default character, load character, save character, save position | `characters` |
| `InventoryRepository` | Load inventory, save inventory slots, load equipment | `character_inventory` |

Each repository takes a `pqxx::connection&` and exposes methods with parameterized queries. No raw SQL in game code.

### Schema

- Exact 1:1 with Unity PostgreSQL tables
- **Before writing any migration or repository code**, query the live Unity DB for exact column names, types, defaults, and constraints
- Phase 7 tables: `accounts`, `characters`, `character_inventory`
- All other tables (quests, skills, guilds, vendors, etc.) deferred to later phases

### Server Integration Points

- `ServerApp::init()` — open both DB connections, initialize repositories, start AuthServer thread
- `onClientConnected()` (modified) — validates auth token from Connect payload, looks up pending session to get account_id + character_id, loads full character from DB via game-thread connection, creates player entity with DB data (character_id becomes the PersistentId for stable cross-session identity)
- `onClientDisconnected()` (modified) — saves character state to DB before destroying entity; on save failure, logs error and retries once before giving up (data loss is logged)

### Threading Model

- `AuthServer` runs on its own thread, handles TLS TCP connections and DB queries (password verify, account create) using the auth DB connection
- On successful auth, `AuthServer` pushes a `PendingSession` into a thread-safe queue
- Game thread consumes the queue during `tick()` — no DB queries block the game loop for auth
- Game thread uses its own DB connection for character load (on connect) and save (on disconnect)

## Section 3: Networking & Protocol Changes

### New TLS TCP Auth Server

- `AuthServer` class — listens on separate port (default 7778) with OpenSSL TLS
- Runs on its own thread with its own DB connection
- Handles two message types only: `RegisterRequest` and `LoginRequest`
- Returns `AuthResponse` with auth token on success, error string on failure
- On success, pushes `PendingSession` to thread-safe queue for game thread to consume
- Connections are short-lived (auth only, then close)

### Auth Message Types (TCP Only)

```
RegisterRequest:  username, password, character_name, class_name
LoginRequest:     username, password
AuthResponse:     success (bool), auth_token (16 bytes), error_reason (string)
                  + on login success: character_name, class_name, level (for UI display during transition)
```

### Modified UDP Handshake

- Current: Client sends `Connect` with zero payload -> server sends `ConnectAccept` with clientId + sessionToken
- New: Client sends `Connect` with **16 bytes of auth token as payload** (written via `writeBytes(token.data(), 16)`)
- The `PacketHeader` is unchanged — the existing 32-bit `sessionToken` field remains as-is
- Server reads 16-byte auth token from Connect payload, validates against pending session map
- If valid: proceeds with `ConnectAccept` (assigns 32-bit session token as before) + loads character from DB
- If invalid/expired: sends `ConnectReject` (packet type 0x81 — exists in protocol but needs handling on both sides)
- Client must handle `ConnectReject` to transition back to LoginScreen with error message

### Pending Session Map (In-Memory, Game Thread)

```cpp
using AuthToken = std::array<uint8_t, 16>;

struct PendingSession {
    int account_id;
    std::string character_id;  // VARCHAR(64) matching Unity DB
    double created_at;         // steady_clock, not float (avoids precision loss after hours)
    double expires_at;         // 30 seconds after creation
};

// Hash for AuthToken to use as unordered_map key
struct AuthTokenHash { ... };

std::unordered_map<AuthToken, PendingSession, AuthTokenHash> pendingSessions_;
```

- AuthServer pushes to thread-safe queue on successful auth
- Game thread consumes queue during `tick()`, adds to pendingSessions_
- Consumed (and removed) when matching UDP Connect arrives
- Expired entries cleaned up each server tick

### Duplicate Login Handling

- On successful auth, check if account_id is already in an active UDP session
- If so: save that player's data, send `Disconnect` to existing client, destroy their entity
- Then proceed with new auth normally

## Section 4: Client UI & Flow

### State Machine

```
Disconnected -> LoginScreen -> Authenticating -> UDPConnecting -> InGame
                    ^  v
              RegisterScreen
```

- **Authenticating** = TLS TCP auth in progress (background thread)
- **UDPConnecting** = Auth succeeded, UDP handshake in progress with auth token

### LoginScreen (ImGui)

- Username + password fields
- "Login" button -> initiates TLS auth
- "Create Account" button -> switches to RegisterScreen
- Error display area (wrong password, banned, server unreachable)

### RegisterScreen (ImGui)

- Username, password, confirm password fields
- Character name field
- Class selection (Warrior / Archer / Mage radio buttons)
- "Register" button -> initiates TLS auth + character creation
- "Back to Login" button

> **Future:** Replace radio buttons with TWOM-style interactive class showcase (clickable class models with stat previews, animations, skill previews)

### Authenticating State

- Shows "Connecting..." spinner/text
- TLS TCP auth happens on a background thread (no UI freeze)
- On success: auto-transitions to UDP connect with session token
- On failure: returns to LoginScreen with error message

### InGame Transition

- Once UDP `ConnectAccept` received and character data loaded from DB
- Player spawns at saved position/scene from database (not hardcoded)
- Stats, inventory, equipment restored from DB

### Disconnect Flow

- Player clicks disconnect or connection times out
- Server saves character state to DB (on failure: log error, retry once)
- Client returns to LoginScreen (ready to log in again)

## Section 5: Save/Load Data Mapping

### Conceptual Mapping (Exact Columns TBD From DB Queries)

| Engine Component | DB Table |
|------------------|----------|
| `CharacterStatsComponent` (hp, mp, xp, level, fury, death state) | `characters` |
| `CharacterStatsComponent` (base stats) | `characters` |
| `CharacterStatsComponent` (gold, honor, pvp stats) | `characters` |
| `Transform.position` + current scene | `characters` |
| `InventoryComponent` (item slots, equipment) | `character_inventory` |

**Before writing repository code:** Run `\d characters`, `\d character_inventory`, `\d accounts` on the live Unity DB to get exact column names, types, and defaults. Schema must be 1:1.

### Runtime-Only State (Not Persisted)

- `CombatControllerComponent` — reset on spawn
- `StatusEffectComponent` — buffs/debuffs cleared
- `CrowdControlComponent` — cleared
- `TargetingComponent` — no target on login
- `PartyComponent`, `TradeComponent` — session-only state

### Future Notes

- Periodic auto-save (every 60s) using same save path
- Reference Unity project's `CharacterRepository.SaveCharacter()` and `InventoryRepository` implementations when wiring up the C++ repositories
- User has existing Unity implementation code to reference for exact query patterns

## Section 6: Dependencies & Build Integration

### New Dependencies

| Dependency | Purpose | Linked To |
|------------|---------|-----------|
| **libpqxx** | C++ PostgreSQL client (wraps libpq) | FateServer |
| **OpenSSL** | TLS for auth TCP + required by libpqxx | FateServer, FateEngine |
| **bcrypt** (small C lib) | Password hashing | FateServer |

### libpq (System Dependency)

- libpqxx requires libpq (the C PostgreSQL driver)
- On Windows, comes from PostgreSQL installation or prebuilt binaries
- Path must be configured for the build

### Build Target Changes

| Target | New Dependencies |
|--------|-----------------|
| `FateServer` | libpqxx, OpenSSL, bcrypt (DB + auth) |
| `FateEngine` (client) | OpenSSL only (TLS for auth connection) |
| `fate_engine` (static lib) | None (keeps engine clean) |

### Configuration

- DB connection string via `DATABASE_URL` env var or server config file
- Auth server port configurable (default 7778, alongside game UDP 7777)
- TLS certificate path configurable (self-signed for dev, proper cert for production)

### TLS Certificates for Dev

- Generate self-signed cert for local development
- Stored at `config/server.crt` and `config/server.key` (configurable via server config)
- Client accepts self-signed in dev mode (`FATE_DEV_TLS` compile flag disables cert verification)
- Production uses a proper certificate

## New Files (Estimated)

| File | Purpose |
|------|---------|
| `server/auth/auth_server.h/.cpp` | TLS TCP auth listener (server-only, has DB + bcrypt deps) |
| `engine/net/auth_client.h/.cpp` | TLS TCP auth client (client-side, OpenSSL only) |
| `engine/net/auth_protocol.h` | Auth message types (Register, Login, Response) — shared |
| `server/db/db_connection.h/.cpp` | PostgreSQL connection wrapper |
| `server/db/account_repository.h/.cpp` | Account CRUD |
| `server/db/character_repository.h/.cpp` | Character load/save |
| `server/db/inventory_repository.h/.cpp` | Inventory load/save |
| `game/ui/login_screen.h/.cpp` | ImGui login/register UI |

## Modified Files (Estimated)

| File | Changes |
|------|---------|
| `CMakeLists.txt` | Add libpqxx, OpenSSL, bcrypt dependencies |
| `server/server_app.h/.cpp` | Add AuthServer, DB connection, repositories, modified connect/disconnect |
| `engine/net/protocol.h` | Modified Connect packet to carry auth session token |
| `engine/net/connection.h` | Add account_id, character_id to ClientConnection |
| `game/entity_factory.h` | Add createPlayerFromDB() or modify createPlayer() to accept DB data |
| `game/game_app.cpp` | Add login screen state machine, auth client |
