# FateMMO Game Engine Demo Manual

This manual covers the public demo version of FateMMO Game Engine: the C++23 engine, editor, scene workflow, renderer, ECS, asset pipeline, and demo-safe tooling.

Some files in Caleb's local monorepo include proprietary full-game or server systems. Those systems are useful for understanding the engine's production history, but they are not automatically part of the standalone demo path. Pages in this manual use these labels:

- Demo User: usable in the public demo build.
- Engine Contributor: useful when changing `engine/`.
- Full Game / Server: present in the local full tree, not guaranteed in the demo release.
- Internal Only: do not publish unless the private material has been cleared.

## Start Here

1. Read [Quick Start](quick-start.md).
2. Build or launch `FateDemo`.
3. Open the editor.
4. Load a scene from `assets/scenes/`.
5. Press `Play` or `Observe`.
6. Follow [My First MMORPG Map](tutorials/first-map.md).

## Manuals

- [Quick Start](quick-start.md): install requirements, build the demo, launch the editor, and run a scene.
- [Editor User Guide](editor-user-guide.md): viewport, inspector, content browser, scene dropdown, prefabs, Play/Stop, Observe, and save behavior.
- [Asset Pipeline](asset-pipeline.md): textures, sprites, Aseprite import, MTSDF fonts, shaders, hot-reload, VFS, and VRAM budgets.
- [Architecture Manual](architecture-manual.md): app lifecycle, scenes, ECS archetypes, component registration, prefabs, memory, jobs, and rendering.
- [Networking Protocol](networking-protocol.md): UDP envelope, reliability, protocol versioning, Noise_NK, AuthProof, encryption, rekeying, and packet catalog policy.
- [API Reference](api-reference.md): component registry, demo components, current full-tree registered components, prefab APIs, asset APIs, job APIs, scene APIs, and packet references.
- [Tutorials](tutorials/first-map.md): step-by-step project workflows.
- [Troubleshooting](troubleshooting.md): common build, editor, rendering, asset, and networking problems.
- [Publishing Guide](publishing-guide.md): how to split docs between GitHub, FateMMO.com, and the forums.

## Demo vs Full Game

The demo is intended to let users evaluate the engine and editor loop without requiring the proprietary game layer. In a demo-only checkout, `FateDemo` is the main target. In the full local tree, `FateEngine` and `FateServer` may also exist.

The demo path focuses on:

- Editor launch and navigation.
- Scene loading and saving.
- Play-in-editor snapshot/restore.
- Observe mode.
- ECS serialization for demo components.
- Asset discovery and hot-reload.
- Renderer and sprite/tile workflows.

The full game/server path includes:

- Server-authoritative MMO gameplay.
- Database-backed characters, inventory, quests, combat, economy, and persistence.
- Production UDP networking and auth.
- Proprietary gameplay components under `game/` and `server/`.

When in doubt, treat `engine/` as public engine surface and `game/` or `server/` as full-tree context unless a release package explicitly includes them.

## Source Of Truth

The code wins over the docs. The most important source anchors are:

- `examples/demo_app.cpp`: demo app entry point.
- `engine/components/register_engine_components.h`: demo-safe component registration.
- `engine/editor/editor.cpp`: editor toolbar, scene menu, Play/Stop, Observe, save behavior.
- `engine/ecs/archetype.h`: archetype storage and component column layout.
- `engine/job/job_system.h`: fiber job system API.
- `engine/asset/asset_registry.h`: asset registry and hot-reload API.
- `engine/render/texture.h`: texture cache and VRAM budget behavior.
- `engine/net/packet.h`: protocol version and packet IDs.
- `engine/net/game_messages.h`: packet payload structs.

## Support

- Website: https://www.fatemmo.com
- Forums: https://www.fatemmo.com/forums
- GitHub: use issues for reproducible engine/demo bugs.

