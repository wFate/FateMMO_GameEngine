#pragma once
#include <array>
#include <string>
#include <cstdint>
#include <cstring>
#include <vector>
#include "engine/net/byte_stream.h"
#include "engine/net/protocol.h"
#include "engine/net/packet_crypto.h"
#ifdef FATE_HAS_GAME
#include "game/shared/game_types.h"
#else
namespace fate {
enum class ClassType : uint8_t { Warrior = 0, Mage = 1, Archer = 2, Any = 255 };
enum class AdminRole : int { Player = 0, GM = 1, Admin = 2 };
}
#endif

namespace fate {

// ============================================================================
// Auth Token — 128-bit random value, TCP-only
// Distinct from the 32-bit UDP session token in PacketHeader
// ============================================================================
using AuthToken = std::array<uint8_t, 16>;

inline AuthToken generateAuthToken() {
    AuthToken token;
    PacketCrypto::randomBytes(token.data(), token.size());
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
    RegisterRequest       = 1,
    LoginRequest          = 2,
    AuthResponse          = 3,
    CharCreateRequest     = 4,
    CharCreateResponse    = 5,
    CharDeleteRequest     = 6,
    CharDeleteResponse    = 7,
    SelectCharRequest     = 8,
    SelectCharResponse    = 9,
    Ping                  = 10,
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
    uint8_t faction = 0;
    uint8_t gender = 0;
    uint8_t hairstyle = 0;
    uint16_t clientVersion = 0;

    void write(ByteWriter& w) const {
        w.writeU8(static_cast<uint8_t>(AuthMessageType::RegisterRequest));
        w.writeString(username);
        w.writeString(password);
        w.writeString(email);
        w.writeString(characterName);
        w.writeString(className);
        w.writeU8(faction);
        w.writeU8(gender);
        w.writeU8(hairstyle);
        w.writeU16(clientVersion);
    }

    static RegisterRequest read(ByteReader& r) {
        RegisterRequest m;
        m.username      = r.readString();
        m.password      = r.readString();
        m.email         = r.readString();
        m.characterName = r.readString();
        m.className     = r.readString();
        m.faction       = r.readU8();
        m.gender        = r.readU8();
        m.hairstyle     = r.readU8();
        m.clientVersion = r.readU16();
        return m;
    }
};

struct LoginRequest {
    std::string username;
    std::string password;
    uint16_t clientVersion = 0;

    void write(ByteWriter& w) const {
        w.writeU8(static_cast<uint8_t>(AuthMessageType::LoginRequest));
        w.writeString(username);
        w.writeString(password);
        w.writeU16(clientVersion);
    }

    static LoginRequest read(ByteReader& r) {
        LoginRequest m;
        m.username = r.readString();
        m.password = r.readString();
        m.clientVersion = r.readU16();
        return m;
    }
};

// ============================================================================
// Character Preview — lightweight summary for character select screen
// ============================================================================
struct CharacterPreview {
    std::string characterId;
    std::string characterName;
    std::string className;
    int32_t level = 1;
    uint8_t faction = 0;
    uint8_t gender = 0;
    uint8_t hairstyle = 0;
    std::string weaponStyle;
    std::string armorStyle;
    std::string hatStyle;

    void write(ByteWriter& w) const {
        w.writeString(characterId);
        w.writeString(characterName);
        w.writeString(className);
        w.writeI32(level);
        w.writeU8(faction);
        w.writeU8(gender);
        w.writeU8(hairstyle);
        w.writeString(weaponStyle);
        w.writeString(armorStyle);
        w.writeString(hatStyle);
    }
    static CharacterPreview read(ByteReader& r) {
        CharacterPreview p;
        p.characterId   = r.readString();
        p.characterName = r.readString();
        p.className     = r.readString();
        p.level         = r.readI32();
        p.faction       = r.readU8();
        p.gender        = r.readU8();
        p.hairstyle     = r.readU8();
        p.weaponStyle = r.readString();
        p.armorStyle  = r.readString();
        p.hatStyle    = r.readString();
        return p;
    }
    static void writeList(ByteWriter& w, const std::vector<CharacterPreview>& list) {
        w.writeU8(static_cast<uint8_t>(list.size()));
        for (const auto& p : list) p.write(w);
    }
    static std::vector<CharacterPreview> readList(ByteReader& r) {
        uint8_t count = r.readU8();
        std::vector<CharacterPreview> list;
        list.reserve(count);
        for (uint8_t i = 0; i < count; ++i)
            list.push_back(CharacterPreview::read(r));
        return list;
    }
};

// AuthResponse writes the type byte; client reads type byte separately.
// On success, carries a list of character previews (multi-char login).
struct AuthResponse {
    bool success = false;
    std::string errorReason;
    std::vector<CharacterPreview> characters;

