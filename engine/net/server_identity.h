#pragma once
#include "engine/net/packet_crypto.h"
#include <string>

namespace fate {

// Manages the server's long-term static X25519 identity keypair.
// The public key is embedded in client builds so clients can authenticate
// the server during the Noise_NK handshake (prevents MITM).
//
// Server: call loadOrGenerate() at startup to get the full keypair.
// Client: call loadPublicKey() or use the compiled-in default.
class ServerIdentity {
public:
    // Load keypair from file, or generate + save if file doesn't exist.
    // File format: 64 bytes raw (32 pk + 32 sk).
    // Returns false on I/O or crypto error.
    static bool loadOrGenerate(const std::string& keyFilePath,
                               PacketCrypto::Keypair& outKeypair);

    // Load only the public key from a .pub file (32 bytes raw).
    static bool loadPublicKey(const std::string& pubFilePath,
                              PacketCrypto::PublicKey& outPk);

    // Save just the public key to a .pub file (for distribution to client builds).
    static bool savePublicKey(const std::string& pubFilePath,
                              const PacketCrypto::PublicKey& pk);
};

} // namespace fate
