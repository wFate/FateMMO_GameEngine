#pragma once
#include <cstdint>
#include <unordered_map>
#include <random>

class NonceManager {
public:
    // Issue a new nonce for a client. Returns the nonce value.
    uint64_t issue(uint16_t clientId) {
        uint64_t nonce = dist_(rng_);
        pending_[clientId][nonce] = 0.0f;
        return nonce;
    }

    // Issue with timestamp tracking
    uint64_t issue(uint16_t clientId, float gameTime) {
        uint64_t nonce = dist_(rng_);
        pending_[clientId][nonce] = gameTime;
        return nonce;
    }

    // Consume a nonce. Returns true if valid and not expired.
    bool consume(uint16_t clientId, uint64_t nonce, float gameTime, float maxAge = 60.0f) {
        auto clientIt = pending_.find(clientId);
        if (clientIt == pending_.end()) return false;
        auto it = clientIt->second.find(nonce);
        if (it == clientIt->second.end()) return false;
        if (maxAge > 0.0f && gameTime - it->second > maxAge) {
            clientIt->second.erase(it);
            return false; // expired
        }
        clientIt->second.erase(it);
        return true;
    }

    // Expire all nonces older than maxAge for all clients
    void expireAll(float gameTime, float maxAge = 60.0f) {
        for (auto& [cid, nonces] : pending_) {
            for (auto it = nonces.begin(); it != nonces.end(); ) {
                if (gameTime - it->second > maxAge)
                    it = nonces.erase(it);
                else
                    ++it;
            }
        }
    }

    // Clean up when a client disconnects
    void removeClient(uint16_t clientId) {
        pending_.erase(clientId);
    }

    // For testing
    size_t pendingCount(uint16_t clientId) const {
        auto it = pending_.find(clientId);
        return it != pending_.end() ? it->second.size() : 0;
    }

private:
    std::unordered_map<uint16_t,
        std::unordered_map<uint64_t, float>> pending_; // clientId -> {nonce -> issueTime}
    std::mt19937_64 rng_{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist_;
};
