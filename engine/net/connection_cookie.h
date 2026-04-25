#pragma once
#include <cstdint>

namespace fate {

// Connection cookie for cheap anti-spoof on the UDP connect handshake.
// This is NOT a cryptographic MAC. It's an FNV-1a mix of
// (secret, client_ip, client_port, nonce, 10s_time_bucket) that makes it
// expensive for an off-path attacker to forge a valid reconnect without
// observing real traffic. It must not be used for authentication or
// authorization decisions — use libsodium's keyed MAC (crypto_auth /
// crypto_generichash) for anything security-bearing. The `computeMix`
// helper is deliberately named to avoid suggesting HMAC semantics.
class ConnectionCookieGenerator {
public:
    explicit ConnectionCookieGenerator(const char* secret) {
        // FNV-1a hash of the secret seeds the mix key.
        key_ = 0xcbf29ce484222325ULL;
        for (const char* p = secret; *p; ++p) {
            key_ ^= static_cast<uint64_t>(*p);
            key_ *= 0x100000001b3ULL;
        }
    }

    uint64_t generate(uint32_t ip, uint16_t port, uint64_t nonce, double timestamp) const {
        // Both generate and validate use the same 10s bucket
        auto bucket = static_cast<uint64_t>(timestamp) / 10;
        return computeMix(ip, port, nonce, bucket);
    }

    bool validate(uint64_t cookie, uint32_t ip, uint16_t port,
                  uint64_t nonce, double currentTime) const {
        // Check current 10s bucket and previous bucket (up to 20s validity)
        auto bucket = static_cast<uint64_t>(currentTime) / 10;
        if (computeMix(ip, port, nonce, bucket) == cookie) return true;
        if (bucket > 0 && computeMix(ip, port, nonce, bucket - 1) == cookie) return true;
        return false;
    }

private:
    // Non-cryptographic FNV-1a mix. Name deliberately does NOT contain HMAC
    // — callers must not repurpose this for authentication.
    uint64_t computeMix(uint32_t ip, uint16_t port, uint64_t nonce, uint64_t ts) const {
        uint64_t h = key_;
        auto mix = [&](uint64_t v) { h ^= v; h *= 0x100000001b3ULL; };
        mix(ip);
        mix(port);
        mix(nonce);
        mix(ts);
        return h;
    }

    uint64_t key_;
};

} // namespace fate
