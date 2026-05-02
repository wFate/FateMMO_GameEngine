#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <chrono>

namespace fate {

class PacketCrypto {
public:
    static constexpr size_t KEY_SIZE = 32;
    static constexpr size_t TAG_SIZE = 16;
    static constexpr size_t PUBLIC_KEY_SIZE = 32;
    static constexpr size_t SECRET_KEY_SIZE = 32;

    using Key = std::array<uint8_t, KEY_SIZE>;
    using PublicKey = std::array<uint8_t, PUBLIC_KEY_SIZE>;
    using SecretKey = std::array<uint8_t, SECRET_KEY_SIZE>;

    struct SessionKeys {
        Key txKey;  // encrypt key for this side
        Key rxKey;  // decrypt key for this side
    };

    struct Keypair {
        PublicKey pk;  // public key (safe to send over network)
        SecretKey sk;  // secret key (never leaves this process)
    };

    // Initialize the crypto library. Returns true on success.
    static bool initLibrary();

    // Generate a pair of session keys (one for each direction). LEGACY — prefer DH exchange.
    static SessionKeys generateSessionKeys();

    // Generate an X25519 keypair for Diffie-Hellman key exchange.
    static Keypair generateKeypair();

    // Derive session keys on the client side from client keypair + server public key.
    // Returns keys from the client's perspective (txKey = client encrypts, rxKey = client decrypts).
    static SessionKeys deriveClientSessionKeys(const PublicKey& clientPk, const SecretKey& clientSk,
                                               const PublicKey& serverPk);

    // Derive session keys on the server side from server keypair + client public key.
    // Returns keys from the server's perspective (txKey = server encrypts, rxKey = server decrypts).
    static SessionKeys deriveServerSessionKeys(const PublicKey& serverPk, const SecretKey& serverSk,
                                               const PublicKey& clientPk);

    // ── Noise_NK key derivation (MITM-resistant) ──────────────────────────
    // Two DH operations: es (ephemeral×static) + ee (ephemeral×ephemeral),
    // combined via BLAKE2b.  The server's static key is pre-shared with the
    // client, so a MITM cannot forge the es DH without the private key.

    // Client side: has own ephemeral SK, knows server static PK (embedded),
    // receives server ephemeral PK via KeyExchange packet.
    static SessionKeys deriveNoiseNKClientKeys(const SecretKey& clientEphSk,
                                               const PublicKey& serverStaticPk,
                                               const PublicKey& serverEphPk);

    // Server side: has static SK + ephemeral SK, receives client ephemeral PK
    // via Connect packet.
    static SessionKeys deriveNoiseNKServerKeys(const SecretKey& serverStaticSk,
                                               const SecretKey& serverEphSk,
                                               const PublicKey& clientEphPk);

    // Securely wipe sensitive memory (e.g. secret keys after use).
    static void secureWipe(void* data, size_t size);

    // CSPRNG — fills `out` with `n` cryptographically secure random bytes.
    // Fatal if libsodium is unavailable; never returns predictable output.
    static void randomBytes(void* out, size_t n);

    // Set the encrypt/decrypt keys for this session (also resets nonce counters).
    void setKeys(const Key& encryptKey, const Key& decryptKey);
    bool hasKeys() const { return keysSet_; }
    void clearKeys();

    // Build a unique 64-bit nonce from a per-direction key-derived prefix + the
    // 16-bit packet sequence number.  The prefix is the first 6 bytes of the
    // key shifted left 16 bits, so both sides derive the same prefix for the
    // same direction (sender's encryptKey == receiver's decryptKey).
    // This is order-independent (works with UDP reordering/retransmits) and
    // unique per session because each DH exchange produces fresh keys.
    uint64_t buildEncryptNonce(uint16_t sequence) const { return encNoncePrefix_ | sequence; }
    uint64_t buildDecryptNonce(uint16_t sequence) const { return decNoncePrefix_ | sequence; }

    // Encrypt payload in-place.
    // Output buffer must have capacity for plaintextSize + TAG_SIZE bytes.
    // nonce = monotonic counter from nextSendNonce() (unique per packet direction).
    // Returns true on success.
    bool encrypt(const uint8_t* plaintext, size_t plaintextSize,
                 uint64_t nonce,
                 uint8_t* ciphertext, size_t ciphertextCapacity);

    // Decrypt ciphertext.
    // Output buffer must have capacity for ciphertextSize - TAG_SIZE bytes.
    // Returns false if authentication fails (tampered or wrong key).
    // Falls back to previous keys during rekey grace period.
    bool decrypt(const uint8_t* ciphertext, size_t ciphertextSize,
                 uint64_t nonce,
                 uint8_t* plaintext, size_t plaintextCapacity);

    // Check whether real crypto is available (libsodium compiled in).
    static bool isAvailable();

    // ── Symmetric rekeying ────────────────────────────────────────────────
    // Derives new keys from current keys via KDF.  Both sides call this at
    // the same logical point (server sends Rekey packet, client processes it)
    // so they derive identical new keys.  Previous keys kept for a grace
    // period to handle in-flight UDP packets.
    static constexpr uint64_t REKEY_AFTER_PACKETS = 1ULL << 16; // 65536
    static constexpr int REKEY_AFTER_MINUTES = 15;

    bool needsRekey() const;
    void symmetricRekey();
    uint64_t packetsEncrypted() const { return packetsEncrypted_; }

