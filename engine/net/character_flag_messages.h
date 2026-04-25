#pragma once
#include "engine/net/byte_stream.h"
#include <cstdint>
#include <string>
#include <vector>

namespace fate {

// Full snapshot of the player's CharacterFlagsComponent.flags set. Sent once
// per session immediately after the player-spawn handshake. Replaces any
// existing client mirror in full.
struct SvCharacterFlagsSnapshotMsg {
    std::vector<std::string> flags;

    void write(ByteWriter& w) const {
        w.writeU16(static_cast<uint16_t>(flags.size()));
        for (const auto& f : flags) w.writeString(f);
    }
    static SvCharacterFlagsSnapshotMsg read(ByteReader& r) {
        SvCharacterFlagsSnapshotMsg m;
        uint16_t n = r.readU16();
        m.flags.reserve(n);
        for (uint16_t i = 0; i < n; ++i) m.flags.push_back(r.readString());
        return m;
    }
};

// Single-flag mutation. Sent by the server after every CharacterFlagsComponent
// add/erase. The op byte is u8 so we can extend later if needed.
struct SvCharacterFlagDeltaMsg {
    enum Op : uint8_t { Add = 0, Remove = 1 };
    Op          op = Add;
    std::string flag;

    void write(ByteWriter& w) const {
        w.writeU8(static_cast<uint8_t>(op));
        w.writeString(flag);
    }
    static SvCharacterFlagDeltaMsg read(ByteReader& r) {
        SvCharacterFlagDeltaMsg m;
        m.op   = static_cast<Op>(r.readU8());
        m.flag = r.readString();
        return m;
    }
};

} // namespace fate
