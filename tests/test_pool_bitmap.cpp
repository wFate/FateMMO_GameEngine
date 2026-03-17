#include <doctest/doctest.h>
#include "engine/memory/pool.h"
#include "engine/memory/arena.h"

#if defined(ENGINE_MEMORY_DEBUG)

TEST_CASE("Pool occupancy bitmap tracks alloc/free") {
    fate::Arena arena(1024 * 1024);
    fate::PoolAllocator pool;
    pool.init(arena, 64, 16); // 16 blocks of 64 bytes

    const uint8_t* bitmap = pool.occupancyBitmap();
    REQUIRE(bitmap != nullptr);

    // Initially all free
    CHECK(bitmap[0] == 0x00);
    CHECK(bitmap[1] == 0x00);

    // Alloc block 0
    void* b0 = pool.alloc();
    CHECK((bitmap[0] & 0x01) != 0);

    // Alloc block 1
    void* b1 = pool.alloc();
    CHECK((bitmap[0] & 0x02) != 0);

    // Free block 0
    pool.free(b0);
    CHECK((bitmap[0] & 0x01) == 0);
    CHECK((bitmap[0] & 0x02) != 0); // block 1 still set

    pool.free(b1);
    CHECK(bitmap[0] == 0x00);
}

#endif // ENGINE_MEMORY_DEBUG
