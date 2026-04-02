#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "engine/net/byte_stream.h"

namespace fate {

// ============================================================================
// Admin Content Types (what kind of content is being saved/deleted)
// ============================================================================
namespace AdminContentType {
    constexpr uint8_t Mob       = 0;
    constexpr uint8_t Item      = 1;
    constexpr uint8_t LootDrop  = 2;
    constexpr uint8_t SpawnZone = 3;
}

// ============================================================================
// Admin Cache Types (which server cache to reload)
// ============================================================================
namespace AdminCacheType {
    constexpr uint8_t MobDefs     = 0;
    constexpr uint8_t ItemDefs    = 1;
    constexpr uint8_t LootTables  = 2;
    constexpr uint8_t SpawnZones  = 3;
    constexpr uint8_t SkillDefs   = 4;
    constexpr uint8_t Recipes     = 5;
    constexpr uint8_t Pets        = 6;
    constexpr uint8_t Collections = 7;
    constexpr uint8_t Costumes    = 8;
    constexpr uint8_t All         = 255;
}

// ============================================================================
// Client -> Server: Save content definition
// ============================================================================
struct CmdAdminSaveContentMsg {
    uint8_t contentType = 0;   // AdminContentType
    uint32_t contentId = 0;    // 0 = new (server assigns ID)
    std::string jsonPayload;   // Full definition as JSON

    void write(ByteWriter& w) const {
        w.writeU8(contentType);
        w.writeU32(contentId);
        w.writeString(jsonPayload);
    }
    static CmdAdminSaveContentMsg read(ByteReader& r) {
        CmdAdminSaveContentMsg m;
        m.contentType  = r.readU8();
        m.contentId    = r.readU32();
        m.jsonPayload  = r.readString();
        return m;
    }
};

// ============================================================================
// Client -> Server: Delete content definition
// ============================================================================
struct CmdAdminDeleteContentMsg {
    uint8_t contentType = 0;
    uint32_t contentId = 0;

    void write(ByteWriter& w) const {
        w.writeU8(contentType);
        w.writeU32(contentId);
    }
    static CmdAdminDeleteContentMsg read(ByteReader& r) {
        CmdAdminDeleteContentMsg m;
        m.contentType = r.readU8();
        m.contentId   = r.readU32();
        return m;
    }
};

// ============================================================================
// Client -> Server: Reload server cache
// ============================================================================
struct CmdAdminReloadCacheMsg {
    uint8_t cacheType = 0;  // AdminCacheType

    void write(ByteWriter& w) const {
        w.writeU8(cacheType);
    }
    static CmdAdminReloadCacheMsg read(ByteReader& r) {
        CmdAdminReloadCacheMsg m;
        m.cacheType = r.readU8();
        return m;
    }
};

// ============================================================================
// Client -> Server: Validate content
// ============================================================================
struct CmdAdminValidateMsg {
    uint8_t contentType = 0;
    uint32_t contentId = 0;   // 0 = validate all of this type

    void write(ByteWriter& w) const {
        w.writeU8(contentType);
        w.writeU32(contentId);
    }
    static CmdAdminValidateMsg read(ByteReader& r) {
        CmdAdminValidateMsg m;
        m.contentType = r.readU8();
        m.contentId   = r.readU32();
        return m;
    }
};

// ============================================================================
// Client -> Server: Request content list
// ============================================================================
struct CmdAdminRequestContentListMsg {
    uint8_t contentType = 0;
    uint16_t page = 0;

    void write(ByteWriter& w) const {
        w.writeU8(contentType);
        w.writeU16(page);
    }
    static CmdAdminRequestContentListMsg read(ByteReader& r) {
        CmdAdminRequestContentListMsg m;
        m.contentType = r.readU8();
        m.page        = r.readU16();
        return m;
    }
};

// ============================================================================
// Server -> Client: Admin operation result
// ============================================================================
struct SvAdminResultMsg {
    uint8_t action = 0;      // Original command type (Save/Delete/Reload)
    uint8_t success = 0;     // 1 = success, 0 = failure
    uint32_t contentId = 0;  // Assigned/affected content ID
    std::string message;     // Human-readable result or error

    void write(ByteWriter& w) const {
        w.writeU8(action);
        w.writeU8(success);
        w.writeU32(contentId);
        w.writeString(message);
    }
    static SvAdminResultMsg read(ByteReader& r) {
        SvAdminResultMsg m;
        m.action    = r.readU8();
        m.success   = r.readU8();
        m.contentId = r.readU32();
        m.message   = r.readString();
        return m;
    }
};

// ============================================================================
// Server -> Client: Content list response
// ============================================================================
struct SvAdminContentListMsg {
    uint8_t contentType = 0;
    uint16_t page = 0;
    uint16_t totalPages = 0;
    std::string entriesJson;  // JSON array of content summaries

    void write(ByteWriter& w) const {
        w.writeU8(contentType);
        w.writeU16(page);
        w.writeU16(totalPages);
        w.writeString(entriesJson);
    }
    static SvAdminContentListMsg read(ByteReader& r) {
        SvAdminContentListMsg m;
        m.contentType = r.readU8();
        m.page        = r.readU16();
        m.totalPages  = r.readU16();
        m.entriesJson = r.readString();
        return m;
    }
};

// ============================================================================
// Server -> Client: Validation report
// ============================================================================
struct SvValidationReportMsg {
    struct ValidationIssueNet {
        uint8_t severity = 0;    // 0=info, 1=warning, 2=error
        std::string message;
    };

    uint8_t contentType = 0;
    uint32_t contentId = 0;
    std::vector<ValidationIssueNet> issues;

    void write(ByteWriter& w) const {
        w.writeU8(contentType);
        w.writeU32(contentId);
        uint16_t count = static_cast<uint16_t>(issues.size());
        w.writeU16(count);
        for (const auto& issue : issues) {
            w.writeU8(issue.severity);
            w.writeString(issue.message);
        }
    }
    static SvValidationReportMsg read(ByteReader& r) {
        SvValidationReportMsg m;
        m.contentType = r.readU8();
        m.contentId   = r.readU32();
        uint16_t count = r.readU16();
        m.issues.reserve(count);
        for (uint16_t i = 0; i < count; ++i) {
            ValidationIssueNet issue;
            issue.severity = r.readU8();
            issue.message  = r.readString();
            m.issues.push_back(std::move(issue));
        }
        return m;
    }
};

} // namespace fate
