#pragma once
#include <cstdint>
#include <cstddef>
#include <array>

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

    // Securely wipe sensitive memory (e.g. secret keys after use).
    static void secureWipe(void* data, size_t size);

    // Set the encrypt/decrypt keys for this session (also resets nonce counters).
    void setKeys(const Key& encryptKey, const Key& decryptKey);
    bool hasKeys() const { return keysSet_; }
    void clearKeys() { keysSet_ = false; }

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
                 uint8_t* ciphertext, size_t ciphertextCapacity) const;

    // Decrypt ciphertext.
    // Output buffer must have capacity for ciphertextSize - TAG_SIZE bytes.
    // Returns false if authentication fails (tampered or wrong key).
    bool decrypt(const uint8_t* ciphertext, size_t ciphertextSize,
                 uint64_t nonce,
                 uint8_t* plaintext, size_t plaintextCapacity) const;

    // Check whether real crypto is available (libsodium compiled in).
    static bool isAvailable();

private:
    Key encryptKey_{};
    Key decryptKey_{};
    bool keysSet_ = false;
    uint64_t encNoncePrefix_ = 0; // derived from encryptKey, upper 48 bits
    uint64_t decNoncePrefix_ = 0; // derived from decryptKey, upper 48 bits
};

// Returns true if the packet type is a system packet that must NOT be encrypted.
inline bool isSystemPacket(uint8_t packetType) {
    // System packets: Connect (0x01), Disconnect (0x02), Heartbeat (0x03),
    // ConnectAccept (0x80), ConnectReject (0x81), KeyExchange (0x82)
    return packetType == 0x01 || packetType == 0x02 || packetType == 0x03 ||
           packetType == 0x80 || packetType == 0x81 || packetType == 0x82;
}

} // namespace fate
