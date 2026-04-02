#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "engine/net/byte_stream.h"
#include "engine/net/protocol.h"

namespace fate {

// ============================================================================
// Client -> Server: CmdTrade sub-actions
// ============================================================================
// CmdTrade payload: [subAction:u8] [... sub-action-specific fields]
namespace TradeAction {
    constexpr uint8_t Initiate     = 0;  // + targetCharId:string
    constexpr uint8_t AcceptInvite = 1;  // + sessionId:i32
    constexpr uint8_t AddItem      = 2;  // + slotIndex:u8, sourceSlot:i32, instanceId:string, quantity:i32
    constexpr uint8_t RemoveItem   = 3;  // + slotIndex:u8
    constexpr uint8_t SetGold      = 4;  // + gold:i64
    constexpr uint8_t Lock         = 5;  // (no extra fields)
    constexpr uint8_t Unlock       = 6;
    constexpr uint8_t Confirm      = 7;  // + nonce:u64
    constexpr uint8_t Cancel       = 8;
}

// ============================================================================
// Client -> Server: CmdMarket sub-actions
// ============================================================================
namespace MarketAction {
    constexpr uint8_t ListItem       = 0;  // + instanceId:string, priceGold:i64 + nonce:u64
    constexpr uint8_t BuyItem        = 1;  // + listingId:i32 + nonce:u64
    constexpr uint8_t CancelListing  = 2;  // + listingId:i32
    constexpr uint8_t GetListings    = 3;  // + page:i32, filterJson:string
    constexpr uint8_t GetMyListings  = 4;  // (no extra fields)
}

// ============================================================================
// Client -> Server: CmdBounty sub-actions
// ============================================================================
namespace BountyAction {
    constexpr uint8_t PlaceBounty   = 0;  // + targetCharId:string, amount:i64
    constexpr uint8_t CancelBounty  = 1;  // + targetCharId:string
    constexpr uint8_t GetBoard      = 2;  // (no extra fields)
}

// ============================================================================
// Client -> Server: CmdGauntlet sub-actions
// ============================================================================
namespace GauntletAction {
    constexpr uint8_t Register   = 0;  // (no extra fields — division auto-detected from level)
    constexpr uint8_t Unregister = 1;
    constexpr uint8_t GetStatus  = 2;
}

// ============================================================================
// Client -> Server: CmdGuild sub-actions
// ============================================================================
namespace GuildAction {
    constexpr uint8_t Create     = 0;  // + guildName:string
    constexpr uint8_t Invite     = 1;  // + targetCharId:string
    constexpr uint8_t AcceptInvite = 2; // + inviteId:i32
    constexpr uint8_t Leave      = 3;
    constexpr uint8_t Kick       = 4;  // + targetCharId:string
    constexpr uint8_t Promote    = 5;  // + targetCharId:string
    constexpr uint8_t Demote     = 6;  // + targetCharId:string
    constexpr uint8_t Disband    = 7;
    constexpr uint8_t DeclineInvite = 8;  // + inviterCharId:string
    constexpr uint8_t RequestRoster = 9;  // (no extra fields)
}

// ============================================================================
// Client -> Server: CmdSocial sub-actions
// ============================================================================
namespace SocialAction {
    constexpr uint8_t SendFriendRequest = 0;  // + targetCharId:string
    constexpr uint8_t AcceptFriend      = 1;  // + fromCharId:string
    constexpr uint8_t DeclineFriend     = 2;  // + fromCharId:string
    constexpr uint8_t RemoveFriend      = 3;  // + friendCharId:string
    constexpr uint8_t BlockPlayer       = 4;  // + targetCharId:string
    constexpr uint8_t UnblockPlayer     = 5;  // + targetCharId:string
}

// ============================================================================
// Client -> Server: CmdParty sub-actions
// ============================================================================
namespace PartyAction {
    constexpr uint8_t Invite        = 0;  // + targetCharId:string
    constexpr uint8_t AcceptInvite  = 1;  // + inviterCharId:string
    constexpr uint8_t DeclineInvite = 2;  // + inviterCharId:string
    constexpr uint8_t Kick          = 3;  // + targetCharId:string
    constexpr uint8_t Promote       = 4;  // + targetCharId:string
    constexpr uint8_t Leave         = 5;
    constexpr uint8_t SetLootMode   = 6;  // + mode:uint8
}

// ============================================================================
// Client -> Server: CmdQuestAction sub-actions
// ============================================================================
namespace QuestAction {
    constexpr uint8_t Accept   = 0;  // + questId:string
    constexpr uint8_t Abandon  = 1;  // + questId:string
    constexpr uint8_t TurnIn   = 2;  // + questId:string
}

// ============================================================================
// Client -> Server: CmdZoneTransition
// ============================================================================
struct CmdZoneTransition {
    std::string targetScene;  // scene name the client wants to enter

    void write(ByteWriter& w) const {
        w.writeString(targetScene);
    }
    static CmdZoneTransition read(ByteReader& r) {
        CmdZoneTransition m;
        m.targetScene = r.readString();
        return m;
    }
};

// ============================================================================
// Server -> Client result messages
// ============================================================================

struct SvTradeUpdateMsg {
    uint8_t updateType = 0;  // 0=invited, 1=sessionStarted, 2=itemAdded, 3=locked, 4=confirmed, 5=completed, 6=cancelled
    int32_t sessionId  = 0;
    std::string otherPlayerName;
    uint8_t resultCode = 0;  // 0=success, 1+=error codes from TradeActionResult
    uint64_t nonce = 0;

    void write(ByteWriter& w) const {
        w.writeU8(updateType);
        w.writeI32(sessionId);
        w.writeString(otherPlayerName);
        w.writeU8(resultCode);
        detail::writeU64(w, nonce);
    }
    static SvTradeUpdateMsg read(ByteReader& r) {
        SvTradeUpdateMsg m;
        m.updateType     = r.readU8();
        m.sessionId      = r.readI32();
        m.otherPlayerName = r.readString();
        m.resultCode     = r.readU8();
        m.nonce          = detail::readU64(r);
        return m;
    }
};

struct SvMarketResultMsg {
    uint8_t action     = 0;  // mirrors MarketAction
    uint8_t resultCode = 0;  // 0=success, 1+=error
    int32_t listingId  = 0;
    std::string message;
    uint64_t nonce = 0;

    void write(ByteWriter& w) const {
        w.writeU8(action);
        w.writeU8(resultCode);
        w.writeI32(listingId);
        w.writeString(message);
        detail::writeU64(w, nonce);
    }
    static SvMarketResultMsg read(ByteReader& r) {
        SvMarketResultMsg m;
        m.action     = r.readU8();
        m.resultCode = r.readU8();
        m.listingId  = r.readI32();
        m.message    = r.readString();
        m.nonce      = detail::readU64(r);
        return m;
    }
};

struct SvBountyUpdateMsg {
    uint8_t updateType = 0;  // 0=boardRefresh, 1=placed, 2=claimed, 3=cancelled, 4=result
    uint8_t resultCode = 0;
    std::string message;

    void write(ByteWriter& w) const {
        w.writeU8(updateType);
        w.writeU8(resultCode);
        w.writeString(message);
    }
    static SvBountyUpdateMsg read(ByteReader& r) {
        SvBountyUpdateMsg m;
        m.updateType = r.readU8();
        m.resultCode = r.readU8();
        m.message    = r.readString();
        return m;
    }
};

struct SvGauntletUpdateMsg {
    uint8_t updateType = 0;  // 0=signupOpen, 1=registered, 2=matchStart, 3=waveStart, 4=scores, 5=matchEnd
    uint8_t resultCode = 0;
    std::string message;

