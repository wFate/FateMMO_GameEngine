#pragma once
#include "game/shared/faction.h"
#include "engine/core/types.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fate {

// ============================================================================
// Enums
// ============================================================================

enum class ArenaMode : uint8_t { Solo = 1, Duo = 2, Team = 3 };
enum class ArenaMatchState : uint8_t { Countdown = 0, Active = 1, Ended = 2 };

// ============================================================================
// Arena Structures
// ============================================================================

struct ArenaGroup {
    uint32_t groupId = 0;
    std::vector<uint32_t> playerIds;
    Faction faction = Faction::None;
    ArenaMode mode = ArenaMode::Solo;
    int minLevel = 0;
    int maxLevel = 0;
    float queuedAt = 0.0f;
};

struct ArenaPlayerStats {
    uint32_t entityId = 0;
    Faction faction = Faction::None;
    bool isAlive = true;
    int damageDealt = 0;
    float lastActionTime = 0.0f;
    Vec2 returnPosition;
    std::string returnScene;
};

struct ArenaMatch {
    uint32_t matchId = 0;
    ArenaMode mode = ArenaMode::Solo;
    ArenaMatchState state = ArenaMatchState::Countdown;
    float startTime = 0.0f;      // when combat started (after countdown)
    float countdownEnd = 0.0f;   // when countdown finishes
    std::vector<uint32_t> teamA; // entity IDs
    std::vector<uint32_t> teamB;
    Faction factionA = Faction::None;
    Faction factionB = Faction::None;
    std::unordered_map<uint32_t, ArenaPlayerStats> players;

    static constexpr float MATCH_DURATION    = 180.0f; // 3 minutes
    static constexpr float COUNTDOWN_DURATION = 3.0f;
    static constexpr float AFK_TIMEOUT       = 30.0f;
    static constexpr int   HONOR_WIN         = 30;
    static constexpr int   HONOR_LOSS        = 5;
    static constexpr int   HONOR_TIE         = 5;

    bool allDeadOnTeam(const std::vector<uint32_t>& team) const {
        for (uint32_t id : team) {
            auto it = players.find(id);
            if (it != players.end() && it->second.isAlive) return false;
        }
        return !team.empty();
    }
};

// ============================================================================
// ArenaManager
// ============================================================================

class ArenaManager {
public:
    // -------------------------------------------------------------------------
    // Registration
    // -------------------------------------------------------------------------

    /// Register a group into the arena queue.
    /// Returns false if: player count mismatches mode, any player is already
    /// queued/in-match, or group is invalid.
    bool registerGroup(const std::vector<uint32_t>& playerIds,
                       Faction faction,
                       ArenaMode mode,
                       const std::vector<int>& levels,
                       float currentTime) {
        // Validate player count matches mode
        int required = static_cast<int>(mode);
        if (static_cast<int>(playerIds.size()) != required) return false;
        if (static_cast<int>(levels.size()) != required) return false;
        if (faction == Faction::None) return false;

        // Check no player is already queued or in a match
        for (uint32_t id : playerIds) {
            if (playerToGroup_.count(id) || playerToMatch_.count(id)) return false;
        }

        ArenaGroup group;
        group.groupId = nextGroupId_++;
        group.playerIds = playerIds;
        group.faction = faction;
        group.mode = mode;
        group.queuedAt = currentTime;
        group.minLevel = *std::min_element(levels.begin(), levels.end());
        group.maxLevel = *std::max_element(levels.begin(), levels.end());

        for (uint32_t id : playerIds) {
            playerToGroup_[id] = group.groupId;
        }

        queue_.push_back(std::move(group));
        return true;
    }

    /// Remove a group from the queue by any player's entity ID.
    void unregisterGroup(uint32_t anyPlayerId) {
        auto git = playerToGroup_.find(anyPlayerId);
        if (git == playerToGroup_.end()) return;
        uint32_t groupId = git->second;
        removeGroupFromQueue(groupId, "");
    }

    bool isPlayerQueued(uint32_t entityId) const {
        return playerToGroup_.count(entityId) > 0;
    }

    bool isPlayerInMatch(uint32_t entityId) const {
        return playerToMatch_.count(entityId) > 0;
    }

    // -------------------------------------------------------------------------
    // Matchmaking
    // -------------------------------------------------------------------------

