#pragma once
#include <cstdint>
#include <string>
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
    constexpr uint8_t Confirm      = 7;
    constexpr uint8_t Cancel       = 8;
}

// ============================================================================
// Client -> Server: CmdMarket sub-actions
// ============================================================================
namespace MarketAction {
    constexpr uint8_t ListItem       = 0;  // + instanceId:string, priceGold:i64
    constexpr uint8_t BuyItem        = 1;  // + listingId:i32
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
// Client -> Server: CmdQuestAction sub-actions
// ============================================================================
namespace QuestAction {
    constexpr uint8_t Accept   = 0;  // + questId:string
    constexpr uint8_t Abandon  = 1;  // + questId:string
    constexpr uint8_t TurnIn   = 2;  // + questId:string
}

// ============================================================================
// Server -> Client result messages
// ============================================================================

struct SvTradeUpdateMsg {
    uint8_t updateType = 0;  // 0=invited, 1=sessionStarted, 2=itemAdded, 3=locked, 4=confirmed, 5=completed, 6=cancelled
    int32_t sessionId  = 0;
    std::string otherPlayerName;
    uint8_t resultCode = 0;  // 0=success, 1+=error codes from TradeActionResult

    void write(ByteWriter& w) const {
        w.writeU8(updateType);
        w.writeI32(sessionId);
        w.writeString(otherPlayerName);
        w.writeU8(resultCode);
    }
    static SvTradeUpdateMsg read(ByteReader& r) {
        SvTradeUpdateMsg m;
        m.updateType     = r.readU8();
        m.sessionId      = r.readI32();
        m.otherPlayerName = r.readString();
        m.resultCode     = r.readU8();
        return m;
    }
};

struct SvMarketResultMsg {
    uint8_t action     = 0;  // mirrors MarketAction
    uint8_t resultCode = 0;  // 0=success, 1+=error
    int32_t listingId  = 0;
    std::string message;

    void write(ByteWriter& w) const {
        w.writeU8(action);
        w.writeU8(resultCode);
        w.writeI32(listingId);
        w.writeString(message);
    }
    static SvMarketResultMsg read(ByteReader& r) {
        SvMarketResultMsg m;
        m.action     = r.readU8();
        m.resultCode = r.readU8();
        m.listingId  = r.readI32();
        m.message    = r.readString();
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

struct SvQuestUpdateMsg {
    uint8_t updateType = 0;  // 0=accepted, 1=progressUpdate, 2=completed, 3=abandoned, 4=result
    std::string questId;
    int32_t currentCount = 0;
    int32_t targetCount  = 0;
    std::string message;

    void write(ByteWriter& w) const {
        w.writeU8(updateType);
        w.writeString(questId);
        w.writeI32(currentCount);
        w.writeI32(targetCount);
        w.writeString(message);
    }
    static SvQuestUpdateMsg read(ByteReader& r) {
        SvQuestUpdateMsg m;
        m.updateType   = r.readU8();
        m.questId      = r.readString();
        m.currentCount = r.readI32();
        m.targetCount  = r.readI32();
        m.message      = r.readString();
        return m;
    }
};

} // namespace fate
