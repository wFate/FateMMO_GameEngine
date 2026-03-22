#include <doctest/doctest.h>
#include "engine/render/gfx/types.h"
#include <cstring>
#include <vector>
#include <cstdint>

using namespace gfx;

TEST_SUITE("KTX & Compressed Textures") {

TEST_CASE("isCompressedFormat identifies compressed vs uncompressed") {
    CHECK_FALSE(isCompressedFormat(TextureFormat::RGBA8));
    CHECK_FALSE(isCompressedFormat(TextureFormat::RGB8));
    CHECK_FALSE(isCompressedFormat(TextureFormat::R8));
    CHECK_FALSE(isCompressedFormat(TextureFormat::Depth24Stencil8));

    CHECK(isCompressedFormat(TextureFormat::ETC2_RGBA8));
    CHECK(isCompressedFormat(TextureFormat::ASTC_4x4_RGBA));
    CHECK(isCompressedFormat(TextureFormat::ASTC_8x8_RGBA));
}

TEST_CASE("estimateTextureBytes for uncompressed RGBA8") {
    CHECK(estimateTextureBytes(256, 256, TextureFormat::RGBA8) == 256 * 256 * 4);
    CHECK(estimateTextureBytes(1024, 1024, TextureFormat::RGBA8) == 1024 * 1024 * 4);
    CHECK(estimateTextureBytes(1, 1, TextureFormat::RGBA8) == 4);
}

TEST_CASE("estimateTextureBytes for ETC2 (4x4 blocks, 8 bpp)") {
    // 256x256: 64x64 blocks * 16 bytes = 65536 (vs 262144 uncompressed = 4:1)
    CHECK(estimateTextureBytes(256, 256, TextureFormat::ETC2_RGBA8) == 64 * 64 * 16);
    // 1024x1024: 256x256 blocks * 16 bytes = 1048576 (vs 4194304 = 4:1)
    CHECK(estimateTextureBytes(1024, 1024, TextureFormat::ETC2_RGBA8) == 256 * 256 * 16);
}

TEST_CASE("estimateTextureBytes for ASTC 4x4 (same as ETC2 block size)") {
    CHECK(estimateTextureBytes(256, 256, TextureFormat::ASTC_4x4_RGBA) == 64 * 64 * 16);
    // Non-multiple-of-4 dimensions round up
    CHECK(estimateTextureBytes(33, 33, TextureFormat::ASTC_4x4_RGBA) == 9 * 9 * 16); // ceil(33/4)=9
}

TEST_CASE("estimateTextureBytes for ASTC 8x8 (best compression, 2 bpp)") {
    // 256x256: 32x32 blocks * 16 bytes = 16384 (vs 262144 = 16:1)
    CHECK(estimateTextureBytes(256, 256, TextureFormat::ASTC_8x8_RGBA) == 32 * 32 * 16);
    // 1024x1024: 128x128 blocks * 16 bytes = 262144 (vs 4194304 = 16:1)
    CHECK(estimateTextureBytes(1024, 1024, TextureFormat::ASTC_8x8_RGBA) == 128 * 128 * 16);
    // Non-multiple-of-8 rounds up
    CHECK(estimateTextureBytes(100, 100, TextureFormat::ASTC_8x8_RGBA) == 13 * 13 * 16);
}

TEST_CASE("VRAM savings: compressed vs uncompressed") {
    // A typical mobile game texture budget comparison
    int w = 512, h = 512;
    size_t rgba = estimateTextureBytes(w, h, TextureFormat::RGBA8);
    size_t etc2 = estimateTextureBytes(w, h, TextureFormat::ETC2_RGBA8);
    size_t astc4 = estimateTextureBytes(w, h, TextureFormat::ASTC_4x4_RGBA);
    size_t astc8 = estimateTextureBytes(w, h, TextureFormat::ASTC_8x8_RGBA);

    CHECK(rgba == 512 * 512 * 4);     // 1,048,576 bytes
    CHECK(etc2 == rgba / 4);           // 4:1 compression
    CHECK(astc4 == rgba / 4);          // 4:1 compression
    CHECK(astc8 == rgba / 16);         // 16:1 compression
}

TEST_CASE("KTX1 header identifier constant") {
    // Verify the KTX1 magic bytes (from Khronos spec)
    static constexpr uint8_t KTX1_ID[12] = {
        0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
    };
    // This spells out: «KTX 11»\r\n\x1a\n
    CHECK(KTX1_ID[0] == 0xAB);  // «
    CHECK(KTX1_ID[1] == 'K');
    CHECK(KTX1_ID[2] == 'T');
    CHECK(KTX1_ID[3] == 'X');
    CHECK(KTX1_ID[4] == ' ');
    CHECK(KTX1_ID[5] == '1');
    CHECK(KTX1_ID[6] == '1');
    CHECK(KTX1_ID[7] == 0xBB);  // »
    CHECK(KTX1_ID[8] == '\r');
    CHECK(KTX1_ID[9] == '\n');
    CHECK(KTX1_ID[10] == 0x1A);
    CHECK(KTX1_ID[11] == '\n');
}

} // TEST_SUITE