    /// Try to form matches from the current queue.
    /// Returns a list of newly created match IDs.
    std::vector<uint32_t> tryMatchmaking() {
        std::vector<uint32_t> newMatchIds;

        // Group queue entries by mode for efficient pairing
        for (auto modeVal : { ArenaMode::Solo, ArenaMode::Duo, ArenaMode::Team }) {
            bool found = true;
            while (found) {
                found = false;
                // Find two compatible groups of the same mode but different factions
                int idxA = -1, idxB = -1;
                for (int i = 0; i < static_cast<int>(queue_.size()); ++i) {
                    if (queue_[i].mode != modeVal) continue;
                    for (int j = i + 1; j < static_cast<int>(queue_.size()); ++j) {
                        if (queue_[j].mode != modeVal) continue;
                        if (!canMatch(queue_[i], queue_[j])) continue;
                        idxA = i;
                        idxB = j;
                        break;
                    }
                    if (idxA >= 0) break;
                }

                if (idxA < 0) break;

                // Create match — copy before erasing (erase invalidates indices)
                ArenaGroup groupA = queue_[idxA];
                ArenaGroup groupB = queue_[idxB];

                // Remove higher index first to keep lower index valid
                if (idxA < idxB) {
                    queue_.erase(queue_.begin() + idxB);
                    queue_.erase(queue_.begin() + idxA);
                } else {
                    queue_.erase(queue_.begin() + idxA);
                    queue_.erase(queue_.begin() + idxB);
                }

                // Unmap from group lookup
                for (uint32_t id : groupA.playerIds) playerToGroup_.erase(id);
                for (uint32_t id : groupB.playerIds) playerToGroup_.erase(id);

                uint32_t matchId = createMatch(groupA, groupB);
                newMatchIds.push_back(matchId);
                found = true; // look for more matches in this mode
            }
        }

        return newMatchIds;
    }

    // -------------------------------------------------------------------------
    // Match lifecycle
    // -------------------------------------------------------------------------

    /// Advance all matches and expire stale queue entries. Call every frame or ~1s.
    void tickMatches(float currentTime) {
        // Expire queue entries that have waited too long
        {
            std::vector<uint32_t> toExpire;
            for (const auto& g : queue_) {
                if (currentTime - g.queuedAt > QUEUE_TIMEOUT) {
                    toExpire.push_back(g.groupId);
                }
            }
            for (uint32_t gid : toExpire) {
                removeGroupFromQueue(gid, "Queue timeout");
            }
        }

        // Tick active/countdown matches
        std::vector<uint32_t> toEnd;
        for (auto& [matchId, match] : matches_) {
            if (match.state == ArenaMatchState::Ended) continue;

            if (match.state == ArenaMatchState::Countdown) {
                if (currentTime >= match.countdownEnd) {
                    // Transition to Active
                    match.state = ArenaMatchState::Active;
                    match.startTime = currentTime;
                    // Initialize lastActionTime for all players so AFK clock
                    // starts from match start, not from when they queued.
                    for (auto& [eid, stats] : match.players) {
                        stats.lastActionTime = currentTime;
                    }
                    if (onMatchStart) onMatchStart(matchId);
                }
                continue;
            }

            // --- Active match ---

            // Check timer expiry → tie
            if (currentTime - match.startTime >= ArenaMatch::MATCH_DURATION) {
                bool aAlive = !match.allDeadOnTeam(match.teamA);
                bool bAlive = !match.allDeadOnTeam(match.teamB);
                if (aAlive && bAlive) {
                    toEnd.push_back(matchId);
                    endMatch(match, /*teamAWins=*/false, /*tie=*/true);
                    continue;
                }
                // One side is already dead — that win should have been caught by
                // onPlayerKill, but handle it defensively here.
                if (!aAlive && bAlive) {
                    toEnd.push_back(matchId);
                    endMatch(match, /*teamAWins=*/false, /*tie=*/false);
                    continue;
                }
                if (aAlive && !bAlive) {
                    toEnd.push_back(matchId);
                    endMatch(match, /*teamAWins=*/true, /*tie=*/false);
                    continue;
                }
            }

            // Check AFK on LIVING players only — collect all AFK players first,
            // fire all callbacks, then evaluate win conditions once.
            {
                std::vector<uint32_t> afkPlayers;
                for (auto& [eid, stats] : match.players) {
                    if (!stats.isAlive) continue; // dead → exempt
                    if (currentTime - stats.lastActionTime > ArenaMatch::AFK_TIMEOUT) {
                        afkPlayers.push_back(eid);
                    }
                }
                for (uint32_t afkId : afkPlayers) {
                    auto it = match.players.find(afkId);
                    if (it == match.players.end()) continue;
                    it->second.isAlive = false;
                    if (onPlayerForfeited) onPlayerForfeited(afkId, "AFK");
                }
                if (!afkPlayers.empty()) {
                    bool aAllDead = match.allDeadOnTeam(match.teamA);
                    bool bAllDead = match.allDeadOnTeam(match.teamB);
                    if (aAllDead || bAllDead) {
                        toEnd.push_back(matchId);
                        endMatch(match, /*teamAWins=*/!aAllDead,
                                 /*tie=*/(aAllDead && bAllDead));
                    }
                }
            }
        }

        // Remove ended matches from the map
        for (uint32_t id : toEnd) {
            cleanupMatch(id);
        }
    }

