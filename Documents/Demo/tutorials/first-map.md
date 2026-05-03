# Tutorial: My First MMORPG Map

Audience: Demo User

This tutorial creates a simple walkable-looking 2D scene in the editor. The public demo focuses on engine/editor workflow, not full server-authoritative MMO gameplay, so this tutorial is about authoring and previewing a map.

## Goal

By the end, you should have:

- A new scene JSON file.
- A visible tiled area or grid-based layout.
- At least one sprite entity.
- A saved scene.
- A Play/Stop test pass.
- An Observe preview.

## 1. Launch The Demo

Build and run `FateDemo`.

```powershell
.\out\build\x64-Debug-Demo\FateDemo.exe
```

You should see the editor and a large grid. The red and green origin axes mark world zero.

## 2. Create Or Open A Scene

Use the File menu:

1. Choose New Scene.
2. Save it under `assets/scenes/`.
3. Use a simple lowercase name, for example `my_first_map.json`.

If the demo package already includes example scenes, you can open one first to inspect the expected format.

## 3. Set Up The Camera

Use the viewport controls to pan and zoom around the origin.

Suggested starting area:

- Keep the map centered near `(0, 0)`.
- Build a small area first, around 20 by 12 tiles.
- Remember that negative world coordinates are valid.

## 4. Paint Terrain Or Place Tile Layers

Use the tile tools if available in your demo build.

Recommended first layout:

- Grass or base floor across the whole area.
- A path through the middle.
- A few blocked-looking decorative tiles around the edge.
- A landmark near the origin so you can orient yourself.

Keep the first map small. The goal is to validate the workflow, not produce final content.

## 5. Add A Sprite Entity

Create or select an entity with:

- `Transform`
- `SpriteComponent`

Set the transform near the origin. Assign a texture path under `assets/`.

If the texture shows as magenta, the texture path failed to load. Check the asset key, extension, and working directory.

## 6. Save

Use `Ctrl+S` or the File menu.

The editor writes scene JSON atomically. If saving fails, check the log viewer and make sure the target path is under a writable folder.

## 7. Test With Play

Press `Play`.

While Play is active:

- Runtime update is active.
- Editor tool shortcuts are limited.
- Save paths may be blocked.
- The pre-Play snapshot is preserved.

Press `Stop`.

After Stop, the editor restores the scene from the snapshot. Any runtime-only changes should be gone.

## 8. Preview With Observe

Press `Observe`.

The editor chrome should hide and the scene should preview more like the player view. Press `Stop Obs` to return to editing.

## 9. Edit And Repeat

Make one small change:

- Move the sprite.
- Add another tile patch.
- Change a sprite tint.
- Adjust the camera.

Save again, then repeat Play and Observe.

## Completion Checklist

- The scene exists under `assets/scenes/`.
- The scene reloads from the scene dropdown.
- At least one entity appears.
- Play enters without errors.
- Stop restores the authoring state.
- Observe shows the scene without editor chrome.
- No magenta missing textures remain.

## Next Steps

- Import a sprite sheet through the animation editor.
- Create a reusable prefab.
- Add multiple tile layers.
- Test the scene on the target mobile aspect ratio.
- Read the [Asset Pipeline](../asset-pipeline.md) before adding high-resolution art.