    void write(ByteWriter& w) const {
        w.writeU8(updateType);
        w.writeU8(resultCode);
        w.writeString(message);
    }
    static SvGauntletUpdateMsg read(ByteReader& r) {
        SvGauntletUpdateMsg m;
        m.updateType = r.readU8();
        m.resultCode = r.readU8();
        m.message    = r.readString();
        return m;
    }
};

struct SvGuildUpdateMsg {
    uint8_t updateType = 0;  // 0=created, 1=joined, 2=left, 3=disbanded, 4=rankChanged, 5=result
    uint8_t resultCode = 0;
    std::string guildName;
    std::string message;

    void write(ByteWriter& w) const {
        w.writeU8(updateType);
        w.writeU8(resultCode);
        w.writeString(guildName);
        w.writeString(message);
    }
    static SvGuildUpdateMsg read(ByteReader& r) {
        SvGuildUpdateMsg m;
        m.updateType = r.readU8();
        m.resultCode = r.readU8();
        m.guildName  = r.readString();
        m.message    = r.readString();
        return m;
    }
};

struct SvSocialUpdateMsg {
    uint8_t updateType = 0;  // 0=friendRequest, 1=friendAccepted, 2=friendRemoved, 3=blocked, 4=result
    uint8_t resultCode = 0;
    std::string characterName;
    std::string message;

    void write(ByteWriter& w) const {
        w.writeU8(updateType);
        w.writeU8(resultCode);
        w.writeString(characterName);
        w.writeString(message);
    }
    static SvSocialUpdateMsg read(ByteReader& r) {
        SvSocialUpdateMsg m;
        m.updateType    = r.readU8();
        m.resultCode    = r.readU8();
        m.characterName = r.readString();
        m.message       = r.readString();
        return m;
    }
};

// ============================================================================
// Server -> Client: SvPartyUpdate
// ============================================================================
namespace PartyEvent {
    constexpr uint8_t Invited         = 0;
    constexpr uint8_t Joined          = 1;
    constexpr uint8_t Left            = 2;
    constexpr uint8_t Kicked          = 3;
    constexpr uint8_t Promoted        = 4;
    constexpr uint8_t Disbanded       = 5;
    constexpr uint8_t LootModeChanged = 6;
    constexpr uint8_t FullSync        = 7;
}

struct PartyMemberNetInfo {
    std::string charId;
    std::string name;
    uint8_t level = 0;
    int32_t hp = 0, maxHp = 0, mp = 0, maxMp = 0;

    void write(ByteWriter& w) const {
        w.writeString(charId);
        w.writeString(name);
        w.writeU8(level);
        w.writeI32(hp); w.writeI32(maxHp);
        w.writeI32(mp); w.writeI32(maxMp);
    }
    static PartyMemberNetInfo read(ByteReader& r) {
        PartyMemberNetInfo m;
        m.charId = r.readString();
        m.name   = r.readString();
        m.level  = r.readU8();
        m.hp = r.readI32(); m.maxHp = r.readI32();
        m.mp = r.readI32(); m.maxMp = r.readI32();
        return m;
    }
};

struct SvPartyUpdateMsg {
    uint8_t event = 0;
    std::string actorCharId;    // who triggered the event
    std::string targetCharId;   // who was affected (kick/promote)
    std::string leaderId;
    uint8_t lootMode = 0;
    std::vector<PartyMemberNetInfo> members;

    void write(ByteWriter& w) const {
        w.writeU8(event);
        w.writeString(actorCharId);
        w.writeString(targetCharId);
        w.writeString(leaderId);
        w.writeU8(lootMode);
        w.writeU8(static_cast<uint8_t>(members.size()));
        for (const auto& m : members) m.write(w);
    }
    static SvPartyUpdateMsg read(ByteReader& r) {
        SvPartyUpdateMsg m;
        m.event        = r.readU8();
        m.actorCharId  = r.readString();
        m.targetCharId = r.readString();
        m.leaderId     = r.readString();
        m.lootMode     = r.readU8();
        uint8_t count  = r.readU8();
        m.members.resize(count);
        for (uint8_t i = 0; i < count; ++i)
            m.members[i] = PartyMemberNetInfo::read(r);
        return m;
    }
};

struct SvQuestUpdateMsg {
    uint8_t updateType = 0;  // 0=accepted, 1=progressUpdate, 2=completed, 3=abandoned, 4=result
    std::string questId;
    int32_t currentCount = 0;
    int32_t targetCount  = 0;
    std::string message;
    int64_t completionTime = 0;  // epoch seconds; non-zero for repeatable quest completion

    void write(ByteWriter& w) const {
        w.writeU8(updateType);
        w.writeString(questId);
        w.writeI32(currentCount);
        w.writeI32(targetCount);
        w.writeString(message);
        w.writeI64(completionTime);
    }
    static SvQuestUpdateMsg read(ByteReader& r) {
        SvQuestUpdateMsg m;
        m.updateType   = r.readU8();
        m.questId      = r.readString();
        m.currentCount = r.readI32();
        m.targetCount  = r.readI32();
        m.message      = r.readString();
        m.completionTime = r.readI64();
        return m;
    }
};

struct SvZoneTransitionMsg {
    std::string targetScene;
    float spawnX = 0;
    float spawnY = 0;

    void write(ByteWriter& w) const {
        w.writeString(targetScene);
        w.writeFloat(spawnX);
        w.writeFloat(spawnY);
    }
    static SvZoneTransitionMsg read(ByteReader& r) {
        SvZoneTransitionMsg m;
        m.targetScene = r.readString();
        m.spawnX = r.readFloat();
        m.spawnY = r.readFloat();
        return m;
    }
};

struct SvDeathNotifyMsg {
    uint8_t deathSource  = 0;  // 0=PvE, 1=PvP, 2=Gauntlet, 3=Environment
    float   respawnTimer = 5.0f;
    int32_t xpLost       = 0;
    int32_t honorLost    = 0;

    void write(ByteWriter& w) const {
        w.writeU8(deathSource);
        w.writeFloat(respawnTimer);
        w.writeI32(xpLost);
        w.writeI32(honorLost);
    }
    static SvDeathNotifyMsg read(ByteReader& r) {
        SvDeathNotifyMsg m;
        m.deathSource  = r.readU8();
        m.respawnTimer = r.readFloat();
        m.xpLost       = r.readI32();
        m.honorLost    = r.readI32();
        return m;
    }
};

struct CmdRespawnMsg {
    uint8_t respawnType = 0;  // 0=town, 1=map spawn, 2=here (Phoenix Down)

    void write(ByteWriter& w) const {
        w.writeU8(respawnType);
    }
    static CmdRespawnMsg read(ByteReader& r) {
        CmdRespawnMsg m;
        m.respawnType = r.readU8();
        return m;
    }
};

struct SvRespawnMsg {
    uint8_t respawnType = 0;
    float   spawnX      = 0.0f;
    float   spawnY      = 0.0f;

