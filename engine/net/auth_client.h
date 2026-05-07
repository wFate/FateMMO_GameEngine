#pragma once
#include "engine/net/auth_protocol.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <optional>
#include <vector>
#include <queue>
#include <condition_variable>

namespace fate {

// ---------------------------------------------------------------------------
// Command queue types — client pushes commands, worker thread processes them
// ---------------------------------------------------------------------------
enum class AuthCommandType { Login, Register, Create, Delete, Select, Disconnect };

struct AuthCommand {
    AuthCommandType type;
    std::string host;
    uint16_t port = 0;
    // Login/Register:
    std::string username, password, email;
    // Create:
    std::string characterName, className;
    uint8_t faction = 0, gender = 0, hairstyle = 0;
    // Delete/Select:
    std::string characterId;
};

// ---------------------------------------------------------------------------
// Result types — worker pushes results, main thread polls
// ---------------------------------------------------------------------------
enum class AuthResultType { Login, Register, Create, Delete, Select };

// Distinguishes server-returned validation errors (player-fixable: name taken,
// invalid class, etc.) from transport / availability failures (DNS, TCP refused,
// TLS, malformed response, mid-session disconnect). The UI routes them
// differently — validation stays on the form, transport bounces to login.
enum class AuthFailureKind { None, Validation, Transport };

struct AuthClientResult {
    AuthResultType type;
    bool success = false;
    AuthFailureKind failureKind = AuthFailureKind::None;
    std::string errorMessage;
    std::vector<CharacterPreview> characters; // for Login/Create/Delete
    SelectCharResponse selectData;            // for Select
};

// ---------------------------------------------------------------------------
// AuthClient — persistent TLS connection with command queue
// ---------------------------------------------------------------------------
class AuthClient {
public:
    ~AuthClient();

    // Initial connection — spawns worker thread with TLS setup. Returns
    // true if the worker was actually spawned, false if the request couldn't
    // start (already busy with a prior request, or serialization overflow).
    // A false return means no AuthClientResult will ever arrive for this
    // call — the UI must handle it synchronously instead of waiting on poll.
    bool loginAsync(const std::string& host, uint16_t port,
                    const std::string& username, const std::string& password);
    bool registerAsync(const std::string& host, uint16_t port,
                       const std::string& username, const std::string& password,
                       const std::string& email,
                       const std::string& characterName, const std::string& className,
                       uint8_t faction = 0, uint8_t gender = 0, uint8_t hairstyle = 0);

    // Commands on persistent connection (must be connected first)
    void createCharacterAsync(const std::string& name, const std::string& className,
                              uint8_t faction, uint8_t gender, uint8_t hairstyle);
    void deleteCharacterAsync(const std::string& characterId);
    void selectCharacterAsync(const std::string& characterId);
    void disconnectAuth();

    // Main-thread polling
    bool hasResult() const;
    AuthClientResult consumeResult();
    bool isBusy() const { return busy_; }

private:
    // Test-only seams: synchronously stamp internal state that normally only
    // exists between a successful enqueue and the worker's completion of the
    // cmd, or between the worker spawn and the worker clearing busy_. Let
    // unit tests exercise the gates and the disconnect drain without spinning
    // up a real socket / worker thread. The functions are declared here but
    // defined only in tests/test_auth_failure_kind.cpp -- ODR-safe because
    // the class definition is token-identical across TUs.
    friend void testForceConnectedInFlight(AuthClient&);
    friend void testForceConnected(AuthClient&);
    friend void testForceBusy(AuthClient&);
    friend void testEnqueueOrphanCommand(AuthClient&, AuthCommandType);
    friend void testRunAuthClientDisconnectDrain(AuthClient&);

    std::atomic<bool> busy_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> shouldStop_{false};

    // Tags the initial-auth result with the kind of request that triggered it so
    // main-thread routing can distinguish Login-failures from Register-failures.
    AuthResultType pendingRequestType_ = AuthResultType::Login;

    mutable std::mutex resultMutex_;
    std::optional<AuthClientResult> result_;

    std::mutex cmdMutex_;
    std::condition_variable cmdCv_;
    std::queue<AuthCommand> cmdQueue_;
    // Single-result invariant: result_ is one std::optional slot, so at most
    // one post-login command may be in flight at a time. The gate is set on
    // enqueue and cleared after the worker finishes processing the command
    // (success/validation/malformed) or the connection-loss drain runs. The
    // UI is single-screen-at-a-time and naturally serializes commands; this
    // flag prevents the drain loop from overwriting an earlier orphan
    // failure with a later one when more than one command sits in cmdQueue_.
    bool cmdInFlight_ = false;

    std::thread worker_;

    void workerLoop(const std::string& host, uint16_t port,
                    const std::vector<uint8_t>& initialData);
    void pushResult(AuthClientResult result);
    void pushTransportFailure(AuthResultType type, const std::string& reason);
    void cleanup();
    // Atomic disconnect transition: connected_=false, drain orphan cmds in
    // cmdQueue_ into at-most-one Transport failure (skipped on orderly
    // shutdown via shouldStop_, or when result_ already holds an unconsumed
    // prior to avoid overwriting a legitimate response), clear cmdInFlight_.
    // Caller MUST hold cmdMutex_.
    void performDisconnectDrainLocked();
};

} // namespace fate
