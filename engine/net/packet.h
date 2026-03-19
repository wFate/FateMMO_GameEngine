#pragma once
#include <cstdint>
#include "engine/net/byte_stream.h"

namespace fate {

// ============================================================================
// Protocol Constants
// ============================================================================
constexpr uint16_t PROTOCOL_ID       = 0xFA7E;
constexpr uint8_t  PROTOCOL_VERSION  = 1;
constexpr size_t   PACKET_HEADER_SIZE = 16;
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
} // namespace PacketType

// ============================================================================
// PacketHeader (16 bytes)
// ============================================================================
struct PacketHeader {
    uint16_t protocolId   = PROTOCOL_ID;
    uint32_t sessionToken = 0;
    uint16_t sequence     = 0;
    uint16_t ack          = 0;
    uint16_t ackBits      = 0;
    Channel  channel      = Channel::Unreliable;
    uint8_t  packetType   = 0;
    uint16_t payloadSize  = 0;

    void write(ByteWriter& w) const {
        w.writeU16(protocolId);
        w.writeU32(sessionToken);
        w.writeU16(sequence);
        w.writeU16(ack);
        w.writeU16(ackBits);
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
        h.ackBits      = r.readU16();
        h.channel      = static_cast<Channel>(r.readU8());
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