    void write(ByteWriter& w) const {
        w.writeU8(respawnType);
        w.writeFloat(spawnX);
        w.writeFloat(spawnY);
    }
    static SvRespawnMsg read(ByteReader& r) {
        SvRespawnMsg m;
        m.respawnType = r.readU8();
        m.spawnX      = r.readFloat();
        m.spawnY      = r.readFloat();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdUseSkill (string-based skill ID)
// ============================================================================
struct CmdUseSkillMsg {
    std::string skillId;
    uint8_t rank = 1;
    uint64_t targetId = 0;  // PersistentId of target

    void write(ByteWriter& w) const {
        w.writeString(skillId);
        w.writeU8(rank);
        detail::writeU64(w, targetId);
    }
    static CmdUseSkillMsg read(ByteReader& r) {
        CmdUseSkillMsg m;
        m.skillId  = r.readString();
        m.rank     = r.readU8();
        m.targetId = detail::readU64(r);
        return m;
    }
};

// ============================================================================
// Server -> Client: SvSkillResult (skill execution outcome)
// ============================================================================
struct SvSkillResultMsg {
    uint64_t casterId = 0;   // PersistentId
    uint64_t targetId = 0;   // PersistentId
    std::string skillId;
    int32_t  damage       = 0;
    int32_t  overkill     = 0;    // damage beyond lethal (0 if alive)
    int32_t  targetNewHP  = 0;    // target's HP after this hit
    uint8_t  hitFlags     = 0;    // HitFlags bitmask (HIT|CRIT|MISS|DODGE|BLOCKED|ABSORBED|KILLED)
    uint16_t resourceCost = 0;    // mana/fury actually consumed
    uint16_t cooldownMs   = 0;    // authoritative cooldown duration
    uint16_t casterNewMP  = 0;    // caster's new mana value after cost

    void write(ByteWriter& w) const {
        detail::writeU64(w, casterId);
        detail::writeU64(w, targetId);
        w.writeString(skillId);
        w.writeI32(damage);
        w.writeI32(overkill);
        w.writeI32(targetNewHP);
        w.writeU8(hitFlags);
        w.writeU16(resourceCost);
        w.writeU16(cooldownMs);
        w.writeU16(casterNewMP);
    }
    static SvSkillResultMsg read(ByteReader& r) {
        SvSkillResultMsg m;
        m.casterId     = detail::readU64(r);
        m.targetId     = detail::readU64(r);
        m.skillId      = r.readString();
        m.damage       = r.readI32();
        m.overkill     = r.readI32();
        m.targetNewHP  = r.readI32();
        m.hitFlags     = r.readU8();
        m.resourceCost = r.readU16();
        m.cooldownMs   = r.readU16();
        m.casterNewMP  = r.readU16();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdEquip (equip or unequip an item)
// ============================================================================
struct CmdEquipMsg {
    uint8_t action = 0;       // 0=equip, 1=unequip
    int32_t inventorySlot = -1; // source inventory slot (equip only)
    uint8_t equipSlot = 0;    // EquipmentSlot enum value

    void write(ByteWriter& w) const {
        w.writeU8(action);
        w.writeI32(inventorySlot);
        w.writeU8(equipSlot);
    }
    static CmdEquipMsg read(ByteReader& r) {
        CmdEquipMsg m;
        m.action        = r.readU8();
        m.inventorySlot = r.readI32();
        m.equipSlot     = r.readU8();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdMoveItem (swap/stack inventory slots)
// ============================================================================
struct CmdMoveItemMsg {
    int32_t sourceSlot = -1;
    int32_t destSlot   = -1;

    void write(ByteWriter& w) const {
        w.writeI32(sourceSlot);
        w.writeI32(destSlot);
    }
    static CmdMoveItemMsg read(ByteReader& r) {
        CmdMoveItemMsg m;
        m.sourceSlot = r.readI32();
        m.destSlot   = r.readI32();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdDestroyItem (discard an inventory item)
// ============================================================================
struct CmdDestroyItemMsg {
    int32_t slot = -1;
    std::string expectedItemId;  // instance ID for race-condition safety

    void write(ByteWriter& w) const {
        w.writeI32(slot);
        w.writeString(expectedItemId);
    }
    static CmdDestroyItemMsg read(ByteReader& r) {
        CmdDestroyItemMsg m;
        m.slot = r.readI32();
        m.expectedItemId = r.readString();
        return m;
    }
};

// ============================================================================
// Server -> Client: SvLevelUp (explicit level-up event with full stat snapshot)
// ============================================================================
struct SvLevelUpMsg {
    int32_t newLevel     = 0;
    int32_t newMaxHP     = 0;
    int32_t newMaxMP     = 0;
    int32_t newCurrentHP = 0;
    int32_t newCurrentMP = 0;
    int32_t newArmor     = 0;
    float   newCritRate  = 0.0f;
    float   newSpeed     = 1.0f;
    float   newDamageMult = 1.0f;

    void write(ByteWriter& w) const {
        w.writeI32(newLevel);
        w.writeI32(newMaxHP);
        w.writeI32(newMaxMP);
        w.writeI32(newCurrentHP);
        w.writeI32(newCurrentMP);
        w.writeI32(newArmor);
        w.writeFloat(newCritRate);
        w.writeFloat(newSpeed);
        w.writeFloat(newDamageMult);
    }
    static SvLevelUpMsg read(ByteReader& r) {
        SvLevelUpMsg m;
        m.newLevel     = r.readI32();
        m.newMaxHP     = r.readI32();
        m.newMaxMP     = r.readI32();
        m.newCurrentHP = r.readI32();
        m.newCurrentMP = r.readI32();
        m.newArmor     = r.readI32();
        m.newCritRate  = r.readFloat();
        m.newSpeed     = r.readFloat();
        m.newDamageMult = r.readFloat();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdEnchant / Server -> Client: SvEnchantResult
// ============================================================================
struct CmdEnchantMsg {
    uint8_t inventorySlot = 0;
    uint8_t useProtectionStone = 0;

    void write(ByteWriter& w) const {
        w.writeU8(inventorySlot);
        w.writeU8(useProtectionStone);
    }
    static CmdEnchantMsg read(ByteReader& r) {
        CmdEnchantMsg m;
        m.inventorySlot = r.readU8();
        m.useProtectionStone = r.readU8();
        return m;
    }
};

struct SvEnchantResultMsg {
    uint8_t success = 0;
    uint8_t newLevel = 0;
    uint8_t broke = 0;
    std::string message;

    void write(ByteWriter& w) const {
        w.writeU8(success);
        w.writeU8(newLevel);
        w.writeU8(broke);
        w.writeString(message);
    }
    static SvEnchantResultMsg read(ByteReader& r) {
        SvEnchantResultMsg m;
        m.success = r.readU8();
        m.newLevel = r.readU8();
        m.broke = r.readU8();
        m.message = r.readString();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdRepair / Server -> Client: SvRepairResult
// ============================================================================
struct CmdRepairMsg {
    uint8_t inventorySlot = 0;

    void write(ByteWriter& w) const { w.writeU8(inventorySlot); }
    static CmdRepairMsg read(ByteReader& r) {
        CmdRepairMsg m;
        m.inventorySlot = r.readU8();
        return m;
    }
};

struct SvRepairResultMsg {
    uint8_t success = 0;
    uint8_t newLevel = 0;
    std::string message;

    void write(ByteWriter& w) const {
        w.writeU8(success);
        w.writeU8(newLevel);
        w.writeString(message);
    }
    static SvRepairResultMsg read(ByteReader& r) {
        SvRepairResultMsg m;
        m.success = r.readU8();
        m.newLevel = r.readU8();
        m.message = r.readString();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdExtractCore / Server -> Client: SvExtractResult
// ============================================================================
struct CmdExtractCoreMsg {
    uint8_t itemSlot = 0;
    uint8_t scrollSlot = 0;

    void write(ByteWriter& w) const {
        w.writeU8(itemSlot);
        w.writeU8(scrollSlot);
    }
    static CmdExtractCoreMsg read(ByteReader& r) {
        CmdExtractCoreMsg m;
        m.itemSlot = r.readU8();
        m.scrollSlot = r.readU8();
        return m;
    }
};

struct SvExtractResultMsg {
    uint8_t success = 0;
    std::string coreItemId;
    uint8_t coreQuantity = 0;
    std::string message;

    void write(ByteWriter& w) const {
        w.writeU8(success);
        w.writeString(coreItemId);
        w.writeU8(coreQuantity);
        w.writeString(message);
    }
    static SvExtractResultMsg read(ByteReader& r) {
        SvExtractResultMsg m;
        m.success = r.readU8();
        m.coreItemId = r.readString();
        m.coreQuantity = r.readU8();
        m.message = r.readString();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdCraft / Server -> Client: SvCraftResult
// ============================================================================
struct CmdCraftMsg {
    std::string recipeId;

    void write(ByteWriter& w) const { w.writeString(recipeId); }
    static CmdCraftMsg read(ByteReader& r) {
        CmdCraftMsg m;
        m.recipeId = r.readString();
        return m;
    }
};

struct SvCraftResultMsg {
    uint8_t success = 0;
    std::string resultItemId;
    uint8_t resultQuantity = 0;
    std::string message;

    void write(ByteWriter& w) const {
        w.writeU8(success);
        w.writeString(resultItemId);
        w.writeU8(resultQuantity);
        w.writeString(message);
    }
    static SvCraftResultMsg read(ByteReader& r) {
        SvCraftResultMsg m;
        m.success = r.readU8();
        m.resultItemId = r.readString();
        m.resultQuantity = r.readU8();
        m.message = r.readString();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdBattlefield / Server -> Client: SvBattlefieldUpdate
// ============================================================================
struct CmdBattlefieldMsg {
    uint8_t action = 0; // 0=Register, 1=Unregister
    void write(ByteWriter& w) const { w.writeU8(action); }
    static CmdBattlefieldMsg read(ByteReader& r) {
        CmdBattlefieldMsg m; m.action = r.readU8(); return m;
    }
};

struct SvBattlefieldUpdateMsg {
    uint8_t state = 0;
    uint16_t timeRemaining = 0;
    uint8_t factionCount = 0;
    std::vector<uint8_t> factionIds;
    std::vector<uint16_t> factionKills;
    uint16_t personalKills = 0;
    uint8_t result = 0;

    void write(ByteWriter& w) const {
        w.writeU8(state);
        w.writeU16(timeRemaining);
        w.writeU8(factionCount);
        for (uint8_t i = 0; i < factionCount; ++i) {
            w.writeU8(i < factionIds.size() ? factionIds[i] : 0);
            w.writeU16(i < factionKills.size() ? factionKills[i] : 0);
        }
        w.writeU16(personalKills);
        w.writeU8(result);
    }
    static SvBattlefieldUpdateMsg read(ByteReader& r) {
        SvBattlefieldUpdateMsg m;
        m.state = r.readU8();
        m.timeRemaining = r.readU16();
        m.factionCount = r.readU8();
        for (uint8_t i = 0; i < m.factionCount; ++i) {
            m.factionIds.push_back(r.readU8());
            m.factionKills.push_back(r.readU16());
        }
        m.personalKills = r.readU16();
        m.result = r.readU8();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdArena / Server -> Client: SvArenaUpdate
// ============================================================================
struct CmdArenaMsg {
    uint8_t action = 0; // 0=Register, 1=Unregister
    uint8_t mode = 1;   // 1=Solo, 2=Duo, 3=Team
    void write(ByteWriter& w) const { w.writeU8(action); w.writeU8(mode); }
    static CmdArenaMsg read(ByteReader& r) {
        CmdArenaMsg m; m.action = r.readU8(); m.mode = r.readU8(); return m;
    }
};

struct SvArenaUpdateMsg {
    uint8_t state = 0;
    uint16_t timeRemaining = 0;
    uint8_t teamAlive = 0;
    uint8_t enemyAlive = 0;
    uint8_t result = 0;
    int32_t honorReward = 0;

    void write(ByteWriter& w) const {
        w.writeU8(state);
        w.writeU16(timeRemaining);
        w.writeU8(teamAlive);
        w.writeU8(enemyAlive);
        w.writeU8(result);
        w.writeI32(honorReward);
    }
    static SvArenaUpdateMsg read(ByteReader& r) {
        SvArenaUpdateMsg m;
        m.state = r.readU8();
        m.timeRemaining = r.readU16();
        m.teamAlive = r.readU8();
        m.enemyAlive = r.readU8();
        m.result = r.readU8();
        m.honorReward = r.readI32();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdPet / Server -> Client: SvPetUpdate
// ============================================================================
struct CmdPetMsg {
    uint8_t action = 0; // 0=Equip, 1=Unequip
    int32_t petDbId = 0;

    void write(ByteWriter& w) const { w.writeU8(action); w.writeI32(petDbId); }
    static CmdPetMsg read(ByteReader& r) {
        CmdPetMsg m; m.action = r.readU8(); m.petDbId = r.readI32(); return m;
    }
};

struct SvPetUpdateMsg {
    uint8_t equipped = 0;
    std::string petDefId;
    std::string petName;
    uint8_t level = 0;
    int32_t currentXP = 0;
    int32_t xpToNextLevel = 0;

    void write(ByteWriter& w) const {
        w.writeU8(equipped);
        w.writeString(petDefId);
        w.writeString(petName);
        w.writeU8(level);
        w.writeI32(currentXP);
        w.writeI32(xpToNextLevel);
    }
    static SvPetUpdateMsg read(ByteReader& r) {
        SvPetUpdateMsg m;
        m.equipped = r.readU8();
        m.petDefId = r.readString();
        m.petName = r.readString();
        m.level = r.readU8();
        m.currentXP = r.readI32();
        m.xpToNextLevel = r.readI32();
        return m;
    }
};

// ============================================================================
// Client -> Server: Shop messages
// ============================================================================
struct CmdShopBuyMsg {
    uint32_t npcId = 0;
    std::string itemId;
    uint16_t quantity = 0;

    void write(ByteWriter& w) const {
        w.writeU32(npcId);
        w.writeString(itemId);
        w.writeU16(quantity);
    }
    static CmdShopBuyMsg read(ByteReader& r) {
        CmdShopBuyMsg m;
        m.npcId    = r.readU32();
        m.itemId   = r.readString();
        m.quantity = r.readU16();
        return m;
    }
};

struct CmdShopSellMsg {
    uint32_t npcId = 0;
    uint8_t inventorySlot = 0;
    uint16_t quantity = 0;

    void write(ByteWriter& w) const {
        w.writeU32(npcId);
        w.writeU8(inventorySlot);
        w.writeU16(quantity);
    }
    static CmdShopSellMsg read(ByteReader& r) {
        CmdShopSellMsg m;
        m.npcId         = r.readU32();
        m.inventorySlot = r.readU8();
        m.quantity      = r.readU16();
        return m;
    }
};

// ============================================================================
// Server -> Client: SvShopResult
// ============================================================================
struct SvShopResultMsg {
    uint8_t action = 0;
    uint8_t success = 0;
    int64_t updatedGold = 0;
    std::string reason;

    void write(ByteWriter& w) const {
        w.writeU8(action);
        w.writeU8(success);
        detail::writeI64(w, updatedGold);
        w.writeString(reason);
    }
    static SvShopResultMsg read(ByteReader& r) {
        SvShopResultMsg m;
        m.action      = r.readU8();
        m.success     = r.readU8();
        m.updatedGold = detail::readI64(r);
        m.reason      = r.readString();
        return m;
    }
};

// ============================================================================
// Client -> Server: Split bank messages
// ============================================================================
struct CmdBankDepositItemMsg {
    uint32_t npcId = 0;
    uint8_t inventorySlot = 0;

    void write(ByteWriter& w) const {
        w.writeU32(npcId);
        w.writeU8(inventorySlot);
    }
    static CmdBankDepositItemMsg read(ByteReader& r) {
        CmdBankDepositItemMsg m;
        m.npcId         = r.readU32();
        m.inventorySlot = r.readU8();
        return m;
    }
};

struct CmdBankWithdrawItemMsg {
    uint32_t npcId = 0;
    uint16_t itemIndex = 0;

    void write(ByteWriter& w) const {
        w.writeU32(npcId);
        w.writeU16(itemIndex);
    }
    static CmdBankWithdrawItemMsg read(ByteReader& r) {
        CmdBankWithdrawItemMsg m;
        m.npcId     = r.readU32();
        m.itemIndex = r.readU16();
        return m;
    }
};

struct CmdBankDepositGoldMsg {
    uint32_t npcId = 0;
    int64_t amount = 0;

    void write(ByteWriter& w) const {
        w.writeU32(npcId);
        detail::writeI64(w, amount);
    }
    static CmdBankDepositGoldMsg read(ByteReader& r) {
        CmdBankDepositGoldMsg m;
        m.npcId  = r.readU32();
        m.amount = detail::readI64(r);
        return m;
    }
};

struct CmdBankWithdrawGoldMsg {
    uint32_t npcId = 0;
    int64_t amount = 0;

    void write(ByteWriter& w) const {
        w.writeU32(npcId);
        detail::writeI64(w, amount);
    }
    static CmdBankWithdrawGoldMsg read(ByteReader& r) {
        CmdBankWithdrawGoldMsg m;
        m.npcId  = r.readU32();
        m.amount = detail::readI64(r);
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdTeleport / Server -> Client: SvTeleportResult
// ============================================================================
struct CmdTeleportMsg {
    uint32_t npcId = 0;
    uint8_t destinationIndex = 0;

    void write(ByteWriter& w) const {
        w.writeU32(npcId);
        w.writeU8(destinationIndex);
    }
    static CmdTeleportMsg read(ByteReader& r) {
        CmdTeleportMsg m;
        m.npcId            = r.readU32();
        m.destinationIndex = r.readU8();
        return m;
    }
};

struct SvTeleportResultMsg {
    uint8_t success = 0;
    std::string sceneId;
    float posX = 0.0f;
    float posY = 0.0f;

    void write(ByteWriter& w) const {
        w.writeU8(success);
        w.writeString(sceneId);
        w.writeFloat(posX);
        w.writeFloat(posY);
    }
    static SvTeleportResultMsg read(ByteReader& r) {
        SvTeleportResultMsg m;
        m.success = r.readU8();
        m.sceneId = r.readString();
        m.posX    = r.readFloat();
        m.posY    = r.readFloat();
        return m;
    }
};

struct SvAuroraStatusMsg {
    uint8_t  favoredFaction = 0;   // Faction enum value
    uint32_t secondsRemaining = 0; // time until next rotation

    void write(ByteWriter& w) const {
        w.writeU8(favoredFaction);
        w.writeU32(secondsRemaining);
    }
    static SvAuroraStatusMsg read(ByteReader& r) {
        SvAuroraStatusMsg m;
        m.favoredFaction   = r.readU8();
        m.secondsRemaining = r.readU32();
        return m;
    }
};

// ============================================================================
// Server -> Client: SvBankResult (unchanged legacy format)
// ============================================================================
struct SvBankResultMsg {
    uint8_t action = 0;
    uint8_t success = 0;
    int64_t bankGold = 0;
    std::string message;

    void write(ByteWriter& w) const {
        w.writeU8(action);
        w.writeU8(success);
        detail::writeI64(w, bankGold);
        w.writeString(message);
    }
    static SvBankResultMsg read(ByteReader& r) {
        SvBankResultMsg m;
        m.action   = r.readU8();
        m.success  = r.readU8();
        m.bankGold = detail::readI64(r);
        m.message  = r.readString();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdSocketItem / Server -> Client: SvSocketResult
// ============================================================================
struct CmdSocketItemMsg {
    uint8_t equipSlot = 0;
    std::string scrollItemId;

    void write(ByteWriter& w) const {
        w.writeU8(equipSlot);
        w.writeString(scrollItemId);
    }
    static CmdSocketItemMsg read(ByteReader& r) {
        CmdSocketItemMsg m;
        m.equipSlot    = r.readU8();
        m.scrollItemId = r.readString();
        return m;
    }
};

struct SvSocketResultMsg {
    uint8_t success = 0;
    uint8_t rolledValue = 0;
    uint8_t previousValue = 0;
    uint8_t wasResocket = 0;
    std::string message;

    void write(ByteWriter& w) const {
        w.writeU8(success);
        w.writeU8(rolledValue);
        w.writeU8(previousValue);
        w.writeU8(wasResocket);
        w.writeString(message);
    }
    static SvSocketResultMsg read(ByteReader& r) {
        SvSocketResultMsg m;
        m.success       = r.readU8();
        m.rolledValue   = r.readU8();
        m.previousValue = r.readU8();
        m.wasResocket   = r.readU8();
        m.message       = r.readString();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdStatEnchant / Server -> Client: SvStatEnchantResult
// ============================================================================
struct CmdStatEnchantMsg {
    uint8_t targetSlot = 0;
    uint8_t scrollStatType = 0;
    std::string scrollItemId;

    void write(ByteWriter& w) const {
        w.writeU8(targetSlot);
        w.writeU8(scrollStatType);
        w.writeString(scrollItemId);
    }
    static CmdStatEnchantMsg read(ByteReader& r) {
        CmdStatEnchantMsg m;
        m.targetSlot     = r.readU8();
        m.scrollStatType = r.readU8();
        m.scrollItemId   = r.readString();
        return m;
    }
};

struct SvStatEnchantResultMsg {
    uint8_t success = 0;
    uint8_t tier = 0;
    int32_t value = 0;
    std::string message;

    void write(ByteWriter& w) const {
        w.writeU8(success);
        w.writeU8(tier);
        w.writeI32(value);
        w.writeString(message);
    }
    static SvStatEnchantResultMsg read(ByteReader& r) {
        SvStatEnchantResultMsg m;
        m.success = r.readU8();
        m.tier    = r.readU8();
        m.value   = r.readI32();
        m.message = r.readString();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdUseConsumable / Server -> Client: SvConsumeResult
// ============================================================================
struct CmdUseConsumableMsg {
    uint8_t inventorySlot = 0;
    uint32_t targetEntityId = 0;  // 0 = no target (normal consumable)

    void write(ByteWriter& w) const {
        w.writeU8(inventorySlot);
        w.writeU32(targetEntityId);
    }
    static CmdUseConsumableMsg read(ByteReader& r) {
        CmdUseConsumableMsg m;
        m.inventorySlot = r.readU8();
        m.targetEntityId = r.readU32();
        return m;
    }
};

struct SvConsumeResultMsg {
    uint8_t success = 0;
    std::string message;

    void write(ByteWriter& w) const {
        w.writeU8(success);
        w.writeString(message);
    }
    static SvConsumeResultMsg read(ByteReader& r) {
        SvConsumeResultMsg m;
        m.success = r.readU8();
        m.message = r.readString();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdRankingQuery / Server -> Client: SvRankingResult
// ============================================================================
struct CmdRankingQueryMsg {
    uint8_t category = 0;
    uint8_t page = 0;
    uint8_t factionFilter = 0;  // 0 = all factions, 1-4 = specific faction

    void write(ByteWriter& w) const {
        w.writeU8(category);
        w.writeU8(page);
        w.writeU8(factionFilter);
    }
    static CmdRankingQueryMsg read(ByteReader& r) {
        CmdRankingQueryMsg m;
        m.category      = r.readU8();
        m.page          = r.readU8();
        m.factionFilter = r.readU8();
        return m;
    }
};

struct SvRankingResultMsg {
    uint8_t category = 0;
    uint8_t page = 0;
    uint8_t factionFilter = 0;
    uint16_t totalEntries = 0;
    std::string entriesJson;

    void write(ByteWriter& w) const {
        w.writeU8(category);
        w.writeU8(page);
        w.writeU8(factionFilter);
        w.writeU16(totalEntries);
        w.writeString(entriesJson);
    }
    static SvRankingResultMsg read(ByteReader& r) {
        SvRankingResultMsg m;
        m.category      = r.readU8();
        m.page          = r.readU8();
        m.factionFilter = r.readU8();
        m.totalEntries  = r.readU16();
        m.entriesJson   = r.readString();
        return m;
    }
};

// ============================================================================
// Dungeon Instance Messages
// ============================================================================

struct CmdStartDungeonMsg {
    std::string sceneId;
    void write(ByteWriter& w) const { w.writeString(sceneId); }
    static CmdStartDungeonMsg read(ByteReader& r) {
        CmdStartDungeonMsg m;
        m.sceneId = r.readString();
        return m;
    }
};

struct CmdDungeonResponseMsg {
    uint8_t accept = 0;  // 1 = accept, 0 = decline
    void write(ByteWriter& w) const { w.writeU8(accept); }
    static CmdDungeonResponseMsg read(ByteReader& r) {
        CmdDungeonResponseMsg m;
        m.accept = r.readU8();
        return m;
    }
};

struct SvDungeonInviteMsg {
    std::string sceneId;
    std::string dungeonName;
    uint16_t timeLimitSeconds = 600;
    uint8_t levelReq = 1;
    void write(ByteWriter& w) const {
        w.writeString(sceneId);
        w.writeString(dungeonName);
        w.writeU16(timeLimitSeconds);
        w.writeU8(levelReq);
    }
    static SvDungeonInviteMsg read(ByteReader& r) {
        SvDungeonInviteMsg m;
        m.sceneId = r.readString();
        m.dungeonName = r.readString();
        m.timeLimitSeconds = r.readU16();
        m.levelReq = r.readU8();
        return m;
    }
};

struct SvDungeonStartMsg {
    std::string sceneId;
    uint16_t timeLimitSeconds = 600;
    void write(ByteWriter& w) const {
        w.writeString(sceneId);
        w.writeU16(timeLimitSeconds);
    }
    static SvDungeonStartMsg read(ByteReader& r) {
        SvDungeonStartMsg m;
        m.sceneId = r.readString();
        m.timeLimitSeconds = r.readU16();
        return m;
    }
};

struct SvDungeonEndMsg {
    uint8_t reason = 0;  // 0=boss_killed, 1=timeout, 2=abandoned
    void write(ByteWriter& w) const { w.writeU8(reason); }
    static SvDungeonEndMsg read(ByteReader& r) {
        SvDungeonEndMsg m;
        m.reason = r.readU8();
        return m;
    }
};

// ============================================================================
// Server -> Client: SvSkillDefs (all skill definitions for client's class)
// ============================================================================
struct SkillDefEntry {
    std::string skillId;
    std::string skillName;
    std::string description;
    uint8_t skillType = 0;     // 0=Active, 1=Passive
    uint8_t resourceType = 0;  // 0=None, 1=Fury, 2=Mana
    uint8_t targetType = 0;    // SkillTargetType enum
    uint8_t levelRequired = 1;
    uint8_t maxRank = 3;
    uint8_t isUltimate = 0;
    uint8_t consumesAllResource = 0;
    float range = 0.0f;
    float aoeRadius = 0.0f;
    std::string vfxId;

    struct RankData {
        uint16_t resourceCost = 0;
        uint16_t cooldownMs = 0;
        uint16_t damagePercent = 100;
        uint8_t maxTargets = 1;
        uint16_t effectDurationMs = 0;
        uint16_t effectValue = 0;
        uint16_t stunDurationMs = 0;
        int16_t passiveDamageReduction = 0;
        int16_t passiveCritBonus = 0;
        int16_t passiveSpeedBonus = 0;
        int16_t passiveHPBonus = 0;
        int16_t passiveStatBonus = 0;
    };
    RankData ranks[3];

    void write(ByteWriter& w) const {
        w.writeString(skillId);
        w.writeString(skillName);
        w.writeString(description);
        w.writeU8(skillType);
        w.writeU8(resourceType);
        w.writeU8(targetType);
        w.writeU8(levelRequired);
        w.writeU8(maxRank);
        w.writeU8(isUltimate);
        w.writeU8(consumesAllResource);
        w.writeFloat(range);
        w.writeFloat(aoeRadius);
        w.writeString(vfxId);
        for (int i = 0; i < 3; ++i) {
            w.writeU16(ranks[i].resourceCost);
            w.writeU16(ranks[i].cooldownMs);
            w.writeU16(ranks[i].damagePercent);
            w.writeU8(ranks[i].maxTargets);
            w.writeU16(ranks[i].effectDurationMs);
            w.writeU16(ranks[i].effectValue);
            w.writeU16(ranks[i].stunDurationMs);
            w.writeU16(static_cast<uint16_t>(ranks[i].passiveDamageReduction));
            w.writeU16(static_cast<uint16_t>(ranks[i].passiveCritBonus));
            w.writeU16(static_cast<uint16_t>(ranks[i].passiveSpeedBonus));
            w.writeU16(static_cast<uint16_t>(ranks[i].passiveHPBonus));
            w.writeU16(static_cast<uint16_t>(ranks[i].passiveStatBonus));
        }
    }
    static SkillDefEntry read(ByteReader& r) {
        SkillDefEntry e;
        e.skillId = r.readString();
        e.skillName = r.readString();
        e.description = r.readString();
        e.skillType = r.readU8();
        e.resourceType = r.readU8();
        e.targetType = r.readU8();
        e.levelRequired = r.readU8();
        e.maxRank = r.readU8();
        e.isUltimate = r.readU8();
        e.consumesAllResource = r.readU8();
        e.range = r.readFloat();
        e.aoeRadius = r.readFloat();
        e.vfxId = r.readString();
        for (int i = 0; i < 3; ++i) {
            e.ranks[i].resourceCost = r.readU16();
            e.ranks[i].cooldownMs = r.readU16();
            e.ranks[i].damagePercent = r.readU16();
            e.ranks[i].maxTargets = r.readU8();
            e.ranks[i].effectDurationMs = r.readU16();
            e.ranks[i].effectValue = r.readU16();
            e.ranks[i].stunDurationMs = r.readU16();
            e.ranks[i].passiveDamageReduction = static_cast<int16_t>(r.readU16());
            e.ranks[i].passiveCritBonus = static_cast<int16_t>(r.readU16());
            e.ranks[i].passiveSpeedBonus = static_cast<int16_t>(r.readU16());
            e.ranks[i].passiveHPBonus = static_cast<int16_t>(r.readU16());
            e.ranks[i].passiveStatBonus = static_cast<int16_t>(r.readU16());
        }
        return e;
    }
};

struct SvSkillDefsMsg {
    std::vector<SkillDefEntry> defs;

    void write(ByteWriter& w) const {
        w.writeU16(static_cast<uint16_t>(defs.size()));
        for (const auto& d : defs) d.write(w);
    }
    static SvSkillDefsMsg read(ByteReader& r) {
        SvSkillDefsMsg m;
        uint16_t count = r.readU16();
        m.defs.resize(count);
        for (uint16_t i = 0; i < count; ++i) m.defs[i] = SkillDefEntry::read(r);
        return m;
    }
};

// ============================================================================
// Collection System Messages
// ============================================================================

struct CollectionDefEntry {
    uint32_t collectionId = 0;
    std::string name;
    std::string description;
    std::string category;
    std::string conditionType;
    std::string conditionTarget;
    int32_t conditionValue = 0;
    std::string rewardType;
    float rewardValue = 0.0f;

    void write(ByteWriter& w) const {
        w.writeU32(collectionId);
        w.writeString(name);
        w.writeString(description);
        w.writeString(category);
        w.writeString(conditionType);
        w.writeString(conditionTarget);
        w.writeI32(conditionValue);
        w.writeString(rewardType);
        w.writeFloat(rewardValue);
    }
    static CollectionDefEntry read(ByteReader& r) {
        CollectionDefEntry e;
        e.collectionId = r.readU32();
        e.name = r.readString();
        e.description = r.readString();
        e.category = r.readString();
        e.conditionType = r.readString();
        e.conditionTarget = r.readString();
        e.conditionValue = r.readI32();
        e.rewardType = r.readString();
        e.rewardValue = r.readFloat();
        return e;
    }
};

struct SvCollectionDefsMsg {
    std::vector<CollectionDefEntry> defs;

    void write(ByteWriter& w) const {
        w.writeU16(static_cast<uint16_t>(defs.size()));
        for (const auto& d : defs) d.write(w);
    }
    static SvCollectionDefsMsg read(ByteReader& r) {
        SvCollectionDefsMsg m;
        uint16_t count = r.readU16();
        m.defs.resize(count);
        for (uint16_t i = 0; i < count; ++i) m.defs[i] = CollectionDefEntry::read(r);
        return m;
    }
};

struct SvCollectionSyncMsg {
    std::vector<uint32_t> completedIds;
    int32_t bonusSTR = 0, bonusINT = 0, bonusDEX = 0, bonusCON = 0, bonusWIS = 0;
    int32_t bonusHP = 0, bonusMP = 0, bonusDamage = 0, bonusArmor = 0;
    float bonusCritRate = 0.0f, bonusMoveSpeed = 0.0f;

    void write(ByteWriter& w) const {
        w.writeU16(static_cast<uint16_t>(completedIds.size()));
        for (uint32_t id : completedIds) w.writeU32(id);
        w.writeI32(bonusSTR); w.writeI32(bonusINT); w.writeI32(bonusDEX);
        w.writeI32(bonusCON); w.writeI32(bonusWIS);
        w.writeI32(bonusHP); w.writeI32(bonusMP);
        w.writeI32(bonusDamage); w.writeI32(bonusArmor);
        w.writeFloat(bonusCritRate); w.writeFloat(bonusMoveSpeed);
    }
    static SvCollectionSyncMsg read(ByteReader& r) {
        SvCollectionSyncMsg m;
        uint16_t count = r.readU16();
        m.completedIds.resize(count);
        for (uint16_t i = 0; i < count; ++i) m.completedIds[i] = r.readU32();
        m.bonusSTR = r.readI32(); m.bonusINT = r.readI32(); m.bonusDEX = r.readI32();
        m.bonusCON = r.readI32(); m.bonusWIS = r.readI32();
        m.bonusHP = r.readI32(); m.bonusMP = r.readI32();
        m.bonusDamage = r.readI32(); m.bonusArmor = r.readI32();
        m.bonusCritRate = r.readFloat(); m.bonusMoveSpeed = r.readFloat();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdActivateSkillRank (spend a skill point)
// ============================================================================
struct CmdActivateSkillRankMsg {
    std::string skillId;

    void write(ByteWriter& w) const { w.writeString(skillId); }
    static CmdActivateSkillRankMsg read(ByteReader& r) {
        CmdActivateSkillRankMsg m;
        m.skillId = r.readString();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdAssignSkillSlot (rearrange skill bar)
// ============================================================================
struct CmdAssignSkillSlotMsg {
    uint8_t action = 0;   // 0=assign, 1=clear, 2=swap
    std::string skillId;  // used for action=0 (assign)
    uint8_t slotA = 0;    // target slot (0-19) for assign/clear, first slot for swap
    uint8_t slotB = 0;    // second slot for swap

    void write(ByteWriter& w) const {
        w.writeU8(action);
        w.writeString(skillId);
        w.writeU8(slotA);
        w.writeU8(slotB);
    }
    static CmdAssignSkillSlotMsg read(ByteReader& r) {
        CmdAssignSkillSlotMsg m;
        m.action  = r.readU8();
        m.skillId = r.readString();
        m.slotA   = r.readU8();
        m.slotB   = r.readU8();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdAllocateStat (allocate free stat points)
// ============================================================================
struct CmdAllocateStatMsg {
    uint8_t statType = 0;  // 0=STR, 1=INT, 2=DEX, 3=CON, 4=WIS
    int16_t amount = 1;    // how many points to allocate (usually 1)

    void write(ByteWriter& w) const {
        w.writeU8(statType);
        w.writeU16(static_cast<uint16_t>(amount));
    }
    static CmdAllocateStatMsg read(ByteReader& r) {
        CmdAllocateStatMsg m;
        m.statType = r.readU8();
        m.amount   = static_cast<int16_t>(r.readU16());
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdEquipCostume
// ============================================================================
struct CmdEquipCostumeMsg {
    std::string costumeDefId;

    void write(ByteWriter& w) const { w.writeString(costumeDefId); }
    static CmdEquipCostumeMsg read(ByteReader& r) {
        CmdEquipCostumeMsg m;
        m.costumeDefId = r.readString();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdUnequipCostume
// ============================================================================
struct CmdUnequipCostumeMsg {
    uint8_t slotType = 0;

    void write(ByteWriter& w) const { w.writeU8(slotType); }
    static CmdUnequipCostumeMsg read(ByteReader& r) {
        CmdUnequipCostumeMsg m;
        m.slotType = r.readU8();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdToggleCostumes
// ============================================================================
struct CmdToggleCostumesMsg {
    uint8_t show = 1;

    void write(ByteWriter& w) const { w.writeU8(show); }
    static CmdToggleCostumesMsg read(ByteReader& r) {
        CmdToggleCostumesMsg m;
        m.show = r.readU8();
        return m;
    }
};

// ============================================================================
// Client -> Server: CmdEditorPause
// ============================================================================
struct CmdEditorPauseMsg {
    uint8_t paused = 0;

    void write(ByteWriter& w) const { w.writeU8(paused); }
    static CmdEditorPauseMsg read(ByteReader& r) {
        CmdEditorPauseMsg m;
        m.paused = r.readU8();
        return m;
    }
};

// ============================================================================
// Server -> Client: SvCostumeDefs (all costume definitions)
// ============================================================================
struct CostumeDefEntry {
    std::string costumeDefId;
    std::string displayName;
    uint8_t     slotType    = 0;
    uint16_t    visualIndex = 0;
    uint8_t     rarity      = 0;
    std::string source;

    void write(ByteWriter& w) const {
        w.writeString(costumeDefId);
        w.writeString(displayName);
        w.writeU8(slotType);
        w.writeU16(visualIndex);
        w.writeU8(rarity);
        w.writeString(source);
    }
    static CostumeDefEntry read(ByteReader& r) {
        CostumeDefEntry e;
        e.costumeDefId = r.readString();
        e.displayName  = r.readString();
        e.slotType     = r.readU8();
        e.visualIndex  = r.readU16();
        e.rarity       = r.readU8();
        e.source       = r.readString();
        return e;
    }
};

struct SvCostumeDefsMsg {
    std::vector<CostumeDefEntry> defs;

    void write(ByteWriter& w) const {
        w.writeU16(static_cast<uint16_t>(defs.size()));
        for (const auto& d : defs) d.write(w);
    }
    static SvCostumeDefsMsg read(ByteReader& r) {
        SvCostumeDefsMsg m;
        uint16_t count = r.readU16();
        m.defs.resize(count);
        for (uint16_t i = 0; i < count; ++i) m.defs[i] = CostumeDefEntry::read(r);
        return m;
    }
};

// ============================================================================
// Server -> Client: SvCostumeSync (full state on login)
// ============================================================================
struct SvCostumeSyncMsg {
    uint8_t showCostumes = 1;
    std::vector<std::string> ownedCostumeIds;
    std::vector<std::pair<uint8_t, std::string>> equipped; // {slotType, costumeDefId}

    void write(ByteWriter& w) const {
        w.writeU8(showCostumes);
        w.writeU16(static_cast<uint16_t>(ownedCostumeIds.size()));
        for (const auto& id : ownedCostumeIds) w.writeString(id);
        w.writeU8(static_cast<uint8_t>(equipped.size()));
        for (const auto& [slot, id] : equipped) {
            w.writeU8(slot);
            w.writeString(id);
        }
    }
    static SvCostumeSyncMsg read(ByteReader& r) {
        SvCostumeSyncMsg m;
        m.showCostumes = r.readU8();
        uint16_t ownedCount = r.readU16();
        m.ownedCostumeIds.reserve(ownedCount);
        for (uint16_t i = 0; i < ownedCount; ++i) m.ownedCostumeIds.push_back(r.readString());
        uint8_t equipCount = r.readU8();
        m.equipped.reserve(equipCount);
        for (uint8_t i = 0; i < equipCount; ++i) {
            uint8_t slot = r.readU8();
            std::string id = r.readString();
            m.equipped.emplace_back(slot, std::move(id));
        }
        return m;
    }
};

// ============================================================================
// Server -> Client: SvCostumeUpdate (incremental)
// ============================================================================
struct SvCostumeUpdateMsg {
    uint8_t updateType = 0; // 0=obtained, 1=equipped, 2=unequipped, 3=toggleChanged
    std::string costumeDefId;
    uint8_t slotType = 0;
    uint8_t show = 1;

    void write(ByteWriter& w) const {
        w.writeU8(updateType);
        w.writeString(costumeDefId);
        w.writeU8(slotType);
        w.writeU8(show);
    }
    static SvCostumeUpdateMsg read(ByteReader& r) {
        SvCostumeUpdateMsg m;
        m.updateType   = r.readU8();
        m.costumeDefId = r.readString();
        m.slotType     = r.readU8();
        m.show         = r.readU8();
        return m;
    }
};

// ============================================================================
// Server -> Client: SvGuildRoster (full member list for GuildPanel)
// ============================================================================
struct SvGuildRosterMsg {
    struct Member {
        std::string name;
        uint8_t level = 1;
        std::string rank;
        uint8_t online = 0;
    };
    std::vector<Member> members;

    void write(ByteWriter& w) const {
        w.writeU8(static_cast<uint8_t>(members.size()));
        for (const auto& m : members) {
            w.writeString(m.name);
            w.writeU8(m.level);
            w.writeString(m.rank);
            w.writeU8(m.online);
        }
    }
    static SvGuildRosterMsg read(ByteReader& r) {
        SvGuildRosterMsg msg;
        uint8_t count = r.readU8();
        msg.members.reserve(count);
        for (uint8_t i = 0; i < count; ++i) {
            Member m;
            m.name   = r.readString();
            m.level  = r.readU8();
            m.rank   = r.readString();
            m.online = r.readU8();
            msg.members.push_back(std::move(m));
        }
        return msg;
    }
};

// ============================================================================
// Server -> Client: SvMarketListings (browse/my-listings data)
// ============================================================================
struct SvMarketListingsMsg {
    uint8_t action = 0;     // 3=Browse, 4=MyListings
    int32_t page = 0;
    int32_t totalPages = 0;
    uint64_t nonce = 0;

    struct ListingEntry {
        int32_t listingId = 0;
        std::string itemId;
        std::string itemName;
        uint8_t enchantLevel = 0;
        std::string rarity;
        int32_t quantity = 0;
        std::string sellerName;
        int64_t priceGold = 0;
        std::string category;
    };
    std::vector<ListingEntry> listings;

    void write(ByteWriter& w) const {
        w.writeU8(action);
        w.writeI32(page);
        w.writeI32(totalPages);
        detail::writeU64(w, nonce);
        w.writeU16(static_cast<uint16_t>(listings.size()));
        for (const auto& l : listings) {
            w.writeI32(l.listingId);
            w.writeString(l.itemId);
            w.writeString(l.itemName);
            w.writeU8(l.enchantLevel);
            w.writeString(l.rarity);
            w.writeI32(l.quantity);
            w.writeString(l.sellerName);
            detail::writeI64(w, l.priceGold);
            w.writeString(l.category);
        }
    }
    static SvMarketListingsMsg read(ByteReader& r) {
        SvMarketListingsMsg msg;
        msg.action     = r.readU8();
        msg.page       = r.readI32();
        msg.totalPages = r.readI32();
        msg.nonce      = detail::readU64(r);
        uint16_t count = r.readU16();
        msg.listings.reserve(count);
        for (uint16_t i = 0; i < count; ++i) {
            ListingEntry l;
            l.listingId    = r.readI32();
            l.itemId       = r.readString();
            l.itemName     = r.readString();
            l.enchantLevel = r.readU8();
            l.rarity       = r.readString();
            l.quantity     = r.readI32();
            l.sellerName   = r.readString();
            l.priceGold    = detail::readI64(r);
            l.category     = r.readString();
            msg.listings.push_back(std::move(l));
        }
        return msg;
    }
};

} // namespace fate