    void write(ByteWriter& w) const {
        w.writeU8(static_cast<uint8_t>(AuthMessageType::AuthResponse));
        w.writeU8(success ? 1 : 0);
        if (!success) {
            w.writeString(errorReason);
        } else {
            CharacterPreview::writeList(w, characters);
        }
    }

    static AuthResponse read(ByteReader& r) {
        AuthResponse resp;
        resp.success = r.readU8() != 0;
        if (!resp.success) {
            resp.errorReason = r.readString();
        } else {
            resp.characters = CharacterPreview::readList(r);
        }
        return resp;
    }
};

// ============================================================================
// Character Create / Delete / Select Messages
// ============================================================================
struct CharCreateRequest {
    std::string characterName;
    std::string className;
    uint8_t faction = 0;
    uint8_t gender = 0;
    uint8_t hairstyle = 0;

    void write(ByteWriter& w) const {
        w.writeU8(static_cast<uint8_t>(AuthMessageType::CharCreateRequest));
        w.writeString(characterName);
        w.writeString(className);
        w.writeU8(faction);
        w.writeU8(gender);
        w.writeU8(hairstyle);
    }

    static CharCreateRequest read(ByteReader& r) {
        CharCreateRequest m;
        m.characterName = r.readString();
        m.className     = r.readString();
        m.faction       = r.readU8();
        m.gender        = r.readU8();
        m.hairstyle     = r.readU8();
        return m;
    }
};

struct CharCreateResponse {
    bool success = false;
    std::string errorMessage;
    std::vector<CharacterPreview> characters;

    void write(ByteWriter& w) const {
        w.writeU8(static_cast<uint8_t>(AuthMessageType::CharCreateResponse));
        w.writeU8(success ? 1 : 0);
        if (!success) {
            w.writeString(errorMessage);
        } else {
            CharacterPreview::writeList(w, characters);
        }
    }

    static CharCreateResponse read(ByteReader& r) {
        CharCreateResponse m;
        m.success = r.readU8() != 0;
        if (!m.success) {
            m.errorMessage = r.readString();
        } else {
            m.characters = CharacterPreview::readList(r);
        }
        return m;
    }
};

struct CharDeleteRequest {
    std::string characterId;

    void write(ByteWriter& w) const {
        w.writeU8(static_cast<uint8_t>(AuthMessageType::CharDeleteRequest));
        w.writeString(characterId);
    }

    static CharDeleteRequest read(ByteReader& r) {
        CharDeleteRequest m;
        m.characterId = r.readString();
        return m;
    }
};

struct CharDeleteResponse {
    bool success = false;
    std::string errorMessage;
    std::vector<CharacterPreview> characters;

    void write(ByteWriter& w) const {
        w.writeU8(static_cast<uint8_t>(AuthMessageType::CharDeleteResponse));
        w.writeU8(success ? 1 : 0);
        if (!success) {
            w.writeString(errorMessage);
        } else {
            CharacterPreview::writeList(w, characters);
        }
    }

    static CharDeleteResponse read(ByteReader& r) {
        CharDeleteResponse m;
        m.success = r.readU8() != 0;
        if (!m.success) {
            m.errorMessage = r.readString();
        } else {
            m.characters = CharacterPreview::readList(r);
        }
        return m;
    }
};

struct SelectCharRequest {
    std::string characterId;

    void write(ByteWriter& w) const {
        w.writeU8(static_cast<uint8_t>(AuthMessageType::SelectCharRequest));
        w.writeString(characterId);
    }

    static SelectCharRequest read(ByteReader& r) {
        SelectCharRequest m;
        m.characterId = r.readString();
        return m;
    }
};

struct SelectCharResponse {
    bool success = false;
    std::string errorMessage;

    // Full character snapshot — only meaningful on success
    AuthToken authToken = {};
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
        w.writeU8(static_cast<uint8_t>(AuthMessageType::SelectCharResponse));
        w.writeU8(success ? 1 : 0);
        if (!success) {
            w.writeString(errorMessage);
        } else {
            w.writeBytes(authToken.data(), 16);
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

    static SelectCharResponse read(ByteReader& r) {
        SelectCharResponse m;
        m.success = r.readU8() != 0;
        if (!m.success) {
            m.errorMessage = r.readString();
        } else {
            r.readBytes(m.authToken.data(), 16);
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
