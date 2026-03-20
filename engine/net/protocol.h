#pragma once
#include <cstdint>
#include <string>
#include "engine/net/byte_stream.h"
#include "engine/core/types.h"

namespace fate {

// ============================================================================
// Helper: int64 serialization via two U32s (low then high)
// ============================================================================
namespace detail {
    inline void writeI64(ByteWriter& w, int64_t val) {
        w.writeU32(static_cast<uint32_t>(val & 0xFFFFFFFF));
        w.writeU32(static_cast<uint32_t>(static_cast<uint64_t>(val) >> 32));
    }
    inline int64_t readI64(ByteReader& r) {
        uint32_t lo = r.readU32();
        uint32_t hi = r.readU32();
        return static_cast<int64_t>(static_cast<uint64_t>(hi) << 32 | lo);
    }
    inline void writeU64(ByteWriter& w, uint64_t val) {
        w.writeU32(static_cast<uint32_t>(val & 0xFFFFFFFF));
        w.writeU32(static_cast<uint32_t>(val >> 32));
    }
    inline uint64_t readU64(ByteReader& r) {
        uint32_t lo = r.readU32();
        uint32_t hi = r.readU32();
        return static_cast<uint64_t>(hi) << 32 | lo;
    }
} // namespace detail

// ============================================================================
// Client -> Server Messages
// ============================================================================

struct CmdMove {
    Vec2  position;
    Vec2  velocity;
    float timestamp = 0.0f;

    void write(ByteWriter& w) const {
        w.writeVec2(position);
        w.writeVec2(velocity);
        w.writeFloat(timestamp);
    }

    static CmdMove read(ByteReader& r) {
        CmdMove m;
        m.position  = r.readVec2();
        m.velocity  = r.readVec2();
        m.timestamp = r.readFloat();
        return m;
    }
};

struct CmdAction {
    uint8_t  actionType = 0; // 0=attack, 1=skill, 2=interact, 3=pickup
    uint64_t targetId   = 0; // PersistentId
    uint16_t skillId    = 0;

    void write(ByteWriter& w) const {
        w.writeU8(actionType);
        detail::writeU64(w, targetId);
        w.writeU16(skillId);
    }

    static CmdAction read(ByteReader& r) {
        CmdAction m;
        m.actionType = r.readU8();
        m.targetId   = detail::readU64(r);
        m.skillId    = r.readU16();
        return m;
    }
};

struct CmdChat {
    uint8_t     channel = 0;
    std::string message;
    std::string targetName;

    void write(ByteWriter& w) const {
        w.writeU8(channel);
        w.writeString(message);
        w.writeString(targetName);
    }

    static CmdChat read(ByteReader& r) {
        CmdChat m;
        m.channel    = r.readU8();
        m.message    = r.readString();
        m.targetName = r.readString();
        return m;
    }
};

// ============================================================================
// Server -> Client Messages
// ============================================================================

struct SvEntityEnterMsg {
    uint64_t    persistentId = 0;
    uint8_t     entityType   = 0; // 0=player, 1=mob, 2=npc, 3=dropped_item
    Vec2        position;
    std::string name;
    int32_t     level     = 0;
    int32_t     currentHP = 0;
    int32_t     maxHP     = 0;
    uint8_t     faction   = 0;

    // Dropped item fields (only serialized when entityType == 3)
    std::string itemId;
    int32_t     quantity     = 0;
    uint8_t     isGold       = 0;
    int32_t     goldAmount   = 0;
    int32_t     enchantLevel = 0;
    std::string rarity;

    // Mob fields (only serialized when entityType == 1)
    std::string mobDefId;
    uint8_t     isBoss = 0;

    void write(ByteWriter& w) const {
        detail::writeU64(w, persistentId);
        w.writeU8(entityType);
        w.writeVec2(position);
        w.writeString(name);
        w.writeI32(level);
        w.writeI32(currentHP);
        w.writeI32(maxHP);
        w.writeU8(faction);
        if (entityType == 3) {
            w.writeString(itemId);
            w.writeI32(quantity);
            w.writeU8(isGold);
            w.writeI32(goldAmount);
            w.writeI32(enchantLevel);
            w.writeString(rarity);
        }
        if (entityType == 1) {
            w.writeString(mobDefId);
            w.writeU8(isBoss);
        }
    }

    static SvEntityEnterMsg read(ByteReader& r) {
        SvEntityEnterMsg m;
        m.persistentId = detail::readU64(r);
        m.entityType   = r.readU8();
        m.position     = r.readVec2();
        m.name         = r.readString();
        m.level        = r.readI32();
        m.currentHP    = r.readI32();
        m.maxHP        = r.readI32();
        m.faction      = r.readU8();
        if (m.entityType == 3) {
            m.itemId       = r.readString();
            m.quantity     = r.readI32();
            m.isGold       = r.readU8();
            m.goldAmount   = r.readI32();
            m.enchantLevel = r.readI32();
            m.rarity       = r.readString();
        }
        if (m.entityType == 1) {
            m.mobDefId = r.readString();
            m.isBoss   = r.readU8();
        }
        return m;
    }
};

struct SvEntityLeaveMsg {
    uint64_t persistentId = 0;

    void write(ByteWriter& w) const {
        detail::writeU64(w, persistentId);
    }

