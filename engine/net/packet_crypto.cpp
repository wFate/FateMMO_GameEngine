#include "engine/net/packet_crypto.h"
#include "engine/core/logger.h"
#include <cstring>

#if FATE_HAS_SODIUM
#include <sodium.h>
#endif

namespace fate {

bool PacketCrypto::initLibrary() {
#if FATE_HAS_SODIUM
    if (sodium_init() < 0) {
        LOG_ERROR("PacketCrypto", "Failed to initialize libsodium");
        return false;
    }
    LOG_INFO("PacketCrypto", "AEAD encryption enabled (XChaCha20-Poly1305 via libsodium)");
    return true;
#else
    LOG_WARN("PacketCrypto", "AEAD encryption unavailable - running in plaintext mode");
    return true; // still "succeeds" — just no encryption
#endif
}

bool PacketCrypto::isAvailable() {
#if FATE_HAS_SODIUM
    return true;
#else
    return false;
#endif
}

PacketCrypto::SessionKeys PacketCrypto::generateSessionKeys() {
    SessionKeys keys{};
#if FATE_HAS_SODIUM
    randombytes_buf(keys.txKey.data(), KEY_SIZE);
    randombytes_buf(keys.rxKey.data(), KEY_SIZE);
#else
    // Stub: zero keys — encryption won't actually be used
    keys.txKey.fill(0);
    keys.rxKey.fill(0);
#endif
    return keys;
}

PacketCrypto::Keypair PacketCrypto::generateKeypair() {
    Keypair kp{};
#if FATE_HAS_SODIUM
    crypto_kx_keypair(kp.pk.data(), kp.sk.data());
#else
    kp.pk.fill(0);
    kp.sk.fill(0);
#endif
    return kp;
}

PacketCrypto::SessionKeys PacketCrypto::deriveClientSessionKeys(
    const PublicKey& clientPk, const SecretKey& clientSk, const PublicKey& serverPk) {
    SessionKeys keys{};
#if FATE_HAS_SODIUM
    // crypto_kx_client_session_keys: 1st = rx (client receives), 2nd = tx (client sends)
    if (crypto_kx_client_session_keys(keys.rxKey.data(), keys.txKey.data(),
                                       clientPk.data(), clientSk.data(),
                                       serverPk.data()) != 0) {
        LOG_ERROR("PacketCrypto", "DH client session key derivation failed (suspect server key)");
        keys.txKey.fill(0);
        keys.rxKey.fill(0);
    }
#else
    keys.txKey.fill(0);
    keys.rxKey.fill(0);
#endif
    return keys;
}

PacketCrypto::SessionKeys PacketCrypto::deriveServerSessionKeys(
    const PublicKey& serverPk, const SecretKey& serverSk, const PublicKey& clientPk) {
    SessionKeys keys{};
#if FATE_HAS_SODIUM
    // crypto_kx_server_session_keys: 1st = rx (server receives), 2nd = tx (server sends)
    if (crypto_kx_server_session_keys(keys.rxKey.data(), keys.txKey.data(),
                                       serverPk.data(), serverSk.data(),
                                       clientPk.data()) != 0) {
        LOG_ERROR("PacketCrypto", "DH server session key derivation failed (suspect client key)");
        keys.txKey.fill(0);
        keys.rxKey.fill(0);
    }
#else
    keys.txKey.fill(0);
    keys.rxKey.fill(0);
#endif
    return keys;
}

void PacketCrypto::secureWipe(void* data, size_t size) {
#if FATE_HAS_SODIUM
    sodium_memzero(data, size);
#else
    volatile uint8_t* p = static_cast<volatile uint8_t*>(data);
    while (size--) *p++ = 0;
#endif
}

void PacketCrypto::setKeys(const Key& encryptKey, const Key& decryptKey) {
    encryptKey_ = encryptKey;
    decryptKey_ = decryptKey;
    keysSet_ = true;

    // Derive per-direction nonce prefixes from the first 6 bytes of each key,
    // shifted into the upper 48 bits.  Both sides share the same key material
    // (sender's encryptKey == receiver's decryptKey) so the prefixes match.
    auto derivePrefix = [](const Key& key) -> uint64_t {
        uint64_t v = 0;
        std::memcpy(&v, key.data(), 6); // first 6 bytes of key
        return v << 16;                 // shift into upper 48 bits
    };
    encNoncePrefix_ = derivePrefix(encryptKey_);
    decNoncePrefix_ = derivePrefix(decryptKey_);
}

bool PacketCrypto::encrypt(const uint8_t* plaintext, size_t plaintextSize,
                           uint64_t nonce,
                           uint8_t* ciphertext, size_t ciphertextCapacity) const {
    if (!keysSet_) return false;
    if (ciphertextCapacity < plaintextSize + TAG_SIZE) return false;

#if FATE_HAS_SODIUM
    // Build 24-byte nonce from 8-byte sequence number (zero-padded)
    uint8_t nonceBytes[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES] = {};
    std::memcpy(nonceBytes, &nonce, sizeof(nonce));

    unsigned long long ciphertextLen = 0;
    int rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
        ciphertext, &ciphertextLen,
        plaintext, plaintextSize,
        nullptr, 0,         // no additional data
        nullptr,            // nsec (unused)
        nonceBytes,
        encryptKey_.data()
    );
    return rc == 0;
#else
    // Stub: copy plaintext through, append zero tag
    std::memcpy(ciphertext, plaintext, plaintextSize);
    std::memset(ciphertext + plaintextSize, 0, TAG_SIZE);
    return true;
#endif
}

bool PacketCrypto::decrypt(const uint8_t* ciphertext, size_t ciphertextSize,
                           uint64_t nonce,
                           uint8_t* plaintext, size_t plaintextCapacity) const {
    if (!keysSet_) return false;
    if (ciphertextSize < TAG_SIZE) return false;
    size_t expectedPlaintextSize = ciphertextSize - TAG_SIZE;
    if (plaintextCapacity < expectedPlaintextSize) return false;

#if FATE_HAS_SODIUM
    // Build 24-byte nonce from 8-byte sequence number (zero-padded)
    uint8_t nonceBytes[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES] = {};
    std::memcpy(nonceBytes, &nonce, sizeof(nonce));

    unsigned long long decryptedLen = 0;
    int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
        plaintext, &decryptedLen,
        nullptr,            // nsec (unused)
        ciphertext, ciphertextSize,
        nullptr, 0,         // no additional data
        nonceBytes,
        decryptKey_.data()
    );
    return rc == 0;
#else
    // Stub: copy ciphertext minus tag
    std::memcpy(plaintext, ciphertext, expectedPlaintextSize);
    return true;
#endif
}

} // namespace fate