    /// Called when a player kills another player in a match.
    void onPlayerKill(uint32_t matchId, uint32_t killerId, uint32_t victimId) {
        auto mit = matches_.find(matchId);
        if (mit == matches_.end()) return;
        ArenaMatch& match = mit->second;
        if (match.state != ArenaMatchState::Active) return;

        auto kit = match.players.find(killerId);
        auto vit = match.players.find(victimId);
        if (vit == match.players.end()) return;

        vit->second.isAlive = false;
        if (kit != match.players.end()) {
            kit->second.damageDealt += 1;
        }

        // Check win condition
        bool aAllDead = match.allDeadOnTeam(match.teamA);
        bool bAllDead = match.allDeadOnTeam(match.teamB);

        if (aAllDead || bAllDead) {
            bool tie = aAllDead && bAllDead;
            endMatch(match, /*teamAWins=*/!aAllDead, tie);
            cleanupMatch(matchId);
        }
    }

    /// Called whenever a player takes an action (move, attack, skill use, etc.).
    void onPlayerAction(uint32_t matchId, uint32_t playerId, float currentTime) {
        auto mit = matches_.find(matchId);
        if (mit == matches_.end()) return;
        auto pit = mit->second.players.find(playerId);
        if (pit == mit->second.players.end()) return;
        pit->second.lastActionTime = currentTime;
    }

    /// Called when a player disconnects. Forfeits them from any active match or
    /// removes them from the queue.
    void onPlayerDisconnect(uint32_t entityId) {
        // Remove from queue if present
        if (playerToGroup_.count(entityId)) {
            unregisterGroup(entityId);
            return;
        }

        // Remove from active match
        auto mit = playerToMatch_.find(entityId);
        if (mit == playerToMatch_.end()) return;
        uint32_t matchId = mit->second;

        auto mmit = matches_.find(matchId);
        if (mmit == matches_.end()) return;
        ArenaMatch& match = mmit->second;

        auto pit = match.players.find(entityId);
        if (pit != match.players.end()) {
            pit->second.isAlive = false;
        }

        if (onPlayerForfeited) onPlayerForfeited(entityId, "Disconnect");

        bool aAllDead = match.allDeadOnTeam(match.teamA);
        bool bAllDead = match.allDeadOnTeam(match.teamB);
        if (aAllDead || bAllDead) {
            bool tie = aAllDead && bAllDead;
            endMatch(match, !aAllDead, tie);
            cleanupMatch(matchId);
        }
    }

    // -------------------------------------------------------------------------
    // Queries
    // -------------------------------------------------------------------------

    ArenaMatch* getMatch(uint32_t matchId) {
        auto it = matches_.find(matchId);
        return it != matches_.end() ? &it->second : nullptr;
    }

    const ArenaMatch* getMatch(uint32_t matchId) const {
        auto it = matches_.find(matchId);
        return it != matches_.end() ? &it->second : nullptr;
    }

    uint32_t getMatchForPlayer(uint32_t entityId) const {
        auto it = playerToMatch_.find(entityId);
        return it != playerToMatch_.end() ? it->second : 0;
    }

    // -------------------------------------------------------------------------
    // Callbacks
    // -------------------------------------------------------------------------
    std::function<void(uint32_t matchId)> onMatchStart;
    std::function<void(uint32_t matchId, bool teamAWins, bool tie)> onMatchEnd;
    std::function<void(uint32_t entityId, const std::string& reason)> onPlayerForfeited;
    std::function<void(const std::vector<uint32_t>& playerIds, const std::string& reason)> onGroupUnregistered;

private:
    std::vector<ArenaGroup> queue_;
    std::unordered_map<uint32_t, ArenaMatch> matches_;
    std::unordered_map<uint32_t, uint32_t> playerToMatch_; // entityId → matchId
    std::unordered_map<uint32_t, uint32_t> playerToGroup_; // entityId → groupId (in queue)
    uint32_t nextGroupId_ = 1;
    uint32_t nextMatchId_ = 1;

