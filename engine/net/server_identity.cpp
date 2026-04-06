#include "engine/net/server_identity.h"
#include "engine/core/logger.h"
#include <fstream>
#include <cstring>
#include <filesystem>

#if FATE_HAS_SODIUM
#include <sodium.h>
#endif

namespace fate {

bool ServerIdentity::loadOrGenerate(const std::string& keyFilePath,
                                    PacketCrypto::Keypair& outKeypair) {
#if FATE_HAS_SODIUM
    // Try loading existing keypair
    {
        std::ifstream f(keyFilePath, std::ios::binary);
        if (f.good()) {
            uint8_t buf[64];
            f.read(reinterpret_cast<char*>(buf), 64);
            if (f.gcount() == 64) {
                std::memcpy(outKeypair.pk.data(), buf, 32);
                std::memcpy(outKeypair.sk.data(), buf + 32, 32);
                PacketCrypto::secureWipe(buf, 64);
                LOG_INFO("ServerIdentity", "Loaded static identity keypair from %s", keyFilePath.c_str());
                return true;
            }
            LOG_WARN("ServerIdentity", "Key file %s exists but has wrong size (%lld bytes), regenerating",
                     keyFilePath.c_str(), static_cast<long long>(f.gcount()));
        }
    }

    // Generate new keypair (crypto_box keypair = X25519)
    crypto_box_keypair(outKeypair.pk.data(), outKeypair.sk.data());

    // Ensure parent directory exists
    {
        auto parent = std::filesystem::path(keyFilePath).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
    }

    // Save to file
    {
        std::ofstream f(keyFilePath, std::ios::binary | std::ios::trunc);
        if (!f.good()) {
            LOG_ERROR("ServerIdentity", "Failed to write key file %s", keyFilePath.c_str());
            return false;
        }
        f.write(reinterpret_cast<const char*>(outKeypair.pk.data()), 32);
        f.write(reinterpret_cast<const char*>(outKeypair.sk.data()), 32);
    }

    // Also save the .pub file alongside
    std::string pubPath = keyFilePath + ".pub";
    savePublicKey(pubPath, outKeypair.pk);

    LOG_INFO("ServerIdentity", "Generated new static identity keypair -> %s", keyFilePath.c_str());
    LOG_INFO("ServerIdentity", "Public key saved to %s (distribute to client builds)", pubPath.c_str());

    return true;
#else
    LOG_WARN("ServerIdentity", "libsodium not available, cannot manage identity keys");
    outKeypair.pk.fill(0);
    outKeypair.sk.fill(0);
    return true;
#endif
}

bool ServerIdentity::loadPublicKey(const std::string& pubFilePath,
                                   PacketCrypto::PublicKey& outPk) {
    std::ifstream f(pubFilePath, std::ios::binary);
    if (!f.good()) {
        LOG_WARN("ServerIdentity", "Could not open public key file %s", pubFilePath.c_str());
        return false;
    }
    f.read(reinterpret_cast<char*>(outPk.data()), 32);
    if (f.gcount() != 32) {
        LOG_ERROR("ServerIdentity", "Public key file %s has wrong size (%lld bytes)",
                  pubFilePath.c_str(), static_cast<long long>(f.gcount()));
        return false;
    }
    LOG_INFO("ServerIdentity", "Loaded server public key from %s", pubFilePath.c_str());
    return true;
}

bool ServerIdentity::savePublicKey(const std::string& pubFilePath,
                                   const PacketCrypto::PublicKey& pk) {
    std::ofstream f(pubFilePath, std::ios::binary | std::ios::trunc);
    if (!f.good()) {
        LOG_ERROR("ServerIdentity", "Failed to write public key file %s", pubFilePath.c_str());
        return false;
    }
    f.write(reinterpret_cast<const char*>(pk.data()), 32);
    return true;
}

} // namespace fate
