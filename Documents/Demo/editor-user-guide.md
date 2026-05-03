# Editor User Guide

Audience: Demo User, Engine Contributor

The FateMMO editor is built into the engine runtime. The demo uses the same editor-runtime loop as the full client, but only the demo-safe component set is guaranteed in a public build.

## Editor Layout

The exact panels can change as the editor evolves, but the core workflow is:

- Scene viewport: camera, map view, Play/Stop controls, Observe, scene dropdown.
- Inspector: selected entity fields and components.
- Content browser: assets and source tree browsing.
- Tile tools: tile painting and tile layer editing.
- Animation editor: Aseprite-oriented sprite animation import and preview.
- Log viewer: runtime/editor diagnostics.

## Scene Viewport Toolbar

The scene viewport toolbar is the fastest way to test content.

- `Play`: snapshot the current ECS state and enter runtime input/update mode.
- `Pause`: pause runtime update while keeping the play snapshot active.
- `Resume`: continue a paused play session.
- `Stop`: restore the pre-Play snapshot and return to editing.
- `Observe`: run a live local preview with editor chrome hidden.
- `Stop Obs`: leave Observe mode.
- Scene dropdown: load a scene from `assets/scenes/*.json`.

Scene switching is guarded while Play is active. Pause or stop first if a scene switch is blocked.

## Play Mode

Use Play mode when you want to test behavior without committing runtime state.

Play mode:

- Serializes a snapshot of the current world.
- Runs the normal update path.
- Preserves camera state.
- Blocks unsafe save paths while active.
- Restores the snapshot on Stop.

If a runtime entity appears while playing, Stop should remove it because the original snapshot is restored.

## Observe Mode

Observe mode is for previewing the scene as a player would see it. The default demo behavior hides editor panels and runs a local preview.

Engine users can replace this with a custom flow by assigning:

- `AppConfig::onObserveStart`
- `AppConfig::onObserveStop`

The full game can use those hooks for admin spectate or network observe flows. That is outside the core public demo path unless the release explicitly includes it.

## Saving Scenes

Scene saving uses atomic writes:

- Write to a temporary file.
- Rename into place.
- Create parent directories when needed.
- Fall back for cross-volume moves.
- Clean up failed temporary writes.

Use `Ctrl+S` or the File menu when the scene has a current path. Use Save As when creating a new scene file.

Do not save during active Play unless the UI explicitly allows it. The intended workflow is edit, save, Play, Stop, continue editing.

## Entity Selection

The inspector edits the currently selected entity. Typical editable data includes:

- Transform position, rotation, and scale.
- Sprite texture path and render fields.
- Tile layer state.
- Game-only fields when the full game layer is present.

Public demo docs should not assume full-game components are available. The demo-safe serialized components are registered in `engine/components/register_engine_components.h`.

## Demo-Safe Components

The public demo registration path includes:

- `Transform`
- `SpriteComponent`
- `TileLayerComponent`

These are enough for placing entities, rendering sprites, and saving/loading tile layers. Full-game checkouts may register many more components through `game/register_components.h`.

## Prefab Workflow

The prefab system serializes entities and their registered components into JSON.

Basic workflow:

1. Create or select an entity.
2. Add/edit registered components.
3. Save the entity as a prefab.
4. Reuse the prefab in scenes.
5. Reload after changing prefab JSON or source assets.

Only registered components round-trip safely. If a component is missing from the registry, the editor cannot serialize it correctly.

## Hot-Reload

The app runs a file watcher over the asset root and queues reloads through `AssetRegistry`. Reloads are debounced so rapid editor writes do not thrash asset loading.

Expected behavior:

- Loose files under `assets/` hot-reload in editor builds.
- Packaged `.pak` files are stable runtime inputs, not live editable overlays.
- File watcher paths are normalized to logical asset keys.
- Failed loads are cached to avoid repeated work.

If hot-reload does not fire, check that the edited file is under the active asset root and that the path is not escaping the root after canonicalization.

## Common Rules

- Use `GameViewport` for game UI sizing, not raw display size.
- Negative world coordinates are valid.
- Tile grids are origin-centered.
- Do not rely on right-click for mobile-facing workflows.
- Prefer tap, drag-drop, long-press, and confirm popups for player UI.
- Treat Play/Stop snapshot behavior as a safety boundary for authored content.

