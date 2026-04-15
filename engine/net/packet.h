#pragma once
#include <cstdint>
#include "engine/net/byte_stream.h"

namespace fate {

// ============================================================================
// Protocol Constants
// ============================================================================
constexpr uint16_t PROTOCOL_ID       = 0xFA7E;
constexpr uint8_t  PROTOCOL_VERSION  = 1;
constexpr size_t   PACKET_HEADER_SIZE = 18;
constexpr size_t   MAX_PACKET_SIZE   = 1200;
constexpr size_t   MAX_PAYLOAD_SIZE  = MAX_PACKET_SIZE - PACKET_HEADER_SIZE;

// ============================================================================
// Channel
// ============================================================================
enum class Channel : uint8_t {
    Unreliable       = 0,
    ReliableOrdered  = 1,
    ReliableUnordered = 2
};

// ============================================================================
// PacketType — system, client->server, server->client
// ============================================================================
namespace PacketType {
    // System
    constexpr uint8_t Connect       = 0x01;
    constexpr uint8_t Disconnect    = 0x02;
    constexpr uint8_t Heartbeat     = 0x03;
    constexpr uint8_t ConnectAccept = 0x80;
    constexpr uint8_t ConnectReject = 0x81;
    constexpr uint8_t KeyExchange  = 0x82;
    constexpr uint8_t Rekey        = 0x83;

    // Client -> Server
    constexpr uint8_t CmdMove          = 0x10;
    constexpr uint8_t CmdAction        = 0x11;
    constexpr uint8_t CmdChat          = 0x12;
    constexpr uint8_t CmdTrade         = 0x13;
    constexpr uint8_t CmdQuestAction   = 0x14;
    constexpr uint8_t CmdMarket        = 0x15;
    constexpr uint8_t CmdBounty        = 0x16;
    constexpr uint8_t CmdGauntlet      = 0x17;
    constexpr uint8_t CmdGuild         = 0x18;
    constexpr uint8_t CmdSocial        = 0x19;
    constexpr uint8_t CmdZoneTransition = 0x1A;
    constexpr uint8_t CmdRespawn       = 0x1B;
    constexpr uint8_t CmdUseSkill     = 0x1C;

    // Server -> Client
    constexpr uint8_t SvEntityEnter        = 0x90;
    constexpr uint8_t SvEntityLeave        = 0x91;
    constexpr uint8_t SvEntityUpdate       = 0x92;
    constexpr uint8_t SvCombatEvent        = 0x93;
    constexpr uint8_t SvChatMessage        = 0x94;
    constexpr uint8_t SvPlayerState        = 0x95;
    constexpr uint8_t SvMovementCorrection = 0x96;
    constexpr uint8_t SvZoneTransition     = 0x97;
    constexpr uint8_t SvLootPickup         = 0x98;
    constexpr uint8_t SvTradeUpdate        = 0x99;
    constexpr uint8_t SvMarketResult       = 0x9A;
    constexpr uint8_t SvBountyUpdate       = 0x9B;
    constexpr uint8_t SvGauntletUpdate     = 0x9C;
    constexpr uint8_t SvGuildUpdate        = 0x9D;
    constexpr uint8_t SvSocialUpdate       = 0x9E;
    constexpr uint8_t SvQuestUpdate        = 0x9F;
    constexpr uint8_t SvDeathNotify    = 0xA0;
    constexpr uint8_t SvRespawn        = 0xA1;
    constexpr uint8_t SvSkillResult   = 0xA2;
    constexpr uint8_t SvSkillSync     = 0xA3;
    constexpr uint8_t SvQuestSync     = 0xA4;
    constexpr uint8_t SvInventorySync = 0xA5;
    constexpr uint8_t SvLevelUp       = 0xA6;
    constexpr uint8_t SvBossLootOwner = 0xA7;

