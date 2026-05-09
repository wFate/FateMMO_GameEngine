#pragma once
// Pure state machine for the inspector's "Server Mob AI" live-tuning
// panel. No ImGui calls -- the inspector glues ImGui events into method
// calls. Extracting the state machine here makes the panel's behavior
// (apply throttle, stale-snapshot rejection, no-resync, self-heal)
// directly unit-testable without an ImGui frame.

#include <cstdint>
#include "engine/net/mob_ai_tuning_messages.h"

namespace fate {

struct MobAITuningPanelState {
    uint64_t subscribedPid     = 0;
    uint32_t nextApplySeq      = 1;
    uint32_t lastSentApplySeq  = 0;
    double   lastSendTime      = 0.0;
    double   nextAllowedSendAt = 0.0;
    double   lastEditTime      = -1.0;
    bool     dirty             = false;
    MobAITuningDTO local;
    MobAITuningDTO lastReceivedLive;
    SvAdminMobAITuneStateMsg lastSnapshot;
    bool     haveSnapshot      = false;

    void resetForNewPid(uint64_t newPid) {
        subscribedPid     = newPid;
        nextApplySeq      = 1;
        lastSentApplySeq  = 0;
        lastSendTime      = 0.0;
        nextAllowedSendAt = 0.0;
        lastEditTime      = -1.0;
        dirty             = false;
        local             = MobAITuningDTO{};
        lastReceivedLive  = MobAITuningDTO{};
        lastSnapshot      = SvAdminMobAITuneStateMsg{};
        haveSnapshot      = false;
    }
};

enum class SnapshotConsumeAction : uint8_t {
    Ignored,
    DiagnosticsOnly,
    Accepted
};

SnapshotConsumeAction consumeSnapshot(MobAITuningPanelState& panel,
                                      const SvAdminMobAITuneStateMsg& msg);

bool preDrawPump(MobAITuningPanelState& panel, double now,
                 bool anyWidgetActive,
                 double idleGraceSeconds = 0.3,
                 double sendGraceSeconds = 0.5);

int postDrawApplyPump(MobAITuningPanelState& panel, double now,
                      bool forceFinalApply,
                      double minSendIntervalSeconds = 0.1);

} // namespace fate