    static SvEntityLeaveMsg read(ByteReader& r) {
        SvEntityLeaveMsg m;
        m.persistentId = detail::readU64(r);
        return m;
    }
};

struct SvEntityUpdateMsg {
    uint64_t persistentId = 0;
    uint16_t fieldMask    = 0;

    // Conditional fields
    Vec2    position;           // bit 0
    uint8_t animFrame = 0;     // bit 1
    uint8_t flipX     = 0;     // bit 2 (bool as u8)
    int32_t currentHP = 0;     // bit 3
    uint8_t updateSeq = 0;      // monotonic per-entity, for stale update rejection

    void write(ByteWriter& w) const {
        w.writeU8(updateSeq);
        detail::writeU64(w, persistentId);
        w.writeU16(fieldMask);
        if (fieldMask & (1 << 0)) w.writeVec2(position);
        if (fieldMask & (1 << 1)) w.writeU8(animFrame);
        if (fieldMask & (1 << 2)) w.writeU8(flipX);
        if (fieldMask & (1 << 3)) w.writeI32(currentHP);
    }

    static SvEntityUpdateMsg read(ByteReader& r) {
        SvEntityUpdateMsg m;
        m.updateSeq    = r.readU8();
        m.persistentId = detail::readU64(r);
        m.fieldMask    = r.readU16();
        if (m.fieldMask & (1 << 0)) m.position  = r.readVec2();
        if (m.fieldMask & (1 << 1)) m.animFrame = r.readU8();
        if (m.fieldMask & (1 << 2)) m.flipX     = r.readU8();
        if (m.fieldMask & (1 << 3)) m.currentHP = r.readI32();
        return m;
    }
};

struct SvCombatEventMsg {
    uint64_t attackerId = 0;
    uint64_t targetId   = 0;
    int32_t  damage     = 0;
    uint16_t skillId    = 0;
    uint8_t  isCrit     = 0; // bool as u8
    uint8_t  isKill     = 0; // bool as u8

    void write(ByteWriter& w) const {
        detail::writeU64(w, attackerId);
        detail::writeU64(w, targetId);
        w.writeI32(damage);
        w.writeU16(skillId);
        w.writeU8(isCrit);
        w.writeU8(isKill);
    }

    static SvCombatEventMsg read(ByteReader& r) {
        SvCombatEventMsg m;
        m.attackerId = detail::readU64(r);
        m.targetId   = detail::readU64(r);
        m.damage     = r.readI32();
        m.skillId    = r.readU16();
        m.isCrit     = r.readU8();
        m.isKill     = r.readU8();
        return m;
    }
};

struct SvChatMessageMsg {
    uint8_t     channel = 0;
    std::string senderName;
    std::string message;
    uint8_t     faction = 0;

    void write(ByteWriter& w) const {
        w.writeU8(channel);
        w.writeString(senderName);
        w.writeString(message);
        w.writeU8(faction);
    }

    static SvChatMessageMsg read(ByteReader& r) {
        SvChatMessageMsg m;
        m.channel    = r.readU8();
        m.senderName = r.readString();
        m.message    = r.readString();
        m.faction    = r.readU8();
        return m;
    }
};

struct SvPlayerStateMsg {
    int32_t currentHP   = 0;
    int32_t maxHP       = 0;
    int32_t currentMP   = 0;
    int32_t maxMP       = 0;
    float   currentFury = 0.0f;
    int64_t currentXP   = 0;
    int64_t gold        = 0;
    int32_t level       = 0;

    void write(ByteWriter& w) const {
        w.writeI32(currentHP);
        w.writeI32(maxHP);
        w.writeI32(currentMP);
        w.writeI32(maxMP);
        w.writeFloat(currentFury);
        detail::writeI64(w, currentXP);
        detail::writeI64(w, gold);
        w.writeI32(level);
    }

    static SvPlayerStateMsg read(ByteReader& r) {
        SvPlayerStateMsg m;
        m.currentHP   = r.readI32();
        m.maxHP       = r.readI32();
        m.currentMP   = r.readI32();
        m.maxMP       = r.readI32();
        m.currentFury = r.readFloat();
        m.currentXP   = detail::readI64(r);
        m.gold        = detail::readI64(r);
        m.level       = r.readI32();
        return m;
    }
};

struct SvMovementCorrectionMsg {
    Vec2    correctedPosition;
    uint8_t rubberBand = 0; // bool as u8

    void write(ByteWriter& w) const {
        w.writeVec2(correctedPosition);
        w.writeU8(rubberBand);
    }

    static SvMovementCorrectionMsg read(ByteReader& r) {
        SvMovementCorrectionMsg m;
        m.correctedPosition = r.readVec2();
        m.rubberBand        = r.readU8();
        return m;
    }
};

struct SvLootPickupMsg {
    std::string itemId;
    std::string displayName;
    int32_t     quantity   = 0;
    uint8_t     isGold     = 0;
    int32_t     goldAmount = 0;
    std::string rarity;

    void write(ByteWriter& w) const {
        w.writeString(itemId);
        w.writeString(displayName);
        w.writeI32(quantity);
        w.writeU8(isGold);
        w.writeI32(goldAmount);
        w.writeString(rarity);
    }

    static SvLootPickupMsg read(ByteReader& r) {
        SvLootPickupMsg m;
        m.itemId      = r.readString();
        m.displayName = r.readString();
        m.quantity    = r.readI32();
        m.isGold      = r.readU8();
        m.goldAmount  = r.readI32();
        m.rarity      = r.readString();
        return m;
    }
};

} // namespace fate
