#include <doctest/doctest.h>
#include "game/animation_loader.h"
#include <nlohmann/json.hpp>

TEST_CASE("AnimationLoader parses packed sheet metadata") {
    nlohmann::json meta = {
        {"version", 1},
        {"frameWidth", 32}, {"frameHeight", 32},
        {"columns", 6}, {"totalFrames", 6},
        {"states", {
            {"idle_down", {{"startFrame", 0}, {"frameCount", 3}, {"frameRate", 8.0}, {"loop", true}, {"hitFrame", -1}}},
            {"idle_up", {{"startFrame", 3}, {"frameCount", 3}, {"frameRate", 8.0}, {"loop", true}, {"hitFrame", -1}}}
        }}
    };
    fate::PackedSheetMeta result;
    bool ok = fate::AnimationLoader::parsePackedMeta(meta, result);
    CHECK(ok);
    CHECK(result.frameWidth == 32);
    CHECK(result.frameHeight == 32);
    CHECK(result.columns == 6);
    CHECK(result.totalFrames == 6);
    CHECK(result.states.size() == 2);
    CHECK(result.states["idle_down"].startFrame == 0);
    CHECK(result.states["idle_down"].frameCount == 3);
    CHECK(result.states["idle_down"].frameRate == doctest::Approx(8.0f));
    CHECK(result.states["idle_down"].loop == true);
    CHECK(result.states["idle_down"].hitFrame == -1);
    CHECK(result.states["idle_down"].flipX == false);
}

TEST_CASE("AnimationLoader parses flipX entries") {
    nlohmann::json meta = {
        {"version", 1},
        {"frameWidth", 32}, {"frameHeight", 32},
        {"columns", 3}, {"totalFrames", 3},
        {"states", {
            {"idle_left", {{"startFrame", 0}, {"frameCount", 3}, {"frameRate", 8.0}, {"loop", true}, {"hitFrame", -1}}},
            {"idle_right", {{"startFrame", 0}, {"frameCount", 3}, {"frameRate", 8.0}, {"loop", true}, {"hitFrame", -1}, {"flipX", true}}}
        }}
    };
    fate::PackedSheetMeta result;
    bool ok = fate::AnimationLoader::parsePackedMeta(meta, result);
    CHECK(ok);
    CHECK(result.states["idle_left"].flipX == false);
    CHECK(result.states["idle_right"].flipX == true);
    CHECK(result.states["idle_left"].startFrame == result.states["idle_right"].startFrame);
}

TEST_CASE("AnimationLoader rejects invalid version") {
    nlohmann::json meta = {{"version", 999}, {"frameWidth", 32}, {"frameHeight", 32},
        {"columns", 1}, {"totalFrames", 1}, {"states", nlohmann::json::object()}};
    fate::PackedSheetMeta result;
    CHECK_FALSE(fate::AnimationLoader::parsePackedMeta(meta, result));
}

TEST_CASE("AnimationLoader round-trips packed metadata with 4-direction model") {
    nlohmann::json meta = {
        {"version", 1},
        {"frameWidth", 32}, {"frameHeight", 32},
        {"columns", 9}, {"totalFrames", 9},
        {"states", {
            {"idle_down",  {{"startFrame", 0}, {"frameCount", 3}, {"frameRate", 8.0}, {"loop", true}, {"hitFrame", -1}}},
            {"idle_up",    {{"startFrame", 3}, {"frameCount", 3}, {"frameRate", 8.0}, {"loop", true}, {"hitFrame", -1}}},
            {"idle_left",  {{"startFrame", 6}, {"frameCount", 3}, {"frameRate", 8.0}, {"loop", true}, {"hitFrame", -1}}},
            {"idle_right", {{"startFrame", 6}, {"frameCount", 3}, {"frameRate", 8.0}, {"loop", true}, {"hitFrame", -1}, {"flipX", true}}}
        }}
    };
    fate::PackedSheetMeta result;
    CHECK(fate::AnimationLoader::parsePackedMeta(meta, result));
    CHECK(result.states.size() == 4);
    CHECK(result.states["idle_left"].startFrame == result.states["idle_right"].startFrame);
    CHECK(result.states["idle_left"].flipX == false);
    CHECK(result.states["idle_right"].flipX == true);
    auto flipMap = fate::AnimationLoader::getFlipXMap(result);
    CHECK(flipMap["idle_right"] == true);
    CHECK(flipMap["idle_down"] == false);
}
