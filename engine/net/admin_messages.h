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
    uint8_t isNew = 1;         // 1 = INSERT, 0 = UPDATE
    std::string jsonPayload;   // Full definition as JSON (includes string ID)

    void write(ByteWriter& w) const {
        w.writeU8(contentType);
        w.writeU8(isNew);
        w.writeString(jsonPayload);
    }
    static CmdAdminSaveContentMsg read(ByteReader& r) {
        CmdAdminSaveContentMsg m;
        m.contentType  = r.readU8();
        m.isNew        = r.readU8();
        m.jsonPayload  = r.readString();
        return m;
    }
};

// ============================================================================
// Client -> Server: Delete content definition
// ============================================================================
struct CmdAdminDeleteContentMsg {
    uint8_t contentType = 0;
    std::string contentId;     // String PK (mob_def_id, item_id, or int as string for zone_id/drop_id)

    void write(ByteWriter& w) const {
        w.writeU8(contentType);
        w.writeString(contentId);
    }
    static CmdAdminDeleteContentMsg read(ByteReader& r) {
        CmdAdminDeleteContentMsg m;
        m.contentType = r.readU8();
        m.contentId   = r.readString();
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
    // No payload — validates all content types
    void write(ByteWriter&) const {}
    static CmdAdminValidateMsg read(ByteReader&) { return {}; }
};

// ============================================================================
// Client -> Server: Request content list
// ============================================================================
struct CmdAdminRequestContentListMsg {
    uint8_t contentType = 0;

    void write(ByteWriter& w) const {
        w.writeU8(contentType);
    }
    static CmdAdminRequestContentListMsg read(ByteReader& r) {
        CmdAdminRequestContentListMsg m;
        m.contentType = r.readU8();
        return m;
    }
};

// ============================================================================
// Server -> Client: Admin operation result
// ============================================================================
struct SvAdminResultMsg {
    uint8_t requestType = 0;  // PacketType of the command that triggered this
    uint8_t success = 0;      // 1 = success, 0 = failure
    std::string message;      // Human-readable result or error

    void write(ByteWriter& w) const {
        w.writeU8(requestType);
        w.writeU8(success);
        w.writeString(message);
    }
    static SvAdminResultMsg read(ByteReader& r) {
        SvAdminResultMsg m;
        m.requestType = r.readU8();
        m.success     = r.readU8();
        m.message     = r.readString();
        return m;
    }
};

// ============================================================================
// Server -> Client: Content list response
// ============================================================================
struct SvAdminContentListMsg {
    uint8_t contentType = 0;
    uint16_t pageIndex = 0;    // 0-based page number
    uint16_t totalPages = 0;   // total page count (0 = single page)
    std::string jsonPayload;   // JSON array of entries for this page

    void write(ByteWriter& w) const {
        w.writeU8(contentType);
        w.writeU16(pageIndex);
        w.writeU16(totalPages);
        w.writeString(jsonPayload);
    }
    static SvAdminContentListMsg read(ByteReader& r) {
        SvAdminContentListMsg m;
        m.contentType = r.readU8();
        m.pageIndex   = r.readU16();
        m.totalPages  = r.readU16();
        m.jsonPayload = r.readString();
        return m;
    }
};

// ============================================================================
// Server -> Client: Validation report
// ============================================================================
struct SvValidationReportMsg {
    struct ValidationIssueNet {
        uint8_t severity = 0;    // 0=Error, 1=Warning, 2=Info
        std::string message;
    };

    std::vector<ValidationIssueNet> issues;

    void write(ByteWriter& w) const {
        uint16_t count = static_cast<uint16_t>(issues.size());
        w.writeU16(count);
        for (const auto& issue : issues) {
            w.writeU8(issue.severity);
            w.writeString(issue.message);
        }
    }
    static SvValidationReportMsg read(ByteReader& r) {
        SvValidationReportMsg m;
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
