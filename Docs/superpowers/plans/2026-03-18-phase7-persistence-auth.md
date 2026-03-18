# Phase 7: Player Persistence + Auth — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add TLS-authenticated login, PostgreSQL player persistence, and ImGui login/register UI so player progress persists between sessions.

**Architecture:** TLS-encrypted TCP for auth (port 7778), existing UDP for gameplay (port 7777). Auth server runs on its own thread with its own DB connection. Repositories mirror Unity project's pattern. Auth token bridges TCP auth to UDP connect via Connect packet payload.

**Tech Stack:** C++20, OpenSSL (TLS), libpqxx (PostgreSQL), bcrypt (password hashing), ImGui (login UI), doctest (tests)

**Spec:** `docs/superpowers/specs/2026-03-17-phase7-persistence-auth-design.md`

---

## File Structure

### New Files

| File | Responsibility |
|------|---------------|
| `engine/net/auth_protocol.h` | Auth message types (RegisterRequest, LoginRequest, AuthResponse) + input validation + AuthToken type |
| `engine/net/auth_client.h` | TLS TCP auth client header |
| `engine/net/auth_client.cpp` | TLS TCP client — connect, send register/login, receive response |
| `server/auth/auth_server.h` | TLS TCP auth server header |
| `server/auth/auth_server.cpp` | TLS TCP listener — accept connections, verify credentials, push pending sessions |
| `server/db/db_connection.h` | PostgreSQL connection wrapper header |
| `server/db/db_connection.cpp` | Connection open/close/reconnect logic |
| `server/db/account_repository.h` | Account CRUD header |
| `server/db/account_repository.cpp` | Create account, find by username, verify bcrypt password |
| `server/db/character_repository.h` | Character load/save header |
| `server/db/character_repository.cpp` | Create default character, load from DB, save to DB |
| `server/db/inventory_repository.h` | Inventory load/save header |
| `server/db/inventory_repository.cpp` | Load inventory slots, save inventory slots |
| `game/ui/login_screen.h` | Login/register UI header |
| `game/ui/login_screen.cpp` | ImGui login screen, register screen, state machine |
| `tests/test_auth_protocol.cpp` | Tests for auth message serialization + input validation |
| `config/` | Directory for TLS certs (server.crt, server.key) |

### Modified Files

| File | Changes |
|------|---------|
| `CMakeLists.txt` | Add OpenSSL, libpqxx, bcrypt dependencies; link to correct targets |
| `engine/net/connection.h:13-23` | Add `account_id`, `character_id` fields to `ClientConnection` |
| `engine/net/net_server.h:48` | Change `handleConnect` signature to pass payload reader |
| `engine/net/net_server.cpp:37-39,73-121` | Pass payload to `handleConnect`; read auth token from payload; send ConnectReject |
| `engine/net/net_client.h:14,36-45` | Add `connectWithToken()` method; add `authToken_` field; add `onConnectRejected` callback |
| `engine/net/net_client.cpp:6-38,108-117` | Send auth token in Connect payload; handle ConnectReject packet |
| `server/server_app.h:1-46` | Add AuthServer, DB connections, repositories, pending session map |
| `server/server_app.cpp:24-53,112-155` | Init DB + AuthServer; consume pending sessions in tick; load from DB on connect; save to DB on disconnect |
| `game/entity_factory.h:22-154` | Add `createPlayerFromDB()` that accepts DB-loaded stats instead of defaults |
| `game/game_app.h:23-61` | Add login screen, auth client, connection state enum |
| `game/game_app.cpp` | Add login state machine, wire auth client to login screen |

---

## Task 1: Add Dependencies to CMake

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add OpenSSL find_package**

Add after the Tracy FetchContent block (after line 101):

```cmake
# OpenSSL (TLS for auth)
find_package(OpenSSL REQUIRED)
```

- [ ] **Step 2: Add libpqxx via FetchContent**

```cmake
# libpqxx (C++ PostgreSQL client)
FetchContent_Declare(
    libpqxx
    GIT_REPOSITORY https://github.com/jtv/libpqxx.git
    GIT_TAG        7.9.2
    GIT_SHALLOW    TRUE
)
set(SKIP_BUILD_TEST ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libpqxx)
```

- [ ] **Step 3: Add bcrypt source**

