#pragma once
#include <cstdint>

namespace fate {

class ConnectionCookieGenerator {
public:
    explicit ConnectionCookieGenerator(const char* secret) {
        // FNV-1a hash of the secret for the HMAC key
        key_ = 0xcbf29ce484222325ULL;
        for (const char* p = secret; *p; ++p) {
            key_ ^= static_cast<uint64_t>(*p);
            key_ *= 0x100000001b3ULL;
        }
    }

    uint64_t generate(uint32_t ip, uint16_t port, uint64_t nonce, double timestamp) const {
        // Both generate and validate use the same 10s bucket
        auto bucket = static_cast<uint64_t>(timestamp) / 10;
        return computeHmac(ip, port, nonce, bucket);
    }

    bool validate(uint64_t cookie, uint32_t ip, uint16_t port,
                  uint64_t nonce, double currentTime) const {
        // Check current 10s bucket and previous bucket (up to 20s validity)
        auto bucket = static_cast<uint64_t>(currentTime) / 10;
        if (computeHmac(ip, port, nonce, bucket) == cookie) return true;
        if (bucket > 0 && computeHmac(ip, port, nonce, bucket - 1) == cookie) return true;
        return false;
    }

private:
    uint64_t computeHmac(uint32_t ip, uint16_t port, uint64_t nonce, uint64_t ts) const {
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
