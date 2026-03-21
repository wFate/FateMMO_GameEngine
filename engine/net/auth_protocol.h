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

    static bool isValidEmail(const std::string& s) {
        if (s.size() < 5 || s.size() > 128) return false;
        auto at = s.find('@');
        if (at == std::string::npos || at == 0 || at == s.size() - 1) return false;
        auto dot = s.find('.', at);
        if (dot == std::string::npos || dot == s.size() - 1) return false;
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

// RegisterRequest and LoginRequest write a type byte first.
// The server reads the type byte to determine which message to parse,
// then calls read() which reads only the fields (not the type byte).

struct RegisterRequest {
    std::string username;
    std::string password;
    std::string email;
    std::string characterName;
    std::string className;

    void write(ByteWriter& w) const {
        w.writeU8(static_cast<uint8_t>(AuthMessageType::RegisterRequest));
        w.writeString(username);
        w.writeString(password);
        w.writeString(email);
        w.writeString(characterName);
        w.writeString(className);
    }

    static RegisterRequest read(ByteReader& r) {
        RegisterRequest m;
        m.username      = r.readString();
        m.password      = r.readString();
        m.email         = r.readString();
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

// AuthResponse does NOT write a type byte — the client always knows
// the response follows a request. Preview data only meaningful on success.
struct AuthResponse {
    bool success = false;
    AuthToken authToken = {};
    std::string errorReason;
    // Preview data (only meaningful on login success — for UI display during transition)
    std::string characterName;
    std::string className;
    int32_t level = 0;
    float spawnX = 0.0f;  // pixel coords — saved position from DB
    float spawnY = 0.0f;
    std::string sceneName;  // scene the player should load into

    void write(ByteWriter& w) const {
        w.writeU8(success ? 1 : 0);
        w.writeBytes(authToken.data(), 16);
        w.writeString(errorReason);
        if (success) {
            w.writeString(characterName);
            w.writeString(className);
            w.writeI32(level);
            w.writeFloat(spawnX);
            w.writeFloat(spawnY);
            w.writeString(sceneName);
        }
    }

    static AuthResponse read(ByteReader& r) {
        AuthResponse m;
        m.success = r.readU8() != 0;
        r.readBytes(m.authToken.data(), 16);
        m.errorReason = r.readString();
        if (m.success) {
            m.characterName = r.readString();
            m.className     = r.readString();
            m.level         = r.readI32();
            m.spawnX        = r.readFloat();
            m.spawnY        = r.readFloat();
            m.sceneName     = r.readString();
        }
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
