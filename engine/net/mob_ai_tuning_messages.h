#pragma once
// Wire DTO + admin tuning messages for the editor's "Server Mob AI"
// inspector section. Engine-side only; server translates DTO <-> game/MobAI.

#include <cstdint>
#include <string>

#include "engine/net/byte_stream.h"
#include "engine/net/protocol.h"   // detail::writeU64 / readU64

namespace fate {

// Strict-equality schema. v1 server only accepts v1 client. Bump in lockstep
// with PROTOCOL_VERSION when fields are added/removed/reordered.
constexpr uint8_t kMobAITuningSchemaV1 = 1;

struct MobAITuningDTO {
    uint8_t schemaVersion = kMobAITuningSchemaV1;
    // Editable tunables (excluding schemaVersion): 15 floats + 2 u8 = 62 bytes.
    // Total DTO body including schemaVersion = 63 bytes.
    float dominantHysteresisPx           = 6.0f;
    float axisDwellMaxSeconds            = 0.6f;
    float axisInterceptWindow            = 0.4f;
    float axisProgressFraction           = 0.85f;
    float interceptPerpMinPx             = 32.0f;
    float detourCommitSeconds            = 0.25f;
    float escapeBlockedThreshold         = 0.15f;
    float escapePerpBlockedHoldSeconds   = 0.30f;
    float escapeProbeBlockedHoldSeconds  = 0.5f;
    float escapeHoldDuration             = 0.4f;
    uint8_t escapeProbeRepeats           = 2;
    float escapeMinPerpProgressForProbe  = 12.0f;
    float escapeLaneMinPx                = 24.0f;
    float escapeLaneMaxPx                = 64.0f;
    float escapePrimaryProbeDistance     = 24.0f;
    float escapeBackoffDistance          = 12.0f;
    uint8_t debugEscape                  = 0;

    void write(ByteWriter& w) const {
        w.writeU8(schemaVersion);
        w.writeFloat(dominantHysteresisPx);
        w.writeFloat(axisDwellMaxSeconds);
        w.writeFloat(axisInterceptWindow);
        w.writeFloat(axisProgressFraction);
        w.writeFloat(interceptPerpMinPx);
        w.writeFloat(detourCommitSeconds);
        w.writeFloat(escapeBlockedThreshold);
        w.writeFloat(escapePerpBlockedHoldSeconds);
        w.writeFloat(escapeProbeBlockedHoldSeconds);
        w.writeFloat(escapeHoldDuration);
        w.writeU8(escapeProbeRepeats);
        w.writeFloat(escapeMinPerpProgressForProbe);
        w.writeFloat(escapeLaneMinPx);
        w.writeFloat(escapeLaneMaxPx);
        w.writeFloat(escapePrimaryProbeDistance);
        w.writeFloat(escapeBackoffDistance);
        w.writeU8(debugEscape);
    }

    // Reads schemaVersion FIRST. If it does not match v1, sets ok=false and
    // returns immediately without consuming the field block as v1 layout.
    static MobAITuningDTO read(ByteReader& r, bool& ok) {
        MobAITuningDTO m;
        m.schemaVersion = r.readU8();
        if (!r.ok() || m.schemaVersion != kMobAITuningSchemaV1) {
            ok = false;
            return m;
        }
        m.dominantHysteresisPx           = r.readFloat();
        m.axisDwellMaxSeconds            = r.readFloat();
        m.axisInterceptWindow            = r.readFloat();
        m.axisProgressFraction           = r.readFloat();
        m.interceptPerpMinPx             = r.readFloat();
        m.detourCommitSeconds            = r.readFloat();
        m.escapeBlockedThreshold         = r.readFloat();
        m.escapePerpBlockedHoldSeconds   = r.readFloat();
        m.escapeProbeBlockedHoldSeconds  = r.readFloat();
        m.escapeHoldDuration             = r.readFloat();
        m.escapeProbeRepeats             = r.readU8();
        m.escapeMinPerpProgressForProbe  = r.readFloat();
        m.escapeLaneMinPx                = r.readFloat();
        m.escapeLaneMaxPx                = r.readFloat();
        m.escapePrimaryProbeDistance     = r.readFloat();
        m.escapeBackoffDistance          = r.readFloat();
        m.debugEscape                    = r.readU8();
        ok = r.ok();
        return m;
    }
};

// Client -> Server: apply full tunable snapshot to selected mob.
struct CmdAdminMobAITuneApplyMsg {
    uint64_t persistentId = 0;
    uint32_t applySeq     = 0;   // monotonic per-panel; for stale-state suppression on client
    uint8_t  finalApply   = 0;   // 1 = mouse-up final, 0 = mid-drag stream
    MobAITuningDTO tuning;

