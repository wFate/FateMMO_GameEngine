#include "engine/net/auth_client.h"
#include "engine/net/byte_stream.h"
#include "engine/core/logger.h"

#ifdef FATE_HAS_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#endif

#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <ws2ipdef.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif

namespace fate
{

#ifndef FATE_HAS_OPENSSL
    // Stub implementations when OpenSSL is not available (demo/open-source build)
    AuthClient::~AuthClient() {}
    void AuthClient::connect(const std::string &, uint16_t, const std::string &)
    {
        LOG_WARN("AuthClient", "TLS not available --OpenSSL not linked");
    }
    void AuthClient::disconnect() {}
    void AuthClient::login(const std::string &, const std::string &) {}
    void AuthClient::registerAccount(const std::string &, const std::string &, const std::string &) {}
    void AuthClient::createCharacter(const std::string &, const std::string &) {}
    void AuthClient::deleteCharacter(const std::string &) {}
    void AuthClient::selectCharacter(const std::string &) {}
    bool AuthClient::isConnected() const { return false; }
    std::optional<AuthClientResult> AuthClient::poll() { return std::nullopt; }
#else

    // ---------------------------------------------------------------------------
    // Platform socket helpers
    // ---------------------------------------------------------------------------
    static constexpr uintptr_t INVALID_SOCK_HANDLE = ~uintptr_t(0);

#ifdef _WIN32
    static void closeSocket(uintptr_t fd)
    {
        if (fd != INVALID_SOCK_HANDLE)
        {
            ::closesocket(static_cast<SOCKET>(fd));
        }
    }
#else
    static void closeSocket(uintptr_t fd)
    {
        if (fd != INVALID_SOCK_HANDLE)
        {
            ::close(static_cast<int>(fd));
        }
    }
#endif

    // ---------------------------------------------------------------------------
    // SSL I/O helpers --length-prefixed (4-byte LE length + body)
    // ---------------------------------------------------------------------------
    static bool sslWriteAll(SSL *ssl, const void *data, int len)
    {
        const uint8_t *p = static_cast<const uint8_t *>(data);
        int remaining = len;
        while (remaining > 0)
        {
            int written = SSL_write(ssl, p, remaining);
            if (written <= 0)
                return false;
            p += written;
            remaining -= written;
        }
        return true;
    }

    static bool sslReadAll(SSL *ssl, void *data, int len)
    {
        uint8_t *p = static_cast<uint8_t *>(data);
        int remaining = len;
        while (remaining > 0)
        {
            int r = SSL_read(ssl, p, remaining);
            if (r <= 0)
                return false;
            p += r;
            remaining -= r;
        }
        return true;
    }

    static bool sslSendMessage(SSL *ssl, const uint8_t *data, uint32_t len)
    {
        if (!sslWriteAll(ssl, &len, 4))
            return false;
        if (!sslWriteAll(ssl, data, static_cast<int>(len)))
            return false;
        return true;
    }

    static constexpr uint32_t MAX_RESP_SIZE = 8192;

    static bool sslRecvMessage(SSL *ssl, std::vector<uint8_t> &out)
    {
        uint8_t lenBuf[4];
        if (!sslReadAll(ssl, lenBuf, 4))
            return false;

        uint32_t respLen = 0;
        std::memcpy(&respLen, lenBuf, 4);

        if (respLen == 0 || respLen > MAX_RESP_SIZE)
            return false;

        out.resize(respLen);
        if (!sslReadAll(ssl, out.data(), static_cast<int>(respLen)))
            return false;
        return true;
    }

    // ---------------------------------------------------------------------------
    // Lifetime
    // ---------------------------------------------------------------------------
    AuthClient::~AuthClient()
    {
        disconnectAuth();
    }

    void AuthClient::cleanup()
    {
        shouldStop_.store(true);
        cmdCv_.notify_all();
        if (worker_.joinable())
        {
            worker_.join();
        }
        shouldStop_.store(false);
        connected_.store(false);
        // Drain any stale commands (e.g. Disconnect left over from disconnectAuth)
        {
            std::lock_guard<std::mutex> lock(cmdMutex_);
            std::queue<AuthCommand> empty;
            cmdQueue_.swap(empty);
            cmdInFlight_ = false;
        }
    }

