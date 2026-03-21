#include <doctest/doctest.h>
#include "engine/audio/audio_manager.h"

using namespace fate;

// =============================================================================
// Lifecycle tests
// =============================================================================

TEST_CASE("AudioManager init and shutdown") {
    AudioManager mgr;
    CHECK_FALSE(mgr.isInitialized());
    CHECK(mgr.init());
    CHECK(mgr.isInitialized());
    mgr.shutdown();
    CHECK_FALSE(mgr.isInitialized());
}

TEST_CASE("AudioManager double init is safe") {
    AudioManager mgr;
    CHECK(mgr.init());
    CHECK(mgr.init()); // second call warns but returns true
    CHECK(mgr.isInitialized());
    mgr.shutdown();
}

TEST_CASE("AudioManager shutdown without init is safe") {
    AudioManager mgr;
    mgr.shutdown(); // should not crash
    CHECK_FALSE(mgr.isInitialized());
}

TEST_CASE("AudioManager destructor calls shutdown") {
    {
        AudioManager mgr;
        CHECK(mgr.init());
    } // destructor runs; should not crash
}

// =============================================================================
// Volume clamping tests
// =============================================================================

TEST_CASE("AudioManager master volume clamped 0-1") {
    AudioManager mgr;
    CHECK(mgr.init());

    mgr.setMasterVolume(0.5f);
    CHECK(mgr.getMasterVolume() == doctest::Approx(0.5f));

    mgr.setMasterVolume(-1.0f);
    CHECK(mgr.getMasterVolume() == doctest::Approx(0.0f));

    mgr.setMasterVolume(2.0f);
    CHECK(mgr.getMasterVolume() == doctest::Approx(1.0f));

    mgr.shutdown();
}

TEST_CASE("AudioManager SFX volume clamped 0-1") {
    AudioManager mgr;
    CHECK(mgr.init());

    mgr.setSFXVolume(0.3f);
    CHECK(mgr.getSFXVolume() == doctest::Approx(0.3f));

    mgr.setSFXVolume(-5.0f);
    CHECK(mgr.getSFXVolume() == doctest::Approx(0.0f));

    mgr.setSFXVolume(100.0f);
    CHECK(mgr.getSFXVolume() == doctest::Approx(1.0f));

    mgr.shutdown();
}

TEST_CASE("AudioManager music volume clamped 0-1") {
    AudioManager mgr;
    CHECK(mgr.init());

    mgr.setMusicVolume(0.8f);
    CHECK(mgr.getMusicVolume() == doctest::Approx(0.8f));

    mgr.setMusicVolume(-0.1f);
    CHECK(mgr.getMusicVolume() == doctest::Approx(0.0f));

    mgr.setMusicVolume(1.5f);
    CHECK(mgr.getMusicVolume() == doctest::Approx(1.0f));

    mgr.shutdown();
}

TEST_CASE("AudioManager volume setters work before init") {
    AudioManager mgr;
    // Setting volumes before init should not crash
    mgr.setMasterVolume(0.5f);
    mgr.setSFXVolume(0.3f);
    mgr.setMusicVolume(0.8f);
    CHECK(mgr.getMasterVolume() == doctest::Approx(0.5f));
    CHECK(mgr.getSFXVolume() == doctest::Approx(0.3f));
    CHECK(mgr.getMusicVolume() == doctest::Approx(0.8f));
}

// =============================================================================
// SFX safety tests
// =============================================================================

TEST_CASE("AudioManager playSFX with no loaded sounds does not crash") {
    AudioManager mgr;
    CHECK(mgr.init());
    mgr.playSFX("nonexistent"); // should warn once, not crash
    mgr.playSFX("nonexistent"); // second call should not warn again
    mgr.shutdown();
}

TEST_CASE("AudioManager playSFX before init does not crash") {
    AudioManager mgr;
    mgr.playSFX("anything"); // not initialized, should silently return
}

TEST_CASE("AudioManager playSFXSpatial before init does not crash") {
    AudioManager mgr;
    mgr.playSFXSpatial("anything", 100.0f, 100.0f, 0.0f, 0.0f);
}

TEST_CASE("AudioManager loadSFX with invalid path returns false") {
    AudioManager mgr;
    CHECK(mgr.init());
    CHECK_FALSE(mgr.loadSFX("bad", "/nonexistent/path/to/audio.wav"));
    mgr.shutdown();
}

TEST_CASE("AudioManager loadSFX before init returns false") {
    AudioManager mgr;
    CHECK_FALSE(mgr.loadSFX("test", "test.wav"));
}

TEST_CASE("AudioManager loadSFXDirectory with nonexistent dir returns 0") {
    AudioManager mgr;
    CHECK(mgr.init());
    CHECK(mgr.loadSFXDirectory("/this/path/does/not/exist") == 0);
    mgr.shutdown();
}

