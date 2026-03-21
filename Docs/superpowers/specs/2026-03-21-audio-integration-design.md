# Audio Integration Design Spec

## Goal

Integrate SoLoud audio library into the FateMMO engine, providing SFX playback, zone music with crossfade, independent volume buses, and 2D spatial audio. Gracefully handle missing audio files (log warning, continue silently).

## Architecture

A single `AudioManager` class wraps SoLoud's `Soloud` engine instance. It manages two pools: preloaded `SoLoud::Wav` objects for SFX (short, low-latency) and a streamed `SoLoud::WavStream` for the current music track. SFX are keyed by string name (e.g., `"hit_sword"`), loaded from `assets/audio/sfx/`. Music tracks are streamed from `assets/audio/music/`.

SoLoud uses SDL2 as its audio backend (`WITH_SDL2_STATIC`), requiring zero additional platform dependencies. SoLoud's virtual voice system handles channel management — when all active voices are full, the lowest-priority, furthest voice is automatically virtualized (silenced but tracked).

## Components

### AudioManager (`engine/audio/audio_manager.h/cpp`)

**Lifecycle:**
- `init()` — initialize SoLoud engine with SDL2 backend, set max active voices to 32
- `shutdown()` — stop all audio, deinit SoLoud, free all loaded sounds
- `update(float dt)` — tick faders (music crossfade uses SoLoud's built-in `fadeVolume`)

**SFX API:**
- `loadSFX(name, path)` — preload a WAV into a `SoLoud::Wav`, store in `unordered_map<string, Wav>`
- `loadSFXDirectory(dirPath)` — scan directory for .wav files, load each with filename (no extension) as key
- `playSFX(name, volume=1.0f)` — play a preloaded SFX at given volume. Returns silently if name not found (log warning once per missing name)
- `playSFXSpatial(name, worldX, worldY, listenerX, listenerY, maxDistance=500.0f)` — play with distance-based volume falloff and stereo pan. Volume = `1.0 - clamp(distance / maxDistance, 0, 1)`. Pan = `clamp((worldX - listenerX) / (maxDistance * 0.5), -1, 1)`
- `unloadSFX(name)` — free a specific SFX
- `unloadAllSFX()` — free all preloaded SFX

**Music API:**
- `playMusic(path, fadeInSeconds=2.0f)` — stream an OGG file. If music is already playing, crossfade: fade out current over `fadeInSeconds`, start new track fading in
- `stopMusic(fadeOutSeconds=2.0f)` — fade out and stop current music
- `isMusicPlaying()` — query

**Volume API:**
- `setMasterVolume(float 0-1)` — SoLoud global volume
- `setSFXVolume(float 0-1)` — multiplied into all SFX play calls
- `setMusicVolume(float 0-1)` — applied to music handle
- `getMasterVolume()`, `getSFXVolume()`, `getMusicVolume()`

**Internals:**
- `SoLoud::Soloud engine_` — the core engine
- `std::unordered_map<std::string, std::unique_ptr<SoLoud::Wav>> sfxCache_` — preloaded SFX
- `std::unique_ptr<SoLoud::WavStream> currentMusic_` — active music stream
- `SoLoud::handle musicHandle_` — handle to playing music for volume/fade control
- `float sfxVolume_ = 1.0f`, `float musicVolume_ = 0.7f` — bus volumes
- `std::unordered_set<std::string> warnedMissing_` — suppress repeated warnings for the same missing file

### Integration Points in game_app.cpp

**Combat SFX** (wired into existing callbacks):
- `onCombatEvent` / `onSkillResult` → `playSFX("hit_melee")`, `playSFX("hit_crit")`, `playSFX("miss")`
- `onDeathNotify` → `playSFX("death")`
- `onLevelUp` → `playSFX("level_up")`
- `onLootPickup` → `playSFX("loot_pickup")`

**UI SFX** (wired into UI code):
- Button clicks → `playSFX("ui_click")`
- Inventory open/close → `playSFX("ui_open")`
- Equip item → `playSFX("equip")`
- Chat message received → `playSFX("chat_blip")`

**Zone Music** (wired into zone transition):
- `onZoneTransition` → `playMusic("music/" + zoneName + ".ogg")`
- Each zone can define a music track in its metadata

**Spatial SFX** (for nearby entity combat):
- Remote player/mob combat events use `playSFXSpatial()` with the entity's world position and camera center as listener position

### Asset Directory Structure

```
assets/audio/
├── sfx/
│   ├── hit_melee.wav
│   ├── hit_crit.wav
│   ├── miss.wav
│   ├── death.wav
│   ├── level_up.wav
│   ├── loot_pickup.wav
│   ├── equip.wav
│   ├── ui_click.wav
│   ├── ui_open.wav
│   └── chat_blip.wav
└── music/
    └── (zone-specific .ogg files)
```

Audio files are NOT included — the system loads whatever is present and silently skips missing files.

### CMake Integration

SoLoud added via FetchContent. Backend selection:
- `WITH_SDL2_STATIC=ON` — uses SDL2 audio (already linked)
- All other backends disabled (`WITH_MINIAUDIO=OFF`, `WITH_COREAUDIO=OFF`, etc.)

SoLoud linked to `fate_engine` static library.

### Testing Strategy

Unit tests for AudioManager (headless, no actual audio playback):
- Init/shutdown lifecycle
- loadSFX with valid/invalid paths
- playSFX with loaded/missing names (no crash)
- Volume getters/setters clamped to 0-1
- Music play/stop state transitions
- Spatial volume/pan calculation (pure math, testable without audio device)
- loadSFXDirectory loads all .wav files from a directory
- Unload clears cache

SoLoud supports a "null driver" for headless testing — no audio device required.

## Constraints

- No audio files shipped — system is silent until assets are added
- Missing files produce a single warning log per unique name, then silence
- Max 32 active voices (SoLoud virtualizes the rest automatically)
- Music is one track at a time (crossfade handles transitions)
- SFX preloaded as WAV (low latency); music streamed as OGG (low memory)
- Volume settings are runtime-only for now (persistence via user prefs is future work)
