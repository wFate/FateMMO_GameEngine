# Asset Pipeline

Audience: Demo User, Engine Contributor

The demo uses the same asset registry architecture as the full engine: logical asset keys, loader registration, hot-reload, async decode where supported, and main-thread GPU upload.

## Asset Roots

The demo app points the editor at the project root and uses `assets/` for runtime assets.

Important folders:

- `assets/scenes/`: scene JSON.
- `assets/shaders/`: GLSL shaders.
- `assets/shaders/metal/`: Metal shader sources.
- `assets/ui/`: retained-mode UI JSON and themes.
- `assets/prefabs/`: prefab JSON when present.
- `assets/data/`: data-driven content when present.

Use forward slashes in asset keys. Runtime keys should look like `assets/foo.png`, not an absolute Windows path.

## Asset Registry

`AssetRegistry` owns generic asset slots and loader callbacks.

Main concepts:

- `AssetHandle`: generational handle to an asset slot.
- `AssetKind`: texture, JSON, or shader.
- `AssetState`: empty, loading, ready, or failed.
- `AssetLoader`: load, reload, validate, destroy, optional decode/upload pair.
- `queueReload`: thread-safe reload request.
- `processReloads`: main-thread debounced reload processing.
- `loadAsync`: fiber decode plus main-thread upload when the loader supports it.

Most registry methods are main-thread-only. Worker fibers may decode data, but GPU upload must happen on the main thread.

## Textures

The texture cache avoids duplicate GPU texture loads and tracks estimated VRAM.

Supported workflow:

1. Put the texture under `assets/`.
2. Reference it by logical key from scene, prefab, or component data.
3. Let `TextureCache::load` or the asset registry load it.
4. Use hot-reload while editing loose files.

The engine supports normal image loading and compressed KTX paths where the relevant platform support exists.

## Compressed Textures

Mobile builds should prefer GPU compressed formats when possible.

The codebase has support hooks for:

- ETC2.
- ASTC 4x4.
- ASTC 8x8.
- KTX1 loading.

Compression reduces VRAM pressure and upload cost. The exact shipping choice should be tested on target devices.

## VRAM Budgeting

The texture cache has an LRU eviction path and a default budget of 512 MB.

Device recommendations are:

- Low: 200 MB, for devices with 3 GB RAM or less.
- Medium: 350 MB, for devices with 4-6 GB RAM.
- High: 512 MB, for devices with 8 GB RAM or more.

Artist rules:

- Prefer power-of-two dimensions where practical.
- Use atlases for small sprite families.
- Avoid shipping large unused transparent regions.
- Use ASTC or ETC2 for mobile background and tile textures.
- Keep UI texture sizes honest; do not use 4K source art for small buttons.
- Check texture memory after large scene or UI changes.

## Sprites

Sprite entities use `SpriteComponent` in the demo-safe registry.

Key fields include:

- `texturePath`
- `sourceRect`
- `size`
- `tint`
- `flipX`
- `flipY`
- `frameWidth`
- `frameHeight`
- `currentFrame`
- `totalFrames`
- `columns`

Use sprites when a single entity has visual identity. Use tile layers when building terrain.

## Aseprite Workflow

The editor includes an Aseprite-oriented animation importer.

Recommended workflow:

1. Author sprite sheets in Aseprite.
2. Export the sheet image and JSON metadata.
3. Keep sibling directional files together when using directional animation sets.
4. Import through the animation editor.
5. Preview in-editor.
6. Save generated metadata under `assets/`.
7. Verify the sprite in Play or Observe mode.

Keep animation frame sizes consistent within a sheet unless the importer path explicitly supports the variant.

## Fonts And MTSDF

The engine has SDF and MTSDF font rendering support. MTSDF assets are intended for crisp text with outline and shadow effects.

Recommended font workflow:

1. Choose a font with a license suitable for redistribution.
2. Generate an atlas offline using an MTSDF-capable generator.
3. Store the atlas and metadata under `assets/`.
4. Reference the font through the font registry or UI style data.
5. Verify outline thickness and shadow behavior in the editor.

Do not treat a bitmap coverage atlas as equivalent to a true signed-distance atlas. Effects that rely on distance fields need actual distance data.

## Shaders

Shader files live under:

- `assets/shaders/`
- `assets/shaders/metal/`

Use the existing shader naming and backend split. If a shader is referenced by runtime code, keep its asset key stable and test both loose-file and packaged paths before release.

## VFS And Packaged Assets

The engine has an `IAssetSource` abstraction for loose files and packaged archives.

The usual public demo path uses DirectFS loose files. Packaged builds can use `FATE_USE_VFS=ON` and `.pak` assets when the target platform path is ready.

Rules:

- Loaders should read through `AssetRegistry::readBytes` or `readText`.
- Do not use ad hoc `fopen` paths in new loaders.
- Keep logical asset keys stable.
- Treat packaged archives as runtime data, not an editor hot-reload surface.

## Troubleshooting Assets

If a texture is magenta, the asset was missing or failed to load.

If a file does not reload, check:

- It is under the active asset root.
- The path normalizes to the same logical key used by the registry.
- The file extension is registered with a loader.
- The asset did not enter the failed-load cache.
- The editor is running from a folder where `assets/` can be found.

