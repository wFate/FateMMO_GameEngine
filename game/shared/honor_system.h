#pragma once
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "game/shared/game_types.h"

namespace fate {

// ============================================================================
// Honor Rank — computed from honor points
// ============================================================================
enum class HonorRank : uint8_t {
    Recruit = 0,
    Scout = 1,
    CombatSoldier = 2,
    VeteranSoldier = 3,
    ApprenticeKnight = 4,
    Fighter = 5,
    EliteFighter = 6,
    FieldCommander = 7,
    Commander = 8,
    General = 9
};

// ============================================================================
// PvP Kill Result — returned by HonorSystem::processKill()
// ============================================================================
struct PvPKillHonorResult {
    int attackerGain = 0;
    int victimLoss   = 0;
};

// ============================================================================
// Honor System - PvP honor gain/loss with kill tracking
// ============================================================================
class HonorSystem {
public:
    HonorSystem() = delete;

    static constexpr int MAX_HONOR             = 1'000'000;
    static constexpr int HONOR_LOSS_ON_DEATH   = 30;
    static constexpr int MAX_KILLS_PER_HOUR    = 5;
    static constexpr auto KILL_TRACKING_DURATION = std::chrono::hours(1);

    /// Returns true if the attacker can still earn honor from killing this victim
    /// (fewer than MAX_KILLS_PER_HOUR kills of this victim within the last hour).
    static bool canEarnHonorFromKill(const std::string& attackerId,
                                     const std::string& victimId);

    /// Returns true if the victim can still lose honor from being killed by this attacker.
    /// Uses the same 5-per-hour tracking — after 5 deaths to the same attacker, stop losing.
    static bool canLoseHonorFromKill(const std::string& attackerId,
                                     const std::string& victimId);

    /// Records a kill timestamp for the attacker/victim pair.
    static void recordKill(const std::string& attackerId,
                           const std::string& victimId);

    /// Calculates honor gained from a PvP kill based on PK statuses.
    /// Black attackers earn no honor.
    static int calculateHonorGain(PKStatus attackerStatus, PKStatus victimStatus);

    /// Returns the fixed honor lost on PvP death.
    static int calculateHonorLoss();

    /// Process a full PvP kill — calculates honor changes for both players.
    /// Handles kill tracking, capping gain to what victim can pay out,
    /// and capping loss to what victim actually has.
    /// Uses in-memory kill tracking (lost on restart).
    static PvPKillHonorResult processKill(PKStatus attackerStatus,
                                          PKStatus victimStatus,
                                          const std::string& attackerId,
                                          const std::string& victimId,
                                          int victimCurrentHonor);

    /// DB-backed variant: caller provides the recent kill count from the database
    /// instead of relying on in-memory tracking. The caller is responsible for
    /// recording the kill to the DB after this returns.
    static PvPKillHonorResult processKillWithCount(PKStatus attackerStatus,
                                                    PKStatus victimStatus,
                                                    int recentKillCount,
                                                    int victimCurrentHonor);

    /// Clears all kill tracking data.
    static void clearKillTracking();

    /// Returns the HonorRank for a given honor point total.
    static HonorRank getHonorRank(int honor) {
        if (honor >= 99999) return HonorRank::General;
        if (honor >= 75000) return HonorRank::Commander;
        if (honor >= 50000) return HonorRank::FieldCommander;
        if (honor >= 25000) return HonorRank::EliteFighter;
        if (honor >= 10000) return HonorRank::Fighter;
        if (honor >= 5000)  return HonorRank::ApprenticeKnight;
        if (honor >= 2000)  return HonorRank::VeteranSoldier;
        if (honor >= 500)   return HonorRank::CombatSoldier;
        if (honor >= 100)   return HonorRank::Scout;
        return HonorRank::Recruit;
    }

    /// Returns the display name for a given HonorRank.
    static const char* getHonorRankName(HonorRank rank) {
        switch (rank) {
            case HonorRank::Recruit:          return "Recruit";
            case HonorRank::Scout:            return "Scout";
            case HonorRank::CombatSoldier:    return "Combat Soldier";
            case HonorRank::VeteranSoldier:   return "Veteran Soldier";
            case HonorRank::ApprenticeKnight: return "Apprentice Knight";
            case HonorRank::Fighter:          return "Fighter";
            case HonorRank::EliteFighter:     return "Elite Fighter";
            case HonorRank::FieldCommander:   return "Field Commander";
            case HonorRank::Commander:        return "Commander";
            case HonorRank::General:          return "General";
            default:                          return "Unknown";
        }
    }

private:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    /// Builds a composite key "attackerId\0victimId" (null separator avoids collisions).
    static std::string makeKey(const std::string& attackerId,
                               const std::string& victimId);

    /// Removes timestamps older than KILL_TRACKING_DURATION from the list.
    static void pruneExpired(std::vector<TimePoint>& timestamps);

    // Lock-free internal helpers called while s_mutex is already held
    static bool canEarnHonorFromKillUnlocked(const std::string& attackerId,
                                              const std::string& victimId);
    static bool canLoseHonorFromKillUnlocked(const std::string& attackerId,
                                              const std::string& victimId);
    static void recordKillUnlocked(const std::string& attackerId,
                                    const std::string& victimId);

    static std::mutex                                               s_mutex;
    static std::unordered_map<std::string, std::vector<TimePoint>> s_killTracking;
};

} // namespace fate
