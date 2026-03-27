#pragma once
#include <array>
#include <string>
#include <cstdint>
#include <cstring>
#include <random>
#include "engine/net/byte_stream.h"
#include "engine/net/protocol.h"
#include "game/shared/game_types.h"

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
        if (s.empty() || s.size() > 10) return false;
        for (char c : s) {
            if (!std::isalnum(static_cast<unsigned char>(c))) return false;
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

    // Full character snapshot — client uses this to initialize without waiting for SvPlayerState
    std::string characterName;
    std::string className;
    std::string sceneName;
    float spawnX = 0.0f;
    float spawnY = 0.0f;

    // Progression
    int32_t level = 1;
    int64_t currentXP = 0;
    int64_t gold = 0;

    // Vitals
    int32_t currentHP = 100;
    int32_t maxHP = 100;
    int32_t currentMP = 50;
    int32_t maxMP = 50;
    float   currentFury = 0.0f;

    // PvP
    int32_t honor = 0;
    int32_t pvpKills = 0;
    int32_t pvpDeaths = 0;

    // State
    uint8_t isDead = 0;
    uint8_t faction = 0;

    void write(ByteWriter& w) const {
        w.writeU8(success ? 1 : 0);
        w.writeBytes(authToken.data(), 16);
        w.writeString(errorReason);
        if (success) {
            w.writeString(characterName);
            w.writeString(className);
            w.writeString(sceneName);
            w.writeFloat(spawnX);
            w.writeFloat(spawnY);
            w.writeI32(level);
            detail::writeI64(w, currentXP);
            detail::writeI64(w, gold);
            w.writeI32(currentHP);
            w.writeI32(maxHP);
            w.writeI32(currentMP);
            w.writeI32(maxMP);
            w.writeFloat(currentFury);
            w.writeI32(honor);
            w.writeI32(pvpKills);
            w.writeI32(pvpDeaths);
            w.writeU8(isDead);
            w.writeU8(faction);
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
            m.sceneName     = r.readString();
            m.spawnX        = r.readFloat();
            m.spawnY        = r.readFloat();
            m.level         = r.readI32();
            m.currentXP     = detail::readI64(r);
            m.gold          = detail::readI64(r);
            m.currentHP     = r.readI32();
            m.maxHP         = r.readI32();
            m.currentMP     = r.readI32();
            m.maxMP         = r.readI32();
            m.currentFury   = r.readFloat();
            m.honor         = r.readI32();
            m.pvpKills      = r.readI32();
            m.pvpDeaths     = r.readI32();
            m.isDead        = r.readU8();
            m.faction       = r.readU8();
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
    AdminRole admin_role = AdminRole::Player;
};

} // namespace fate
