#include "test_bot.h"

namespace fate {

TestBot::TestBot()
    : epoch_(std::chrono::steady_clock::now())
{
    // Wire NetClient callbacks to event queues
    client_.onPlayerState = [this](const SvPlayerStateMsg& msg) {
        playerStates_.push_back(msg);
    };
    client_.onCombatEvent = [this](const SvCombatEventMsg& msg) {
        combatEvents_.push_back(msg);
    };
    client_.onEntityEnter = [this](const SvEntityEnterMsg& msg) {
        entityEnters_.push_back(msg);
    };
    client_.onEntityLeave = [this](const SvEntityLeaveMsg& msg) {
        entityLeaves_.push_back(msg);
    };
    client_.onEntityUpdate = [this](const SvEntityUpdateMsg& msg) {
        entityUpdates_.push_back(msg);
    };
    client_.onZoneTransition = [this](const SvZoneTransitionMsg& msg) {
        zoneTransitions_.push_back(msg);
    };
    client_.onDeathNotify = [this](const SvDeathNotifyMsg& msg) {
        deathNotifies_.push_back(msg);
    };
    client_.onSkillResult = [this](const SvSkillResultMsg& msg) {
        skillResults_.push_back(msg);
    };
    client_.onLevelUp = [this](const SvLevelUpMsg& msg) {
        levelUps_.push_back(msg);
    };
    client_.onInventorySync = [this](const SvInventorySyncMsg& msg) {
        inventorySyncs_.push_back(msg);
    };
    client_.onSkillSync = [this](const SvSkillSyncMsg& msg) {
        skillSyncs_.push_back(msg);
    };
    client_.onQuestSync = [this](const SvQuestSyncMsg& msg) {
        questSyncs_.push_back(msg);
    };
    client_.onLootPickup = [this](const SvLootPickupMsg& msg) {
        lootPickups_.push_back(msg);
    };
    client_.onRespawn = [this](const SvRespawnMsg& msg) {
        respawns_.push_back(msg);
    };
    client_.onMovementCorrection = [this](const SvMovementCorrectionMsg& msg) {
        movementCorrections_.push_back(msg);
    };
    client_.onChatMessage = [this](const SvChatMessageMsg& msg) {
        chatMessages_.push_back(msg);
    };
    client_.onConnected = [this]() {
        udpConnected_ = true;
    };
    client_.onConnectRejected = [this](const std::string& reason) {
        connectRejectReason_ = reason;
        wasRejected_ = true;
    };
}

float TestBot::currentTime() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(now - epoch_).count();
}

// --- Lifecycle ---

AuthResponse TestBot::login(const std::string& host, uint16_t authPort,
                            const std::string& username, const std::string& password,
                            float timeoutSec) {
    auth_.loginAsync(host, authPort, username, password);

    auto start = std::chrono::steady_clock::now();
    while (!auth_.hasResult()) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        float elapsedSec = std::chrono::duration<float>(elapsed).count();
        if (elapsedSec > timeoutSec) {
            FAIL("TestBot::login timed out after ", timeoutSec, "s");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    authResponse_ = auth_.consumeResult();
    return authResponse_;
}

void TestBot::connectUDP(const std::string& host, uint16_t gamePort,
                         float timeoutSec) {
    udpConnected_ = false;
    wasRejected_ = false;
    connectRejectReason_.clear();

    bool started = client_.connectWithToken(host, gamePort, authResponse_.authToken);
    REQUIRE_MESSAGE(started, "NetClient::connectWithToken failed to start");

    auto start = std::chrono::steady_clock::now();
    while (!udpConnected_ && !wasRejected_) {
        client_.poll(currentTime());
        auto elapsed = std::chrono::steady_clock::now() - start;
        float elapsedSec = std::chrono::duration<float>(elapsed).count();
        if (elapsedSec > timeoutSec) {
            FAIL("TestBot::connectUDP timed out after ", timeoutSec, "s");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (wasRejected_) {
        FAIL("TestBot::connectUDP rejected: ", connectRejectReason_);
    }
}

void TestBot::disconnect() {
    client_.disconnect();
    udpConnected_ = false;
}

// --- Commands ---

void TestBot::sendMove(Vec2 position, Vec2 velocity) {
    client_.sendMove(position, velocity, currentTime());
}

void TestBot::sendAttack(uint64_t targetPersistentId) {
    client_.sendAction(0, targetPersistentId, 0);
}

void TestBot::sendUseSkill(const std::string& skillId, uint8_t rank, uint64_t targetId) {
    client_.sendUseSkill(skillId, rank, targetId);
}

void TestBot::sendZoneTransition(const std::string& targetScene) {
    client_.sendZoneTransition(targetScene);
}

void TestBot::sendRespawn(uint8_t type) {
    client_.sendRespawn(type);
}

void TestBot::sendChat(uint8_t channel, const std::string& message,
                       const std::string& target) {
    client_.sendChat(channel, message, target);
}

// --- Event collection ---

void TestBot::pollEvents() {
    client_.poll(currentTime());
}

void TestBot::pollFor(float durationSec) {
    auto start = std::chrono::steady_clock::now();
    while (true) {
        client_.poll(currentTime());
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration<float>(elapsed).count() >= durationSec) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void TestBot::clearEvents() {
    playerStates_.clear();
    combatEvents_.clear();
    entityEnters_.clear();
    entityLeaves_.clear();
    entityUpdates_.clear();
    zoneTransitions_.clear();
    deathNotifies_.clear();
    skillResults_.clear();
    levelUps_.clear();
    inventorySyncs_.clear();
    skillSyncs_.clear();
    questSyncs_.clear();
    lootPickups_.clear();
    respawns_.clear();
    movementCorrections_.clear();
    chatMessages_.clear();
}

} // namespace fate