TEST_CASE("AudioManager loadSFXDirectory before init returns 0") {
    AudioManager mgr;
    CHECK(mgr.loadSFXDirectory("/any/path") == 0);
}

TEST_CASE("AudioManager unloadSFX is safe for missing key") {
    AudioManager mgr;
    CHECK(mgr.init());
    mgr.unloadSFX("never_loaded"); // should not crash
    mgr.shutdown();
}

TEST_CASE("AudioManager unloadAllSFX is safe when empty") {
    AudioManager mgr;
    CHECK(mgr.init());
    mgr.unloadAllSFX(); // should not crash
    mgr.shutdown();
}

TEST_CASE("AudioManager unloadSFX without init is safe") {
    AudioManager mgr;
    mgr.unloadSFX("anything"); // should not crash
    mgr.unloadAllSFX();
}

// =============================================================================
// Music safety tests
// =============================================================================

TEST_CASE("AudioManager playMusic with missing file does not crash") {
    AudioManager mgr;
    CHECK(mgr.init());
    mgr.playMusic("/nonexistent/music.ogg"); // should warn, not crash
    CHECK_FALSE(mgr.isMusicPlaying());
    mgr.shutdown();
}

TEST_CASE("AudioManager playMusic before init does not crash") {
    AudioManager mgr;
    mgr.playMusic("/any/music.ogg");
}

TEST_CASE("AudioManager stopMusic without playing is safe") {
    AudioManager mgr;
    CHECK(mgr.init());
    mgr.stopMusic(); // nothing playing, should be safe
    mgr.shutdown();
}

TEST_CASE("AudioManager stopMusic before init is safe") {
    AudioManager mgr;
    mgr.stopMusic(); // not initialized, should silently return
}

TEST_CASE("AudioManager isMusicPlaying returns false when not initialized") {
    AudioManager mgr;
    CHECK_FALSE(mgr.isMusicPlaying());
}

TEST_CASE("AudioManager isMusicPlaying returns false after shutdown") {
    AudioManager mgr;
    CHECK(mgr.init());
    mgr.shutdown();
    CHECK_FALSE(mgr.isMusicPlaying());
}

// =============================================================================
// Spatial math tests (pure, no audio needed)
// =============================================================================

TEST_CASE("AudioManager calculateSpatialVolume") {
    // At listener position (distance = 0) -> full volume
    CHECK(AudioManager::calculateSpatialVolume(0.0f, 500.0f) == doctest::Approx(1.0f));

    // At max distance -> silent
    CHECK(AudioManager::calculateSpatialVolume(500.0f, 500.0f) == doctest::Approx(0.0f));

    // Beyond max distance -> clamped to 0
    CHECK(AudioManager::calculateSpatialVolume(1000.0f, 500.0f) == doctest::Approx(0.0f));

    // Halfway -> 0.5
    CHECK(AudioManager::calculateSpatialVolume(250.0f, 500.0f) == doctest::Approx(0.5f));

    // maxDistance <= 0 -> 0
    CHECK(AudioManager::calculateSpatialVolume(100.0f, 0.0f) == doctest::Approx(0.0f));
    CHECK(AudioManager::calculateSpatialVolume(100.0f, -10.0f) == doctest::Approx(0.0f));
}

TEST_CASE("AudioManager calculateSpatialPan") {
    // Same position -> center
    CHECK(AudioManager::calculateSpatialPan(100.0f, 100.0f, 500.0f) == doctest::Approx(0.0f));

    // Far right -> clamped to 1.0
    CHECK(AudioManager::calculateSpatialPan(1000.0f, 0.0f, 500.0f) == doctest::Approx(1.0f));

    // Far left -> clamped to -1.0
    CHECK(AudioManager::calculateSpatialPan(-1000.0f, 0.0f, 500.0f) == doctest::Approx(-1.0f));

    // Half-right: offset 125 / (500*0.5) = 0.5
    CHECK(AudioManager::calculateSpatialPan(125.0f, 0.0f, 500.0f) == doctest::Approx(0.5f));

    // Half-left
    CHECK(AudioManager::calculateSpatialPan(-125.0f, 0.0f, 500.0f) == doctest::Approx(-0.5f));

    // maxDistance <= 0 -> center
    CHECK(AudioManager::calculateSpatialPan(100.0f, 0.0f, 0.0f) == doctest::Approx(0.0f));
    CHECK(AudioManager::calculateSpatialPan(100.0f, 0.0f, -10.0f) == doctest::Approx(0.0f));
}

// =============================================================================
// Update safety
// =============================================================================

TEST_CASE("AudioManager update does not crash") {
    AudioManager mgr;
    mgr.update(0.016f); // before init
    CHECK(mgr.init());
    mgr.update(0.016f); // after init
    mgr.shutdown();
    mgr.update(0.016f); // after shutdown
}
