#include "server/wal/write_ahead_log.h"
#include "engine/core/logger.h"
#include <cstring>
#include <chrono>

#ifdef _WIN32
#include <io.h>   // _commit, _fileno
#else
#include <unistd.h>  // fsync, fileno
#endif

namespace fate {

// ---------------------------------------------------------------------------
// CRC32 (standard polynomial 0xEDB88320, reflected input/output)
// ---------------------------------------------------------------------------
static uint32_t s_crc32Table[256];
static bool     s_crc32TableReady = false;

static void buildCrc32Table() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            if (c & 1)
                c = 0xEDB88320u ^ (c >> 1);
            else
                c >>= 1;
        }
        s_crc32Table[i] = c;
    }
    s_crc32TableReady = true;
}

/*static*/ uint32_t WriteAheadLog::crc32(const uint8_t* data, size_t len) {
    if (!s_crc32TableReady) buildCrc32Table();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        crc = s_crc32Table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// Helpers: little-endian serialisation into a byte buffer
// ---------------------------------------------------------------------------
static void writeU16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

static void writeU32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

static void writeU64(std::vector<uint8_t>& buf, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}

static void writeI64(std::vector<uint8_t>& buf, int64_t v) {
    writeU64(buf, static_cast<uint64_t>(v));
}

static void writeF64(std::vector<uint8_t>& buf, double v) {
    uint64_t tmp;
    memcpy(&tmp, &v, 8);
    writeU64(buf, tmp);
}

// Helpers: little-endian deserialisation from a raw pointer
static uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])        |
           (static_cast<uint32_t>(p[1]) << 8)  |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

static uint64_t readU64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(p[i]) << (i * 8);
    return v;
}

static int64_t readI64(const uint8_t* p) {
    return static_cast<int64_t>(readU64(p));
}

static double readF64(const uint8_t* p) {
    uint64_t tmp = readU64(p);
    double v;
    memcpy(&v, &tmp, 8);
    return v;
}

// ---------------------------------------------------------------------------
// Entry binary format (all little-endian):
//   [seq:8][type:1][charIdLen:2][charId:N][intValue:8][strLen:2][str:M][ts:8][crc32:4]
// Total fixed overhead = 8+1+2+8+2+8+4 = 33 bytes (+ charId + str lengths)
// ---------------------------------------------------------------------------
static constexpr size_t FIXED_OVERHEAD = 8 + 1 + 2 + 8 + 2 + 8 + 4; // 33

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool WriteAheadLog::open(const char* path) {
    path_ = path;
    // "a+b" opens for append + read; file is created if it doesn't exist
    file_ = fopen(path, "a+b");
    if (!file_) return false;

    // Determine the current highest sequence by scanning existing entries
    // (we do a quick scan rather than full readAll to avoid allocating entries)
    fseek(file_, 0, SEEK_END);
    long fileSize = ftell(file_);
    if (fileSize > 0) {
        // Scan forward to find the last valid sequence
        fseek(file_, 0, SEEK_SET);
        while (true) {
            // Read the fixed-size prefix to figure out variable lengths
            uint8_t hdr[8 + 1 + 2]; // seq + type + charIdLen
            if (fread(hdr, 1, sizeof(hdr), file_) != sizeof(hdr)) break;

            uint64_t seq        = readU64(hdr);
            uint16_t charIdLen  = readU16(hdr + 9);

            // Skip charId
            if (fseek(file_, charIdLen, SEEK_CUR) != 0) break;

            // intValue + strLen
            uint8_t mid[8 + 2];
            if (fread(mid, 1, sizeof(mid), file_) != sizeof(mid)) break;

            uint16_t strLen = readU16(mid + 8);

            // Skip str + timestamp + crc32
            if (fseek(file_, strLen + 8 + 4, SEEK_CUR) != 0) break;

            sequence_ = seq; // keep updating; last valid wins
        }
    }
    // Seek to end for future appends
    fseek(file_, 0, SEEK_END);
    return true;
}

void WriteAheadLog::close() {
    if (file_) {
        flush();
        fclose(file_);
        file_ = nullptr;
    }
}

void WriteAheadLog::flush() {
    if (!file_) return;
    fflush(file_);
#ifdef _WIN32
    _commit(_fileno(file_));
#else
    fsync(fileno(file_));
#endif
}

void WriteAheadLog::truncate() {
    if (!file_) {
        // re-open in write mode to truncate
        FILE* f = fopen(path_.c_str(), "wb");
        if (f) fclose(f);
        file_ = fopen(path_.c_str(), "a+b");
        sequence_ = 0;
        return;
    }
    fclose(file_);
    file_ = nullptr;
    // Truncate by reopening in "wb" (write-only, truncate)
    FILE* f = fopen(path_.c_str(), "wb");
    if (f) fclose(f);
    // Re-open in append+read mode
    file_ = fopen(path_.c_str(), "a+b");
    sequence_ = 0;
}