    void AuthClient::pushResult(AuthClientResult result)
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        result_ = std::move(result);
    }

    // Maps the post-login command kind onto its matching result kind so a
    // mid-session disconnect can publish a typed Transport failure for the
    // in-flight command instead of silently dropping it.
    static AuthResultType cmdToResultType(AuthCommandType ct)
    {
        switch (ct)
        {
        case AuthCommandType::Create: return AuthResultType::Create;
        case AuthCommandType::Delete: return AuthResultType::Delete;
        case AuthCommandType::Select: return AuthResultType::Select;
        default:                      return AuthResultType::Login;
        }
    }

    void AuthClient::pushTransportFailure(AuthResultType type, const std::string &reason)
    {
        AuthClientResult res;
        res.type = type;
        res.success = false;
        res.failureKind = AuthFailureKind::Transport;
        res.errorMessage = reason;
        pushResult(std::move(res));
    }

    // ---------------------------------------------------------------------------
    // loginAsync
    // ---------------------------------------------------------------------------
    bool AuthClient::loginAsync(const std::string &host, uint16_t port,
                                const std::string &username, const std::string &password)
    {
        if (busy_.load())
        {
            LOG_WARN("AuthClient", "loginAsync: another request is already in flight");
            return false;
        }

        // Serialize LoginRequest (including the type byte from write())
        LoginRequest req;
        req.username = username;
        req.password = password;
        req.clientVersion = 1;

        uint8_t buf[1024];
        ByteWriter w(buf, sizeof(buf));
        req.write(w);

        if (w.overflowed())
        {
            LOG_ERROR("AuthClient", "LoginRequest serialization overflow");
            return false;
        }

        std::vector<uint8_t> requestData(w.data(), w.data() + w.size());

        // Clear previous result and mark busy
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            result_.reset();
        }
        // Tear down any previous connection (its teardown may clear busy_)
        cleanup();

        pendingRequestType_ = AuthResultType::Login;
        busy_.store(true);
        worker_ = std::thread(&AuthClient::workerLoop, this, host, port, requestData);
        return true;
    }

    // ---------------------------------------------------------------------------
    // registerAsync
    // ---------------------------------------------------------------------------
    bool AuthClient::registerAsync(const std::string &host, uint16_t port,
                                   const std::string &username, const std::string &password,
                                   const std::string &email,
                                   const std::string &characterName, const std::string &className,
                                   uint8_t faction, uint8_t gender, uint8_t hairstyle)
    {
        if (busy_.load())
        {
            LOG_WARN("AuthClient", "registerAsync: another request is already in flight");
            return false;
        }

        // Serialize RegisterRequest (including the type byte from write())
        RegisterRequest req;
        req.username = username;
        req.password = password;
        req.email = email;
        req.characterName = characterName;
        req.className = className;
        req.faction = faction;
        req.gender = gender;
        req.hairstyle = hairstyle;
        req.clientVersion = 1;

        uint8_t buf[2048];
        ByteWriter w(buf, sizeof(buf));
        req.write(w);

        if (w.overflowed())
        {
            LOG_ERROR("AuthClient", "RegisterRequest serialization overflow");
            return false;
        }

        std::vector<uint8_t> requestData(w.data(), w.data() + w.size());

        // Clear previous result and mark busy
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            result_.reset();
        }
        // Tear down any previous connection (its teardown may clear busy_)
        cleanup();

        pendingRequestType_ = AuthResultType::Register;
        busy_.store(true);
        worker_ = std::thread(&AuthClient::workerLoop, this, host, port, requestData);
        return true;
    }

    // ---------------------------------------------------------------------------
    // Command queue methods (post-login operations on persistent connection)
    // ---------------------------------------------------------------------------
    // Returns true if the gate dropped the cmd because a prior result is
    // still waiting for the UI to consume. Caller must already hold cmdMutex_.
    // The check briefly acquires resultMutex_ -- lock order cmdMutex_ ->
    // resultMutex_ matches the pre-existing fast-fail path (which calls
    // pushTransportFailure under cmdMutex_) and the disconnect drain.
    //
    // The race this closes: worker pushes result1 then clears cmdInFlight_=
    // false before the UI's poll runs. A duplicate click landing in that gap
    // would otherwise pass the cmdInFlight_ check, enqueue cmd2, and have
    // cmd2's eventual pushResult overwrite result1 in the single std::optional
    // slot -- OR pass to the !connected_ fast-fail below and have the
    // synthetic Transport failure overwrite result1. Either way, the awaited
    // response is lost and the UI either gets the wrong cmd's result or sits
    // forever on a stale "creating..." status.
    static bool dropIfResultPending(std::mutex& resultMutex,
                                    const std::optional<AuthClientResult>& result,
                                    const char* methodName)
    {
        std::lock_guard<std::mutex> rlock(resultMutex);
        if (result.has_value())
        {
            LOG_WARN("AuthClient", "%s dropped: prior result not yet consumed", methodName);
            return true;
        }
        return false;
    }

    void AuthClient::createCharacterAsync(const std::string &name, const std::string &className,
                                          uint8_t faction, uint8_t gender, uint8_t hairstyle)
    {
        AuthCommand cmd;
        cmd.type = AuthCommandType::Create;
        cmd.characterName = name;
        cmd.className = className;
        cmd.faction = faction;
        cmd.gender = gender;
        cmd.hairstyle = hairstyle;

        {
            // Hold cmdMutex_ across the gates + push so we can't race with the
            // worker's exit drain. Otherwise the main thread could see
            // connected_=true, push a cmd, and only afterwards have the
            // worker tear down without ever processing it.
            std::lock_guard<std::mutex> lock(cmdMutex_);
            // FIRST gate: drop if a prior result is still pending. Must run
            // before the !connected_ branch -- that branch publishes a
            // Transport failure and would clobber the unconsumed prior.
            if (dropIfResultPending(resultMutex_, result_, "createCharacterAsync"))
                return;
            if (!connected_.load())
            {
                LOG_WARN("AuthClient", "createCharacterAsync called but not connected");
                pushTransportFailure(AuthResultType::Create, "Auth connection lost");
                return;
            }
            if (cmdInFlight_)
            {
                // Drop the duplicate WITHOUT publishing a result. The first
                // command is still in flight and owns result_'s single slot;
                // overwriting it with a synthetic "already in flight" failure
                // would poison the UI's awaited consume and could bounce a
                // pending Create/Delete/Select to the error path while the
                // server response is still on the wire.
                LOG_WARN("AuthClient", "createCharacterAsync dropped: another cmd is in flight");
                return;
            }
            cmdInFlight_ = true;
            cmdQueue_.push(std::move(cmd));
        }
        cmdCv_.notify_one();
    }

    void AuthClient::deleteCharacterAsync(const std::string &characterId)
    {
        AuthCommand cmd;
        cmd.type = AuthCommandType::Delete;
        cmd.characterId = characterId;

        {
            std::lock_guard<std::mutex> lock(cmdMutex_);
            if (dropIfResultPending(resultMutex_, result_, "deleteCharacterAsync"))
                return;
            if (!connected_.load())
            {
                LOG_WARN("AuthClient", "deleteCharacterAsync called but not connected");
                pushTransportFailure(AuthResultType::Delete, "Auth connection lost");
                return;
            }
            if (cmdInFlight_)
            {
                LOG_WARN("AuthClient", "deleteCharacterAsync dropped: another cmd is in flight");
                return;
            }
            cmdInFlight_ = true;
            cmdQueue_.push(std::move(cmd));
        }
        cmdCv_.notify_one();
    }

    void AuthClient::selectCharacterAsync(const std::string &characterId)
    {
        AuthCommand cmd;
        cmd.type = AuthCommandType::Select;
        cmd.characterId = characterId;

        {
            std::lock_guard<std::mutex> lock(cmdMutex_);
            if (dropIfResultPending(resultMutex_, result_, "selectCharacterAsync"))
                return;
            if (!connected_.load())
            {
                LOG_WARN("AuthClient", "selectCharacterAsync called but not connected");
                pushTransportFailure(AuthResultType::Select, "Auth connection lost");
                return;
            }
            if (cmdInFlight_)
            {
                LOG_WARN("AuthClient", "selectCharacterAsync dropped: another cmd is in flight");
                return;
            }
            cmdInFlight_ = true;
            cmdQueue_.push(std::move(cmd));
        }
        cmdCv_.notify_one();
    }

    void AuthClient::disconnectAuth()
    {
        if (!worker_.joinable())
            return;

        {
            std::lock_guard<std::mutex> lock(cmdMutex_);
            AuthCommand cmd;
            cmd.type = AuthCommandType::Disconnect;
            cmdQueue_.push(std::move(cmd));
        }
        shouldStop_.store(true);
        cmdCv_.notify_all();

        if (worker_.joinable())
        {
            worker_.join();
        }
        shouldStop_.store(false);
        connected_.store(false);
        busy_.store(false);
    }

    // ---------------------------------------------------------------------------
    // performDisconnectDrainLocked
    // ---------------------------------------------------------------------------
    // Caller MUST hold cmdMutex_. Three responsibilities:
    //   1. Flip connected_=false so any subsequent enqueue takes the
    //      not-connected branch instead of pushing onto a dead queue.
    //   2. Drain orphan commands -- but only on connection-loss exits. On
    //      orderly shutdown via cleanup()/disconnectAuth(), shouldStop_ is
    //      true and the caller swap-empties cmdQueue_ itself; we don't want
    //      to publish spurious Transport failures for cmds the caller
    //      legitimately abandoned.
    //   3. Clear cmdInFlight_ unconditionally so a future reconnect's first
    //      enqueue isn't gated by a stale flag.
    //
    // Publish policy on connection-loss:
    //   - At most ONE Transport failure is published (result_ is a single
    //     std::optional slot; the new pending-result gate keeps cmdQueue_ at
    //     size <= 1 in normal flow but the loop is defensive for that).
    //   - The publish is SUPPRESSED entirely if result_ already holds an
    //     unconsumed prior result. Overwriting it would lose the legitimate
    //     response (e.g. a successful Create whose UI-visible flow hasn't
    //     run yet) in favor of an orphan-failure for a cmd that, with the
    //     pending-result gate in place, should not have been enqueued at
    //     all. Defense-in-depth: if the gate ever fails, the worse outcome
    //     (silent orphan) is preferred over the worst outcome (clobbered
    //     legitimate result).
    //   - Disconnect cmds in the queue are skipped (control messages).
    void AuthClient::performDisconnectDrainLocked()
    {
        connected_.store(false);
        if (!shouldStop_.load())
        {
            // Snapshot result_ presence under resultMutex_ so the suppression
            // decision can't race with a concurrent UI consume(). Lock order
            // cmdMutex_ -> resultMutex_ matches the enqueue gates.
            bool resultAlreadyPending;
            {
                std::lock_guard<std::mutex> rlock(resultMutex_);
                resultAlreadyPending = result_.has_value();
            }
            // Seeding published=true when a prior result exists makes the
            // loop drain the queue without publishing -- same shape as the
            // "already published our one slot" exit.
            bool published = resultAlreadyPending;
            while (!cmdQueue_.empty())
            {
                AuthCommand orphan = std::move(cmdQueue_.front());
                cmdQueue_.pop();
                if (orphan.type == AuthCommandType::Disconnect) continue;
                if (!published)
                {
                    pushTransportFailure(cmdToResultType(orphan.type),
                                         "Auth connection lost");
                    published = true;
                }
            }
        }
        cmdInFlight_ = false;
    }

    // ---------------------------------------------------------------------------
    // workerLoop --runs on background thread, maintains persistent TLS connection
    // ---------------------------------------------------------------------------
    void AuthClient::workerLoop(const std::string &host, uint16_t port,
                                const std::vector<uint8_t> &initialData)
    {
        // Every fail() callsite below corresponds to a transport / availability
        // failure (DNS, TCP, TLS, send/recv before the AuthResponse). Server-
        // returned validation errors take a separate path (see authResp.success
        // check below) and are tagged Validation.
        auto fail = [this](const std::string &reason)
        {
            AuthClientResult res;
            res.type = pendingRequestType_;
            res.success = false;
            res.failureKind = AuthFailureKind::Transport;
            res.errorMessage = reason;
            pushResult(std::move(res));
            busy_.store(false);
        };

        // --- OpenSSL context ---
        SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx)
        {
            fail("Failed to create SSL context");
            return;
        }

        // Enforce TLS 1.2+ and restrict to AEAD cipher suites
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION);
        SSL_CTX_set_cipher_list(ctx,
                                "ECDHE+AESGCM:ECDHE+CHACHA20:DHE+AESGCM:DHE+CHACHA20:!aNULL:!MD5:!DSS");
        SSL_CTX_set_ciphersuites(ctx,
                                 "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256");

