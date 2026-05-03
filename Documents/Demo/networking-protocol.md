# Networking Protocol

Audience: Engine Contributor, Full Game / Server

The networking layer is a custom UDP protocol with reliability, encrypted sessions, version gating, and server-authoritative game messages. The public demo may not expose a complete MMO server, but the protocol is part of the engine architecture.

## Source Anchors

- `engine/net/packet.h`: packet IDs, protocol version, packet header.
- `engine/net/game_messages.h`: payload structs and serializers.
- `engine/net/reliability.h`: reliable packet tracking and ACK processing.
- `engine/net/net_client.cpp`: client connect, key exchange, AuthProof, receive dispatch.
- `engine/net/net_server.cpp`: server accept, version check, auth gate, packet dispatch.
- `engine/net/packet_crypto.h`: encryption, Noise_NK derivation, rekeying.
- `engine/net/server_identity.h`: server identity key support.

## Protocol Version

The current source declares:

```cpp
constexpr uint8_t PROTOCOL_VERSION = 20;
```

Clients and servers must agree on the protocol version. A mismatch is rejected during connection setup. Do not document a hard-coded version in website copy without checking `engine/net/packet.h` first.

## Packet Envelope

The packet layer uses:

- Magic value.
- Protocol version.
- Packet type.
- Channel.
- Sequence data.
- ACK data.
- Payload bytes.

The v9+ reliability model widened the inline ACK bitfield to 64 bits and added `CmdAckExtended` for out-of-window recovery.

## Channels

The protocol distinguishes traffic by channel semantics. Common categories are:

- Unreliable: state that can be superseded.
- Reliable ordered: game actions and critical state.
- Critical-lane reliable packets: load-bearing packets that bypass congestion checks.

Packet channel choice is part of gameplay correctness. Death, respawn, zone transition, and entity enter/leave behavior must not be dropped just because ordinary reliable traffic is congested.

## Reliability

The reliability layer tracks pending reliable packets, retransmits based on RTT, and processes ACKs from the packet header.

Key behavior:

- RTT is estimated from ACK timing.
- Retransmission delay is based on RTT with a floor.
- Pending queues have capacity limits.
- Congestion checks protect the queue.
- Critical-lane packets bypass the normal congestion rejection.
- `CmdAckExtended` removes stranded packets that fall outside the inline ACK window.

## Noise_NK Handshake

The secure connection path uses a Noise_NK-style handshake with:

- Client ephemeral X25519 key.
- Server static identity key.
- Server ephemeral X25519 key.
- Two DH results: `es` and `ee`.
- BLAKE2b-based key derivation.
- Separate transmit and receive keys.

The client can load the server identity public key to prevent man-in-the-middle attacks. Without a trusted server identity key, the client may still connect in some development paths, but that is not the secure production posture.

## Encryption

Payload encryption uses XChaCha20-Poly1305 through libsodium.

Important properties:

- Separate tx/rx keys.
- Per-session nonce prefix.
- Packet sequence folded into nonce data.
- 16-byte authentication tag.
- Tampered encrypted packets are rejected.
- `CmdAuthProof` is sent after encryption is active.

Do not send auth tokens in plaintext. The current flow sends the auth proof only after the encrypted Noise session has been established.

## AuthProof Gate

Connection state is split into phases:

- Handshake pending.
- Proof received.
- Authenticated.

Game packet handlers are gated until the auth proof verifies. This prevents unauthenticated clients from issuing gameplay commands after only a transport-level handshake.

## Rekeying

The protocol supports symmetric rekeying.

The source describes:

- Rekey after a packet-count threshold.
- Rekey after a time threshold.
- Server-initiated rekey epoch.
- Client-side epoch gate to avoid applying duplicate/retransmitted rekeys twice.
- Grace period for previous keys during transition.

When changing rekey behavior, test retransmits and out-of-order packets. Rekey bugs usually look like sudden silent packet drops.

## Critical-Lane Packets

Critical-lane packets are reliable packets that bypass the regular congestion check because dropping them breaks visible game state.

Current examples include:

- `SvEntityEnter`
- `SvEntityLeave`
- `SvPlayerState`
- `SvZoneTransition`
- `SvDeathNotify`
- `SvRespawn`
- `SvKick`
- `SvScenePopulated`
- `SvEntityEnterBatch`
- `SvSkillResultBatch`
- `SvEntityLeaveBatch`

Treat this list as source-derived. Re-check `packet_crypto.h` or the packet classification helper before publishing a final list.

## Packet Catalog Policy

The packet catalog changes often. The docs should avoid hand-maintained stale tables where possible.

Recommended source command:

```powershell
rg -n "constexpr uint8_t .*=" engine\net\packet.h
```

For payload shape, inspect:

```powershell
rg -n "struct Cmd|struct Sv" engine\net\game_messages.h
```

## Common Packet Groups

System:

- `Connect`
- `Disconnect`
- `Heartbeat`
- `ConnectAccept`
- `ConnectReject`
- `KeyExchange`
- `Rekey`
- `CmdAckExtended`
- `CmdAuthProof`

Movement and entity state:

- `CmdMove`
- `CmdAction`
- `SvEntityEnter`
- `SvEntityLeave`
- `SvEntityUpdate`
- `SvEntityEnterBatch`
- `SvEntityLeaveBatch`
- `SvEntityUpdateBatch`
- `SvMovementCorrection`

Player/gameplay state:

- `SvPlayerState`
- `SvDeathNotify`
- `SvRespawn`
- `SvSkillResult`
- `SvSkillResultBatch`
- `SvInventorySync`
- `SvZoneTransition`

Full game/server packets:

- Trade, market, bounty, guild, social, party, quest, dungeon, pet, bank, crafting, shop, admin, and dialogue packets are full-game/server surface unless the release package explicitly includes those systems.

## Version Change Checklist

When changing packet shape or packet meaning:

1. Update `PROTOCOL_VERSION` when old and new peers cannot safely interoperate.
2. Add packet IDs in `engine/net/packet.h`.
3. Add payload serializers in `engine/net/game_messages.h`.
4. Update client dispatch.
5. Update server dispatch.
6. Add focused encode/decode tests.
7. Add client/server round-trip or handler tests where practical.
8. Document whether the change requires client redistribution, server restart, or both.

