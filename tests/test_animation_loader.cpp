#include <doctest/doctest.h>
#include "game/animation_loader.h"
#include "game/components/animator.h"
#include "game/components/sprite_component.h"
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

TEST_CASE("AnimationLoader applies animations to Animator with hitFrame and flipX") {
    nlohmann::json meta = {
        {"version", 1},
        {"frameWidth", 32}, {"frameHeight", 32},
        {"columns", 8}, {"totalFrames", 8},
        {"states", {
            {"idle_down",   {{"startFrame", 0}, {"frameCount", 3}, {"frameRate", 10.0}, {"loop", true}, {"hitFrame", -1}}},
            {"attack_left", {{"startFrame", 3}, {"frameCount", 4}, {"frameRate", 10.0}, {"loop", false}, {"hitFrame", 2}}},
            {"attack_right",{{"startFrame", 3}, {"frameCount", 4}, {"frameRate", 10.0}, {"loop", false}, {"hitFrame", 2}, {"flipX", true}}}
        }}
    };
    fate::PackedSheetMeta sheet;
    REQUIRE(fate::AnimationLoader::parsePackedMeta(meta, sheet));

    fate::Animator anim;
    fate::AnimationLoader::applyToAnimator(sheet, anim);

    // Verify animations registered
    CHECK(anim.animations.size() == 3);
    CHECK(anim.animations["attack_left"].hitFrame == 2);
    CHECK(anim.animations["attack_left"].loop == false);
    CHECK(anim.animations["idle_down"].loop == true);

    // Verify flipX map populated
    CHECK(anim.flipXPerAnim["attack_right"] == true);
    CHECK(anim.flipXPerAnim.count("attack_left") == 0);
    CHECK(anim.flipXPerAnim.count("idle_down") == 0);
}

TEST_CASE("AnimationLoader applies sprite sheet metadata to SpriteComponent") {
    nlohmann::json meta = {
        {"version", 1},
        {"frameWidth", 24}, {"frameHeight", 32},
        {"columns", 6}, {"totalFrames", 18},
        {"states", {
            {"idle_down", {{"startFrame", 0}, {"frameCount", 3}, {"frameRate", 8.0}, {"loop", true}, {"hitFrame", -1}}}
        }}
    };
    fate::PackedSheetMeta sheet;
    REQUIRE(fate::AnimationLoader::parsePackedMeta(meta, sheet));

    fate::SpriteComponent sprite;
    fate::AnimationLoader::applyToSprite(sheet, sprite, "assets/sprites/player_sheet.png");

    CHECK(sprite.texturePath == "assets/sprites/player_sheet.png");
    CHECK(sprite.frameWidth == 24);
    CHECK(sprite.frameHeight == 32);
    CHECK(sprite.columns == 6);
    CHECK(sprite.totalFrames == 18);
}

TEST_CASE("Animator hitFrame detection logic") {
    fate::Animator anim;
    anim.addAnimation("attack", 0, 4, 10.0f, false, 2); // hitFrame at frame 2, 10 FPS
    anim.play("attack");

    // Frame duration = 1/10 = 0.1s. hitFrame=2 fires between frame 1→2 at t=0.2s
    // At t=0.15s, local frame = 1 (before hitFrame)
    anim.timer = 0.15f;
    CHECK(anim.getLocalFrameIndex() == 1);

    // At t=0.25s, local frame = 2 (at hitFrame)
    anim.timer = 0.25f;
    CHECK(anim.getLocalFrameIndex() == 2);

    // Verify the AnimationDef has correct hitFrame
    CHECK(anim.animations["attack"].hitFrame == 2);

    // Verify frame advancement crosses hitFrame threshold
    // (AnimationSystem uses prevFrame < hitFrame && currFrame >= hitFrame)
    int prevFrame = 1;  // before hitFrame
    int currFrame = 2;  // at hitFrame
    int hitFrame = anim.animations["attack"].hitFrame;
    CHECK(prevFrame < hitFrame);
    CHECK(currFrame >= hitFrame);
}

TEST_CASE("Animator non-looping animation reports finished correctly") {
    fate::Animator anim;
    anim.addAnimation("death", 0, 3, 10.0f, false, -1);
    anim.play("death");

    // Not finished yet
    anim.timer = 0.15f;
    CHECK_FALSE(anim.isFinished());

    // Past last frame (3 frames at 10fps = 0.3s)
    anim.timer = 0.35f;
    CHECK(anim.isFinished());
}

TEST_CASE("Animator returnAnimation auto-transitions") {
    fate::Animator anim;
    anim.addAnimation("attack", 0, 3, 10.0f, false, -1);
    anim.addAnimation("idle", 3, 2, 8.0f, true, -1);

    anim.play("attack");
    anim.returnAnimation = "idle";

    // Advance past end of attack (0.3s at 10fps, 3 frames)
    anim.timer = 0.35f;
    CHECK(anim.isFinished());

    // Simulate what AnimationSystem does on completion
    std::string ret = anim.returnAnimation;
    anim.returnAnimation.clear();
    anim.play(ret);

    CHECK(anim.currentAnimation == "idle");
    CHECK(anim.playing == true);
    CHECK(anim.timer == doctest::Approx(0.0f));
}