    // Client -> Server: Equipment
    constexpr uint8_t CmdEquip        = 0x1D;
    constexpr uint8_t CmdEnchant      = 0x1E;
    constexpr uint8_t CmdRepair       = 0x1F;
    constexpr uint8_t CmdExtractCore  = 0x20;
    constexpr uint8_t CmdCraft        = 0x21;
    constexpr uint8_t CmdBattlefield  = 0x22;
    constexpr uint8_t CmdArena        = 0x23;
    constexpr uint8_t CmdPet          = 0x24;
    constexpr uint8_t CmdBank         = 0x25;
    constexpr uint8_t CmdSocketItem   = 0x26;
    constexpr uint8_t CmdStatEnchant  = 0x27;
    constexpr uint8_t CmdUseConsumable = 0x28;
    constexpr uint8_t CmdRankingQuery = 0x29;
    constexpr uint8_t CmdStartDungeon    = 0x2A;
    constexpr uint8_t CmdDungeonResponse = 0x2B;
    constexpr uint8_t CmdShopBuy           = 0x2C;
    constexpr uint8_t CmdShopSell          = 0x2D;
    constexpr uint8_t CmdTeleport          = 0x2E;
    constexpr uint8_t CmdBankDepositItem   = 0x2F;
    constexpr uint8_t CmdBankWithdrawItem  = 0x30;
    constexpr uint8_t CmdBankDepositGold   = 0x31;
    constexpr uint8_t CmdBankWithdrawGold  = 0x32;
    constexpr uint8_t CmdMoveItem          = 0x33;
    constexpr uint8_t CmdDestroyItem       = 0x34;
    constexpr uint8_t CmdActivateSkillRank = 0x35;
    constexpr uint8_t CmdAssignSkillSlot   = 0x36;
    constexpr uint8_t CmdAllocateStat      = 0x37;
    constexpr uint8_t CmdEquipCostume      = 0x38;
    constexpr uint8_t CmdUnequipCostume    = 0x39;
    constexpr uint8_t CmdToggleCostumes    = 0x3A;
    constexpr uint8_t CmdEditorPause       = 0x3B;
    constexpr uint8_t CmdParty             = 0x3C;

    // Admin content pipeline (editor -> server)
    constexpr uint8_t CmdAdminSaveContent       = 0x3D;
    constexpr uint8_t CmdAdminDeleteContent      = 0x3E;
    constexpr uint8_t CmdAdminReloadCache        = 0x3F;
    constexpr uint8_t CmdAdminValidate           = 0x40;
    constexpr uint8_t CmdAdminRequestContentList = 0x41;
    constexpr uint8_t CmdEmoticon                = 0x42;
    constexpr uint8_t CmdOpenCrafting            = 0x43;
    constexpr uint8_t CmdOpenBag                = 0x44;
    constexpr uint8_t CmdBagStore               = 0x45;
    constexpr uint8_t CmdBagRetrieve            = 0x46;
    constexpr uint8_t CmdClaimAdReward          = 0x47;
    constexpr uint8_t CmdBagUseItem             = 0x48;
    constexpr uint8_t CmdBagDestroyItem         = 0x49;
    constexpr uint8_t CmdBagMoveItem            = 0x4C;
    constexpr uint8_t CmdSetRecall              = 0x4A;
    constexpr uint8_t CmdSpectateScene          = 0x4B;

