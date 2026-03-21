#pragma once
#include "engine/net/net_client.h"
#include "engine/net/auth_client.h"
#include "engine/net/auth_protocol.h"
#include "engine/net/protocol.h"
#include "engine/net/game_messages.h"
#include <doctest/doctest.h>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <type_traits>

namespace fate {

// TODO: Future enhancement — Approach C scripted DSL
// Replace procedural test code with declarative scenario scripts:
//   bot.scenario("login -> move(100,200) -> attack(nearest_mob) -> expect(damage > 0)");
// Prerequisite: stable TestBot API to build the DSL interpreter on top of.

class TestBot {
public:
    TestBot();

    // --- Lifecycle ---
    AuthResponse login(const std::string& host, uint16_t authPort,
                       const std::string& username, const std::string& password,
                       float timeoutSec = 10.0f);

    void connectUDP(const std::string& host, uint16_t gamePort,
                    float timeoutSec = 15.0f);

    void disconnect();

    // --- Commands ---
    void sendMove(Vec2 position, Vec2 velocity);
    void sendAttack(uint64_t targetPersistentId);
    void sendUseSkill(const std::string& skillId, uint8_t rank, uint64_t targetId);
    void sendZoneTransition(const std::string& targetScene);
    void sendRespawn(uint8_t type);
    void sendChat(uint8_t channel, const std::string& message,
                  const std::string& target = "");

    // --- Event collection ---
    void pollEvents();
    void pollFor(float durationSec);

    template<typename T>
    T waitFor(float timeoutSec = 2.0f);

    // --- Query helpers ---
    const AuthResponse& authData() const { return authResponse_; }
    const std::vector<SvEntityEnterMsg>& entityEnters() const { return entityEnters_; }

    std::vector<SvEntityEnterMsg> entityEntersOfType(uint8_t entityType) const {
        std::vector<SvEntityEnterMsg> result;
        for (const auto& e : entityEnters_) {
            if (e.entityType == entityType) result.push_back(e);
        }
        return result;
    }

    void clearEvents();

    template<typename T>
    void clearEventsOf() { getQueue<T>().clear(); }

    template<typename T>
    bool hasEvent() { return !getQueue<T>().empty(); }

    template<typename T>
    size_t eventCount() { return getQueue<T>().size(); }

    bool isConnected() const { return udpConnected_; }

    // Public for test ergonomics — this is a test utility, not production code.
    template<typename T>
    std::vector<T>& getQueue() {
        if constexpr (std::is_same_v<T, SvPlayerStateMsg>)        return playerStates_;
        else if constexpr (std::is_same_v<T, SvCombatEventMsg>)   return combatEvents_;
        else if constexpr (std::is_same_v<T, SvEntityEnterMsg>)   return entityEnters_;
        else if constexpr (std::is_same_v<T, SvEntityLeaveMsg>)   return entityLeaves_;
        else if constexpr (std::is_same_v<T, SvEntityUpdateMsg>)  return entityUpdates_;
        else if constexpr (std::is_same_v<T, SvZoneTransitionMsg>) return zoneTransitions_;
        else if constexpr (std::is_same_v<T, SvDeathNotifyMsg>)   return deathNotifies_;
        else if constexpr (std::is_same_v<T, SvSkillResultMsg>)   return skillResults_;
        else if constexpr (std::is_same_v<T, SvLevelUpMsg>)       return levelUps_;
        else if constexpr (std::is_same_v<T, SvInventorySyncMsg>) return inventorySyncs_;
        else if constexpr (std::is_same_v<T, SvSkillSyncMsg>)     return skillSyncs_;
        else if constexpr (std::is_same_v<T, SvQuestSyncMsg>)     return questSyncs_;
        else if constexpr (std::is_same_v<T, SvLootPickupMsg>)    return lootPickups_;
        else if constexpr (std::is_same_v<T, SvRespawnMsg>)       return respawns_;
        else if constexpr (std::is_same_v<T, SvMovementCorrectionMsg>) return movementCorrections_;
        else if constexpr (std::is_same_v<T, SvChatMessageMsg>)   return chatMessages_;
        else static_assert(!sizeof(T), "No event queue registered for this type");
    }

private:
    AuthClient auth_;
    NetClient  client_;
    std::chrono::steady_clock::time_point epoch_;

    // Event queues
    std::vector<SvPlayerStateMsg>        playerStates_;
    std::vector<SvCombatEventMsg>        combatEvents_;
    std::vector<SvEntityEnterMsg>        entityEnters_;
    std::vector<SvEntityLeaveMsg>        entityLeaves_;
    std::vector<SvEntityUpdateMsg>       entityUpdates_;
    std::vector<SvZoneTransitionMsg>     zoneTransitions_;
    std::vector<SvDeathNotifyMsg>        deathNotifies_;
    std::vector<SvSkillResultMsg>        skillResults_;
    std::vector<SvLevelUpMsg>            levelUps_;
    std::vector<SvInventorySyncMsg>      inventorySyncs_;
    std::vector<SvSkillSyncMsg>          skillSyncs_;
    std::vector<SvQuestSyncMsg>          questSyncs_;
    std::vector<SvLootPickupMsg>         lootPickups_;
    std::vector<SvRespawnMsg>            respawns_;
    std::vector<SvMovementCorrectionMsg> movementCorrections_;
    std::vector<SvChatMessageMsg>        chatMessages_;

    AuthResponse authResponse_;
    std::string connectRejectReason_;
    bool wasRejected_ = false;
    bool udpConnected_ = false;

    float currentTime() const;
};

// --- Template implementation (must be in header) ---

template<typename T>
T TestBot::waitFor(float timeoutSec) {
    auto& queue = getQueue<T>();
    auto start = std::chrono::steady_clock::now();
    while (true) {
        client_.poll(currentTime());
        if (!queue.empty()) {
            T result = queue.front();
            queue.erase(queue.begin());
            return result;
        }
        auto elapsed = std::chrono::steady_clock::now() - start;
        float elapsedSec = std::chrono::duration<float>(elapsed).count();
        if (elapsedSec > timeoutSec) {
            FAIL("TestBot::waitFor timed out after ", timeoutSec, "s");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

} // namespace fate
