#include "engine/editor/mobai_tune_panel.h"

#include <cmath>

namespace fate {

namespace {
inline bool nearly(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}
bool dtoEqual(const MobAITuningDTO& a, const MobAITuningDTO& b) {
    return nearly(a.dominantHysteresisPx,           b.dominantHysteresisPx)
        && nearly(a.axisDwellMaxSeconds,            b.axisDwellMaxSeconds)
        && nearly(a.axisInterceptWindow,            b.axisInterceptWindow)
        && nearly(a.axisProgressFraction,           b.axisProgressFraction)
        && nearly(a.interceptPerpMinPx,             b.interceptPerpMinPx)
        && nearly(a.detourCommitSeconds,            b.detourCommitSeconds)
        && nearly(a.escapeBlockedThreshold,         b.escapeBlockedThreshold)
        && nearly(a.escapePerpBlockedHoldSeconds,   b.escapePerpBlockedHoldSeconds)
        && nearly(a.escapeProbeBlockedHoldSeconds,  b.escapeProbeBlockedHoldSeconds)
        && nearly(a.escapeHoldDuration,             b.escapeHoldDuration)
        && a.escapeProbeRepeats == b.escapeProbeRepeats
        && nearly(a.escapeMinPerpProgressForProbe,  b.escapeMinPerpProgressForProbe)
        && nearly(a.escapeLaneMinPx,                b.escapeLaneMinPx)
        && nearly(a.escapeLaneMaxPx,                b.escapeLaneMaxPx)
        && nearly(a.escapePrimaryProbeDistance,     b.escapePrimaryProbeDistance)
        && nearly(a.escapeBackoffDistance,          b.escapeBackoffDistance)
        && a.debugEscape == b.debugEscape;
}
} // namespace

SnapshotConsumeAction consumeSnapshot(MobAITuningPanelState& panel,
                                      const SvAdminMobAITuneStateMsg& msg) {
    if (msg.persistentId != panel.subscribedPid) {
        return SnapshotConsumeAction::Ignored;
    }
    bool firstSnapshot = !panel.haveSnapshot;
    panel.lastSnapshot = msg;
    panel.haveSnapshot = true;
    if (msg.found == 0) {
        // Server has dropped this subscription. Hold the greyed-out state
        // sticky -- subscribedPid stays bound to this pid so the inspector
        // gate (engine/editor/editor_inspector.cpp) does NOT re-subscribe
        // every frame against an entity the server has already declared
        // missing. The user clears this state by changing selection
        // (deselect/reselect or pick a different mob), which routes through
        // resetForNewPid via the eligibility gate.
        return SnapshotConsumeAction::Accepted;
    }
    if (msg.appliedSeq < panel.lastSentApplySeq) {
        // Stale: keep diagnostics for display, drop the live mirror.
        return SnapshotConsumeAction::DiagnosticsOnly;
    }
    panel.lastReceivedLive = msg.live;
    // On the FIRST snapshot for a subscription, seed local from server
    // truth so the user sees real values instead of struct defaults.
    // After that, local is user-owned -- we never overwrite it from the
    // dispatcher path. Self-heal is the only way local resyncs after
    // that, and it only fires on persistent divergence past send-grace.
    if (firstSnapshot) {
        panel.local = msg.live;
    }
    return SnapshotConsumeAction::Accepted;
}

bool preDrawPump(MobAITuningPanelState& panel, double now,
                 bool anyWidgetActive,
                 double /*idleGraceSeconds*/,   // kept for API stability
                 double sendGraceSeconds) {
    if (panel.subscribedPid == 0 || !panel.haveSnapshot) return false;
    // No idle-resync. local is user-owned after the first snapshot.
    // Self-heal: if user's local has diverged from the last server-acked
    // mirror AND we're past the send-grace window AND no widget is
    // actively being edited, mark dirty so the next post-draw pump
    // resends a fresh full snapshot. Catches rate-limiter-drop without
    // fighting active edits or erasing user intent.
    if (!anyWidgetActive
        && (now - panel.lastSendTime) > sendGraceSeconds
        && !dtoEqual(panel.local, panel.lastReceivedLive)) {
        panel.dirty = true;
        return true;
    }
    return false;
}

int postDrawApplyPump(MobAITuningPanelState& panel, double now,
                      bool forceFinalApply,
                      double minSendIntervalSeconds) {
    if (panel.subscribedPid == 0) return 0;
    // Defense-in-depth: never send Apply before the first valid snapshot has
    // seeded panel.local. The drawServerMobAISection_ gate already returns
    // before any widgets are drawn in that window, but a stray dirty=true
    // (e.g. from a self-heal that fired on a stale local mirror) could
    // otherwise slip a defaults-laden DTO past the UI gate.
    if (!panel.haveSnapshot || panel.lastSnapshot.found != 1) return 0;
    if (forceFinalApply) {
        panel.lastSentApplySeq = panel.nextApplySeq++;
        panel.lastSendTime     = now;
        panel.nextAllowedSendAt = now + minSendIntervalSeconds;
        panel.dirty            = false;
        return 2;   // final apply
    }
    if (panel.dirty && now >= panel.nextAllowedSendAt) {
        panel.lastSentApplySeq = panel.nextApplySeq++;
        panel.lastSendTime     = now;
        panel.nextAllowedSendAt = now + minSendIntervalSeconds;
        panel.dirty            = false;
        return 1;   // mid-drag apply
    }
    return 0;
}

} // namespace fate