    // Server -> Client: Item system results
    constexpr uint8_t SvEnchantResult    = 0xA8;
    constexpr uint8_t SvRepairResult     = 0xA9;
    constexpr uint8_t SvExtractResult    = 0xAA;
    constexpr uint8_t SvCraftResult      = 0xAB;
    constexpr uint8_t SvBattlefieldUpdate = 0xAC;
    constexpr uint8_t SvArenaUpdate       = 0xAD;
    constexpr uint8_t SvPetUpdate         = 0xAE;
    constexpr uint8_t SvBankResult        = 0xAF;
    constexpr uint8_t SvSocketResult      = 0xB0;
    constexpr uint8_t SvStatEnchantResult = 0xB1;
    constexpr uint8_t SvConsumeResult     = 0xB2;
    constexpr uint8_t SvRankingResult     = 0xB3;
    constexpr uint8_t SvDungeonInvite = 0xB4;
    constexpr uint8_t SvDungeonStart  = 0xB5;
    constexpr uint8_t SvDungeonEnd       = 0xB6;
    constexpr uint8_t SvShopResult       = 0xB7;
    constexpr uint8_t SvTeleportResult   = 0xB8;
    constexpr uint8_t SvAuroraStatus    = 0xB9;
    constexpr uint8_t SvEntityUpdateBatch = 0xBA; // Multiple delta updates in one packet
    constexpr uint8_t SvSkillDefs       = 0xBB; // Skill definitions for client's class
    constexpr uint8_t SvCollectionSync  = 0xBC;
    constexpr uint8_t SvCollectionDefs  = 0xBD;
    constexpr uint8_t SvCostumeSync     = 0xBE;
    constexpr uint8_t SvCostumeUpdate   = 0xBF;
    constexpr uint8_t SvCostumeDefs     = 0xC0;
    constexpr uint8_t SvBuffSync        = 0xC1;
    constexpr uint8_t SvPartyUpdate     = 0xC2;

    // Admin content pipeline (server -> editor)
    constexpr uint8_t SvAdminResult       = 0xC3;
    constexpr uint8_t SvAdminContentList  = 0xC4;
    constexpr uint8_t SvValidationReport  = 0xC5;
    constexpr uint8_t SvGuildRoster       = 0xC6;
    constexpr uint8_t SvMarketListings    = 0xC7;
    constexpr uint8_t SvEmoticon                 = 0xC8;
    constexpr uint8_t SvCraftingRecipeList       = 0xC9;
    constexpr uint8_t SvBagContents              = 0xCA;
    constexpr uint8_t SvAdRewardResult           = 0xCB;
    constexpr uint8_t SvKick                     = 0xCC;
    constexpr uint8_t SvRecallResult             = 0xCD;
    constexpr uint8_t SvScenePopulated           = 0xCE; // Server finished initial entity replication for scene
} // namespace PacketType

// ============================================================================
// PacketHeader (18 bytes)
// ============================================================================
struct PacketHeader {
    uint16_t protocolId   = PROTOCOL_ID;
    uint32_t sessionToken = 0;
    uint16_t sequence     = 0;
    uint16_t ack          = 0;
    uint32_t ackBits      = 0;
    Channel  channel      = Channel::Unreliable;
    uint8_t  packetType   = 0;
    uint16_t payloadSize  = 0;

    void write(ByteWriter& w) const {
        w.writeU16(protocolId);
        w.writeU32(sessionToken);
        w.writeU16(sequence);
        w.writeU16(ack);
        w.writeU32(ackBits);
        w.writeU8(static_cast<uint8_t>(channel));
        w.writeU8(packetType);
        w.writeU16(payloadSize);
    }

    static PacketHeader read(ByteReader& r) {
        PacketHeader h;
        h.protocolId   = r.readU16();
        h.sessionToken = r.readU32();
        h.sequence     = r.readU16();
        h.ack          = r.readU16();
        h.ackBits      = r.readU32();
        uint8_t rawChannel = r.readU8();
        if (rawChannel > static_cast<uint8_t>(Channel::ReliableUnordered)) {
            h.channel = Channel::Unreliable; // invalid channel, default to Unreliable
            h.protocolId = 0; // mark header as invalid so callers reject it
        } else {
            h.channel = static_cast<Channel>(rawChannel);
        }
        h.packetType   = r.readU8();
        h.payloadSize  = r.readU16();
        return h;
    }
};

// ============================================================================
// Sequence number wrap-safe comparison
// ============================================================================
inline bool sequenceGreaterThan(uint16_t a, uint16_t b) {
    return static_cast<int16_t>(a - b) > 0;
}

} // namespace fate
