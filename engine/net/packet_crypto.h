#pragma once
#include <cstdint>
#include <cstddef>
#include <array>

namespace fate {

class PacketCrypto {
public:
    static constexpr size_t KEY_SIZE = 32;
    static constexpr size_t TAG_SIZE = 16;

    using Key = std::array<uint8_t, KEY_SIZE>;

    struct SessionKeys {
        Key txKey;  // encrypt key for sender (client)
        Key rxKey;  // decrypt key for sender (client) — i.e. server encrypt key
    };

    // Initialize the crypto library. Returns true on success.
    static bool initLibrary();

    // Generate a pair of session keys (one for each direction).
    static SessionKeys generateSessionKeys();

    // Set the encrypt/decrypt keys for this session.
    void setKeys(const Key& encryptKey, const Key& decryptKey);
    bool hasKeys() const { return keysSet_; }
    void clearKeys() { keysSet_ = false; }

    // Encrypt payload in-place.
    // Output buffer must have capacity for plaintextSize + TAG_SIZE bytes.
    // nonce = packet sequence number (unique per packet direction).
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
};

// Returns true if the packet type is a system packet that must NOT be encrypted.
inline bool isSystemPacket(uint8_t packetType) {
    // System packets: Connect (0x01), Disconnect (0x02), Heartbeat (0x03),
    // ConnectAccept (0x80), ConnectReject (0x81), KeyExchange (0x82)
    return packetType == 0x01 || packetType == 0x02 || packetType == 0x03 ||
           packetType == 0x80 || packetType == 0x81 || packetType == 0x82;
}

} // namespace fate
