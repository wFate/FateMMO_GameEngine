# Audio System Guide

## Overview

FateMMO uses [SoLoud](https://solhsa.com/soloud/) for audio, wrapped in an `AudioManager` class at `engine/audio/audio_manager.h`. SoLoud runs on the SDL2 audio backend (same as the rest of the engine) and falls back to a silent null driver when no audio device is available (CI, headless servers).

The system handles two types of audio:
- **SFX** — short sounds (hits, clicks, pickups) preloaded into memory as WAV/OGG for instant playback
- **Music** — longer tracks streamed from OGG files with looping and crossfade

## Adding Sound Effects

### 1. Drop files into the SFX directory

Place `.wav` or `.ogg` files in:
```
assets/audio/sfx/
```

The filename (without extension) becomes the SFX name used in code. For example:
```
assets/audio/sfx/hit_melee.wav    → playSFX("hit_melee")
assets/audio/sfx/hit_crit.ogg     → playSFX("hit_crit")
assets/audio/sfx/loot_gold.wav    → playSFX("loot_gold")
```

### 2. They load automatically

On startup, `AudioManager::loadSFXDirectory()` scans the directory and loads every `.wav` and `.ogg` file it finds. No code changes needed to add new sounds — just drop files in.

### 3. Currently wired SFX names

These names are already referenced in `game_app.cpp`. If a file with the matching name exists, it plays automatically:

| SFX Name | Triggered By |
|----------|-------------|
| `hit_melee` | Auto-attack deals damage |
| `hit_crit` | Critical hit (auto-attack or skill) |
| `hit_skill` | Skill deals damage (non-crit) |
| `miss` | Attack or skill misses/dodged |
| `kill` | Target killed |
| `death` | Local player dies |
| `loot_item` | Item picked up |
| `loot_gold` | Gold picked up |
| `respawn` | Player respawns |
| `chat_send` | Chat message sent |

### 4. Recommended audio specs

- **Format:** WAV (uncompressed, lowest latency) or OGG (smaller files, slightly more CPU)
- **Sample rate:** 44100 Hz (SoLoud resamples if needed, but native rate avoids overhead)
- **Channels:** Mono for SFX (stereo pan is applied by the spatial system)
- **Duration:** Under 2 seconds for combat SFX, under 0.5s for UI clicks
- **File size:** Keep individual SFX under 500KB. Total SFX budget ~5-10MB

## Adding Zone Music

### 1. Drop OGG files into the music directory

```
assets/audio/music/
```

Music files are named after zones. When the player transitions to a zone, the engine looks for:
```
assets/audio/music/{zoneName}.ogg
```

For example:
```
assets/audio/music/WhisperingWoods.ogg
assets/audio/music/CrystalCaverns.ogg
assets/audio/music/LanosCastle.ogg
```

### 2. Automatic crossfade

When a zone transition occurs, the current music fades out over 1 second and the new track fades in over 2 seconds. Music loops indefinitely until a zone change or `stopMusic()` is called.

### 3. Recommended music specs

- **Format:** OGG Vorbis (streamed from disk, not loaded into memory)
- **Quality:** 128-192 kbps (good quality, ~1MB per minute)
- **Duration:** 2-5 minutes (loops seamlessly — ensure the file loops cleanly)
- **Channels:** Stereo

## Using AudioManager in Code

### Access

`AudioManager` is a member of `GameApp` (`audioManager_`). For code outside GameApp that needs audio, pass a reference or use a service locator pattern.

### Playing SFX

```cpp
// Simple playback (uses SFX volume bus)
audioManager_.playSFX("hit_melee");

// With custom volume (0.0 - 1.0, multiplied by SFX volume bus)
audioManager_.playSFX("hit_crit", 0.8f);
```

### Playing spatial SFX

For sounds that should get quieter and pan based on distance from the camera:

```cpp
// worldX/Y = where the sound originates (entity position)
// listenerX/Y = camera center position
// maxDistance = radius beyond which volume is 0 (default 500px)
audioManager_.playSFXSpatial("hit_melee",
    entityPos.x, entityPos.y,
    camera.position().x, camera.position().y,
    500.0f);
```

**Volume formula:** `1.0 - clamp(distance / maxDistance, 0, 1)`
**Pan formula:** `clamp((worldX - listenerX) / (maxDistance * 0.5), -1, 1)`

At 480x270 virtual resolution, `maxDistance = 500` means sounds are audible within roughly 1 screen width and silent beyond 2 screens.

### Loading SFX manually

```cpp
// Load a single SFX by name and path
audioManager_.loadSFX("custom_sound", "assets/audio/custom/beep.wav");

// Load all .wav/.ogg files from a directory (filename stem = SFX name)
int count = audioManager_.loadSFXDirectory("assets/audio/sfx");
```

### Music control

```cpp
// Play music with crossfade (default 2 second fade-in)
audioManager_.playMusic("assets/audio/music/WhisperingWoods.ogg");

// Custom fade duration
audioManager_.playMusic("assets/audio/music/BossFight.ogg", 0.5f);

// Stop with fade-out (default 2 seconds)
audioManager_.stopMusic();

// Immediate stop
audioManager_.stopMusic(0.0f);

// Check if music is playing
if (audioManager_.isMusicPlaying()) { ... }
```

### Volume control

Three independent volume buses, all clamped to 0.0 - 1.0:

```cpp
audioManager_.setMasterVolume(0.8f);   // Affects everything
audioManager_.setSFXVolume(1.0f);      // Multiplied into SFX playback
audioManager_.setMusicVolume(0.5f);    // Applied to music voice

float m = audioManager_.getMasterVolume();   // Read back
float s = audioManager_.getSFXVolume();
float u = audioManager_.getMusicVolume();
```

**Effective SFX volume** = `playSFX volume * sfxVolume * masterVolume`
**Effective music volume** = `musicVolume * masterVolume`

Default values: master=1.0, SFX=1.0, music=0.7

### Unloading

```cpp
audioManager_.unloadSFX("hit_melee");   // Free one sound
audioManager_.unloadAllSFX();            // Free all loaded SFX
```

Useful on zone transitions to free memory. Call `loadSFXDirectory()` again after entering the new zone if using per-zone SFX sets.

## Architecture

```
GameApp
  └── AudioManager (member: audioManager_)
        ├── SoLoud::Soloud engine_ (32 max active voices)
        ├── sfxCache_ (unordered_map<string, unique_ptr<Wav>>)
        ├── currentMusic_ (unique_ptr<WavStream>, streamed)
        ├── musicHandle_ (unsigned int, voice handle for volume/fade)
        ├── Volume buses (masterVolume_, sfxVolume_, musicVolume_)
        └── warnedMissing_ (set<string>, suppress repeated warnings)
```

**Voice management:** SoLoud supports up to 4,095 virtual voices. We cap active (audible) voices at 32. When all 32 are in use and a new sound plays, SoLoud automatically virtualizes (silences) the quietest, lowest-priority voice to make room. Music is marked as a protected voice and cannot be stolen.

**Backend:** Uses SDL2's audio subsystem on desktop and mobile. Falls back to a null driver (silent) if no audio device is available — this is how tests and CI run without failing.

## File Locations

| File | Purpose |
|------|---------|
| `engine/audio/audio_manager.h` | AudioManager class declaration |
| `engine/audio/audio_manager.cpp` | Full implementation |
| `game/game_app.h` | Holds `AudioManager audioManager_` member |
| `game/game_app.cpp` | Init, shutdown, and 10 SFX/music hooks |
| `assets/audio/sfx/` | Drop WAV/OGG SFX files here |
| `assets/audio/music/` | Drop OGG music files here (named after zones) |
| `tests/test_audio_manager.cpp` | 27 unit tests (null driver, no audio device needed) |

## Troubleshooting

**No sound at all:**
- Check that `.wav`/`.ogg` files exist in `assets/audio/sfx/`
- Check the log for `AudioManager initialized (backend: ...)` — if it says "null", no audio device was found
- Check `getMasterVolume()` and `getSFXVolume()` are not 0

**"SFX 'xxx' not loaded" warnings:**
- The SFX name in code doesn't match any loaded file
- Check that the filename (without extension) matches exactly (case-sensitive)

**Music doesn't play on zone change:**
- The engine looks for `assets/audio/music/{zoneName}.ogg` — check the zone name matches the scene name exactly
- Check the log for "failed to load music" warnings

**Audio crackles or pops:**
- SFX files may have different sample rates — SoLoud resamples but 44100 Hz is recommended
- Too many simultaneous sounds — reduce max active voices or increase the spatial maxDistance to cull distant sounds earlier