    void write(ByteWriter& w) const {
        detail::writeU64(w, persistentId);
        w.writeU32(applySeq);
        w.writeU8(finalApply);
        tuning.write(w);
    }
    static CmdAdminMobAITuneApplyMsg read(ByteReader& r, bool& ok) {
        CmdAdminMobAITuneApplyMsg m;
        m.persistentId = detail::readU64(r);
        m.applySeq     = r.readU32();
        m.finalApply   = r.readU8();
        if (!r.ok()) { ok = false; return m; }
        m.tuning = MobAITuningDTO::read(r, ok);
        return m;
    }
};

// Client -> Server: subscribe to 5Hz diagnostic snapshots for a PID.
// pid==0 unsubscribes. At most one subscription per client.
struct CmdAdminMobAITuneSubscribeMsg {
    uint64_t persistentId = 0;

    void write(ByteWriter& w) const {
        detail::writeU64(w, persistentId);
    }
    static CmdAdminMobAITuneSubscribeMsg read(ByteReader& r) {
        CmdAdminMobAITuneSubscribeMsg m;
        m.persistentId = detail::readU64(r);
        return m;
    }
};

// Server -> Client: read-only 5Hz state snapshot for the subscribed mob.
// found=0 means the mob no longer exists in this client's world; subscription
// has been dropped server-side and the inspector should grey out the section.
struct SvAdminMobAITuneStateMsg {
    uint64_t persistentId = 0;
    uint8_t  found        = 0;
    uint32_t appliedSeq   = 0;
    MobAITuningDTO live;
    // Diagnostics - server accessor reads only.
    uint8_t  escapePhase            = 0;   // MobAI::EscapePhase
    uint8_t  mobMode                = 0;   // AIMode
    int32_t  escapeProbeRepeatIndex = 0;
    float    escapePerpOffsetA      = 0.0f;
    float    escapePerpOffsetB      = 0.0f;
    float    episodeOriginX = 0.0f, episodeOriginY = 0.0f;
    float    attemptStartX  = 0.0f, attemptStartY  = 0.0f;
    float    currentX = 0.0f, currentY = 0.0f;
    std::string mobDefId;

    void write(ByteWriter& w) const {
        detail::writeU64(w, persistentId);
        w.writeU8(found);
        w.writeU32(appliedSeq);
        live.write(w);
        w.writeU8(escapePhase);
        w.writeU8(mobMode);
        w.writeI32(escapeProbeRepeatIndex);
        w.writeFloat(escapePerpOffsetA);
        w.writeFloat(escapePerpOffsetB);
        w.writeFloat(episodeOriginX); w.writeFloat(episodeOriginY);
        w.writeFloat(attemptStartX);  w.writeFloat(attemptStartY);
        w.writeFloat(currentX);       w.writeFloat(currentY);
        w.writeString(mobDefId);
    }
    static SvAdminMobAITuneStateMsg read(ByteReader& r, bool& ok) {
        SvAdminMobAITuneStateMsg m;
        m.persistentId = detail::readU64(r);
        m.found        = r.readU8();
        m.appliedSeq   = r.readU32();
        if (!r.ok()) { ok = false; return m; }
        m.live = MobAITuningDTO::read(r, ok);
        if (!ok) return m;
        m.escapePhase            = r.readU8();
        m.mobMode                = r.readU8();
        m.escapeProbeRepeatIndex = r.readI32();
        m.escapePerpOffsetA      = r.readFloat();
        m.escapePerpOffsetB      = r.readFloat();
        m.episodeOriginX         = r.readFloat();
        m.episodeOriginY         = r.readFloat();
        m.attemptStartX          = r.readFloat();
        m.attemptStartY          = r.readFloat();
        m.currentX               = r.readFloat();
        m.currentY               = r.readFloat();
        m.mobDefId               = r.readString(256);
        ok = r.ok();
        return m;
    }
};

} // namespace fate