void WriteAheadLog::appendEntry(WalEntryType type,
                                const std::string& charId,
                                int64_t intVal,
                                const std::string& strVal) {
    if (!file_) return;

    if (file_) {
        long pos = ftell(file_);
        if (pos > 0 && static_cast<size_t>(pos) >= MAX_WAL_SIZE) {
            LOG_WARN("WAL", "WAL exceeded %zu bytes, force-truncating (possible auto-save failure)", MAX_WAL_SIZE);
            truncate();
        }
    }

    ++sequence_;

    double ts = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()) / 1e6;

    // Build payload (everything before the CRC)
    std::vector<uint8_t> buf;
    buf.reserve(FIXED_OVERHEAD + charId.size() + strVal.size());

    writeU64(buf, sequence_);
    buf.push_back(static_cast<uint8_t>(type));
    writeU16(buf, static_cast<uint16_t>(charId.size()));
    for (char c : charId) buf.push_back(static_cast<uint8_t>(c));
    writeI64(buf, intVal);
    writeU16(buf, static_cast<uint16_t>(strVal.size()));
    for (char c : strVal) buf.push_back(static_cast<uint8_t>(c));
    writeF64(buf, ts);

    uint32_t checksum = crc32(buf.data(), buf.size());
    writeU32(buf, checksum);

    fwrite(buf.data(), 1, buf.size(), file_);
}

// Convenience wrappers
void WriteAheadLog::appendGoldChange(const std::string& charId, int64_t delta) {
    appendEntry(WalEntryType::GoldChange, charId, delta, "");
}

void WriteAheadLog::appendItemAdd(const std::string& charId, int slot,
                                  const std::string& itemJson) {
    appendEntry(WalEntryType::ItemAdd, charId, static_cast<int64_t>(slot), itemJson);
}

void WriteAheadLog::appendItemRemove(const std::string& charId, int slot) {
    appendEntry(WalEntryType::ItemRemove, charId, static_cast<int64_t>(slot), "");
}

void WriteAheadLog::appendXPGain(const std::string& charId, int64_t xp) {
    appendEntry(WalEntryType::XPGain, charId, xp, "");
}

std::vector<WalEntry> WriteAheadLog::readAll() {
    std::vector<WalEntry> result;
    if (!file_) return result;

    fseek(file_, 0, SEEK_SET);

    while (true) {
        // --- read fixed prefix: seq(8) + type(1) + charIdLen(2) ---
        uint8_t hdr[11];
        if (fread(hdr, 1, sizeof(hdr), file_) != sizeof(hdr)) break;

        uint64_t seq       = readU64(hdr);
        auto     type      = static_cast<WalEntryType>(hdr[8]);
        uint16_t charIdLen = readU16(hdr + 9);

        // --- charId ---
        std::string charId(charIdLen, '\0');
        if (charIdLen > 0 && fread(charId.data(), 1, charIdLen, file_) != charIdLen) break;

        // --- intValue(8) + strLen(2) ---
        uint8_t mid[10];
        if (fread(mid, 1, sizeof(mid), file_) != sizeof(mid)) break;
        int64_t  intVal = readI64(mid);
        uint16_t strLen = readU16(mid + 8);

        // --- strValue ---
        std::string strVal(strLen, '\0');
        if (strLen > 0 && fread(strVal.data(), 1, strLen, file_) != strLen) break;

        // --- timestamp(8) ---
        uint8_t tsBuf[8];
        if (fread(tsBuf, 1, 8, file_) != 8) break;
        double ts = readF64(tsBuf);

        // --- crc32(4) ---
        uint8_t crcBuf[4];
        if (fread(crcBuf, 1, 4, file_) != 4) break;
        uint32_t storedCrc = readU32(crcBuf);

        // Recompute CRC over everything except the stored CRC itself
        std::vector<uint8_t> payload;
        payload.reserve(11 + charIdLen + 10 + strLen + 8);
        writeU64(payload, seq);
        payload.push_back(static_cast<uint8_t>(type));
        writeU16(payload, charIdLen);
        for (char c : charId) payload.push_back(static_cast<uint8_t>(c));
        writeI64(payload, intVal);
        writeU16(payload, strLen);
        for (char c : strVal) payload.push_back(static_cast<uint8_t>(c));
        writeF64(payload, ts);

        uint32_t computed = crc32(payload.data(), payload.size());
        if (computed != storedCrc) {
            // Corrupted entry — skip the rest of the file conservatively
            break;
        }

        WalEntry entry;
        entry.sequence    = seq;
        entry.type        = type;
        entry.characterId = std::move(charId);
        entry.intValue    = intVal;
        entry.strValue    = std::move(strVal);
        entry.timestamp   = ts;
        result.push_back(std::move(entry));
    }

    // Return to end of file for any future appends
    fseek(file_, 0, SEEK_END);
    return result;
}

} // namespace fate