Use the OpenBSD bcrypt implementation. Clone or download from a maintained fork (e.g., https://github.com/trusch/libbcrypt) which provides `bcrypt.c`, `blowfish.c`, and `bcrypt.h`. Copy these three files into `third_party/bcrypt/`:

```bash
mkdir -p third_party/bcrypt
# Download the specific files needed:
# bcrypt.c, blowfish.c, bcrypt.h (and any supporting headers like crypt.h/ow-crypt.h)
```

Then add as a static library in CMakeLists.txt:

```cmake
# bcrypt (password hashing — OpenBSD implementation)
file(GLOB BCRYPT_SOURCES third_party/bcrypt/*.c)
add_library(bcrypt_lib STATIC ${BCRYPT_SOURCES})
target_include_directories(bcrypt_lib PUBLIC third_party/bcrypt)
if(MSVC)
    target_compile_options(bcrypt_lib PRIVATE /wd4100 /wd4244 /wd4267)
endif()
```

Verify the library compiles standalone before proceeding.

- [ ] **Step 3.5: Configure libpq path for Windows**

libpqxx requires libpq (the C PostgreSQL driver). On Windows, it comes from a PostgreSQL installation. Add before the libpqxx FetchContent block:

```cmake
# libpq (required by libpqxx) — find via PostgreSQL installation
find_package(PostgreSQL REQUIRED)
```

If PostgreSQL is not in the default search path, set `PostgreSQL_ROOT` in CMakePresets or via env var to the PostgreSQL installation directory (e.g., `C:/Program Files/PostgreSQL/16`).

- [ ] **Step 4: Link dependencies to targets**

Add OpenSSL to `fate_engine` (since `auth_client.cpp` is in `engine/net/` and compiled into the engine static lib):

```cmake
target_link_libraries(fate_engine PUBLIC
    SDL2::SDL2-static
    nlohmann_json::nlohmann_json
    stb_image
    imgui_lib
    ${OPENGL_LIB}
    TracyClient
    OpenSSL::SSL OpenSSL::Crypto
)
```

Modify the FateServer target (line 196-206):

```cmake
if(SERVER_SOURCES)
    add_executable(FateServer ${SERVER_SOURCES} ${SERVER_SHARED_SOURCES})
    target_link_libraries(FateServer PRIVATE fate_engine pqxx PostgreSQL::PostgreSQL bcrypt_lib)
    target_include_directories(FateServer PRIVATE ${CMAKE_SOURCE_DIR})
    # ... existing MSVC settings ...
endif()
```

Add `FATE_DEV_TLS` compile definition for dev builds (disables TLS cert verification):

```cmake
target_compile_definitions(FateEngine PRIVATE
    FATE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    FATE_DEV_TLS
)
```

- [ ] **Step 5: Verify build compiles**

Run:
```bash
export LIB="..." INCLUDE="..."  # per memory
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_engine
```

Expected: Clean compile (no new source files yet, just dependency availability).

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt third_party/bcrypt/
git commit -m "build: add OpenSSL, libpqxx, bcrypt dependencies for Phase 7"
```

---

## Task 2: Auth Protocol & Input Validation

**Files:**
- Create: `engine/net/auth_protocol.h`
- Create: `tests/test_auth_protocol.cpp`

- [ ] **Step 1: Write auth protocol tests**

Create `tests/test_auth_protocol.cpp`:

```cpp
#include "doctest/doctest.h"
#include "engine/net/auth_protocol.h"

TEST_CASE("Auth input validation") {
    using namespace fate;

    SUBCASE("Username validation") {
        CHECK(AuthValidation::isValidUsername("player1") == true);
        CHECK(AuthValidation::isValidUsername("ab") == false);           // too short
        CHECK(AuthValidation::isValidUsername("a]b") == false);          // invalid char
        CHECK(AuthValidation::isValidUsername("valid_name") == true);
        CHECK(AuthValidation::isValidUsername("") == false);
        CHECK(AuthValidation::isValidUsername("abcdefghijklmnopqrstu") == false); // 21 chars
    }

    SUBCASE("Password validation") {
        CHECK(AuthValidation::isValidPassword("password") == true);     // exactly 8
        CHECK(AuthValidation::isValidPassword("short") == false);       // too short
        CHECK(AuthValidation::isValidPassword("longpassword123") == true);
    }

    SUBCASE("Character name validation") {
        CHECK(AuthValidation::isValidCharacterName("Hero") == true);
        CHECK(AuthValidation::isValidCharacterName("My Hero") == true); // spaces ok
        CHECK(AuthValidation::isValidCharacterName("X") == false);      // too short
        CHECK(AuthValidation::isValidCharacterName(" Hero") == false);  // leading space
        CHECK(AuthValidation::isValidCharacterName("Hero ") == false);  // trailing space
    }
}

TEST_CASE("AuthToken generation") {
    using namespace fate;
    AuthToken t1 = generateAuthToken();
    AuthToken t2 = generateAuthToken();
    CHECK(t1 != t2); // statistically guaranteed
}

TEST_CASE("Auth message serialization") {
    using namespace fate;

    SUBCASE("RegisterRequest round-trip") {
        RegisterRequest req;
        req.username = "testuser";
        req.password = "mypassword";
        req.characterName = "Hero";
        req.className = "Warrior";

        std::vector<uint8_t> buf(512);
        ByteWriter w(buf.data(), buf.size());
        req.write(w);

        ByteReader r(buf.data(), w.size());
        uint8_t type = r.readU8(); // consume the message type byte
        CHECK(type == static_cast<uint8_t>(AuthMessageType::RegisterRequest));
        auto decoded = RegisterRequest::read(r);
        CHECK(decoded.username == "testuser");
        CHECK(decoded.password == "mypassword");
        CHECK(decoded.characterName == "Hero");
        CHECK(decoded.className == "Warrior");
    }

    SUBCASE("LoginRequest round-trip") {
        LoginRequest req;
        req.username = "testuser";
        req.password = "mypassword";

        std::vector<uint8_t> buf(256);
        ByteWriter w(buf.data(), buf.size());
        req.write(w);

        ByteReader r(buf.data(), w.size());
        uint8_t type = r.readU8(); // consume the message type byte
        CHECK(type == static_cast<uint8_t>(AuthMessageType::LoginRequest));
        auto decoded = LoginRequest::read(r);
        CHECK(decoded.username == "testuser");
        CHECK(decoded.password == "mypassword");
    }

    SUBCASE("AuthResponse round-trip") {
        AuthResponse resp;
        resp.success = true;
        resp.authToken = generateAuthToken();
        resp.errorReason = "";
        resp.characterName = "Hero";
        resp.className = "Warrior";
        resp.level = 42;

        std::vector<uint8_t> buf(512);
        ByteWriter w(buf.data(), buf.size());
        resp.write(w);

        ByteReader r(buf.data(), w.size());
        auto decoded = AuthResponse::read(r);
        CHECK(decoded.success == true);
        CHECK(decoded.authToken == resp.authToken);
        CHECK(decoded.characterName == "Hero");
        CHECK(decoded.className == "Warrior");
        CHECK(decoded.level == 42);
    }
}
```

**Note on serialization protocol:** `RegisterRequest::write()` and `LoginRequest::write()` both prepend a `AuthMessageType` byte. The server-side reader consumes this byte first to determine which message to parse, then calls `read()` which reads only the fields. Tests must also consume the type byte before calling `read()`. `AuthResponse` does NOT write a type byte since responses always follow a request — the caller knows what to expect.

- [ ] **Step 2: Run tests to verify they fail**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests
```

Expected: Compile error — `auth_protocol.h` doesn't exist yet.

- [ ] **Step 3: Implement auth_protocol.h**

Create `engine/net/auth_protocol.h`:

```cpp
#pragma once
#include <array>
#include <string>
#include <cstdint>
#include <cstring>
#include <random>
#include "engine/net/byte_stream.h"

namespace fate {

// ============================================================================
// Auth Token — 128-bit random value, TCP-only
// Distinct from the 32-bit UDP session token in PacketHeader
// ============================================================================
using AuthToken = std::array<uint8_t, 16>;

inline AuthToken generateAuthToken() {
    AuthToken token;
    static thread_local std::mt19937_64 gen{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    uint64_t a = dist(gen), b = dist(gen);
    std::memcpy(token.data(), &a, 8);
    std::memcpy(token.data() + 8, &b, 8);
    return token;
}

struct AuthTokenHash {
    size_t operator()(const AuthToken& t) const {
        uint64_t a, b;
        std::memcpy(&a, t.data(), 8);
        std::memcpy(&b, t.data() + 8, 8);
        return std::hash<uint64_t>{}(a) ^ (std::hash<uint64_t>{}(b) << 1);
    }
};

// ============================================================================
// Input Validation
// ============================================================================
struct AuthValidation {
    static bool isValidUsername(const std::string& s) {
        if (s.size() < 3 || s.size() > 20) return false;
        for (char c : s) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
        }
        return true;
    }

    static bool isValidPassword(const std::string& s) {
        if (s.size() < 8 || s.size() > 128) return false;
        for (char c : s) {
            if (c < 0x20 || c > 0x7E) return false; // printable ASCII
        }
        return true;
    }

    static bool isValidCharacterName(const std::string& s) {
        if (s.size() < 2 || s.size() > 16) return false;
        if (s.front() == ' ' || s.back() == ' ') return false;
        for (char c : s) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != ' ') return false;
        }
        return true;
    }
};

// ============================================================================
// Auth Message Types (TCP only — not part of UDP PacketType namespace)
// ============================================================================
enum class AuthMessageType : uint8_t {
    RegisterRequest = 1,
    LoginRequest    = 2,
    AuthResponse    = 3,
};

struct RegisterRequest {
    std::string username;
    std::string password;
    std::string characterName;
    std::string className;

    void write(ByteWriter& w) const {
        w.writeU8(static_cast<uint8_t>(AuthMessageType::RegisterRequest));
        w.writeString(username);
        w.writeString(password);
        w.writeString(characterName);
        w.writeString(className);
    }

    static RegisterRequest read(ByteReader& r) {
        RegisterRequest m;
        // type byte already consumed or caller skips it
        m.username      = r.readString();
        m.password      = r.readString();
        m.characterName = r.readString();
        m.className     = r.readString();
        return m;
    }
};

struct LoginRequest {
    std::string username;
    std::string password;

    void write(ByteWriter& w) const {
        w.writeU8(static_cast<uint8_t>(AuthMessageType::LoginRequest));
        w.writeString(username);
        w.writeString(password);
    }

    static LoginRequest read(ByteReader& r) {
        LoginRequest m;
        m.username = r.readString();
        m.password = r.readString();
        return m;
    }
};

struct AuthResponse {
    bool success = false;
    AuthToken authToken = {};
    std::string errorReason;
    // Preview data (only on login success)
    std::string characterName;
    std::string className;
    int32_t level = 0;

    void write(ByteWriter& w) const {
        w.writeU8(success ? 1 : 0);
        w.writeBytes(authToken.data(), 16);
        w.writeString(errorReason);
        w.writeString(characterName);
        w.writeString(className);
        w.writeI32(level);
    }

    static AuthResponse read(ByteReader& r) {
        AuthResponse m;
        m.success = r.readU8() != 0;
        r.readBytes(m.authToken.data(), 16);
        m.errorReason   = r.readString();
        m.characterName = r.readString();
        m.className     = r.readString();
        m.level         = r.readI32();
        return m;
    }
};

// ============================================================================
// Pending Session — bridges TCP auth to UDP connect
// ============================================================================
struct PendingSession {
    int account_id = 0;
    std::string character_id;  // VARCHAR(64) matching Unity DB
    double created_at = 0.0;
    double expires_at = 0.0;
};

} // namespace fate
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests
out/build/x64-Debug/fate_tests -tc="Auth*"
```

Expected: All auth tests PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/net/auth_protocol.h tests/test_auth_protocol.cpp
git commit -m "feat(auth): add auth protocol messages, token type, and input validation"
```

---

## Task 3: Database Connection Wrapper

**Files:**
- Create: `server/db/db_connection.h`
- Create: `server/db/db_connection.cpp`

- [ ] **Step 1: Create db_connection.h**

```cpp
#pragma once
#include <string>
#include <memory>
#include <pqxx/pqxx>
#include "engine/core/logger.h"

namespace fate {

class DbConnection {
public:
    bool connect(const std::string& connectionString);
    void disconnect();
    bool isConnected() const;

    // Returns the raw pqxx connection for repository use.
    // Caller must not store this across threads.
    pqxx::connection& connection();

    // Reconnect with backoff (call on failure).
    bool reconnect();

private:
    std::unique_ptr<pqxx::connection> conn_;
    std::string connString_;
    int reconnectAttempts_ = 0;
    static constexpr int MAX_RECONNECT_ATTEMPTS = 3;
};

} // namespace fate
```

- [ ] **Step 2: Create db_connection.cpp**

```cpp
#include "server/db/db_connection.h"
#include <thread>
#include <chrono>

namespace fate {

bool DbConnection::connect(const std::string& connectionString) {
    connString_ = connectionString;
    try {
        conn_ = std::make_unique<pqxx::connection>(connString_);
        if (conn_->is_open()) {
            LOG_INFO("DB", "Connected to PostgreSQL: %s", conn_->dbname());
            reconnectAttempts_ = 0;
            return true;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("DB", "Connection failed: %s", e.what());
    }
    return false;
}

void DbConnection::disconnect() {
    if (conn_) {
        conn_->close();
        conn_.reset();
    }
}

bool DbConnection::isConnected() const {
    return conn_ && conn_->is_open();
}

pqxx::connection& DbConnection::connection() {
    return *conn_;
}

bool DbConnection::reconnect() {
    if (reconnectAttempts_ >= MAX_RECONNECT_ATTEMPTS) {
        LOG_ERROR("DB", "Max reconnect attempts reached");
        return false;
    }
    reconnectAttempts_++;
    int backoffMs = reconnectAttempts_ * 1000;
    LOG_WARN("DB", "Reconnecting in %dms (attempt %d/%d)",
             backoffMs, reconnectAttempts_, MAX_RECONNECT_ATTEMPTS);
    std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
    return connect(connString_);
}

} // namespace fate
```

- [ ] **Step 3: Verify build**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateServer
```

Expected: Compiles (DbConnection not yet used, but included in GLOB_RECURSE).

- [ ] **Step 4: Commit**

```bash
git add server/db/db_connection.h server/db/db_connection.cpp
git commit -m "feat(db): add PostgreSQL connection wrapper with reconnect logic"
```

---

## Task 4: GATE — Query Unity DB for Exact Schema

**This task requires the user to run queries on the live Unity PostgreSQL database.**

- [ ] **Step 1: Ask user to run schema queries**

The user must run these on the live Unity DB and provide the output:

```sql
\d accounts
\d characters
\d character_inventory
```

- [ ] **Step 2: Record exact column names, types, defaults**

Save the results. All repository code in Tasks 5-7 must match these exactly. Do not guess column names.

- [ ] **Step 3: Verify the C++ engine's `CharacterStats` fields map to DB columns**

Cross-reference `game/shared/character_stats.h` fields with the DB `characters` columns. Document any mismatches that need adapting in the repository's load/save methods.

---

## Task 5: Account Repository

**Files:**
- Create: `server/db/account_repository.h`
- Create: `server/db/account_repository.cpp`

**Depends on:** Task 4 (exact schema from DB queries)

- [ ] **Step 1: Create account_repository.h**

```cpp
#pragma once
#include <string>
#include <optional>
#include <pqxx/pqxx>

namespace fate {

struct AccountRecord {
    int account_id = 0;
    std::string username;
    std::string password_hash;
    bool is_banned = false;
    std::string ban_reason;
};

class AccountRepository {
public:
    explicit AccountRepository(pqxx::connection& conn) : conn_(conn) {}

    // Returns account_id on success, -1 on failure (duplicate username)
    int createAccount(const std::string& username, const std::string& passwordHash);

    // Returns nullopt if not found
    std::optional<AccountRecord> findByUsername(const std::string& username);

    void updateLastLogin(int accountId);

private:
    pqxx::connection& conn_;
};

} // namespace fate
```

- [ ] **Step 2: Implement account_repository.cpp**

Use parameterized queries matching the exact column names from the Unity DB schema (Task 4 output). Include bcrypt password hashing via the bcrypt library.

```cpp
#include "server/db/account_repository.h"
#include "engine/core/logger.h"

namespace fate {

int AccountRepository::createAccount(const std::string& username, const std::string& passwordHash) {
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "INSERT INTO accounts (username, password_hash) "
            "VALUES ($1, $2) RETURNING account_id",
            username, passwordHash);
        txn.commit();
        if (!result.empty()) return result[0][0].as<int>();
    } catch (const pqxx::unique_violation&) {
        LOG_WARN("AccountRepo", "Username '%s' already exists", username.c_str());
    } catch (const std::exception& e) {
        LOG_ERROR("AccountRepo", "createAccount failed: %s", e.what());
    }
    return -1;
}

std::optional<AccountRecord> AccountRepository::findByUsername(const std::string& username) {
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT account_id, username, password_hash, is_banned, ban_reason "
            "FROM accounts WHERE username = $1",
            username);
        txn.commit();
        if (result.empty()) return std::nullopt;
        AccountRecord rec;
        rec.account_id   = result[0]["account_id"].as<int>();
        rec.username     = result[0]["username"].as<std::string>();
        rec.password_hash = result[0]["password_hash"].as<std::string>();
        rec.is_banned    = result[0]["is_banned"].as<bool>();
        rec.ban_reason   = result[0]["ban_reason"].is_null() ? "" : result[0]["ban_reason"].as<std::string>();
        return rec;
    } catch (const std::exception& e) {
        LOG_ERROR("AccountRepo", "findByUsername failed: %s", e.what());
    }
    return std::nullopt;
}

void AccountRepository::updateLastLogin(int accountId) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params("UPDATE accounts SET last_login = NOW() WHERE account_id = $1", accountId);
        txn.commit();
    } catch (const std::exception& e) {
        LOG_ERROR("AccountRepo", "updateLastLogin failed: %s", e.what());
    }
}

} // namespace fate
```

**Note:** Exact column names above are from the Unity base schema. Adjust if Task 4 reveals differences.

- [ ] **Step 3: Build and verify**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateServer
```

- [ ] **Step 4: Commit**

```bash
git add server/db/account_repository.h server/db/account_repository.cpp
git commit -m "feat(db): add AccountRepository — create, find, update last login"
```

---

## Task 6: Character Repository

**Files:**
- Create: `server/db/character_repository.h`
- Create: `server/db/character_repository.cpp`

**Depends on:** Task 4 (exact schema), Task 5

- [ ] **Step 1: Create character_repository.h**

Define `CharacterRecord` struct with fields matching DB columns (exact names from Task 4). Include methods: `createDefaultCharacter()`, `loadCharacter()`, `saveCharacter()`.

- [ ] **Step 2: Implement character_repository.cpp**

Use exact column names from Task 4. Key queries:
- `INSERT INTO characters (...) VALUES ($1, $2, ...) RETURNING character_id` for create
- `SELECT ... FROM characters WHERE character_id = $1` for load
- `UPDATE characters SET ... WHERE character_id = $1` for save

Map `CharacterRecord` fields to/from `CharacterStatsComponent` and `Transform`.

- [ ] **Step 3: Build and verify**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateServer
```

- [ ] **Step 4: Commit**

```bash
git add server/db/character_repository.h server/db/character_repository.cpp
git commit -m "feat(db): add CharacterRepository — create, load, save character data"
```

---

## Task 7: Inventory Repository

**Files:**
- Create: `server/db/inventory_repository.h`
- Create: `server/db/inventory_repository.cpp`

**Depends on:** Task 4 (exact schema)

- [ ] **Step 1: Create inventory_repository.h**

Define `InventorySlotRecord` struct with fields matching `character_inventory` columns (from Task 4). Include methods: `loadInventory()`, `saveInventory()`.

- [ ] **Step 2: Implement inventory_repository.cpp**

Key queries:
- `SELECT ... FROM character_inventory WHERE character_id = $1 ORDER BY slot_index`
- `INSERT ... ON CONFLICT (character_id, slot_index) DO UPDATE SET ...` for save

- [ ] **Step 3: Build and verify**

- [ ] **Step 4: Commit**

```bash
git add server/db/inventory_repository.h server/db/inventory_repository.cpp
git commit -m "feat(db): add InventoryRepository — load and save inventory slots"
```

---

## Task 8: TLS Auth Server

**Files:**
- Create: `server/auth/auth_server.h`
- Create: `server/auth/auth_server.cpp`

**Depends on:** Tasks 2, 3, 5, 6

- [ ] **Step 1: Create auth_server.h**

```cpp
#pragma once
#include "engine/net/auth_protocol.h"
#include "server/db/db_connection.h"
#include "server/db/account_repository.h"
#include "server/db/character_repository.h"
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <openssl/ssl.h>

namespace fate {

struct AuthResult {
    AuthToken token;
    PendingSession session;
};

class AuthServer {
public:
    bool start(uint16_t port, const std::string& certPath, const std::string& keyPath,
               const std::string& dbConnectionString);
    void stop();

    // Thread-safe: called from game thread to drain completed auths
    bool popAuthResult(AuthResult& out);

private:
    std::atomic<bool> running_{false};
    std::thread listenThread_;

    // TLS
    SSL_CTX* sslCtx_ = nullptr;
    int listenSocket_ = -1;

    // DB (auth thread only)
    DbConnection dbConn_;
    std::unique_ptr<AccountRepository> accountRepo_;
    std::unique_ptr<CharacterRepository> characterRepo_;

    // Thread-safe queue: auth thread pushes, game thread pops
    std::mutex queueMutex_;
    std::queue<AuthResult> resultQueue_;

    void listenLoop();
    void handleClient(SSL* ssl);
    AuthResponse processRegister(const RegisterRequest& req);
    AuthResponse processLogin(const LoginRequest& req);

    void pushResult(const AuthResult& result);
};

} // namespace fate
```

- [ ] **Step 2: Implement auth_server.cpp**

Key implementation details:
- `start()`: Initialize OpenSSL, create SSL_CTX, load cert/key, bind TCP socket, spawn `listenThread_`
- `listenLoop()`: Accept connections, wrap in SSL, read message type byte, dispatch to processRegister/processLogin
- `processRegister()`: Validate input, bcrypt hash password, call AccountRepository::createAccount + CharacterRepository::createDefaultCharacter, generate AuthToken, push PendingSession
- `processLogin()`: Find account, bcrypt verify password, check ban status, load character preview, generate AuthToken, push PendingSession
- `popAuthResult()`: Lock mutex, pop from queue, return true if available

For bcrypt, use:
```cpp
#include "bcrypt.h"
// Hash: bcrypt_hashpw(password, bcrypt_gensalt(12))
// Verify: bcrypt_checkpw(password, stored_hash) == 0
```

- [ ] **Step 3: Build and verify**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateServer
```

- [ ] **Step 4: Commit**

```bash
git add server/auth/auth_server.h server/auth/auth_server.cpp
git commit -m "feat(auth): add TLS TCP auth server with bcrypt + DB integration"
```

---

## Task 9: TLS Auth Client

**Files:**
- Create: `engine/net/auth_client.h`
- Create: `engine/net/auth_client.cpp`

- [ ] **Step 1: Create auth_client.h**

```cpp
#pragma once
#include "engine/net/auth_protocol.h"
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <optional>

namespace fate {

class AuthClient {
public:
    // Non-blocking: spawns background thread for TLS connection
    void loginAsync(const std::string& host, uint16_t port,
                    const std::string& username, const std::string& password);

    void registerAsync(const std::string& host, uint16_t port,
                       const std::string& username, const std::string& password,
                       const std::string& characterName, const std::string& className);

    // Call from main thread each frame to check for result
    bool hasResult() const;
    AuthResponse consumeResult();

    bool isBusy() const { return busy_; }

private:
    std::atomic<bool> busy_{false};
    std::mutex resultMutex_;
    std::optional<AuthResponse> result_;
    std::thread worker_;

    void doAuth(const std::string& host, uint16_t port,
                const std::vector<uint8_t>& requestData);
};

} // namespace fate
```

- [ ] **Step 2: Implement auth_client.cpp**

Key implementation:
- `doAuth()`: Create TCP socket, SSL_connect, send request bytes, read response bytes, parse AuthResponse, store in `result_`
- Use `FATE_DEV_TLS` compile flag to skip cert verification in dev mode
- Thread cleans up on completion, sets `busy_ = false`

- [ ] **Step 3: Build and verify**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
```

- [ ] **Step 4: Commit**

```bash
git add engine/net/auth_client.h engine/net/auth_client.cpp
git commit -m "feat(auth): add TLS TCP auth client with async login/register"
```

---

## Task 10: Modified UDP Handshake

**Files:**
- Modify: `engine/net/connection.h:13-23`
- Modify: `engine/net/net_server.h:48`
- Modify: `engine/net/net_server.cpp:37-39,73-121`
- Modify: `engine/net/net_client.h:14,36-45`
- Modify: `engine/net/net_client.cpp:6-38,108-117`

- [ ] **Step 1: Add fields to ClientConnection**

In `connection.h`, add `#include "engine/net/auth_protocol.h"` at the top, then add to `ClientConnection` struct (after line 22):

```cpp
int account_id = 0;
std::string character_id;
AuthToken authToken = {};  // populated from Connect packet payload, consumed by ServerApp
```

- [ ] **Step 2: Add auth token field and ConnectReject to NetClient**

In `net_client.h`, add `#include "engine/net/auth_protocol.h"`, then add:
- `AuthToken authToken_` field
- `bool connectWithToken(const std::string& host, uint16_t port, const AuthToken& token)` method
- `std::function<void(const std::string&)> onConnectRejected` callback

- [ ] **Step 3: Implement connectWithToken in net_client.cpp**

Similar to existing `connect()` but stores the auth token and sends it as 16-byte payload in the Connect packet. Returns `bool` (matching existing `connect()` pattern):

```cpp
bool NetClient::connectWithToken(const std::string& host, uint16_t port, const AuthToken& token) {
    // ... same setup as connect() (IP parsing, socket open, etc.) ...
    authToken_ = token;
    // Send Connect with auth token payload
    sendPacket(Channel::ReliableOrdered, PacketType::Connect, token.data(), 16);
    waitingForAccept_ = true;
    return true;
}
```

The existing `connect()` method (no token) is removed — all connections now require auth. During dev, if you need to bypass auth for testing, call `connectWithToken()` with a dummy token and skip validation server-side behind a debug flag.

- [ ] **Step 4: Handle ConnectReject in net_client.cpp**

Add `case PacketType::ConnectReject:` in `handlePacket()` (after ConnectAccept case):

```cpp
case PacketType::ConnectReject: {
    waitingForAccept_ = false;
    socket_.close();
    ByteReader payload(data + r.position(), hdr.payloadSize);
    std::string reason = payload.readString();
    LOG_WARN("NetClient", "Connection rejected: %s", reason.c_str());
    if (onConnectRejected) onConnectRejected(reason);
    break;
}
```

- [ ] **Step 5: Modify server handleConnect to accept auth token**

In `net_server.h`, change `handleConnect` signature:
```cpp
void handleConnect(const NetAddress& from, const uint8_t* payload, size_t payloadSize, float currentTime);
```

In `net_server.cpp`, update the call site (line 37-39) to pass the raw packet data after the header:
```cpp
if (hdr.packetType == PacketType::Connect) {
    const uint8_t* payload = data + PACKET_HEADER_SIZE;
    size_t payloadSize = (size > static_cast<int>(PACKET_HEADER_SIZE)) ? size - PACKET_HEADER_SIZE : 0;
    handleConnect(from, payload, payloadSize, currentTime);
    return;
}
```

In `handleConnect()`, if `payloadSize >= 16`, read the 16-byte auth token and store it on the new `ClientConnection`:
```cpp
if (payloadSize >= 16) {
    std::memcpy(client->authToken.data(), payload, 16);
}
```

The `onClientConnected` callback signature stays `void(uint16_t clientId)`. ServerApp retrieves the auth token by looking up the `ClientConnection` by clientId and reading `client->authToken`.

Add `sendConnectReject()` helper that sends a ConnectReject packet with a reason string payload.

- [ ] **Step 6: Build and run existing tests**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests
out/build/x64-Debug/fate_tests
```

Expected: All existing tests still pass (no regression).

- [ ] **Step 7: Commit**

```bash
git add engine/net/connection.h engine/net/net_server.h engine/net/net_server.cpp \
        engine/net/net_client.h engine/net/net_client.cpp
git commit -m "feat(net): add auth token to UDP handshake, ConnectReject handling"
```

---

## Task 11: Server Integration — DB + Auth + Persistence

**Files:**
- Modify: `server/server_app.h:1-46`
- Modify: `server/server_app.cpp:24-155`
- Modify: `game/entity_factory.h:22-154`

- [ ] **Step 1: Add createPlayerFromDB to EntityFactory**

Add a new static method to `entity_factory.h` that creates a player entity from database-loaded data instead of defaults. Takes a `CharacterRecord` struct (from CharacterRepository) and applies the saved stats, position, level, gold, etc.

- [ ] **Step 2: Update server_app.h**

Add members:
```cpp
#include "server/auth/auth_server.h"
#include "server/db/db_connection.h"
#include "server/db/account_repository.h"
#include "server/db/character_repository.h"
#include "server/db/inventory_repository.h"

// Auth server
AuthServer authServer_;

// DB (game thread connection)
DbConnection gameDbConn_;
std::unique_ptr<AccountRepository> accountRepo_;
std::unique_ptr<CharacterRepository> characterRepo_;
std::unique_ptr<InventoryRepository> inventoryRepo_;

// Pending sessions from auth
std::unordered_map<AuthToken, PendingSession, AuthTokenHash> pendingSessions_;

// Map clientId -> account_id for duplicate login detection
std::unordered_map<int, uint16_t> activeAccountSessions_; // account_id -> clientId
```

- [ ] **Step 3: Update ServerApp::init()**

After existing network setup:
1. Read `DATABASE_URL` from environment
2. Open game DB connection via `gameDbConn_.connect()`
3. Initialize repositories with game DB connection
4. Start AuthServer on port 7778 with its own DB connection

- [ ] **Step 4: Update ServerApp::tick()**

Add step between poll and world update:
```cpp
// Consume completed auth results
AuthResult authResult;
while (authServer_.popAuthResult(authResult)) {
    // Check for duplicate login — kick existing
    auto it = activeAccountSessions_.find(authResult.session.account_id);
    if (it != activeAccountSessions_.end()) {
        uint16_t existingClientId = it->second;
        // Save existing player, disconnect them
        onClientDisconnected(existingClientId);
        server_.connections().removeClient(existingClientId);
    }
    pendingSessions_[authResult.token] = authResult.session;
}

// Clean expired pending sessions
auto now = /* steady_clock seconds */;
std::erase_if(pendingSessions_, [now](auto& pair) {
    return now > pair.second.expires_at;
});
```

- [ ] **Step 5: Update onClientConnected()**

Replace the current hardcoded player creation with:
1. Look up auth token from the Connect packet (stored on ClientConnection by modified handleConnect)
2. Find PendingSession, consume it
3. Load character from DB via `characterRepo_->loadCharacter(session.character_id)`
4. Call `EntityFactory::createPlayerFromDB()` with loaded data
5. Use `character_id` as PersistentId (stable across sessions)
6. Track `activeAccountSessions_[session.account_id] = clientId`
7. Set `client->account_id` and `client->character_id`

- [ ] **Step 6: Update onClientDisconnected()**

Before destroying the entity:
1. Gather current character state from components (stats, position, inventory)
2. Call `characterRepo_->saveCharacter(...)` with current values
3. Call `inventoryRepo_->saveInventory(...)` with current inventory
4. On failure: log error, retry once
5. Clean up `activeAccountSessions_`
6. Then proceed with existing entity destroy + replication unregister

- [ ] **Step 7: Update ServerApp::shutdown()**

Add cleanup before existing `server_.stop()`:
1. Save all connected players' data (iterate active sessions, call save for each)
2. `authServer_.stop()` — signals auth thread to exit, joins it
3. `gameDbConn_.disconnect()` — close game thread DB connection
4. Then existing `server_.stop()` and `NetSocket::shutdownPlatform()`

- [ ] **Step 8: Build and verify**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateServer
```

- [ ] **Step 9: Commit**

```bash
git add server/server_app.h server/server_app.cpp game/entity_factory.h
git commit -m "feat(server): integrate auth server, DB persistence, load/save on connect/disconnect"
```

---

## Task 12: Login Screen UI

**Files:**
- Create: `game/ui/login_screen.h`
- Create: `game/ui/login_screen.cpp`

- [ ] **Step 1: Create login_screen.h**

```cpp
#pragma once
#include "engine/net/auth_protocol.h"
#include <string>

namespace fate {

enum class LoginScreenState {
    Login,
    Register,
};

class LoginScreen {
public:
    // Returns true if user submitted (login or register)
    void draw();

    LoginScreenState state = LoginScreenState::Login;

    // Input fields
    char username[21] = {};
    char password[129] = {};
    char confirmPassword[129] = {};
    char characterName[17] = {};
    int selectedClass = 0; // 0=Warrior, 1=Mage, 2=Archer

    // Status
    std::string statusMessage;
    bool isError = false;
    bool loginSubmitted = false;
    bool registerSubmitted = false;

    void reset();
};

} // namespace fate
```

- [ ] **Step 2: Implement login_screen.cpp**

ImGui layout with:
- Login state: username + password fields, Login button, "Create Account" link, error display
- Register state: username + password + confirm + character name + class radio buttons, Register button, "Back to Login" link
- Input validation on submit (call `AuthValidation` methods, show error if invalid)
- Set `loginSubmitted`/`registerSubmitted` flags for GameApp to consume

> **Future note:** Replace class radio buttons with TWOM-style interactive class showcase (clickable class models, stat previews, animations)

- [ ] **Step 3: Build and verify**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
```

- [ ] **Step 4: Commit**

```bash
git add game/ui/login_screen.h game/ui/login_screen.cpp
git commit -m "feat(ui): add ImGui login/register screen"
```

---

## Task 13: Client State Machine & Full Integration

**Files:**
- Modify: `game/game_app.h:23-61`
- Modify: `game/game_app.cpp`

- [ ] **Step 1: Add connection state enum to game_app.h**

```cpp
enum class ConnectionState {
    LoginScreen,
    Authenticating,
    UDPConnecting,
    InGame,
};

// Add members:
ConnectionState connState_ = ConnectionState::LoginScreen;
AuthClient authClient_;
LoginScreen loginScreen_;
AuthToken pendingAuthToken_;
int authPort_ = 7778;
```

- [ ] **Step 2: Implement state machine in onUpdate()**

```
LoginScreen: Draw login screen, check for submit flags
  -> on login submit: authClient_.loginAsync(), transition to Authenticating
  -> on register submit: authClient_.registerAsync(), transition to Authenticating

Authenticating: Check authClient_.hasResult()
  -> on success: store auth token, netClient_.connectWithToken(), transition to UDPConnecting
  -> on failure: show error, transition back to LoginScreen

UDPConnecting: Existing netClient_.poll()
  -> on netClient_ connected: transition to InGame
  -> on ConnectReject: show error, transition back to LoginScreen
  -> on timeout: show error, transition back to LoginScreen

InGame: Existing game loop (no changes)
  -> on disconnect: transition back to LoginScreen
```

- [ ] **Step 3: Gate gameplay behind InGame state**

In `onUpdate()` and `onRender()`, only run gameplay systems, movement, HUD, etc. when `connState_ == ConnectionState::InGame`. When in LoginScreen/Authenticating/UDPConnecting, only draw the login screen (and status messages).

- [ ] **Step 4: Build and verify full compile**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateEngine
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateServer
```

- [ ] **Step 5: Commit**

```bash
git add game/game_app.h game/game_app.cpp
git commit -m "feat(client): add login state machine, wire auth client to login screen"
```

---

## Task 14: TLS Certificate Setup & End-to-End Test

**Files:**
- Create: `config/` directory with self-signed cert

- [ ] **Step 1: Generate self-signed TLS certificate**

```bash
mkdir -p config
openssl req -x509 -newkey rsa:2048 -keyout config/server.key -out config/server.crt \
    -days 365 -nodes -subj "/CN=localhost"
```

Add `config/server.key` to `.gitignore` (private key should not be committed).

- [ ] **Step 2: Add config to .gitignore**

```
config/server.key
```

Commit `config/server.crt` (public cert is fine to commit for dev).

- [ ] **Step 3: Manual end-to-end test**

1. Start server: `out/build/x64-Debug/FateServer.exe 7777`
2. Start client: `out/build/x64-Debug/FateEngine.exe`
3. Client should show login screen (not connect automatically)
4. Click "Create Account" — fill in username/password/character name/class — click Register
5. Should see "Connecting..." then transition to game
6. Move character to a new position
7. Disconnect (close client or click disconnect)
8. Restart client, log in with same credentials
9. Character should spawn at the position where they disconnected, with same stats/level

- [ ] **Step 4: Test duplicate login kick**

1. Start two clients
2. Log in with same account on both
3. First client should be disconnected when second logs in

- [ ] **Step 5: Commit**

```bash
git add config/server.crt .gitignore
git commit -m "feat: add TLS cert for dev, end-to-end auth + persistence verified"
```

---

## Notes for Future Work

- **Periodic auto-save:** Add 60-second timer in `ServerApp::tick()` that calls `saveCharacter()` for all connected clients. Reference Unity project's `CharacterRepository.SaveCharacter()` for exact query patterns.
- **Multiple characters per account:** Add character select screen between login and game. Change account model to allow multiple `characters` rows per `account_id`.
- **TWOM-style character creation:** Replace class radio buttons with interactive class showcase — clickable class models with stat previews, animations, skill previews.
- **Connection pooling:** If auth and game thread both need heavy DB access, consider a connection pool instead of two fixed connections.
- **Unity DB reference code:** User has existing implementation code in the Unity project (`C:\Users\Caleb\FateRPG_Prototype2\`) for repository patterns and query logic.