#ifdef FATE_DEV_TLS
        // Dev-only: self-signed cert accepted.  Gated to non-shipping builds in CMake.
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
        LOG_WARN("AuthClient", "FATE_DEV_TLS: server certificate verification DISABLED (dev build)");
#else
        // Production path: require a valid, trusted certificate chain, and bind the
        // verification to the expected hostname so a cert issued for attacker.com
        // cannot pose as our auth server.
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
        if (SSL_CTX_set_default_verify_paths(ctx) != 1)
        {
            LOG_ERROR("AuthClient", "Failed to load system root CAs for TLS verification");
            SSL_CTX_free(ctx);
            fail("TLS init failed (missing trusted root CAs)");
            return;
        }
#endif

        // --- TCP connect via getaddrinfo (IPv4 + IPv6, DNS or literal) ---
        // Prior implementation required a dotted-quad IPv4 string, which
        // broke cert hostname verification (SNI/X509 match against an IP)
        // and prevented real DNS-based production deployments.
        uintptr_t sockFd = INVALID_SOCK_HANDLE;
        {
            addrinfo hints{};
            hints.ai_family   = AF_UNSPEC;     // IPv4 or IPv6
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;

            char portStr[16];
            std::snprintf(portStr, sizeof(portStr), "%u", static_cast<unsigned>(port));

            addrinfo* resolved = nullptr;
            int gai = ::getaddrinfo(host.c_str(), portStr, &hints, &resolved);
            if (gai != 0 || !resolved)
            {
                SSL_CTX_free(ctx);
                fail("Failed to resolve host: " + host);
                return;
            }

            for (addrinfo* ai = resolved; ai != nullptr; ai = ai->ai_next)
            {
#ifdef _WIN32
                SOCKET s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
                if (s == INVALID_SOCKET) continue;
                if (::connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0)
                {
                    sockFd = static_cast<uintptr_t>(s);
                    break;
                }
                ::closesocket(s);
#else
                int s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
                if (s < 0) continue;
                if (::connect(s, ai->ai_addr, ai->ai_addrlen) == 0)
                {
                    sockFd = static_cast<uintptr_t>(s);
                    break;
                }
                ::close(s);
#endif
            }
            ::freeaddrinfo(resolved);

            if (sockFd == INVALID_SOCK_HANDLE)
            {
                SSL_CTX_free(ctx);
                fail("SERVER DOWN FOR MAINTENANCE");
                return;
            }
        }

        // --- TLS handshake ---
        SSL *ssl = SSL_new(ctx);
        if (!ssl)
        {
            closeSocket(sockFd);
            SSL_CTX_free(ctx);
            fail("SSL_new failed");
            return;
        }

        SSL_set_fd(ssl, static_cast<int>(sockFd));

