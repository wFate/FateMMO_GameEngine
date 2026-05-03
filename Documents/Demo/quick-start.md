# Quick Start

Audience: Demo User

This guide gets the FateMMO Game Engine demo from a fresh checkout to a running editor session.

## What You Get

The demo build opens the editor UI with a live scene viewport, content browser, inspector panels, tile/sprite workflows, Play/Stop snapshot restore, Observe mode, and scene loading from `assets/scenes/`.

The demo is not the full proprietary MMORPG. If the local checkout contains `game/` and `server/`, those folders can build additional targets, but the public demo path should stay focused on `FateDemo` and `engine/`.

## Requirements

Windows development uses:

- Windows 11.
- Visual Studio 2026 / VS 18 preferred.
- `VsDevCmd.bat` at `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat`.
- CMake with the repo presets.
- vcpkg at `C:\vcpkg` for full-tree dependencies.

The full tree may use OpenSSL, libpq, libsodium, and FreeType. The demo target is designed to build without the proprietary game layer.

## Build The Demo

Use the repo wrapper for normal verification. Do not hand-roll CMake commands unless you are diagnosing the build system itself.

For the open-source demo target in a full checkout, force the demo build with CMake if needed:

```powershell
cmake -B out/build/x64-Debug-Demo -DFATE_FORCE_DEMO_BUILD=ON
cmake --build out/build/x64-Debug-Demo --target FateDemo
```

If you are in a demo-only checkout where `game/` is absent, CMake configures `FateDemo` automatically.

For the full local engine client, use the wrapper:

```powershell
.\scripts\check_shipping.ps1 -Preset x64-Release -Target FateEngine
```

For the pre-push shipping guard:

```powershell
.\scripts\check_shipping.ps1
```

## Launch

Run the generated executable from its build output folder so relative asset paths resolve correctly:

```powershell
.\out\build\x64-Debug-Demo\FateDemo.exe
```

The default demo window is titled `FateEngine Demo`.

## First Editor Session

1. Launch `FateDemo`.
2. Use the scene viewport toolbar.
3. Pick a scene from the scene dropdown if scenes are available.
4. Use the viewport camera to inspect the map.
5. Press `Play` to run a snapshot-protected play session.
6. Press `Pause` or `Resume` as needed.
7. Press `Stop` to restore the scene from the snapshot.
8. Press `Observe` to preview the scene with editor chrome hidden.

## Important Behavior

`Play` snapshots the ECS state before running. `Stop` restores that snapshot. This lets users test scene behavior without saving runtime changes back into authored scene JSON.

`Observe` runs a live preview without the Play snapshot model. The default demo implementation hides editor chrome and previews locally. Downstream apps can override the observer hooks through `AppConfig::onObserveStart` and `AppConfig::onObserveStop`.

`Ctrl+S` saves the current scene when saving is allowed. Scene writes use atomic file behavior so failed writes do not corrupt the original JSON.

## Where To Go Next

- Build a simple map: [My First MMORPG Map](tutorials/first-map.md).
- Learn the editor panels: [Editor User Guide](editor-user-guide.md).
- Import assets: [Asset Pipeline](asset-pipeline.md).
- Fix setup problems: [Troubleshooting](troubleshooting.md).

