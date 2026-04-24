#include "engine/net/packet_crypto.h"
#include "engine/core/logger.h"
#include <cstring>
#include <cstdlib>

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

// ── Noise_NK key derivation ──────────────────────────────────────────────

PacketCrypto::SessionKeys PacketCrypto::deriveNoiseNKFinal(
    const uint8_t dh_es[32], const uint8_t dh_ee[32], bool isInitiator) {
    SessionKeys keys{};
#if FATE_HAS_SODIUM
    // Combine es||ee into input keying material
    uint8_t ikm[64];
    std::memcpy(ikm, dh_es, 32);
    std::memcpy(ikm + 32, dh_ee, 32);

    // BLAKE2b-512 with protocol name as key for domain separation
    // Output: 64 bytes → first 32 = initiator→responder key, second 32 = responder→initiator key
    static const char* protocol = "Noise_NK_25519_XChaChaPoly_BLAKE2b";
    uint8_t okm[64];
    crypto_generichash(okm, 64, ikm, 64,
                       reinterpret_cast<const uint8_t*>(protocol),
                       static_cast<size_t>(std::strlen(protocol)));

    if (isInitiator) {
        // Client: first 32 = tx (client→server), second 32 = rx (server→client)
        std::memcpy(keys.txKey.data(), okm, 32);
        std::memcpy(keys.rxKey.data(), okm + 32, 32);
    } else {
        // Server: first 32 = rx (client→server), second 32 = tx (server→client)
        std::memcpy(keys.rxKey.data(), okm, 32);
        std::memcpy(keys.txKey.data(), okm + 32, 32);
    }

    secureWipe(ikm, 64);
    secureWipe(okm, 64);
#else
    keys.txKey.fill(0);
    keys.rxKey.fill(0);
#endif
    return keys;
}

PacketCrypto::SessionKeys PacketCrypto::deriveNoiseNKClientKeys(
    const SecretKey& clientEphSk,
    const PublicKey& serverStaticPk,
    const PublicKey& serverEphPk) {
    SessionKeys keys{};
#if FATE_HAS_SODIUM
    uint8_t dh_es[32], dh_ee[32];

    // es: DH(client_ephemeral, server_static) — authenticates the server
    if (crypto_scalarmult(dh_es, clientEphSk.data(), serverStaticPk.data()) != 0) {
        LOG_ERROR("PacketCrypto", "Noise_NK client: es DH failed (bad server static key)");
        return keys;
    }
    // ee: DH(client_ephemeral, server_ephemeral) — provides forward secrecy
    if (crypto_scalarmult(dh_ee, clientEphSk.data(), serverEphPk.data()) != 0) {
        LOG_ERROR("PacketCrypto", "Noise_NK client: ee DH failed (bad server ephemeral key)");
        secureWipe(dh_es, 32);
        return keys;
    }

    keys = deriveNoiseNKFinal(dh_es, dh_ee, true);
    secureWipe(dh_es, 32);
    secureWipe(dh_ee, 32);
#else
    keys.txKey.fill(0);
    keys.rxKey.fill(0);
#endif
    return keys;
}

PacketCrypto::SessionKeys PacketCrypto::deriveNoiseNKServerKeys(
    const SecretKey& serverStaticSk,
    const SecretKey& serverEphSk,
    const PublicKey& clientEphPk) {
    SessionKeys keys{};
#if FATE_HAS_SODIUM
    uint8_t dh_es[32], dh_ee[32];

    // es: DH(server_static, client_ephemeral) — same result as client's es
    if (crypto_scalarmult(dh_es, serverStaticSk.data(), clientEphPk.data()) != 0) {
        LOG_ERROR("PacketCrypto", "Noise_NK server: es DH failed (bad client ephemeral key)");
        return keys;
    }
    // ee: DH(server_ephemeral, client_ephemeral) — same result as client's ee
    if (crypto_scalarmult(dh_ee, serverEphSk.data(), clientEphPk.data()) != 0) {
        LOG_ERROR("PacketCrypto", "Noise_NK server: ee DH failed (bad client ephemeral key)");
        secureWipe(dh_es, 32);
        return keys;
    }

    keys = deriveNoiseNKFinal(dh_es, dh_ee, false);
    secureWipe(dh_es, 32);
    secureWipe(dh_ee, 32);
#else
    keys.txKey.fill(0);
    keys.rxKey.fill(0);
#endif
    return keys;
}

// ── Memory wiping ────────────────────────────────────────────────────────

void PacketCrypto::secureWipe(void* data, size_t size) {
#if FATE_HAS_SODIUM
    sodium_memzero(data, size);
#else
    volatile uint8_t* p = static_cast<volatile uint8_t*>(data);
    while (size--) *p++ = 0;
#endif
}

void PacketCrypto::randomBytes(void* out, size_t n) {
#if FATE_HAS_SODIUM
    randombytes_buf(out, n);
#else
    LOG_ERROR("PacketCrypto", "randomBytes called without libsodium — no CSPRNG available. Aborting.");
    std::abort();
#endif
}

// ── Key management ───────────────────────────────────────────────────────

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

    packetsEncrypted_ = 0;
    lastRekeyTime_ = std::chrono::steady_clock::now();
}

void PacketCrypto::clearKeys() {
    secureWipe(encryptKey_.data(), KEY_SIZE);
    secureWipe(decryptKey_.data(), KEY_SIZE);
    keysSet_ = false;
    encNoncePrefix_ = 0;
    decNoncePrefix_ = 0;
    packetsEncrypted_ = 0;

    if (hasPrevKeys_) {
        secureWipe(prevEncryptKey_.data(), KEY_SIZE);
        secureWipe(prevDecryptKey_.data(), KEY_SIZE);
        hasPrevKeys_ = false;
        prevEncNoncePrefix_ = 0;
        prevDecNoncePrefix_ = 0;
    }
    serverRekeyEpoch_ = 0; // v9: reset rekey epoch on full key clear (reconnect)
}

