#pragma once
#include <cstdint>
#include <string>
#include "engine/net/byte_stream.h"
#include "engine/net/protocol.h"  // detail::writeU64 / readU64

namespace fate {

// PROTOCOL 10 — Interact-site framework
//
// Result codes intentionally exclude QuestNotActive: per spec §4.4, sites with
// requiredQuestId but no active quest still return Ok with flagWasSet=false
// and the revisit node, so lore stays readable for early visitors.
enum class InteractSiteResult : uint8_t {
    Ok            = 0,
    SiteNotFound  = 1,
    OutOfRange    = 2,
    PlayerDead    = 3,
    InternalError = 255
};

// Client → Server: player tapped an interactable site (lore marker, ritual
// pedestal, etc.). The server re-verifies entity existence + proximity +
// player liveness, then emits SvInteractSiteResult.
struct CmdInteractSiteMsg {
    uint64_t siteEntityId = 0;  // PersistentId raw value of the InteractSite entity

    void write(ByteWriter& w) const {
        detail::writeU64(w, siteEntityId);
    }
    static CmdInteractSiteMsg read(ByteReader& r) {
        CmdInteractSiteMsg m;
        m.siteEntityId = detail::readU64(r);
        return m;
    }
};

// Server → Client: result of an interact-site attempt. On Ok the client
// opens the dialogue tree at `nodeId`; flagWasSet tells the UI whether the
// quest progress flag was actually toggled this visit (false on revisit or
// when no requiredQuestId is active).
struct SvInteractSiteResultMsg {
    uint64_t    siteEntityId   = 0;     // echoes CmdInteractSite.siteEntityId
    uint8_t     result         = 0;     // InteractSiteResult cast to u8
    std::string dialogueTreeId;          // empty on non-Ok results
    uint32_t    nodeId         = 0;      // first node to open (0 default)
    bool        flagWasSet     = false;  // true iff quest flag toggled this visit

    void write(ByteWriter& w) const {
        detail::writeU64(w, siteEntityId);
        w.writeU8(result);
        w.writeString(dialogueTreeId);
        w.writeU32(nodeId);
        w.writeU8(flagWasSet ? 1 : 0);
    }
    static SvInteractSiteResultMsg read(ByteReader& r) {
        SvInteractSiteResultMsg m;
        m.siteEntityId   = detail::readU64(r);
        m.result         = r.readU8();
        m.dialogueTreeId = r.readString();
        m.nodeId         = r.readU32();
        m.flagWasSet     = (r.readU8() != 0);
        return m;
    }
};

} // namespace fate
