# RmlUi Migration Plan

## Current State
- All game UI uses Dear ImGui (v1.91.9b-docking)
- ImGui is mouse-centric, poor touch/scroll support, no CSS theming
- Editor tools (entity inspector, tile palette, debug overlays) should stay ImGui

## Target State
- Game UI (inventory, chat, HUD, dialogue, quest log, trade, crafting) → RmlUi
- Editor/debug tools → keep ImGui
- RmlUi renders at 480×270 game resolution, composited on top of game world

## Migration Phases

### Phase 1: Integration (1 week)
- Add RmlUi via FetchContent (MIT license, SDL2+GL3 backend)
- Initialize RmlUi context at 480×270 alongside ImGui
- Port one simple screen (e.g., main menu) to validate rendering

### Phase 2: Core Game UI (3-4 weeks)
- Port inventory with drag-and-drop (RmlUi native `drag: clone`)
- Port skill bar with cooldown overlays (RCSS animations)
- Port chat window with scrollback (RmlUi native scroll containers)
- Port HUD (HP/MP bars, minimap frame)
- Port NPC dialogue with conditional nodes

### Phase 3: Advanced UI (2-3 weeks)
- Trade window, marketplace browser
- Quest log with objective tracking
- World map, party UI
- Settings/options menus

### Phase 4: Strip ImGui from Release (1 week)
- Gate ImGui behind `#ifdef FATEMMO_EDITOR`
- Release builds link only RmlUi for game UI
- Editor builds link both

## Key Technical Notes
- RmlUi SDL2+GL3 backend: `RmlUi_Platform_SDL.cpp` + `RmlUi_Renderer_GL3.cpp`
- Data bindings map C++ structs to HTML templates (MVC pattern)
- RCSS supports 9-slice panel decorators for pixel art window frames
- Touch input: scale physical coords → 480×270 before passing to RmlUi
- All interactive elements minimum 16×16 pixels at native resolution