// ── Symmetric rekeying ───────────────────────────────────────────────────

bool PacketCrypto::needsRekey() const {
    if (!keysSet_) return false;
    if (packetsEncrypted_ >= REKEY_AFTER_PACKETS) return true;
    auto elapsed = std::chrono::steady_clock::now() - lastRekeyTime_;
    return elapsed >= std::chrono::minutes(REKEY_AFTER_MINUTES);
}

void PacketCrypto::symmetricRekey() {
    if (!keysSet_) return;

#if FATE_HAS_SODIUM
    // Stash current keys as previous (for in-flight packet grace period)
    prevEncryptKey_ = encryptKey_;
    prevDecryptKey_ = decryptKey_;
    prevEncNoncePrefix_ = encNoncePrefix_;
    prevDecNoncePrefix_ = decNoncePrefix_;
    hasPrevKeys_ = true;
    prevKeyExpiry_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    // Derive new keys from current keys via BLAKE2b keyed hash.
    // Use the SAME context for both: directionality comes from the
    // different key material (sender's encKey == receiver's decKey).
    Key newEnc{}, newDec{};
    static const uint8_t rekey_ctx[] = "rekeyctx"; // 8-byte context, same for both

    crypto_generichash(newEnc.data(), KEY_SIZE,
                       rekey_ctx, 8,
                       encryptKey_.data(), KEY_SIZE);
    crypto_generichash(newDec.data(), KEY_SIZE,
                       rekey_ctx, 8,
                       decryptKey_.data(), KEY_SIZE);

    setKeys(newEnc, newDec);
    secureWipe(newEnc.data(), KEY_SIZE);
    secureWipe(newDec.data(), KEY_SIZE);

    LOG_INFO("PacketCrypto", "Symmetric rekey complete");
#endif
}

// ── Encryption / Decryption ──────────────────────────────────────────────

bool PacketCrypto::encryptWith(const Key& key,
                               const uint8_t* plaintext, size_t plaintextSize,
                               uint64_t nonce,
                               uint8_t* ciphertext, size_t ciphertextCapacity) {
    if (ciphertextCapacity < plaintextSize + TAG_SIZE) return false;

#if FATE_HAS_SODIUM
    uint8_t nonceBytes[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES] = {};
    std::memcpy(nonceBytes, &nonce, sizeof(nonce));

    unsigned long long ciphertextLen = 0;
    int rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
        ciphertext, &ciphertextLen,
        plaintext, plaintextSize,
        nullptr, 0,         // no additional data
        nullptr,            // nsec (unused)
        nonceBytes,
        key.data()
    );
    return rc == 0;
#else
    std::memcpy(ciphertext, plaintext, plaintextSize);
    std::memset(ciphertext + plaintextSize, 0, TAG_SIZE);
    return true;
#endif
}

bool PacketCrypto::decryptWith(const Key& key,
                               const uint8_t* ciphertext, size_t ciphertextSize,
                               uint64_t nonce,
                               uint8_t* plaintext, size_t plaintextCapacity) {
    if (ciphertextSize < TAG_SIZE) return false;
    size_t expectedPlaintextSize = ciphertextSize - TAG_SIZE;
    if (plaintextCapacity < expectedPlaintextSize) return false;

#if FATE_HAS_SODIUM
    uint8_t nonceBytes[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES] = {};
    std::memcpy(nonceBytes, &nonce, sizeof(nonce));

    unsigned long long decryptedLen = 0;
    int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
        plaintext, &decryptedLen,
        nullptr,            // nsec (unused)
        ciphertext, ciphertextSize,
        nullptr, 0,         // no additional data
        nonceBytes,
        key.data()
    );
    return rc == 0;
#else
    std::memcpy(plaintext, ciphertext, expectedPlaintextSize);
    return true;
#endif
}

bool PacketCrypto::encrypt(const uint8_t* plaintext, size_t plaintextSize,
                           uint64_t nonce,
                           uint8_t* ciphertext, size_t ciphertextCapacity) {
    if (!keysSet_) return false;
    bool ok = encryptWith(encryptKey_, plaintext, plaintextSize, nonce,
                          ciphertext, ciphertextCapacity);
    if (ok) ++packetsEncrypted_;
    return ok;
}

bool PacketCrypto::decrypt(const uint8_t* ciphertext, size_t ciphertextSize,
                           uint64_t nonce,
                           uint8_t* plaintext, size_t plaintextCapacity) {
    if (!keysSet_) return false;

    // Try current keys first
    if (decryptWith(decryptKey_, ciphertext, ciphertextSize, nonce,
                    plaintext, plaintextCapacity)) {
        // Expire previous keys if grace period has passed
        if (hasPrevKeys_ && std::chrono::steady_clock::now() >= prevKeyExpiry_) {
            secureWipe(prevEncryptKey_.data(), KEY_SIZE);
            secureWipe(prevDecryptKey_.data(), KEY_SIZE);
            hasPrevKeys_ = false;
        }
        return true;
    }

    // Fallback to previous keys during rekey grace period
    if (hasPrevKeys_) {
        if (std::chrono::steady_clock::now() < prevKeyExpiry_) {
            return decryptWith(prevDecryptKey_, ciphertext, ciphertextSize, nonce,
                               plaintext, plaintextCapacity);
        }
        // Grace period expired — wipe prev keys
        secureWipe(prevEncryptKey_.data(), KEY_SIZE);
        secureWipe(prevDecryptKey_.data(), KEY_SIZE);
        hasPrevKeys_ = false;
    }

    return false;
}

} // namespace fate
