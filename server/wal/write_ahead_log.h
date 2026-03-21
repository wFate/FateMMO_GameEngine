#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>

namespace fate {

enum class WalEntryType : uint8_t {
    GoldChange = 1,
    ItemAdd = 2,
    ItemRemove = 3,
    XPGain = 4,
    LevelUp = 5,
};

struct WalEntry {
    uint64_t sequence;
    WalEntryType type;
    std::string characterId;
    int64_t intValue;       // gold delta, XP amount, slot index
    std::string strValue;   // itemId, serialized item JSON
    double timestamp;
};

class WriteAheadLog {
public:
    static constexpr size_t MAX_WAL_SIZE = 16 * 1024 * 1024; // 16 MB safety cap

    ~WriteAheadLog() { close(); }

    bool open(const char* path);
    void close();

    void appendGoldChange(const std::string& charId, int64_t delta);
    void appendItemAdd(const std::string& charId, int slot, const std::string& itemJson);
    void appendItemRemove(const std::string& charId, int slot);
    void appendXPGain(const std::string& charId, int64_t xp);

    void flush();     // fsync to disk
    void truncate();  // clear after successful DB checkpoint

    std::vector<WalEntry> readAll();   // for recovery
    uint64_t lastSequence() const { return sequence_; }

private:
    void appendEntry(WalEntryType type, const std::string& charId,
                     int64_t intVal, const std::string& strVal);
    static uint32_t crc32(const uint8_t* data, size_t len);

    FILE* file_ = nullptr;
    std::string path_;
    uint64_t sequence_ = 0;
};

} // namespace fate
