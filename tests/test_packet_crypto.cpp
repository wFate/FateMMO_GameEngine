#include <doctest/doctest.h>
#include "engine/net/packet_crypto.h"
#include "engine/net/packet.h"
#include <cstring>
#include <vector>

using namespace fate;

TEST_SUITE("PacketCrypto") {

TEST_CASE("library initialization succeeds") {
    CHECK(PacketCrypto::initLibrary());
}

TEST_CASE("key generation produces non-zero keys") {
    PacketCrypto::initLibrary();
    auto keys = PacketCrypto::generateSessionKeys();

    if (PacketCrypto::isAvailable()) {
        // At least one byte should be non-zero in each key
        bool txNonZero = false, rxNonZero = false;
        for (size_t i = 0; i < PacketCrypto::KEY_SIZE; ++i) {
            if (keys.txKey[i] != 0) txNonZero = true;
            if (keys.rxKey[i] != 0) rxNonZero = true;
        }
        CHECK(txNonZero);
        CHECK(rxNonZero);
    }
}

TEST_CASE("round-trip encrypt/decrypt") {
    PacketCrypto::initLibrary();
    auto keys = PacketCrypto::generateSessionKeys();

    PacketCrypto sender, receiver;
    // sender encrypts with txKey, receiver decrypts with txKey
    sender.setKeys(keys.txKey, keys.rxKey);
    receiver.setKeys(keys.rxKey, keys.txKey);

    const uint8_t plaintext[] = "Hello, encrypted world!";
    size_t plaintextSize = sizeof(plaintext);
    uint64_t nonce = 42;

    // Encrypt
    std::vector<uint8_t> ciphertext(plaintextSize + PacketCrypto::TAG_SIZE);
    CHECK(sender.encrypt(plaintext, plaintextSize, nonce, ciphertext.data(), ciphertext.size()));

    // Decrypt
    std::vector<uint8_t> decrypted(plaintextSize);
    CHECK(receiver.decrypt(ciphertext.data(), ciphertext.size(), nonce, decrypted.data(), decrypted.size()));

    CHECK(std::memcmp(plaintext, decrypted.data(), plaintextSize) == 0);
}

TEST_CASE("tampered ciphertext is detected") {
    PacketCrypto::initLibrary();
    if (!PacketCrypto::isAvailable()) return; // stub won't detect tampering

    auto keys = PacketCrypto::generateSessionKeys();

    PacketCrypto sender, receiver;
    sender.setKeys(keys.txKey, keys.rxKey);
    receiver.setKeys(keys.rxKey, keys.txKey);

    const uint8_t plaintext[] = "Sensitive game data";
    size_t plaintextSize = sizeof(plaintext);
    uint64_t nonce = 100;

    std::vector<uint8_t> ciphertext(plaintextSize + PacketCrypto::TAG_SIZE);
    CHECK(sender.encrypt(plaintext, plaintextSize, nonce, ciphertext.data(), ciphertext.size()));

    // Tamper with ciphertext
    ciphertext[0] ^= 0xFF;

    std::vector<uint8_t> decrypted(plaintextSize);
    CHECK_FALSE(receiver.decrypt(ciphertext.data(), ciphertext.size(), nonce, decrypted.data(), decrypted.size()));
}

TEST_CASE("wrong nonce is detected") {
    PacketCrypto::initLibrary();
    if (!PacketCrypto::isAvailable()) return;

    auto keys = PacketCrypto::generateSessionKeys();

    PacketCrypto sender, receiver;
    sender.setKeys(keys.txKey, keys.rxKey);
    receiver.setKeys(keys.rxKey, keys.txKey);

    const uint8_t plaintext[] = "Replay me?";
    size_t plaintextSize = sizeof(plaintext);

    std::vector<uint8_t> ciphertext(plaintextSize + PacketCrypto::TAG_SIZE);
    CHECK(sender.encrypt(plaintext, plaintextSize, 1, ciphertext.data(), ciphertext.size()));

    // Try to decrypt with different nonce (replay attempt)
    std::vector<uint8_t> decrypted(plaintextSize);
    CHECK_FALSE(receiver.decrypt(ciphertext.data(), ciphertext.size(), 2, decrypted.data(), decrypted.size()));
}

TEST_CASE("no-key mode rejects operations") {
    PacketCrypto crypto;
    CHECK_FALSE(crypto.hasKeys());

    const uint8_t data[] = "test";
    uint8_t out[32] = {};

    CHECK_FALSE(crypto.encrypt(data, sizeof(data), 0, out, sizeof(out)));
    CHECK_FALSE(crypto.decrypt(data, sizeof(data), 0, out, sizeof(out)));
}

TEST_CASE("clearKeys disables crypto") {
    PacketCrypto::initLibrary();
    auto keys = PacketCrypto::generateSessionKeys();

    PacketCrypto crypto;
    crypto.setKeys(keys.txKey, keys.rxKey);
    CHECK(crypto.hasKeys());

    crypto.clearKeys();
    CHECK_FALSE(crypto.hasKeys());

    const uint8_t data[] = "test";
    uint8_t out[32] = {};
    CHECK_FALSE(crypto.encrypt(data, sizeof(data), 0, out, sizeof(out)));
}

TEST_CASE("wrong key pair fails decryption") {
    PacketCrypto::initLibrary();
    if (!PacketCrypto::isAvailable()) return;

    auto keys1 = PacketCrypto::generateSessionKeys();
    auto keys2 = PacketCrypto::generateSessionKeys();

    PacketCrypto sender, receiver;
    sender.setKeys(keys1.txKey, keys1.rxKey);
    receiver.setKeys(keys2.rxKey, keys2.txKey); // different keys!

    const uint8_t plaintext[] = "Wrong key test";
    size_t plaintextSize = sizeof(plaintext);

    std::vector<uint8_t> ciphertext(plaintextSize + PacketCrypto::TAG_SIZE);
    CHECK(sender.encrypt(plaintext, plaintextSize, 1, ciphertext.data(), ciphertext.size()));

    std::vector<uint8_t> decrypted(plaintextSize);
    CHECK_FALSE(receiver.decrypt(ciphertext.data(), ciphertext.size(), 1, decrypted.data(), decrypted.size()));
}

TEST_CASE("isSystemPacket identifies system packets correctly") {
    // System packets
    CHECK(isSystemPacket(PacketType::Connect));
    CHECK(isSystemPacket(PacketType::Disconnect));
    CHECK(isSystemPacket(PacketType::Heartbeat));
    CHECK(isSystemPacket(PacketType::ConnectAccept));
    CHECK(isSystemPacket(PacketType::ConnectReject));
    CHECK(isSystemPacket(PacketType::KeyExchange));

    // Game packets — should NOT be system
    CHECK_FALSE(isSystemPacket(PacketType::CmdMove));
    CHECK_FALSE(isSystemPacket(PacketType::CmdAction));
    CHECK_FALSE(isSystemPacket(PacketType::SvEntityEnter));
    CHECK_FALSE(isSystemPacket(PacketType::SvPlayerState));
    CHECK_FALSE(isSystemPacket(PacketType::SvCombatEvent));
}

TEST_CASE("empty plaintext encrypts and decrypts") {
    PacketCrypto::initLibrary();
    auto keys = PacketCrypto::generateSessionKeys();

    PacketCrypto sender, receiver;
    sender.setKeys(keys.txKey, keys.rxKey);
    receiver.setKeys(keys.rxKey, keys.txKey);

    // Zero-length payload
    std::vector<uint8_t> ciphertext(PacketCrypto::TAG_SIZE);
    CHECK(sender.encrypt(nullptr, 0, 99, ciphertext.data(), ciphertext.size()));

    // Decrypt — output is zero-length
    uint8_t dummy[1];
    CHECK(receiver.decrypt(ciphertext.data(), ciphertext.size(), 99, dummy, 0));
}

TEST_CASE("large payload round-trip") {
    PacketCrypto::initLibrary();
    auto keys = PacketCrypto::generateSessionKeys();

    PacketCrypto sender, receiver;
    sender.setKeys(keys.txKey, keys.rxKey);
    receiver.setKeys(keys.rxKey, keys.txKey);

    // 1000-byte payload (close to MAX_PAYLOAD_SIZE)
    std::vector<uint8_t> plaintext(1000);
    for (size_t i = 0; i < plaintext.size(); ++i)
        plaintext[i] = static_cast<uint8_t>(i & 0xFF);

    std::vector<uint8_t> ciphertext(plaintext.size() + PacketCrypto::TAG_SIZE);
    CHECK(sender.encrypt(plaintext.data(), plaintext.size(), 65535, ciphertext.data(), ciphertext.size()));

    std::vector<uint8_t> decrypted(plaintext.size());
    CHECK(receiver.decrypt(ciphertext.data(), ciphertext.size(), 65535, decrypted.data(), decrypted.size()));

    CHECK(std::memcmp(plaintext.data(), decrypted.data(), plaintext.size()) == 0);
}

} // TEST_SUITE
