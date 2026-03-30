#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
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

    // Player fields (only serialized when entityType == 0)
    uint8_t pkStatus  = 0; // PKStatus enum (only for entityType == 0, player)
    uint8_t honorRank = 0; // HonorRank enum (only for entityType == 0, player)

    // Player appearance (only serialized when entityType == 0)
    uint8_t gender    = 0; // 0=male, 1=female
    uint8_t hairstyle = 0; // 0-2 per gender
    uint64_t costumeVisuals = 0; // packed costume visual indices (only for entityType == 0)

    void write(ByteWriter& w) const {
        detail::writeU64(w, persistentId);
        w.writeU8(entityType);
        w.writeVec2(position);
        w.writeString(name);
        w.writeI32(level);
        w.writeI32(currentHP);
        w.writeI32(maxHP);
        w.writeU8(faction);
        if (entityType == 0) { // player-specific fields
            w.writeU8(pkStatus);
            w.writeU8(honorRank);
            w.writeU8(gender);
            w.writeU8(hairstyle);
            detail::writeU64(w, costumeVisuals);
        }
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
        if (m.entityType == 0) {
            m.pkStatus  = r.readU8();
            m.honorRank = r.readU8();
            m.gender    = r.readU8();
            m.hairstyle = r.readU8();
            m.costumeVisuals = detail::readU64(r);
        }
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
    uint32_t fieldMask    = 0;

    // Bit 0: position (Vec2, 8B)
    Vec2    position;
    // Bit 1: animFrame (uint8, 1B)
    uint8_t animFrame = 0;
    // Bit 2: flipX (uint8, 1B)
    uint8_t flipX     = 0;
    // Bit 3: currentHP (int32, 4B)
    int32_t currentHP = 0;
    // Bit 4: maxHP (int32, 4B)
    int32_t maxHP = 0;
    // Bit 5: moveState (uint8, 1B) — idle/walk/dead/sitting
    uint8_t moveState = 0;
    // Bit 6: animId (uint16, 2B) — current skill/animation ID
    uint16_t animId = 0;
    // Bit 7: statusEffectMask (uint32, 4B) — bitfield of up to 32 status effects
    uint32_t statusEffectMask = 0;
    // Bit 8: deathState (uint8, 1B) — alive/dying/dead/ghost
    uint8_t deathState = 0;
    // Bit 9: castingSkillId (uint16, 2B) + castingProgress (uint8, 1B)
    uint16_t castingSkillId = 0;
    uint8_t  castingProgress = 0; // 0-255 maps to 0-100%
    // Bit 10: targetEntityId (uint16, 2B)
    uint16_t targetEntityId = 0;
    // Bit 11: level (uint8, 1B)
    uint8_t level = 0;
    // Bit 12: faction (uint8, 1B)
    uint8_t faction = 0;
    // Bit 13: equipVisuals (uint32, 4B) — packed sprite/palette IDs
    uint32_t equipVisuals = 0;
    // Bit 14: pkStatus (uint8, 1B) — PK name color (0=White, 1=Purple, 2=Red, 3=Black)
    uint8_t pkStatus = 0;
    // Bit 15: honorRank (uint8, 1B) — HonorRank enum
    uint8_t honorRank = 0;
    // Bit 16: costumeVisuals (uint64, 8B) — packed costume sprite indices (6 slots × 10 bits)
    uint64_t costumeVisuals = 0;

    uint8_t updateSeq = 0;

    void write(ByteWriter& w) const {
        w.writeU8(updateSeq);
        detail::writeU64(w, persistentId);
        w.writeU32(fieldMask);
        if (fieldMask & (1 << 0))  w.writeVec2(position);
        if (fieldMask & (1 << 1))  w.writeU8(animFrame);
        if (fieldMask & (1 << 2))  w.writeU8(flipX);
        if (fieldMask & (1 << 3))  w.writeI32(currentHP);
        if (fieldMask & (1 << 4))  w.writeI32(maxHP);
        if (fieldMask & (1 << 5))  w.writeU8(moveState);
        if (fieldMask & (1 << 6))  w.writeU16(animId);
        if (fieldMask & (1 << 7))  w.writeU32(statusEffectMask);
        if (fieldMask & (1 << 8))  w.writeU8(deathState);
        if (fieldMask & (1 << 9))  { w.writeU16(castingSkillId); w.writeU8(castingProgress); }
        if (fieldMask & (1 << 10)) w.writeU16(targetEntityId);
        if (fieldMask & (1 << 11)) w.writeU8(level);
        if (fieldMask & (1 << 12)) w.writeU8(faction);
        if (fieldMask & (1 << 13)) w.writeU32(equipVisuals);
        if (fieldMask & (1 << 14)) w.writeU8(pkStatus);
        if (fieldMask & (1 << 15)) w.writeU8(honorRank);
        if (fieldMask & (1 << 16)) detail::writeU64(w, costumeVisuals);
    }

    static SvEntityUpdateMsg read(ByteReader& r) {
        SvEntityUpdateMsg m;
        m.updateSeq    = r.readU8();
        m.persistentId = detail::readU64(r);
        m.fieldMask    = r.readU32();
        if (m.fieldMask & (1 << 0))  m.position  = r.readVec2();
        if (m.fieldMask & (1 << 1))  m.animFrame = r.readU8();
        if (m.fieldMask & (1 << 2))  m.flipX     = r.readU8();
        if (m.fieldMask & (1 << 3))  m.currentHP = r.readI32();
        if (m.fieldMask & (1 << 4))  m.maxHP     = r.readI32();
        if (m.fieldMask & (1 << 5))  m.moveState = r.readU8();
        if (m.fieldMask & (1 << 6))  m.animId    = r.readU16();
        if (m.fieldMask & (1 << 7))  m.statusEffectMask = r.readU32();
        if (m.fieldMask & (1 << 8))  m.deathState = r.readU8();
        if (m.fieldMask & (1 << 9))  { m.castingSkillId = r.readU16(); m.castingProgress = r.readU8(); }
        if (m.fieldMask & (1 << 10)) m.targetEntityId = r.readU16();
        if (m.fieldMask & (1 << 11)) m.level = r.readU8();
        if (m.fieldMask & (1 << 12)) m.faction = r.readU8();
        if (m.fieldMask & (1 << 13)) m.equipVisuals = r.readU32();
        if (m.fieldMask & (1 << 14)) m.pkStatus  = r.readU8();
        if (m.fieldMask & (1 << 15)) m.honorRank = r.readU8();
        if (m.fieldMask & (1 << 16)) m.costumeVisuals = detail::readU64(r);
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
    int32_t honor       = 0;
    int32_t pvpKills    = 0;
    int32_t pvpDeaths   = 0;

    // Derived stats (server-authoritative snapshot)
    int32_t armor        = 0;
    int32_t magicResist  = 0;
    float   critRate     = 0.0f;
    float   hitRate      = 0.0f;
    float   evasion      = 0.0f;
    float   speed        = 1.0f;
    float   damageMult   = 1.0f;
    uint8_t pkStatus     = 0; // PKStatus enum
    uint8_t honorRank    = 0; // HonorRank enum

    // Stat allocation
    int16_t freeStatPoints = 0;
    int16_t allocatedSTR = 0;
    int16_t allocatedINT = 0;
    int16_t allocatedDEX = 0;
    int16_t allocatedCON = 0;
    int16_t allocatedWIS = 0;

    void write(ByteWriter& w) const {
        w.writeI32(currentHP);
        w.writeI32(maxHP);
        w.writeI32(currentMP);
        w.writeI32(maxMP);
        w.writeFloat(currentFury);
        detail::writeI64(w, currentXP);
        detail::writeI64(w, gold);
        w.writeI32(level);
        w.writeI32(honor);
        w.writeI32(pvpKills);
        w.writeI32(pvpDeaths);
        w.writeI32(armor);
        w.writeI32(magicResist);
        w.writeFloat(critRate);
        w.writeFloat(hitRate);
        w.writeFloat(evasion);
        w.writeFloat(speed);
        w.writeFloat(damageMult);
        w.writeU8(pkStatus);
        w.writeU8(honorRank);
        w.writeU16(static_cast<uint16_t>(freeStatPoints));
        w.writeU16(static_cast<uint16_t>(allocatedSTR));
        w.writeU16(static_cast<uint16_t>(allocatedINT));
        w.writeU16(static_cast<uint16_t>(allocatedDEX));
        w.writeU16(static_cast<uint16_t>(allocatedCON));
        w.writeU16(static_cast<uint16_t>(allocatedWIS));
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
        m.honor       = r.readI32();
        m.pvpKills    = r.readI32();
        m.pvpDeaths   = r.readI32();
        m.armor       = r.readI32();
        m.magicResist = r.readI32();
        m.critRate    = r.readFloat();
        m.hitRate     = r.readFloat();
        m.evasion     = r.readFloat();
        m.speed       = r.readFloat();
        m.damageMult  = r.readFloat();
        m.pkStatus    = r.readU8();
        m.honorRank   = r.readU8();
        m.freeStatPoints = static_cast<int16_t>(r.readU16());
        m.allocatedSTR   = static_cast<int16_t>(r.readU16());
        m.allocatedINT   = static_cast<int16_t>(r.readU16());
        m.allocatedDEX   = static_cast<int16_t>(r.readU16());
        m.allocatedCON   = static_cast<int16_t>(r.readU16());
        m.allocatedWIS   = static_cast<int16_t>(r.readU16());
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

// ============================================================================
// State Sync Messages (server -> client on connect)
// ============================================================================

struct SkillSyncEntry {
    std::string skillId;
    uint8_t unlockedRank = 0;
    uint8_t activatedRank = 0;
};

struct SvSkillSyncMsg {
    std::vector<SkillSyncEntry> skills;
    std::vector<std::string> skillBar; // 20 slots, empty string = unbound
    int16_t availablePoints = 0;
    int16_t earnedPoints = 0;
    int16_t spentPoints = 0;

    void write(ByteWriter& w) const {
        w.writeU16(static_cast<uint16_t>(skills.size()));
        for (const auto& s : skills) {
            w.writeString(s.skillId);
            w.writeU8(s.unlockedRank);
            w.writeU8(s.activatedRank);
        }
        w.writeU8(static_cast<uint8_t>(skillBar.size()));
        for (const auto& slot : skillBar) {
            w.writeString(slot);
        }
        w.writeU16(static_cast<uint16_t>(availablePoints));
        w.writeU16(static_cast<uint16_t>(earnedPoints));
        w.writeU16(static_cast<uint16_t>(spentPoints));
    }

    static SvSkillSyncMsg read(ByteReader& r) {
        SvSkillSyncMsg m;
        uint16_t count = r.readU16();
        m.skills.resize(count);
        for (uint16_t i = 0; i < count; ++i) {
            m.skills[i].skillId = r.readString();
            m.skills[i].unlockedRank = r.readU8();
            m.skills[i].activatedRank = r.readU8();
        }
        uint8_t barCount = r.readU8();
        m.skillBar.resize(barCount);
        for (uint8_t i = 0; i < barCount; ++i) {
            m.skillBar[i] = r.readString();
        }
        m.availablePoints = static_cast<int16_t>(r.readU16());
        m.earnedPoints = static_cast<int16_t>(r.readU16());
        m.spentPoints = static_cast<int16_t>(r.readU16());
        return m;
    }
};

struct QuestSyncEntry {
    std::string questId;
    uint8_t state = 0; // 0=active, 1=completed, 2=failed
    std::vector<std::pair<int32_t, int32_t>> objectives; // current, target
};

struct SvQuestSyncMsg {
    std::vector<QuestSyncEntry> quests;

    void write(ByteWriter& w) const {
        w.writeU16(static_cast<uint16_t>(quests.size()));
        for (const auto& q : quests) {
            w.writeString(q.questId);
            w.writeU8(q.state);
            w.writeU8(static_cast<uint8_t>(q.objectives.size()));
            for (const auto& [cur, tgt] : q.objectives) {
                w.writeI32(cur);
                w.writeI32(tgt);
            }
        }
    }

    static SvQuestSyncMsg read(ByteReader& r) {
        SvQuestSyncMsg m;
        uint16_t count = r.readU16();
        m.quests.resize(count);
        for (uint16_t i = 0; i < count; ++i) {
            m.quests[i].questId = r.readString();
            m.quests[i].state = r.readU8();
            uint8_t objCount = r.readU8();
            m.quests[i].objectives.resize(objCount);
            for (uint8_t j = 0; j < objCount; ++j) {
                m.quests[i].objectives[j].first = r.readI32();
                m.quests[i].objectives[j].second = r.readI32();
            }
        }
        return m;
    }
};

struct InventorySyncSlot {
    int32_t slotIndex = -1;
    std::string itemId;
    std::string displayName;
    std::string rarity;
    std::string itemType;    // Weapon, Armor, Consumable, etc.
    int32_t quantity = 0;
    int32_t enchantLevel = 0;
    int32_t levelReq = 0;
    int32_t damageMin = 0;
    int32_t damageMax = 0;
    int32_t armor = 0;
    std::string rolledStats; // JSON string
    std::string socketStat;
    int32_t socketValue = 0;
    uint8_t isBroken = 0;
};

struct InventorySyncEquip {
    uint8_t slot = 0; // EquipmentSlot enum
    std::string itemId;
    std::string displayName;
    std::string rarity;
    std::string itemType;
    int32_t quantity = 0;
    int32_t enchantLevel = 0;
    int32_t levelReq = 0;
    int32_t damageMin = 0;
    int32_t damageMax = 0;
    int32_t armor = 0;
    std::string rolledStats;
    std::string socketStat;
    int32_t socketValue = 0;
    uint8_t isBroken = 0;
};

struct SvInventorySyncMsg {
    std::vector<InventorySyncSlot> slots;
    std::vector<InventorySyncEquip> equipment;

    void write(ByteWriter& w) const {
        w.writeU16(static_cast<uint16_t>(slots.size()));
        for (const auto& s : slots) {
            w.writeI32(s.slotIndex);
            w.writeString(s.itemId);
            w.writeString(s.displayName);
            w.writeString(s.rarity);
            w.writeString(s.itemType);
            w.writeI32(s.quantity);
            w.writeI32(s.enchantLevel);
            w.writeI32(s.levelReq);
            w.writeI32(s.damageMin);
            w.writeI32(s.damageMax);
            w.writeI32(s.armor);
            w.writeString(s.rolledStats);
            w.writeString(s.socketStat);
            w.writeI32(s.socketValue);
            w.writeU8(s.isBroken);
        }
        w.writeU16(static_cast<uint16_t>(equipment.size()));
        for (const auto& e : equipment) {
            w.writeU8(e.slot);
            w.writeString(e.itemId);
            w.writeString(e.displayName);
            w.writeString(e.rarity);
            w.writeString(e.itemType);
            w.writeI32(e.quantity);
            w.writeI32(e.enchantLevel);
            w.writeI32(e.levelReq);
            w.writeI32(e.damageMin);
            w.writeI32(e.damageMax);
            w.writeI32(e.armor);
            w.writeString(e.rolledStats);
            w.writeString(e.socketStat);
            w.writeI32(e.socketValue);
            w.writeU8(e.isBroken);
        }
    }

    static SvInventorySyncMsg read(ByteReader& r) {
        SvInventorySyncMsg m;
        uint16_t slotCount = r.readU16();
        m.slots.resize(slotCount);
        for (uint16_t i = 0; i < slotCount; ++i) {
            m.slots[i].slotIndex = r.readI32();
            m.slots[i].itemId = r.readString();
            m.slots[i].displayName = r.readString();
            m.slots[i].rarity = r.readString();
            m.slots[i].itemType = r.readString();
            m.slots[i].quantity = r.readI32();
            m.slots[i].enchantLevel = r.readI32();
            m.slots[i].levelReq = r.readI32();
            m.slots[i].damageMin = r.readI32();
            m.slots[i].damageMax = r.readI32();
            m.slots[i].armor = r.readI32();
            m.slots[i].rolledStats = r.readString();
            m.slots[i].socketStat = r.readString();
            m.slots[i].socketValue = r.readI32();
            m.slots[i].isBroken = r.readU8();
        }
        uint16_t equipCount = r.readU16();
        m.equipment.resize(equipCount);
        for (uint16_t i = 0; i < equipCount; ++i) {
            m.equipment[i].slot = r.readU8();
            m.equipment[i].itemId = r.readString();
            m.equipment[i].displayName = r.readString();
            m.equipment[i].rarity = r.readString();
            m.equipment[i].itemType = r.readString();
            m.equipment[i].quantity = r.readI32();
            m.equipment[i].enchantLevel = r.readI32();
            m.equipment[i].levelReq = r.readI32();
            m.equipment[i].damageMin = r.readI32();
            m.equipment[i].damageMax = r.readI32();
            m.equipment[i].armor = r.readI32();
            m.equipment[i].rolledStats = r.readString();
            m.equipment[i].socketStat = r.readString();
            m.equipment[i].socketValue = r.readI32();
            m.equipment[i].isBroken = r.readU8();
        }
        return m;
    }
};

struct SvBossLootOwnerMsg {
    std::string bossId;       // mob definition ID string (matches EnemyStats::enemyId)
    std::string winnerName;   // character name of top damager
    int32_t     topDamage = 0;
    uint8_t     wasParty  = 0;

    void write(ByteWriter& w) const {
        w.writeString(bossId);
        w.writeString(winnerName);
        w.writeI32(topDamage);
        w.writeU8(wasParty);
    }

    static SvBossLootOwnerMsg read(ByteReader& r) {
        SvBossLootOwnerMsg m;
        m.bossId      = r.readString();
        m.winnerName  = r.readString();
        m.topDamage   = r.readI32();
        m.wasParty    = r.readU8();
        return m;
    }
};

struct SvBuffSyncMsg {
    struct BuffEntry {
        uint8_t effectType = 0;
        float remainingTime = 0.0f;
        float totalDuration = 0.0f;
        uint8_t stacks = 1;
    };
    std::vector<BuffEntry> buffs;

    void write(ByteWriter& w) const {
        w.writeU8(static_cast<uint8_t>(buffs.size()));
        for (const auto& b : buffs) {
            w.writeU8(b.effectType);
            w.writeFloat(b.remainingTime);
            w.writeFloat(b.totalDuration);
            w.writeU8(b.stacks);
        }
    }

    static SvBuffSyncMsg read(ByteReader& r) {
        SvBuffSyncMsg msg;
        uint8_t count = r.readU8();
        msg.buffs.resize(count);
        for (uint8_t i = 0; i < count; ++i) {
            msg.buffs[i].effectType = r.readU8();
            msg.buffs[i].remainingTime = r.readFloat();
            msg.buffs[i].totalDuration = r.readFloat();
            msg.buffs[i].stacks = r.readU8();
        }
        return msg;
    }
};

} // namespace fate