    // v9: client-side epoch gate for server-initiated rekeys. Server increments
    // this counter per rekey and sends the new value as the Rekey payload.
    // Client only applies symmetricRekey() if the incoming epoch is strictly
    // greater than the last-applied epoch. Protects against duplicate Rekey
    // retransmits desyncing client keys from server keys.
    uint32_t serverRekeyEpoch() const { return serverRekeyEpoch_; }
    bool tryAdvanceRekeyEpoch(uint32_t incomingEpoch) {
        if (incomingEpoch <= serverRekeyEpoch_) return false;
        serverRekeyEpoch_ = incomingEpoch;
        return true;
    }

private:
    Key encryptKey_{};
    Key decryptKey_{};
    bool keysSet_ = false;
    uint64_t encNoncePrefix_ = 0; // derived from encryptKey, upper 48 bits
    uint64_t decNoncePrefix_ = 0; // derived from decryptKey, upper 48 bits

    // Rekey state
    uint64_t packetsEncrypted_ = 0;
    std::chrono::steady_clock::time_point lastRekeyTime_{};
    uint32_t serverRekeyEpoch_ = 0; // v9: last-applied server rekey epoch

    // Previous keys for grace period during rekey transition
    Key prevEncryptKey_{};
    Key prevDecryptKey_{};
    uint64_t prevEncNoncePrefix_ = 0;
    uint64_t prevDecNoncePrefix_ = 0;
    bool hasPrevKeys_ = false;
    std::chrono::steady_clock::time_point prevKeyExpiry_{};

    // Internal: AEAD encrypt/decrypt with a specific key+nonce prefix
    static bool encryptWith(const Key& key, const uint8_t* plaintext, size_t plaintextSize,
                            uint64_t nonce, uint8_t* ciphertext, size_t ciphertextCapacity);
    static bool decryptWith(const Key& key, const uint8_t* ciphertext, size_t ciphertextSize,
                            uint64_t nonce, uint8_t* plaintext, size_t plaintextCapacity);

    // Internal: Noise_NK shared derivation from two DH results
    static SessionKeys deriveNoiseNKFinal(const uint8_t dh_es[32], const uint8_t dh_ee[32],
                                          bool isInitiator);
};

// Returns true if the packet type is a system packet that must NOT be encrypted.
inline bool isSystemPacket(uint8_t packetType) {
    // System packets: Connect (0x01), Disconnect (0x02), Heartbeat (0x03),
    // ConnectAccept (0x80), ConnectReject (0x81), KeyExchange (0x82), Rekey (0x83),
    // CmdAckExtended (0xE1).
    //
    // S143 audit fix — CmdAckExtended carries only uint16 sequence numbers
    // (no game secrets) and the server special-cases it before the
    // decryption block in handleRawPacket. Without this entry the client
    // encrypted the payload + appended a 16-byte AEAD tag, the server
    // read ciphertext bytes as `count`, hit the `count > 512` guard
    // silently 99%+ of the time, and the stranded-seq drain never fired —
    // pending 0xE0 EntityEnterBatch packets stayed unacked forever (telemetry:
    // Q:0xE0=172 plateau, retx=3440/5s sustained, rx:0xE1=42/window arriving
    // but never parsed). Session-token validation in handleRawPacket still
    // gates this, so a forged CmdAckExtended requires the session token.
    return packetType == 0x01 || packetType == 0x02 || packetType == 0x03 ||
           packetType == 0x80 || packetType == 0x81 || packetType == 0x82 ||
           packetType == 0x83 || packetType == 0xE1;
}

// Critical-lane packets MUST bypass the reliability-queue congestion drop.
// If these are dropped under backpressure the client enters an unrecoverable
// desynced state (invisible mobs that keep attacking, no death overlay on
// death, stuck at loading screen on zone change). Everything else can tolerate
// a drop + eventual retransmit or a later full-state catch-up.
inline bool isCriticalLane(uint8_t packetType) {
    // 0x90 SvEntityEnter       — without it, mob is invisible but still hits
    // 0x91 SvEntityLeave       — without it, dead mob stays as a ghost
    // 0x95 SvPlayerState       — authoritative self-state (HP/XP/level/stats)
    // 0x97 SvZoneTransition    — without it, client stuck on old scene
    // 0x98 SvLootPickup        — drives [Loot] chat + pickup SFX + scalar gold
    //                            mirror; without it, players think loot vanished
    //                            even though the server granted it (S141)
    // 0xA0 SvDeathNotify       — without it, no death overlay on player death
    // 0xA1 SvRespawn           — without it, respawn never completes
    // 0xCC SvKick              — disciplinary/shutdown message must arrive
    // 0xCE SvScenePopulated    — without it, client never exits loading
    // 0xE0 SvEntityEnterBatch  — v9 coalesced entity-enters; same semantics as 0x90
    // 0xED SvEntityLeaveBatch  — v19 coalesced entity-leaves; same semantics as 0x91
    return packetType == 0x90 || packetType == 0x91 || packetType == 0x95 ||
           packetType == 0x97 || packetType == 0x98 || packetType == 0xA0 ||
           packetType == 0xA1 || packetType == 0xCC || packetType == 0xCE ||
           packetType == 0xE0 || packetType == 0xED;
}

} // namespace fate
