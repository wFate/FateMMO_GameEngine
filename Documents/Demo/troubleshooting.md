# Troubleshooting And FAQ

Audience: Demo User, Engine Contributor

This page lists common problems and the first checks to run.

## Build: Visual Studio Tools Not Found

Symptom:

```text
VsDevCmd.bat not found in any known VS install path
```

Fix:

- Install Visual Studio 2026 / VS 18 with C++ desktop development tools.
- Confirm this file exists:

```text
C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat
```

The wrapper also checks the Visual Studio 2022 path as a fallback, but this repo expects VS 18 for current Windows development.

## Build: vcpkg Dependencies Missing

Symptom:

```text
Install via vcpkg: 'vcpkg install openssl'
Install via vcpkg: 'vcpkg install libsodium'
```

Fix:

- Install vcpkg at `C:\vcpkg`.
- Install the required packages for the full tree.
- Reconfigure with the repo presets or wrapper.

The demo target may not need every full-tree dependency, but the full client/server does.

## Build: LNK1168

Symptom:

```text
LNK1168: cannot open FateEngine.exe or FateServer.exe for writing
```

Cause:

The executable is still running.

Fix:

- Stop the running client/server.
- Confirm no background process still holds the exe.
- Rebuild once. Do not repeatedly retry while the process is still alive.

## Build: C1853 Stale PCH

Symptom:

```text
fatal error C1853: ... cmake_pch.cxx.pch
```

Cause:

The precompiled header is stale or incompatible with the current compiler state.

Fix:

- Clean the affected build directory.
- Re-run the wrapper or CMake configure.
- Rebuild.

## Build: C1083 Missing Standard Header

Symptom:

```text
fatal error C1083: Cannot open include file: 'stdio.h'
fatal error C1083: Cannot open include file: 'float.h'
```

Cause:

The Visual Studio or Windows SDK environment is not active or is broken.

Fix:

- Use `scripts/check_shipping.ps1`, which wraps `VsDevCmd.bat`.
- Confirm the Windows SDK is installed.
- Confirm the Visual Studio C++ toolchain is installed.

## Build: Unresolved Externals After Adding Files

Cause:

The repo uses `GLOB_RECURSE` in CMakeLists. New files may require CMake reconfigure before the build target sees them.

Fix:

```powershell
cmake --preset x64-Release
cmake --build out/build/x64-Release --target FateEngine
```

Prefer the wrapper for normal verification.

## Demo Target Missing

Symptom:

`FateDemo` is not configured.

Cause:

In a full checkout, CMake may configure `FateEngine` instead of `FateDemo` unless the demo is forced.

Fix:

```powershell
cmake -B out/build/x64-Debug-Demo -DFATE_FORCE_DEMO_BUILD=ON
cmake --build out/build/x64-Debug-Demo --target FateDemo
```

## Texture Is Magenta

Cause:

The engine loaded the placeholder texture because the requested texture failed.

Fix:

- Check the asset key spelling.
- Confirm the file exists under `assets/`.
- Confirm the extension has a registered loader.
- Confirm the app is running from a folder where `assets/` is available.
- Check recent asset reads if the build exposes that diagnostic.

## Hot-Reload Does Not Apply

Fix:

- Confirm the file is under the active asset root.
- Confirm the file watcher is running on the platform.
- Wait past the debounce delay.
- Check that the path normalizes to the same logical asset key used by the registry.
- Restart the editor if the asset entered the failed-load cache.

## Scene Does Not Save

Fix:

- Confirm you are not in blocked Play state.
- Confirm the current scene path is set.
- Confirm the destination folder is writable.
- Check the log viewer for atomic-write errors.
- Use Save As to pick a known-good path under `assets/scenes/`.

## Runtime Changes Appeared In Scene JSON

Expected demo behavior is that Play mode restores the pre-Play snapshot and scene saves skip replicated/runtime-only entities.

Fix:

- Reproduce in a small scene.
- Confirm the entity is properly marked runtime/replicated if it is created by runtime code.
- Avoid saving while in active Play mode.
- Inspect `Editor::saveScene` skip behavior if changing runtime factories.

## Protocol Version Mismatch

Symptom:

Client cannot connect and the server reports a version mismatch.

Fix:

- Rebuild client and server from the same checkout.
- Check `engine/net/packet.h`.
- Confirm `PROTOCOL_VERSION` matches.

## Server Auth Or Handshake Fails

Fix:

- Confirm server identity key setup.
- Confirm the client has the expected server identity public key.
- Confirm `CmdAuthProof` is sent after the Noise_NK session keys are active.
- Confirm database-backed auth/session state exists.

This is full-game/server surface, not required for basic demo editor usage.

## Duplicate Server On Same Port

Symptom:

Server bind fails or reports port already in use.

Fix:

- Stop the old `FateServer.exe`.
- Start one server process only.

The Windows server path uses exclusive UDP bind behavior so duplicate instances fail loudly.

## FAQ

### Should I edit docs on the website or GitHub?

Keep canonical manuals in GitHub with the source. Use the website as the clean entry point and the forums for support.

### Is the demo the full MMO?

No. The demo is the engine/editor path. The full MMO includes proprietary game and server systems.

### Can I build without `game/`?

Yes. `engine/` is intended to compile standalone. The demo-safe component registration path exists for that reason.

### Why do docs say the code wins?

This engine changes quickly. If a count, packet ID, or component list differs from source, update the docs from source.

