#include <doctest/doctest.h>
#include "server/wal/write_ahead_log.h"
#include <filesystem>

using namespace fate;

TEST_CASE("WAL: write and read back entries") {
    const char* path = "test_wal.bin";
    std::filesystem::remove(path);
    {
        WriteAheadLog wal;
        CHECK(wal.open(path) == true);
        wal.appendGoldChange("char_001", 500);
        wal.appendGoldChange("char_001", -100);
        wal.flush();
    }
    {
        WriteAheadLog wal;
        wal.open(path);
        auto entries = wal.readAll();
        CHECK(entries.size() == 2);
        CHECK(entries[0].characterId == "char_001");
        CHECK(entries[0].type == WalEntryType::GoldChange);
        CHECK(entries[0].intValue == 500);
        CHECK(entries[1].intValue == -100);
    }
    std::filesystem::remove(path);
}

TEST_CASE("WAL: detects corrupted entry via CRC") {
    const char* path = "test_wal_corrupt.bin";
    std::filesystem::remove(path);
    {
        WriteAheadLog wal;
        wal.open(path);
        wal.appendGoldChange("char_001", 999);
        wal.flush();
        wal.close();
    }
    // Corrupt a byte in the payload area
    {
        auto f = fopen(path, "r+b");
        REQUIRE(f != nullptr);
        fseek(f, 12, SEEK_SET);  // inside charId area
        uint8_t bad = 0xFF;
        fwrite(&bad, 1, 1, f);
        fclose(f);
    }
    {
        WriteAheadLog wal;
        wal.open(path);
        auto entries = wal.readAll();
        CHECK(entries.size() == 0);
    }
    std::filesystem::remove(path);
}

TEST_CASE("WAL: truncate clears all entries") {
    const char* path = "test_wal_trunc.bin";
    std::filesystem::remove(path);
    WriteAheadLog wal;
    wal.open(path);
    wal.appendGoldChange("char_001", 100);
    wal.flush();
    wal.truncate();
    auto entries = wal.readAll();
    CHECK(entries.size() == 0);
    wal.close();
    std::filesystem::remove(path);
}

TEST_CASE("WAL: sequence numbers increment") {
    const char* path = "test_wal_seq.bin";
    std::filesystem::remove(path);
    WriteAheadLog wal;
    wal.open(path);
    wal.appendGoldChange("a", 1);
    wal.appendXPGain("b", 100);
    wal.appendItemRemove("c", 5);
    wal.flush();
    auto entries = wal.readAll();
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].sequence == 1);
    CHECK(entries[1].sequence == 2);
    CHECK(entries[2].sequence == 3);
    wal.close();
    std::filesystem::remove(path);
}