    static constexpr float QUEUE_TIMEOUT  = 300.0f; // 5 minutes
    static constexpr int   MAX_LEVEL_DIFF = 5;

    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------

    bool canMatch(const ArenaGroup& a, const ArenaGroup& b) const {
        if (a.faction == b.faction) return false;
        if (a.faction == Faction::None || b.faction == Faction::None) return false;
        // Both directions must satisfy the level range check
        if (std::abs(a.maxLevel - b.minLevel) > MAX_LEVEL_DIFF) return false;
        if (std::abs(b.maxLevel - a.minLevel) > MAX_LEVEL_DIFF) return false;
        return true;
    }

    uint32_t createMatch(const ArenaGroup& groupA, const ArenaGroup& groupB) {
        uint32_t matchId = nextMatchId_++;
        ArenaMatch match;
        match.matchId = matchId;
        match.mode = groupA.mode;
        match.state = ArenaMatchState::Countdown;
        match.countdownEnd = groupA.queuedAt > groupB.queuedAt
                             ? groupA.queuedAt : groupB.queuedAt;
        // countdownEnd = "now" + COUNTDOWN_DURATION; since we don't have a real
        // clock here, base it on the later of the two queue times — but for
        // practical purposes the server passes currentTime into tryMatchmaking
        // indirectly via queuedAt. We simply set countdownEnd relative to the
        // match creation moment which we don't track; instead we set it so
        // tickMatches will fire it at COUNTDOWN_DURATION seconds from now.
        // Best approach: use 0.0 as "creation time" anchor and let tickMatches
        // compare. We store the current (latest) queue time + COUNTDOWN_DURATION.
        float createdAt = std::max(groupA.queuedAt, groupB.queuedAt);
        match.countdownEnd = createdAt + ArenaMatch::COUNTDOWN_DURATION;
        match.startTime = 0.0f;
        match.teamA = groupA.playerIds;
        match.teamB = groupB.playerIds;
        match.factionA = groupA.faction;
        match.factionB = groupB.faction;

        // Build player stats — lastActionTime set to 0 until Active transition
        auto addPlayers = [&](const std::vector<uint32_t>& ids, Faction faction) {
            for (uint32_t id : ids) {
                ArenaPlayerStats stats;
                stats.entityId = id;
                stats.faction = faction;
                stats.isAlive = true;
                stats.damageDealt = 0;
                stats.lastActionTime = 0.0f;
                match.players[id] = stats;
                playerToMatch_[id] = matchId;
            }
        };
        addPlayers(groupA.playerIds, groupA.faction);
        addPlayers(groupB.playerIds, groupB.faction);

        matches_[matchId] = std::move(match);
        return matchId;
    }

    void endMatch(ArenaMatch& match, bool teamAWins, bool tie) {
        if (match.state == ArenaMatchState::Ended) return;
        match.state = ArenaMatchState::Ended;

        if (onMatchEnd) onMatchEnd(match.matchId, teamAWins, tie);
    }

    void cleanupMatch(uint32_t matchId) {
        auto mit = matches_.find(matchId);
        if (mit == matches_.end()) return;
        for (const auto& [eid, stats] : mit->second.players) {
            playerToMatch_.erase(eid);
        }
        // Note: do NOT erase from matches_ so callers can still query the result
        // via getMatch() after the match ends. Mark as Ended instead.
    }

    /// Remove a group from the queue and fire onGroupUnregistered if reason is set.
    void removeGroupFromQueue(uint32_t groupId, const std::string& reason) {
        for (int i = 0; i < static_cast<int>(queue_.size()); ++i) {
            if (queue_[i].groupId != groupId) continue;
            std::vector<uint32_t> ids = queue_[i].playerIds;
            for (uint32_t id : ids) playerToGroup_.erase(id);
            queue_.erase(queue_.begin() + i);
            if (!reason.empty() && onGroupUnregistered) {
                onGroupUnregistered(ids, reason);
            }
            return;
        }
    }
};

} // namespace fate