#ifndef FATE_DEV_TLS
        // Production: verify the server's certificate matches the hostname we dialed.
        // Prevents accepting a valid cert issued for a different host.
        SSL_set_tlsext_host_name(ssl, host.c_str());
        X509_VERIFY_PARAM *vp = SSL_get0_param(ssl);
        if (vp)
        {
            X509_VERIFY_PARAM_set_hostflags(vp, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
            X509_VERIFY_PARAM_set1_host(vp, host.c_str(), host.size());
        }
#endif

        if (SSL_connect(ssl) <= 0)
        {
            SSL_free(ssl);
            closeSocket(sockFd);
            SSL_CTX_free(ctx);
            fail("TLS handshake failed");
            return;
        }

        // --- Send initial login/register message ---
        if (!sslSendMessage(ssl, initialData.data(), static_cast<uint32_t>(initialData.size())))
        {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closeSocket(sockFd);
            SSL_CTX_free(ctx);
            fail("Failed to send initial auth message");
            return;
        }

        // --- Read initial AuthResponse ---
        std::vector<uint8_t> respBuf;
        if (!sslRecvMessage(ssl, respBuf))
        {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closeSocket(sockFd);
            SSL_CTX_free(ctx);
            fail("Failed to read auth response");
            return;
        }

        ByteReader reader(respBuf.data(), respBuf.size());
        uint8_t typeByte = reader.readU8();
        // Validate the opcode before decoding. A wrong-type payload could
        // otherwise be force-decoded as AuthResponse and yield a fake
        // success/validation result -- this needs to be a transport-level
        // protocol failure so the UI bounces to login.
        if (typeByte != static_cast<uint8_t>(AuthMessageType::AuthResponse))
        {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closeSocket(sockFd);
            SSL_CTX_free(ctx);
            fail("Unexpected auth response type");
            return;
        }

        AuthResponse authResp = AuthResponse::read(reader);
        if (reader.overflowed())
        {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closeSocket(sockFd);
            SSL_CTX_free(ctx);
            fail("Malformed auth response from server");
            return;
        }

        if (!authResp.success)
        {
            AuthClientResult res;
            res.type = pendingRequestType_;
            res.success = false;
            res.failureKind = AuthFailureKind::Validation;
            res.errorMessage = authResp.errorReason;
            pushResult(std::move(res));
            LOG_WARN("AuthClient", "Auth failed: %s", authResp.errorReason.c_str());
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closeSocket(sockFd);
            SSL_CTX_free(ctx);
            busy_.store(false);
            return;
        }

        LOG_INFO("AuthClient", "Auth succeeded, persistent connection established");
        // Set connected BEFORE pushing result — the main thread may call
        // selectCharacterAsync immediately after consuming the result
        connected_.store(true);
        busy_.store(false);

        // Push the initial auth result tagged with the pending request kind so the
        // main thread can route Register failures back to the char-create screen
        // and Login failures to the login screen.
        {
            AuthClientResult res;
            res.type = pendingRequestType_;
            res.success = true;
            res.characters = std::move(authResp.characters);
            pushResult(std::move(res));
        }

        // -----------------------------------------------------------------------
        // Command processing loop --waits for commands, sends/receives on the
        // persistent TLS connection
        // -----------------------------------------------------------------------
        static constexpr int KEEPALIVE_SECONDS = 120;

        while (!shouldStop_.load())
        {
            AuthCommand cmd;
            {
                std::unique_lock<std::mutex> lock(cmdMutex_);
                cmdCv_.wait_for(lock, std::chrono::seconds(KEEPALIVE_SECONDS), [this]()
                                { return !cmdQueue_.empty() || shouldStop_.load(); });

                if (shouldStop_.load())
                    break;

                if (cmdQueue_.empty())
                {
                    // Timeout — send keepalive ping to reset server's SO_RCVTIMEO
                    lock.unlock();
                    uint8_t pingMsg[] = {static_cast<uint8_t>(AuthMessageType::Ping)};
                    if (!sslSendMessage(ssl, pingMsg, 1))
                    {
                        LOG_WARN("AuthClient", "Keepalive ping failed — connection lost");
                        break;
                    }
                    continue;
                }

                cmd = std::move(cmdQueue_.front());
                cmdQueue_.pop();
            }

            if (cmd.type == AuthCommandType::Disconnect)
            {
                break;
            }

            // Serialize and send the command
            uint8_t sendBuf[2048];
            ByteWriter w(sendBuf, sizeof(sendBuf));

            switch (cmd.type)
            {
            case AuthCommandType::Create:
            {
                CharCreateRequest req;
                req.characterName = cmd.characterName;
                req.className = cmd.className;
                req.faction = cmd.faction;
                req.gender = cmd.gender;
                req.hairstyle = cmd.hairstyle;
                req.write(w);
                break;
            }
            case AuthCommandType::Delete:
            {
                CharDeleteRequest req;
                req.characterId = cmd.characterId;
                req.write(w);
                break;
            }
            case AuthCommandType::Select:
            {
                SelectCharRequest req;
                req.characterId = cmd.characterId;
                req.write(w);
                break;
            }
            default:
                // Defensive: unknown cmd type slipped past the enum-closed
                // switch above. Drop it AND clear the in-flight gate so the
                // UI isn't permanently locked out by a stuck flag.
                {
                    std::lock_guard<std::mutex> lock(cmdMutex_);
                    cmdInFlight_ = false;
                }
                continue;
            }

            if (w.overflowed())
            {
                LOG_ERROR("AuthClient", "Command serialization overflow");
                // Same as above: clear the gate before skipping this cmd.
                {
                    std::lock_guard<std::mutex> lock(cmdMutex_);
                    cmdInFlight_ = false;
                }
                continue;
            }

            // Send the message
            if (!sslSendMessage(ssl, w.data(), static_cast<uint32_t>(w.size())))
            {
                LOG_ERROR("AuthClient", "Failed to send command --connection lost");
                pushTransportFailure(cmdToResultType(cmd.type),
                                     "Auth connection lost");
                break;
            }

            // Read the response
            respBuf.clear();
            if (!sslRecvMessage(ssl, respBuf))
            {
                LOG_ERROR("AuthClient", "Failed to read response --connection lost");
                pushTransportFailure(cmdToResultType(cmd.type),
                                     "Auth connection lost");
                break;
            }

            // Parse based on command type
            ByteReader rdr(respBuf.data(), respBuf.size());
            uint8_t respType = rdr.readU8();

            // Validate the opcode matches the request kind before decoding.
            // A mismatched type means the server returned the wrong message
            // shape (or the stream desynced) -- decoding the wrong struct
            // would produce a fake validation/success result. Surface as a
            // transport / protocol failure so the UI bounces to login.
            uint8_t expectedRespType = 0;
            switch (cmd.type)
            {
            case AuthCommandType::Create:
                expectedRespType = static_cast<uint8_t>(AuthMessageType::CharCreateResponse);
                break;
            case AuthCommandType::Delete:
                expectedRespType = static_cast<uint8_t>(AuthMessageType::CharDeleteResponse);
                break;
            case AuthCommandType::Select:
                expectedRespType = static_cast<uint8_t>(AuthMessageType::SelectCharResponse);
                break;
            default:
                break;
            }
            if (respType != expectedRespType)
            {
                LOG_ERROR("AuthClient",
                          "Unexpected response type %u (expected %u)",
                          static_cast<unsigned>(respType),
                          static_cast<unsigned>(expectedRespType));
                pushTransportFailure(cmdToResultType(cmd.type),
                                     "Unexpected response type");
                break;
            }

            // Tracks whether a malformed-response branch fired. The stream is
            // desynced once a response fails to decode -- the next sslRecvMessage
            // would either read garbage as a length prefix (most likely return
            // false) or, worse, succeed and decode the wrong shape into the next
            // cmd's response. Tear down the connection after publishing the
            // Transport failure rather than continuing the cmd loop, matching
            // the "Unexpected response type" branch above.
            bool malformedFailure = false;
            switch (cmd.type)
            {
            case AuthCommandType::Create:
            {
                CharCreateResponse resp = CharCreateResponse::read(rdr);
                if (!rdr.overflowed())
                {
                    AuthClientResult res;
                    res.type = AuthResultType::Create;
                    res.success = resp.success;
                    res.failureKind = resp.success ? AuthFailureKind::None
                                                   : AuthFailureKind::Validation;
                    res.errorMessage = resp.errorMessage;
                    res.characters = std::move(resp.characters);
                    pushResult(std::move(res));
                }
                else
                {
                    LOG_ERROR("AuthClient", "Malformed CharCreateResponse");
                    pushTransportFailure(AuthResultType::Create,
                                         "Malformed server response");
                    malformedFailure = true;
                }
                break;
            }
            case AuthCommandType::Delete:
            {
                CharDeleteResponse resp = CharDeleteResponse::read(rdr);
                if (!rdr.overflowed())
                {
                    AuthClientResult res;
                    res.type = AuthResultType::Delete;
                    res.success = resp.success;
                    res.failureKind = resp.success ? AuthFailureKind::None
                                                   : AuthFailureKind::Validation;
                    res.errorMessage = resp.errorMessage;
                    res.characters = std::move(resp.characters);
                    pushResult(std::move(res));
                }
                else
                {
                    LOG_ERROR("AuthClient", "Malformed CharDeleteResponse");
                    pushTransportFailure(AuthResultType::Delete,
                                         "Malformed server response");
                    malformedFailure = true;
                }
                break;
            }
            case AuthCommandType::Select:
            {
                SelectCharResponse resp = SelectCharResponse::read(rdr);
                if (!rdr.overflowed())
                {
                    AuthClientResult res;
                    res.type = AuthResultType::Select;
                    res.success = resp.success;
                    res.failureKind = resp.success ? AuthFailureKind::None
                                                   : AuthFailureKind::Validation;
                    res.errorMessage = resp.errorMessage;
                    res.selectData = std::move(resp);
                    pushResult(std::move(res));
                }
                else
                {
                    LOG_ERROR("AuthClient", "Malformed SelectCharResponse");
                    pushTransportFailure(AuthResultType::Select,
                                         "Malformed server response");
                    malformedFailure = true;
                }
                break;
            }
            default:
                break;
            }

            // Cmd processing complete (success / validation reject / malformed
            // response). Clear the in-flight gate so the next cmd from the UI
            // can enqueue. Send/recv failure paths above break out of the loop
            // and rely on the drain block below to clear this.
            {
                std::lock_guard<std::mutex> lock(cmdMutex_);
                cmdInFlight_ = false;
            }

            // Malformed responses indicate a desynced stream -- continuing the
            // loop would risk decoding garbage as a valid response on the next
            // cmd. Break out so the drain block + SSL_shutdown below close the
            // connection cleanly. The Transport failure was already pushed
            // above; the drain will see result_ pending and suppress publishing
            // any further orphan failure.
            if (malformedFailure) break;
        }

        // -----------------------------------------------------------------------
        // Atomic disconnect transition. Pairs with the create/delete/select
        // enqueue gates so a main-thread command can never observe
        // connected_=true, push a cmd, and then have the worker exit without
        // ever responding. See performDisconnectDrainLocked() for the policy
        // (skip on orderly shutdown, never overwrite an unconsumed result).
        {
            std::lock_guard<std::mutex> lock(cmdMutex_);
            performDisconnectDrainLocked();
        }

        // -----------------------------------------------------------------------
        // Cleanup --tear down TLS and socket
        // -----------------------------------------------------------------------
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closeSocket(sockFd);
        SSL_CTX_free(ctx);
        busy_.store(false);

        LOG_INFO("AuthClient", "Persistent auth connection closed");
    }

    // ---------------------------------------------------------------------------
    // Main-thread polling
    // ---------------------------------------------------------------------------
    bool AuthClient::hasResult() const
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        return result_.has_value();
    }

    AuthClientResult AuthClient::consumeResult()
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        AuthClientResult res = std::move(result_.value());
        result_.reset();
        return res;
    }

#endif // FATE_HAS_OPENSSL

} // namespace fate
