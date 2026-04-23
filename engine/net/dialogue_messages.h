#pragma once
#include "engine/net/byte_stream.h"
#include "engine/net/protocol.h"  // detail::writeU64 / readU64
#include <cstdint>
#include <string>

namespace fate {

// Dialogue-tree framework packet payloads. `npcEntityId` is the client's
// local replicated entity ID for the NPC the player is currently talking
// to — the server re-verifies proximity + faction gate per validator.

struct CmdDialogueGiveItemMsg {
    uint64_t    npcEntityId = 0;  // PersistentId raw value
    std::string itemId;
    uint16_t    quantity    = 0;

    void write(ByteWriter& w) const {
        detail::writeU64(w, npcEntityId);
        w.writeString(itemId);
        w.writeU16(quantity);
    }
    static CmdDialogueGiveItemMsg read(ByteReader& r) {
        CmdDialogueGiveItemMsg m;
        m.npcEntityId = detail::readU64(r);
        m.itemId      = r.readString();
        m.quantity    = r.readU16();
        return m;
    }
};

struct CmdDialogueGiveGoldMsg {
    uint64_t npcEntityId = 0;  // PersistentId raw value
    int64_t  amount      = 0;

    void write(ByteWriter& w) const {
        detail::writeU64(w, npcEntityId);
        w.writeI64(amount);
    }
    static CmdDialogueGiveGoldMsg read(ByteReader& r) {
        CmdDialogueGiveGoldMsg m;
        m.npcEntityId = detail::readU64(r);
        m.amount      = r.readI64();
        return m;
    }
};

struct CmdDialogueSetFlagMsg {
    uint64_t    npcEntityId = 0;  // PersistentId raw value
    std::string flagId;

    void write(ByteWriter& w) const {
        detail::writeU64(w, npcEntityId);
        w.writeString(flagId);
    }
    static CmdDialogueSetFlagMsg read(ByteReader& r) {
        CmdDialogueSetFlagMsg m;
        m.npcEntityId = detail::readU64(r);
        m.flagId      = r.readString();
        return m;
    }
};

struct CmdDialogueHealMsg {
    uint64_t npcEntityId = 0;  // PersistentId raw value
    int32_t  amount      = 0;  // 0 = full heal to maxHP

    void write(ByteWriter& w) const {
        detail::writeU64(w, npcEntityId);
        w.writeI32(amount);
    }
    static CmdDialogueHealMsg read(ByteReader& r) {
        CmdDialogueHealMsg m;
        m.npcEntityId = detail::readU64(r);
        m.amount      = r.readI32();
        return m;
    }
};

// Result code for SvDialogueActionResult. Stays u8 — wire-compatible if we
// add codes; panel only needs to distinguish "ok" from "rejected for reason".
enum class DialogueActionResult : uint8_t {
    Ok                   = 0,
    NpcNotInRange        = 1,
    NpcFactionDenied     = 2,
    ActionNotInTree      = 3,
    PreconditionFailed   = 4,
    PlayerInCombat       = 5,
    PlayerDead           = 6,
    InternalError        = 7,
};

struct SvDialogueActionResultMsg {
    uint64_t             npcEntityId = 0;  // PersistentId raw value
    uint8_t              action      = 0;   // DialogueAction enum value (cast u8)
    DialogueActionResult result      = DialogueActionResult::Ok;

    void write(ByteWriter& w) const {
        detail::writeU64(w, npcEntityId);
        w.writeU8(action);
        w.writeU8(static_cast<uint8_t>(result));
    }
    static SvDialogueActionResultMsg read(ByteReader& r) {
        SvDialogueActionResultMsg m;
        m.npcEntityId = detail::readU64(r);
        m.action      = r.readU8();
        m.result      = static_cast<DialogueActionResult>(r.readU8());
        return m;
    }
};

} // namespace fate
