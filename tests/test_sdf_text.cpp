#include <doctest/doctest.h>
#include "engine/render/sdf_text.h"

TEST_CASE("UTF-8 decode ASCII") {
    std::string text = "Hello";
    size_t idx = 0;
    CHECK(fate::SDFText::decodeUTF8(text, idx) == 'H');
    CHECK(fate::SDFText::decodeUTF8(text, idx) == 'e');
    CHECK(fate::SDFText::decodeUTF8(text, idx) == 'l');
    CHECK(fate::SDFText::decodeUTF8(text, idx) == 'l');
    CHECK(fate::SDFText::decodeUTF8(text, idx) == 'o');
    CHECK(idx == 5);
}

TEST_CASE("UTF-8 decode multibyte") {
    // U+00E9 = e-acute (2-byte: 0xC3 0xA9)
    std::string text = "\xC3\xA9";
    size_t idx = 0;
    CHECK(fate::SDFText::decodeUTF8(text, idx) == 0x00E9);
    CHECK(idx == 2);
}

TEST_CASE("UTF-8 decode 3-byte") {
    // U+20AC = Euro sign (3-byte: 0xE2 0x82 0xAC)
    std::string text = "\xE2\x82\xAC";
    size_t idx = 0;
    CHECK(fate::SDFText::decodeUTF8(text, idx) == 0x20AC);
    CHECK(idx == 3);
}

TEST_CASE("UTF-8 decode 4-byte") {
    // U+1F600 = grinning face emoji (4-byte: 0xF0 0x9F 0x98 0x80)
    std::string text = "\xF0\x9F\x98\x80";
    size_t idx = 0;
    CHECK(fate::SDFText::decodeUTF8(text, idx) == 0x1F600);
    CHECK(idx == 4);
}

TEST_CASE("TextStyle enum values match renderType") {
    CHECK(static_cast<float>(fate::TextStyle::Normal)   == 1.0f);
    CHECK(static_cast<float>(fate::TextStyle::Outlined) == 2.0f);
    CHECK(static_cast<float>(fate::TextStyle::Glow)     == 3.0f);
    CHECK(static_cast<float>(fate::TextStyle::Shadow)   == 4.0f);
}
