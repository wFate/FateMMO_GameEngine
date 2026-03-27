#include "game/game_app.h"
#include "engine/core/logger.h"
#include "engine/render/gfx/backend/gl/gl_loader.h"
#include "engine/scene/scene_manager.h"
#include "engine/editor/editor_shim.h"
#include "game/register_components.h"
#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "game/components/player_controller.h"
#include "game/components/animator.h"
#include "game/components/box_collider.h"
#include "game/components/polygon_collider.h"
#include "game/components/game_components.h"
#include "game/components/tile_layer_component.h"
#include "engine/render/sdf_text.h"
#include "engine/ecs/prefab.h"
#include "game/entity_factory.h"
#include "game/animation_loader.h"
#include "game/systems/movement_system.h"
#include "game/systems/render_system.h"
#include "game/systems/gameplay_system.h"
#include "game/systems/mob_ai_system.h"
#include "game/systems/combat_action_system.h"
#include "game/systems/zone_system.h"
#include "game/systems/spawn_system.h"
#include "game/systems/npc_interaction_system.h"
#include "game/systems/quest_system.h"
#include "engine/job/job_system.h"
#include <thread>
#include <chrono>
#include "engine/particle/particle_system.h"
#include "engine/particle/particle_emitter_component.h"
#include "game/shared/profanity_filter.h"
#include "engine/ui/widgets/button.h"
#include "engine/ui/widgets/window.h"
#include "engine/ui/widgets/fate_status_bar.h"
#include "engine/ui/widgets/target_frame.h"
#include "engine/ui/widgets/exp_bar.h"
#include "engine/ui/widgets/dpad.h"
#include "engine/ui/widgets/skill_arc.h"
#include "engine/ui/widgets/menu_tab_bar.h"
#include "engine/ui/widgets/inventory_panel.h"
#include "engine/ui/widgets/status_panel.h"
#include "engine/ui/widgets/skill_panel.h"
#include "engine/ui/widgets/character_select_screen.h"
#include "engine/ui/widgets/character_creation_screen.h"
#include "engine/ui/widgets/chat_panel.h"
#include "engine/ui/widgets/trade_window.h"
#include "engine/ui/widgets/party_frame.h"
#include "engine/ui/widgets/guild_panel.h"
#include "engine/ui/widgets/npc_dialogue_panel.h"
#include "engine/ui/widgets/shop_panel.h"
#include "engine/ui/widgets/bank_panel.h"
#include "engine/ui/widgets/teleporter_panel.h"
#include "engine/ui/widgets/confirm_dialog.h"
#include "game/shared/npc_types.h"
#include "game/shared/item_stat_roller.h"
#include "engine/vfx/skill_vfx_player.h"
#include "game/shared/client_skill_cache.h"
#include "game/components/spawn_point_component.h"
#include "game/components/faction_component.h"
#ifndef FATE_SHIPPING
#include "imgui.h"
#endif
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include "stb_image_write.h"
namespace fs = std::filesystem;  // std::min, std::max (used with parenthesized calls to avoid Windows macro conflict)

namespace fate {

// File-local helper: maps PKStatus to nameplate Color (mirrors GameplaySystem::pkStatusColor)
static Color pkStatusColor(PKStatus status) {
    switch (status) {
        case PKStatus::White:  return Color::white();
        case PKStatus::Purple: return {0.659f, 0.333f, 0.969f};
        case PKStatus::Red:    return Color::red();
        case PKStatus::Black:  return {0.2f, 0.2f, 0.2f};
        default:               return Color::white();
    }
}

// ============================================================================
// Procedural art generation — pixel-art sprites & tileset
// ============================================================================

// --- Pixel helpers -----------------------------------------------------------

static void setPixel(std::vector<unsigned char>& px, int x, int y, int sz,
                     unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
    if (x < 0 || x >= sz || y < 0 || y >= sz) return;
    int i = (y * sz + x) * 4;
    px[i] = r; px[i+1] = g; px[i+2] = b; px[i+3] = a;
}

// setPixel for arbitrary-stride buffers (width != height)
static void setPixelW(std::vector<unsigned char>& px, int x, int y, int w, int h,
                      unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    int i = (y * w + x) * 4;
    px[i] = r; px[i+1] = g; px[i+2] = b; px[i+3] = a;
}

static void generateTileIfMissing(const std::string& path, int size,
    std::function<void(std::vector<unsigned char>&, int)> generator) {
    if (fs::exists(path)) return;
    std::vector<unsigned char> pixels(size * size * 4, 0);
    generator(pixels, size);
    stbi_write_png(path.c_str(), size, size, 4, pixels.data(), size * 4);
}

// Simple hash for deterministic pseudo-random noise
static inline int pixelHash(int x, int y, int seed = 0) {
    int h = x * 374761393 + y * 668265263 + seed * 1274126177;
    h = (h ^ (h >> 13)) * 1274126177;
    return h ^ (h >> 16);
}

// Clamp int to 0..255
static inline unsigned char clampByte(int v) {
    return (unsigned char)((v < 0) ? 0 : (v > 255) ? 255 : v);
}

// --- Procedural tileset (256x160 = 8 cols x 5 rows of 32x32 tiles) ----------

static void generateProceduralTileset() {
    const std::string path = "assets/tiles/procedural_tileset.png";
    if (fs::exists(path)) return;
    fs::create_directories("assets/tiles");

    const int TILE = 32;
    const int COLS = 8;
    const int ROWS = 5;
    const int W = COLS * TILE;   // 256
    const int H = ROWS * TILE;   // 160
    std::vector<unsigned char> img(W * H * 4, 0);

    auto set = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
        setPixelW(img, x, y, W, H, r, g, b, a);
    };

    auto drawTile = [&](int col, int row, auto fn) {
        int ox = col * TILE, oy = row * TILE;
        for (int ly = 0; ly < TILE; ly++)
            for (int lx = 0; lx < TILE; lx++)
                fn(ox + lx, oy + ly, lx, ly);
    };

    // ---- Row 0: Grass variations ----

    // 0,0 : Plain grass
    drawTile(0, 0, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 1);
        int n = (h % 21) - 10;
        // Warm green base with subtle variation
        int r = 56 + n/2, g = 118 + n, b = 40 + n/3;
        // Subtle horizontal grass blade pattern
        if ((h & 7) == 0) { g += 12; r -= 4; }
        set(gx, gy, clampByte(r), clampByte(g), clampByte(b));
    });

    // 1,0 : Grass with flowers
    drawTile(1, 0, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 2);
        int n = (h % 21) - 10;
        int r = 52 + n/2, g = 112 + n, b = 38 + n/3;
        if ((h & 7) == 0) { g += 10; }
        set(gx, gy, clampByte(r), clampByte(g), clampByte(b));
        // Scatter flower pixels
        int fh = pixelHash(lx, ly, 200);
        if ((fh % 37) == 0) {
            unsigned char fr, fg, fb;
            int colorSel = (fh >> 8) % 4;
            if      (colorSel == 0) { fr=240; fg=70;  fb=70;  }  // red
            else if (colorSel == 1) { fr=255; fg=210; fb=50;  }  // yellow
            else if (colorSel == 2) { fr=200; fg=100; fb=240; }  // purple
            else                    { fr=255; fg=160; fb=200; }  // pink
            set(gx, gy, fr, fg, fb);
            // Petals around center
            if (lx > 0) set(gx-1, gy, clampByte(fr-20), clampByte(fg-20), clampByte(fb-20));
            if (lx < 31) set(gx+1, gy, clampByte(fr-20), clampByte(fg-20), clampByte(fb-20));
        }
    });

    // 2,0 : Tall grass
    drawTile(2, 0, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 3);
        int n = (h % 21) - 10;
        int r = 48 + n/2, g = 105 + n, b = 35 + n/3;
        // Vertical blade highlights every few pixels
        if ((pixelHash(lx, 0, 30) % 5) == 0 && ly > 8 && ly < 28) {
            int bladeH = pixelHash(lx, 0, 31) % 8 + 10;
            if (ly > (TILE - bladeH)) { g += 20; r -= 5; }
        }
        // Darker at bottom
        if (ly > 24) { r -= 8; g -= 12; b -= 6; }
        set(gx, gy, clampByte(r), clampByte(g), clampByte(b));
    });

    // 3,0 : Dark grass (forest floor)
    drawTile(3, 0, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 4);
        int n = (h % 17) - 8;
        int r = 30 + n/2, g = 68 + n, b = 28 + n/3;
        // Occasional dark leaf litter
        if ((h % 19) == 0) { r += 15; g -= 10; b -= 5; }
        set(gx, gy, clampByte(r), clampByte(g), clampByte(b));
    });

    // ---- Row 1: Dirt/path variations ----

    // 0,1 : Dirt
    drawTile(0, 1, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 10);
        int n = (h % 21) - 10;
        int r = 142 + n, g = 102 + n*3/4, b = 62 + n/2;
        // Small pebble detail
        if ((h % 29) == 0) { r += 20; g += 18; b += 15; }
        set(gx, gy, clampByte(r), clampByte(g), clampByte(b));
    });

    // 1,1 : Dirt path center
    drawTile(1, 1, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 11);
        int n = (h % 17) - 8;
        int r = 168 + n, g = 138 + n*3/4, b = 98 + n/2;
        // Smoother center: less noise
        set(gx, gy, clampByte(r), clampByte(g), clampByte(b));
    });

    // 2,1 : Dirt path edge
    drawTile(2, 1, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 12);
        int n = (h % 19) - 9;
        // Left half is dirt-path, right half transitions to grass
        if (lx < 20) {
            int r = 160 + n, g = 130 + n*3/4, b = 90 + n/2;
            set(gx, gy, clampByte(r), clampByte(g), clampByte(b));
        } else {
            // Dithered transition
            float t = (float)(lx - 20) / 12.0f;
            int h2 = pixelHash(lx, ly, 120);
            bool useGrass = ((float)(h2 % 100) / 100.0f) < t;
            if (useGrass) {
                int r = 52 + n/2, g = 112 + n, b = 38 + n/3;
                set(gx, gy, clampByte(r), clampByte(g), clampByte(b));
            } else {
                int r = 155 + n, g = 125 + n*3/4, b = 85 + n/2;
                set(gx, gy, clampByte(r), clampByte(g), clampByte(b));
            }
        }
    });

    // 3,1 : Gravel
    drawTile(3, 1, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 13);
        int n = (h % 31) - 15;
        int base = 140 + n;
        int r = base + 5, g = base + 3, b = base;
        // Occasional stone speck
        if ((h % 11) == 0) { r += 25; g += 23; b += 20; }
        if ((h % 13) == 0) { r -= 20; g -= 18; b -= 15; }
        set(gx, gy, clampByte(r), clampByte(g), clampByte(b));
    });

    // ---- Row 2: Water variations ----

    // 0,2 : Deep water
    drawTile(0, 2, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 20);
        int n = (h % 15) - 7;
        int r = 18 + n/2, g = 45 + n, b = 140 + n;
        // Subtle wave highlight
        int wave = (int)(4.0f * sinf((float)(lx + ly * 2) * 0.3f));
        g += wave; b += wave * 2;
        set(gx, gy, clampByte(r), clampByte(g), clampByte(b), 230);
    });

    // 1,2 : Shallow water
    drawTile(1, 2, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 21);
        int n = (h % 15) - 7;
        int r = 40 + n, g = 100 + n, b = 175 + n;
        int wave = (int)(3.0f * sinf((float)(lx * 2 + ly) * 0.25f));
        g += wave; b += wave;
        set(gx, gy, clampByte(r), clampByte(g), clampByte(b), 210);
    });

    // 2,2 : Water edge (water on left, sand on right)
    drawTile(2, 2, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 22);
        int n = (h % 15) - 7;
        // Wavy edge line
        int edgeX = 16 + (int)(3.0f * sinf((float)ly * 0.6f));
        if (lx < edgeX - 2) {
            // Water
            int r = 35 + n, g = 85 + n, b = 165 + n;
            set(gx, gy, clampByte(r), clampByte(g), clampByte(b), 215);
        } else if (lx < edgeX + 2) {
            // Foam/surf line
            set(gx, gy, clampByte(210 + n), clampByte(225 + n), clampByte(235 + n), 240);
        } else {
            // Sand
            int r = 215 + n, g = 195 + n*3/4, b = 150 + n/2;
            set(gx, gy, clampByte(r), clampByte(g), clampByte(b));
        }
    });

    // 3,2 : Sand/beach
    drawTile(3, 2, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 23);
        int n = (h % 17) - 8;
        int r = 220 + n, g = 200 + n*3/4, b = 155 + n/2;
        // Occasional shell/pebble
        if ((h % 41) == 0) { r -= 30; g -= 25; b -= 10; }
        set(gx, gy, clampByte(r), clampByte(g), clampByte(b));
    });

    // ---- Row 3: Stone/dungeon ----

    // 0,3 : Stone floor
    drawTile(0, 3, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 30);
        int n = (h % 17) - 8;
        int base = 135 + n;
        // Grid mortar lines every 8px
        bool mortarH = (ly % 8 == 0);
        bool mortarV = (lx % 8 == 0);
        // Offset every other row for brick pattern
        bool mortarV2 = ((ly / 8) % 2 == 1) ? ((lx + 4) % 8 == 0) : mortarV;
        if (mortarH || mortarV2) {
            set(gx, gy, clampByte(base - 30), clampByte(base - 28), clampByte(base - 25));
        } else {
            set(gx, gy, clampByte(base), clampByte(base - 2), clampByte(base + 2));
        }
    });

    // 1,3 : Stone wall top
    drawTile(1, 3, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 31);
        int n = (h % 15) - 7;
        // Brick pattern
        int brickH = 6, brickW = 10;
        int rowIdx = ly / brickH;
        int offset = (rowIdx % 2) * (brickW / 2);
        bool mortarH = (ly % brickH == 0);
        bool mortarV = ((lx + offset) % brickW == 0);
        if (mortarH || mortarV) {
            set(gx, gy, clampByte(90 + n), clampByte(85 + n), clampByte(80 + n));
        } else {
            // Darker at bottom for depth
            int shade = (int)(20.0f * (1.0f - (float)ly / TILE));
            set(gx, gy, clampByte(155 + n + shade), clampByte(150 + n + shade), clampByte(145 + n + shade));
        }
    });

    // 2,3 : Cobblestone
    drawTile(2, 3, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 32);
        int n = (h % 21) - 10;
        // Irregular cobble pattern using distance to nearest grid point
        float cx = (float)((lx + 3) / 7) * 7.0f + 3.0f;
        float cy = (float)((ly + 3) / 7) * 7.0f + 3.0f;
        float dx = lx - cx, dy = ly - cy;
        float d = dx*dx + dy*dy;
        if (d > 8.0f) {
            // Mortar gap
            set(gx, gy, clampByte(100 + n/2), clampByte(95 + n/2), clampByte(90 + n/2));
        } else {
            // Stone surface with per-cobble color variation
            int cobbleN = pixelHash((int)cx, (int)cy, 320) % 20 - 10;
            set(gx, gy, clampByte(150 + n + cobbleN), clampByte(148 + n + cobbleN), clampByte(142 + n + cobbleN));
        }
    });

    // 3,3 : Dark stone
    drawTile(3, 3, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 33);
        int n = (h % 15) - 7;
        int base = 70 + n;
        bool mortarH = (ly % 8 == 0);
        int offset = ((ly / 8) % 2) * 4;
        bool mortarV = ((lx + offset) % 8 == 0);
        if (mortarH || mortarV) {
            set(gx, gy, clampByte(base - 20), clampByte(base - 18), clampByte(base - 15));
        } else {
            set(gx, gy, clampByte(base), clampByte(base + 2), clampByte(base + 5));
        }
    });

    // ---- Row 4: Decorative ----

    // 0,4 : Grass-dirt transition (top grass, bottom dirt, dithered)
    drawTile(0, 4, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 40);
        int n = (h % 17) - 8;
        float t = (float)ly / (float)TILE;
        // Dithered transition around the middle
        float threshold = 0.45f + 0.12f * sinf((float)lx * 0.5f);
        int dither = pixelHash(lx, ly, 400);
        bool useDirt = (t + (float)(dither % 10 - 5) * 0.015f) > threshold;
        if (useDirt) {
            int r = 140 + n, g = 100 + n*3/4, b = 60 + n/2;
            set(gx, gy, clampByte(r), clampByte(g), clampByte(b));
        } else {
            int r = 52 + n/2, g = 112 + n, b = 38 + n/3;
            set(gx, gy, clampByte(r), clampByte(g), clampByte(b));
        }
    });

    // 1,4 : Cliff edge (top surface, steep face with shadow)
    drawTile(1, 4, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 41);
        int n = (h % 15) - 7;
        if (ly < 10) {
            // Top grass surface
            int r = 52 + n/2, g = 110 + n, b = 38 + n/3;
            set(gx, gy, clampByte(r), clampByte(g), clampByte(b));
        } else if (ly < 14) {
            // Cliff lip - dark edge
            set(gx, gy, clampByte(80 + n), clampByte(70 + n), clampByte(55 + n));
        } else {
            // Rock face with vertical streaks
            int streak = pixelHash(lx, 0, 410) % 12 - 6;
            int shade = (int)(15.0f * (float)(ly - 14) / 18.0f); // darker lower
            int base = 120 + n + streak - shade;
            set(gx, gy, clampByte(base + 5), clampByte(base + 2), clampByte(base));
        }
    });

    // 2,4 : Wooden planks
    drawTile(2, 4, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 42);
        int n = (h % 13) - 6;
        int plankIdx = ly / 6;
        bool gap = (ly % 6 == 0);
        int plankColor = pixelHash(plankIdx, 0, 420) % 16 - 8;
        if (gap) {
            set(gx, gy, clampByte(65 + n), clampByte(42 + n), clampByte(22 + n));
        } else {
            // Wood grain: horizontal streaks
            int grain = pixelHash(lx, plankIdx, 421) % 8 - 4;
            int r = 155 + n + plankColor + grain;
            int g = 110 + n*3/4 + plankColor + grain;
            int b = 60 + n/2 + plankColor/2;
            // Knot detail
            if ((pixelHash(lx, ly, 422) % 97) == 0) { r -= 25; g -= 20; b -= 10; }
            set(gx, gy, clampByte(r), clampByte(g), clampByte(b));
        }
    });

    // 3,4 : Carpet/rug (red with border pattern)
    drawTile(3, 4, [&](int gx, int gy, int lx, int ly) {
        int h = pixelHash(lx, ly, 43);
        int n = (h % 9) - 4;
        bool border = (lx < 3 || lx > 28 || ly < 3 || ly > 28);
        bool innerBorder = (lx >= 3 && lx <= 5) || (lx >= 26 && lx <= 28) ||
                           (ly >= 3 && ly <= 5) || (ly >= 26 && ly <= 28);
        if (border) {
            // Fringe
            set(gx, gy, clampByte(180 + n), clampByte(150 + n), clampByte(50 + n));
        } else if (innerBorder) {
            // Gold border stripe
            set(gx, gy, clampByte(200 + n), clampByte(170 + n), clampByte(40 + n));
        } else {
            // Red carpet body
            set(gx, gy, clampByte(160 + n), clampByte(35 + n), clampByte(30 + n));
        }
    });

    // Fill remaining columns (4-7) in each row with slight variations of column 0
    for (int row = 0; row < ROWS; row++) {
        for (int col = 4; col < COLS; col++) {
            int srcCol = col - 4;
            int srcOx = srcCol * TILE, srcOy = row * TILE;
            int dstOx = col * TILE, dstOy = row * TILE;
            for (int ly = 0; ly < TILE; ly++) {
                for (int lx = 0; lx < TILE; lx++) {
                    int si = ((srcOy + ly) * W + (srcOx + lx)) * 4;
                    int varN = pixelHash(lx, ly, 500 + row * 8 + col) % 11 - 5;
                    setPixelW(img, dstOx + lx, dstOy + ly, W, H,
                        clampByte(img[si] + varN),
                        clampByte(img[si+1] + varN),
                        clampByte(img[si+2] + varN),
                        img[si+3]);
                }
            }
        }
    }

    stbi_write_png(path.c_str(), W, H, 4, img.data(), W * 4);
    LOG_INFO("Game", "Generated procedural tileset: %s (%dx%d, %d tiles)", path.c_str(), W, H, COLS * ROWS);
}

// --- Test player sprite sheet (128x99 = 4 cols x 3 rows of 32x33 cells) -----
// Rows: 0=walk_down, 1=walk_up, 2=walk_side. 4 frames each with leg swing.
static void generatePlayerSpriteSheet() {
    const std::string path = "assets/sprites/player_sheet.png";
    if (fs::exists(path)) return;
    fs::create_directories("assets/sprites");

    const int CW = 32, CH = 33; // cell size (matches existing player 20x33 centered in 32x33)
    const int COLS = 4, ROWS = 3;
    const int W = CW * COLS, H = CH * ROWS;
    std::vector<unsigned char> pixels(W * H * 4, 0);

    auto sp = [&](int cx, int cy, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
        int px = cx * CW + x;
        int py = cy * CH + y;
        if (px < 0 || px >= W || py < 0 || py >= H) return;
        int i = (py * W + px) * 4;
        pixels[i] = r; pixels[i+1] = g; pixels[i+2] = b; pixels[i+3] = a;
    };

    // Draw a simple chibi character in each cell with slight variations per frame
    auto drawChar = [&](int cx, int cy, int frame, int dir) {
        // dir: 0=down, 1=up, 2=side
        int ox = 6; // x offset to center 20px char in 32px cell

        // Hair (brown)
        for (int y = 1; y <= 4; y++)
            for (int x = 0; x <= 7; x++) {
                int n = pixelHash(x, y, 700+frame) % 5 - 2;
                sp(cx, cy, ox+x, y, clampByte(60+n), clampByte(35+n), clampByte(20+n));
            }
        // Hair outline
        for (int x = 0; x <= 7; x++) sp(cx, cy, ox+x, 0, 20, 20, 25);

        // Head / face (skin)
        for (int y = 4; y <= 10; y++)
            for (int x = 1; x <= 6; x++)
                sp(cx, cy, ox+x, y, 230, 190, 150);

        // Eyes - vary by direction
        if (dir == 0) { // down - two eyes
            sp(cx, cy, ox+2, 7, 40, 40, 50); sp(cx, cy, ox+5, 7, 40, 40, 50);
            sp(cx, cy, ox+3, 9, 180, 120, 100); sp(cx, cy, ox+4, 9, 180, 120, 100); // mouth
        } else if (dir == 1) { // up - no features
            sp(cx, cy, ox+3, 6, 55, 32, 18); sp(cx, cy, ox+4, 6, 55, 32, 18); // hair poking
        } else { // side - one eye
            sp(cx, cy, ox+4, 7, 40, 40, 50);
            sp(cx, cy, ox+4, 9, 180, 120, 100); // mouth
        }

        // Body / tunic (blue)
        for (int y = 11; y <= 22; y++)
            for (int x = -1; x <= 8; x++) {
                int n = pixelHash(x, y, 702+frame) % 5 - 2;
                sp(cx, cy, ox+x, y, clampByte(50+n), clampByte(80+n), clampByte(170+n));
            }
        // Belt
        for (int x = -1; x <= 8; x++)
            sp(cx, cy, ox+x, 20, 120, 85, 40);
        sp(cx, cy, ox+4, 20, 200, 180, 60); // buckle

        // Arms (skin)
        for (int y = 12; y <= 19; y++) {
            sp(cx, cy, ox-2, y, 230, 190, 150);
            sp(cx, cy, ox+9, y, 230, 190, 150);
        }

        // Legs with walk animation - offset varies per frame
        int legOffsets[] = {0, 2, 0, -2}; // walk cycle
        int lo = legOffsets[frame % 4];
        for (int y = 23; y <= 30; y++) {
            // Left leg
            sp(cx, cy, ox+1+lo, y, 80, 65, 50);
            sp(cx, cy, ox+2+lo, y, 80, 65, 50);
            // Right leg
            sp(cx, cy, ox+5-lo, y, 80, 65, 50);
            sp(cx, cy, ox+6-lo, y, 80, 65, 50);
        }
        // Boots
        for (int x = 0; x <= 2; x++) {
            sp(cx, cy, ox+1+lo+x, 31, 50, 35, 25);
            sp(cx, cy, ox+5-lo+x, 31, 50, 35, 25);
        }

        // Outline the whole character
        for (int y = 0; y <= 32; y++)
            for (int x = -3; x <= 12; x++) {
                int px = cx * CW + ox + x;
                int py = cy * CH + y;
                if (px < 0 || px >= W || py < 0 || py >= H) continue;
                int i = (py * W + px) * 4;
                if (pixels[i+3] == 0) continue; // skip transparent
                // Check if any neighbor is transparent
                bool edge = false;
                for (int dy = -1; dy <= 1 && !edge; dy++)
                    for (int dx = -1; dx <= 1 && !edge; dx++) {
                        int nx = px+dx, ny = py+dy;
                        if (nx < 0 || nx >= W || ny < 0 || ny >= H) { edge = true; continue; }
                        int ni = (ny * W + nx) * 4;
                        if (pixels[ni+3] == 0) edge = true;
                    }
                // Don't overwrite — outline is just for context
            }
    };

    // Row 0: walk_down (4 frames)
    for (int f = 0; f < 4; f++) drawChar(f, 0, f, 0);
    // Row 1: walk_up (4 frames)
    for (int f = 0; f < 4; f++) drawChar(f, 1, f, 1);
    // Row 2: walk_side (4 frames)
    for (int f = 0; f < 4; f++) drawChar(f, 2, f, 2);

    stbi_write_png(path.c_str(), W, H, 4, pixels.data(), W * 4);
    LOG_INFO("Game", "Generated test player sprite sheet: %s (%dx%d, %d frames)",
             path.c_str(), W, H, COLS * ROWS);
}

static void generateVillageTiles() {
    fs::create_directories("assets/tiles");
    fs::create_directories("assets/sprites");

    // Generate test player sprite sheet for animation editor workflow
    generatePlayerSpriteSheet();

    // Generate the consolidated procedural tileset PNG
    generateProceduralTileset();

    // --- Ground tiles ---

    generateTileIfMissing("assets/tiles/dirt.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 7 + y * 13 + x * y) % 20) - 10;
            setPixel(px, x, y, sz, 140+n, 100+n, 60+n);
        }
    });

    generateTileIfMissing("assets/tiles/dirt_path.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 5 + y * 11) % 14) - 7;
            setPixel(px, x, y, sz, 160+n, 130+n, 90+n);
        }
    });

    generateTileIfMissing("assets/tiles/stone_floor.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 3 + y * 7) % 16) - 8;
            // Grid pattern for stone blocks
            bool edge = (x % 16 == 0) || (y % 16 == 0);
            if (edge) setPixel(px, x, y, sz, 80+n, 80+n, 85+n);
            else setPixel(px, x, y, sz, 130+n, 130+n, 135+n);
        }
    });

    generateTileIfMissing("assets/tiles/water.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 3 + y * 5 + x * y / 4) % 24) - 12;
            setPixel(px, x, y, sz, 20+n, 60+n, 160+n, 220);
        }
    });

    generateTileIfMissing("assets/tiles/sand.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 11 + y * 7) % 18) - 9;
            setPixel(px, x, y, sz, 210+n, 190+n, 140+n);
        }
    });

    generateTileIfMissing("assets/tiles/grass_dark.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 7 + y * 13) % 20) - 10;
            setPixel(px, x, y, sz, 20+n, 65+n, 20+n);
        }
    });

    generateTileIfMissing("assets/tiles/flowers.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 7 + y * 13) % 20) - 10;
            setPixel(px, x, y, sz, 30+n, 80+n, 30+n); // grass base
        }
        // Scatter flower dots
        int flowerSeeds[][2] = {{5,5},{12,3},{22,7},{8,14},{18,12},{27,4},
                                 {3,22},{15,20},{25,18},{10,27},{20,25},{28,28}};
        unsigned char colors[][3] = {{255,80,80},{255,200,50},{200,80,255},{255,150,200}};
        for (auto& f : flowerSeeds) {
            auto& c = colors[(f[0]+f[1]) % 4];
            setPixel(px, f[0], f[1], sz, c[0], c[1], c[2]);
            setPixel(px, f[0]+1, f[1], sz, c[0], c[1], c[2]);
            setPixel(px, f[0], f[1]+1, sz, c[0], c[1], c[2]);
        }
    });

    // --- Building tiles ---

    generateTileIfMissing("assets/tiles/wood_wall.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 3 + y * 17) % 16) - 8;
            bool plank = (y % 8 < 7);
            if (plank) setPixel(px, x, y, sz, 140+n, 95+n, 50+n);
            else setPixel(px, x, y, sz, 90, 60, 30); // gap between planks
        }
    });

    generateTileIfMissing("assets/tiles/stone_wall.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 7 + y * 3) % 14) - 7;
            int brickX = x % 16; int brickY = y % 8;
            bool mortar = (brickX == 0) || (brickY == 0);
            // Offset every other row
            if ((y / 8) % 2 == 1 && brickX == 8) mortar = true;
            if (mortar) setPixel(px, x, y, sz, 120+n, 115+n, 110+n);
            else setPixel(px, x, y, sz, 160+n, 155+n, 150+n);
        }
    });

    generateTileIfMissing("assets/tiles/roof_red.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 5 + y * 9) % 16) - 8;
            bool shingle = (y % 6 < 5);
            if (shingle) setPixel(px, x, y, sz, 165+n, 50+n, 35+n);
            else setPixel(px, x, y, sz, 120, 35, 25);
        }
    });

    generateTileIfMissing("assets/tiles/roof_blue.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 5 + y * 9) % 16) - 8;
            bool shingle = (y % 6 < 5);
            if (shingle) setPixel(px, x, y, sz, 40+n, 60+n, 140+n);
            else setPixel(px, x, y, sz, 30, 40, 100);
        }
    });

    // --- Objects (sprites) ---

    generateTileIfMissing("assets/tiles/door.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            if (x >= 8 && x <= 23 && y >= 2 && y <= 29) {
                int n = ((x * 3 + y * 11) % 12) - 6;
                setPixel(px, x, y, sz, 110+n, 70+n, 35+n);
                // Door frame
                if (x == 8 || x == 23 || y == 2) setPixel(px, x, y, sz, 70, 45, 20);
                // Handle
                if (x == 20 && y >= 14 && y <= 16) setPixel(px, x, y, sz, 200, 180, 50);
            }
        }
    });

    generateTileIfMissing("assets/tiles/window.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            if (x >= 6 && x <= 25 && y >= 6 && y <= 25) {
                // Glass
                setPixel(px, x, y, sz, 150, 200, 230, 180);
                // Frame
                if (x == 6 || x == 25 || y == 6 || y == 25 || x == 15 || y == 15)
                    setPixel(px, x, y, sz, 100, 70, 40);
            }
        }
    });

    generateTileIfMissing("assets/tiles/fence_h.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            // Horizontal rails
            bool rail = (y >= 10 && y <= 12) || (y >= 20 && y <= 22);
            // Posts
            bool post = (x >= 0 && x <= 3) || (x >= 28 && x <= 31);
            if ((rail || (post && y >= 6 && y <= 26))) {
                int n = ((x+y) % 6) - 3;
                setPixel(px, x, y, sz, 150+n, 110+n, 60+n);
            }
        }
    });

    generateTileIfMissing("assets/tiles/rock.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            float dx = x - 16.0f, dy = y - 18.0f;
            if (dx*dx/(14*14) + dy*dy/(12*12) < 1.0f) {
                int n = ((x * 7 + y * 11) % 20) - 10;
                // Lighter on top, darker on bottom
                int shade = (int)(40.0f * (1.0f - (float)y / sz));
                setPixel(px, x, y, sz, 120+n+shade, 115+n+shade, 110+n+shade);
            }
        }
    });

    generateTileIfMissing("assets/tiles/bush.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            float dx = x - 16.0f, dy = y - 18.0f;
            if (dx*dx/(13*13) + dy*dy/(10*10) < 1.0f) {
                int n = ((x * 5 + y * 9) % 24) - 12;
                setPixel(px, x, y, sz, 25+n, 90+n, 25+n);
            }
        }
    });

    generateTileIfMissing("assets/tiles/well.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            float dx = x - 16.0f, dy = y - 16.0f;
            float dist = dx*dx + dy*dy;
            if (dist < 144 && dist > 81) { // Stone ring
                int n = ((x * 3 + y * 7) % 10) - 5;
                setPixel(px, x, y, sz, 140+n, 135+n, 130+n);
            } else if (dist <= 81) { // Water inside
                int n = ((x * 5 + y * 3) % 16) - 8;
                setPixel(px, x, y, sz, 20+n, 50+n, 130+n);
            }
        }
    });

    generateTileIfMissing("assets/tiles/barrel.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            // Barrel shape (rounded rectangle)
            float dx = x - 16.0f;
            float bulge = 1.0f + 0.15f * (1.0f - ((y-16.0f)*(y-16.0f))/(14*14));
            if (std::abs(dx) < 10 * bulge && y >= 4 && y <= 28) {
                int n = ((x * 3 + y * 11) % 14) - 7;
                setPixel(px, x, y, sz, 130+n, 85+n, 40+n);
                // Metal bands
                if (y == 8 || y == 24) setPixel(px, x, y, sz, 80, 80, 90);
            }
        }
    });

    generateTileIfMissing("assets/tiles/crate.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            if (x >= 4 && x <= 27 && y >= 4 && y <= 27) {
                int n = ((x * 7 + y * 3) % 12) - 6;
                setPixel(px, x, y, sz, 160+n, 120+n, 60+n);
                // Cross braces
                if (x == 4 || x == 27 || y == 4 || y == 27)
                    setPixel(px, x, y, sz, 100+n, 70+n, 30+n);
                if (std::abs(x - y) < 2 || std::abs(x - (31-y)) < 2)
                    setPixel(px, x, y, sz, 120+n, 80+n, 35+n);
            }
        }
    });

    generateTileIfMissing("assets/tiles/sign.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            // Post
            if (x >= 14 && x <= 17 && y >= 16 && y <= 30) {
                setPixel(px, x, y, sz, 100, 70, 35);
            }
            // Sign board
            if (x >= 4 && x <= 27 && y >= 4 && y <= 16) {
                int n = ((x+y) % 6) - 3;
                setPixel(px, x, y, sz, 170+n, 140+n, 80+n);
                if (x == 4 || x == 27 || y == 4 || y == 16)
                    setPixel(px, x, y, sz, 100, 70, 35);
            }
        }
    });

    generateTileIfMissing("assets/tiles/npc.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            float dx = x - 16.0f;
            // Head (circle)
            float headDy = y - 8.0f;
            if (dx*dx + headDy*headDy < 25) {
                setPixel(px, x, y, sz, 230, 190, 150); // skin
                // Eyes
                if ((x == 14 || x == 18) && y == 7) setPixel(px, x, y, sz, 40, 40, 40);
            }
            // Body
            if (x >= 10 && x <= 21 && y >= 13 && y <= 24) {
                setPixel(px, x, y, sz, 60, 100, 160); // blue shirt
            }
            // Legs
            if ((x >= 11 && x <= 14 && y >= 25 && y <= 30) ||
                (x >= 17 && x <= 20 && y >= 25 && y <= 30)) {
                setPixel(px, x, y, sz, 80, 60, 40); // brown pants
            }
        }
    });

    generateTileIfMissing("assets/tiles/torch.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            // Stick
            if (x >= 14 && x <= 17 && y >= 12 && y <= 28) {
                setPixel(px, x, y, sz, 100, 70, 35);
            }
            // Flame
            float dx = x - 16.0f, dy = y - 8.0f;
            float dist = dx*dx + dy*dy;
            if (dist < 20) setPixel(px, x, y, sz, 255, 220, 50);
            else if (dist < 36) setPixel(px, x, y, sz, 255, 140, 20, 200);
        }
    });

    LOG_INFO("Game", "Village tileset generated (%d tiles)", 20);
}

void GameApp::onInit() {
    LOG_INFO("Game", "FateMMO Game Engine initializing...");

    // Register all component types with the reflection/meta registry
    fate::registerAllComponents();

    // Generate village tileset (only creates files that don't exist yet)
    generateVillageTiles();

#ifndef FATE_SHIPPING
    // Set up editor and prefab library
    Editor::instance().setAssetRoot("assets");
#ifdef FATE_SOURCE_DIR
    Editor::instance().setSourceDir(FATE_SOURCE_DIR "/assets/scenes");
    PrefabLibrary::instance().setSourceDirectory(FATE_SOURCE_DIR "/assets/prefabs");
#endif
#else
#ifdef FATE_SOURCE_DIR
    PrefabLibrary::instance().setSourceDirectory(FATE_SOURCE_DIR "/assets/prefabs");
#endif
#endif
    PrefabLibrary::instance().setDirectory("assets/prefabs");
    PrefabLibrary::instance().loadAll();

    // Register fallback scene factory (used if no saved scene exists)
    SceneManager::instance().registerScene("TestScene", [this](Scene& scene) {
        if (!fs::exists("assets/scenes/WhisperingWoods.json")) {
            createPlayer(scene.world());
            createTestEntities(scene.world());
            spawnTestMobs(scene.world());
            spawnTestNPCs(scene.world());
        }
    });

    SceneManager::instance().switchScene("TestScene");

    // Add systems (these operate on whatever entities are in the scene)
    auto* scene = SceneManager::instance().currentScene();
    if (scene) {
        World& world = scene->world();

        movementSystem_ = world.addSystem<MovementSystem>();
        world.addSystem<AnimationSystem>();

        auto* cameraFollow = world.addSystem<CameraFollowSystem>();
        cameraFollow->camera = &camera();

        gameplaySystem_ = world.addSystem<GameplaySystem>();
        mobAISystem_ = world.addSystem<MobAISystem>();

        npcInteractionSystem_ = world.addSystem<NPCInteractionSystem>();
        npcInteractionSystem_->camera = &camera();
        npcInteractionSystem_->uiManager_ = &uiManager();

        combatSystem_ = world.addSystem<CombatActionSystem>();
        combatSystem_->camera = &camera();
        combatSystem_->onSendAttack = [this](Entity* target) {
            if (!netClient_.isConnected() || !target) return;
            // Reverse lookup: find PersistentId for this entity by comparing entity handles.
            // Entity* pointer comparison can fail if getEntity(EntityId) and getEntity(EntityHandle)
            // resolve through different paths, so compare handles directly.
            EntityHandle targetHandle = target->handle();
            for (const auto& [pid, handle] : ghostEntities_) {
                if (handle == targetHandle) {
                    combatPredictions_.addPrediction(pid, netTime_);
                    netClient_.sendAction(0, pid, 0);
                    return;
                }
            }
            LOG_WARN("Combat", "onSendAttack: target '%s' (id=%u) not found in %zu ghosts",
                     target->name().c_str(), target->id(), ghostEntities_.size());
        };
        combatSystem_->onPlaySFX = [this](const std::string& id) {
            audioManager_.playSFX(id);
        };

        questSystem_ = world.addSystem<QuestSystem>();

        zoneSystem_ = world.addSystem<ZoneSystem>();
        zoneSystem_->camera = &camera();
        zoneSystem_->onSceneTransition = [this](const std::string& scene, Vec2 /*spawnPos*/) {
            // Send zone transition request to server for validation (level gate, etc.)
            // Server determines spawn position from portal data
            netClient_.sendZoneTransition(scene);
        };

        // SpawnSystem is server-only — client receives mobs via replication
        world.addSystem<ParticleSystem>();

        renderSystem_ = new SpriteRenderSystem();
        renderSystem_->batch = &spriteBatch();
        renderSystem_->camera = &camera();
        renderSystem_->init(&world);
    }

    // Net client callbacks for ghost entity management
    netClient_.onEntityEnter = [this](const SvEntityEnterMsg& msg) {
        // Buffer enters during pending zone transition — the old scene hasn't
        // been destroyed yet, so creating ghosts now would either duplicate
        // existing ghosts or create entities that get wiped when the scene loads.
        // Buffered messages are replayed after the new scene finishes loading.
        if (pendingZoneTransition_) {
            pendingEntityEnters_.push_back(msg);
            return;
        }

        // Skip our own player — the local entity is already created by GameApp.
        // The server broadcasts SvEntityEnter for all players including ourselves.
        if (msg.entityType == 0 && msg.name == pendingCharName_) {
            localPlayerPid_ = msg.persistentId;
            return;
        }

        // Guard against duplicate entity enter (prevents ghost leak)
        if (ghostEntities_.count(msg.persistentId)) {
            LOG_WARN("Net", "Duplicate SvEntityEnter for PID %llu, ignoring", msg.persistentId);
            return;
        }
        auto* sc = SceneManager::instance().currentScene();
        if (!sc) return;
        auto& world = sc->world();
        Entity* ghost = nullptr;
        // Log ALL entity enters with position to find (0,0) source
        LOG_INFO("Net", "EntityEnter: type=%d pid=%llu name='%s' pos=(%.1f,%.1f)",
                 msg.entityType, msg.persistentId, msg.name.c_str(),
                 msg.position.x, msg.position.y);
        if (msg.entityType == 0) { // player
            ghost = EntityFactory::createGhostPlayer(world, msg.name, msg.position);
        } else if (msg.entityType == 3) { // dropped item
            ghost = EntityFactory::createGhostDroppedItem(world, msg.name, msg.position,
                msg.isGold != 0, msg.rarity);
        } else if (msg.entityType == 1) { // mob
            ghost = EntityFactory::createGhostMob(world, msg.name, msg.position,
                msg.mobDefId, msg.level, msg.currentHP, msg.maxHP, msg.isBoss != 0);
        } else { // npc or unknown
            ghost = EntityFactory::createGhostMob(world, msg.name, msg.position);
        }
        if (ghost) {
            ghostEntities_[msg.persistentId] = ghost->handle();
            if (msg.entityType == 3) droppedItemPids_.insert(msg.persistentId);

            // Auto-load animation metadata from .meta.json
            auto* ghostSprite = ghost->getComponent<SpriteComponent>();
            auto* ghostAnimator = ghost->getComponent<Animator>();
            if (ghostSprite && ghostAnimator) {
                AnimationLoader::tryAutoLoad(*ghostSprite, *ghostAnimator);
            }

            // Apply PK status name color for remote players on enter
            if (msg.entityType == 0) {
                auto* nameplate = ghost->getComponent<NameplateComponent>();
                if (nameplate) {
                    nameplate->nameColor = pkStatusColor(static_cast<PKStatus>(msg.pkStatus));
                }
            }
            // Seed interpolation buffer with initial position so ghosts don't
            // snap to (0,0) before the first SvEntityUpdate arrives.
            ghostInterpolation_.onEntityUpdate(msg.persistentId, msg.position);
        }
    };

    netClient_.onEntityLeave = [this](const SvEntityLeaveMsg& msg) {
        auto it = ghostEntities_.find(msg.persistentId);
        if (it != ghostEntities_.end()) {
            auto* sc = SceneManager::instance().currentScene();
            if (sc) {
                sc->world().destroyEntity(it->second);
            }
            ghostEntities_.erase(it);
            ghostUpdateSeqs_.erase(msg.persistentId);
            ghostDeathTimers_.erase(msg.persistentId);
            droppedItemPids_.erase(msg.persistentId);
            ghostInterpolation_.removeEntity(msg.persistentId);
        }
    };

    netClient_.onEntityUpdate = [this](const SvEntityUpdateMsg& msg) {
        // Reject stale updates via sequence counter (wrapping comparison)
        // First update for any entity is accepted unconditionally (no entry yet).
        auto seqIt = ghostUpdateSeqs_.find(msg.persistentId);
        if (seqIt != ghostUpdateSeqs_.end()) {
            int8_t diff = static_cast<int8_t>(msg.updateSeq - seqIt->second);
            if (diff <= 0) return; // stale or duplicate, discard
        }
        ghostUpdateSeqs_[msg.persistentId] = msg.updateSeq;

        auto it = ghostEntities_.find(msg.persistentId);
        if (it == ghostEntities_.end()) return;

        if (msg.fieldMask & 0x01) { // position — feed interpolation buffer
            ghostInterpolation_.onEntityUpdate(msg.persistentId, msg.position);
        }

        auto* sc = SceneManager::instance().currentScene();
        if (!sc) return;
        Entity* ghost = sc->world().getEntity(it->second);
        if (!ghost) return;

        if (msg.fieldMask & 0x02) { // animFrame
            auto* s = ghost->getComponent<SpriteComponent>();
            if (s) s->currentFrame = msg.animFrame;
        }
        if (msg.fieldMask & 0x04) { // flipX
            auto* s = ghost->getComponent<SpriteComponent>();
            if (s) s->flipX = (msg.flipX != 0);
        }
        // Sync mob/ghost HP from server
        if (msg.fieldMask & 0x08) { // bit 3 = currentHP
            auto* es = ghost->getComponent<EnemyStatsComponent>();
            if (es) {
                es->stats.currentHP = msg.currentHP;
                es->stats.isAlive = msg.currentHP > 0;
                // Hide sprite when mob dies
                if (!es->stats.isAlive) {
                    auto* spr = ghost->getComponent<SpriteComponent>();
                    if (spr) spr->enabled = false;
                }
            }
        }
        if (msg.fieldMask & (1 << 14)) { // bit 14 = pkStatus
            auto* nameplate = ghost->getComponent<NameplateComponent>();
            if (nameplate) {
                nameplate->nameColor = pkStatusColor(static_cast<PKStatus>(msg.pkStatus));
            }
        }

        // moveState (bit 5) — set ghost walking state
        if (msg.fieldMask & (1 << 5)) {
            auto* pc = ghost->getComponent<PlayerController>();
            if (pc) {
                pc->isMoving = (msg.moveState == static_cast<uint8_t>(MoveState::Walking));
            }
        }

        // animId (bit 6) — set ghost animation direction + type
        if (msg.fieldMask & (1 << 6)) {
            auto* anim = ghost->getComponent<Animator>();
            auto* pc = ghost->getComponent<PlayerController>();
            auto* spr = ghost->getComponent<SpriteComponent>();
            if (anim && pc) {
                uint8_t animDir, animType;
                decodeAnimId(msg.animId, animDir, animType);

                // Map animDir back to Direction for PlayerController
                if (animDir == 0) pc->facing = Direction::Down;
                else if (animDir == 1) pc->facing = Direction::Up;
                else if (animDir == 2) {
                    // side — use flipX to determine left vs right
                    if (spr) pc->facing = spr->flipX ? Direction::Left : Direction::Right;
                    else pc->facing = Direction::Right;
                }

                // Map animType to animation name
                static const char* typeNames[] = {"idle", "walk", "attack", "cast"};
                static const char* dirNames[]  = {"_down", "_up", "_side"};
                if (animType == 4) {
                    // death (special)
                    anim->currentAnimation = "death";
                } else if (animType < 4 && animDir < 3) {
                    anim->currentAnimation = std::string(typeNames[animType]) + dirNames[animDir];
                }
            }
        }

        // targetEntityId (bit 10) — store on ghost for display
        if (msg.fieldMask & (1 << 10)) {
            auto* tgt = ghost->getComponent<TargetingComponent>();
            if (tgt) {
                tgt->selectedTargetId = msg.targetEntityId;
            }
        }

        // equipVisuals (bit 13) — unpack and store on ghost
        if (msg.fieldMask & (1 << 13)) {
            auto* ev = ghost->getComponent<EquipVisualsComponent>();
            if (ev) {
                unpackEquipVisuals(msg.equipVisuals,
                    ev->weaponVisualIdx, ev->armorVisualIdx, ev->hatVisualIdx);
            }
        }
    };

    netClient_.onCombatEvent = [this](const SvCombatEventMsg& msg) {
#ifndef FATE_SHIPPING
        if (Editor::instance().isPaused()) return; // Don't process combat while paused
#endif
        if (!combatSystem_) return;
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;

        // Check if attacker is the local player (not in ghostEntities_ = us)
        // Ghost entities are OTHER players/mobs; the local player is never a ghost.
        bool isLocalAttack = (ghostEntities_.find(msg.attackerId) == ghostEntities_.end());

        // Resolve optimistic prediction — animation already playing, server confirmed
        if (isLocalAttack) {
            combatPredictions_.resolveOldest();
        }

        // Only log combat events that deal damage or kill (skip mob-miss spam)
        if (msg.damage > 0 || msg.isKill) {
            LOG_DEBUG("CombatDbg", "SvCombatEvent: attacker=%llu target=%llu dmg=%d kill=%d isLocal=%d ghosts=%zu",
                      msg.attackerId, msg.targetId, msg.damage, (int)msg.isKill,
                      (int)isLocalAttack, ghostEntities_.size());
        }

        // Find target entity position for floating text
        Vec2 targetPos{0, 0};
        bool foundTarget = false;

        // Check ghost entities (replicated remote players/mobs from server)
        auto it = ghostEntities_.find(msg.targetId);
        if (it != ghostEntities_.end()) {
            Entity* ghost = scene->world().getEntity(it->second);
            if (ghost) {
                auto* t = ghost->getComponent<Transform>();
                if (t) { targetPos = t->position; foundTarget = true; }

                // On kill: set HP to 0 immediately. The server destroys the
                // mob entity before the next SvEntityUpdate (HP=0) can be sent,
                // so we must apply the final HP here from the combat event.
                if (msg.isKill) {
                    auto* es = ghost->getComponent<EnemyStatsComponent>();
                    if (es) {
                        es->stats.currentHP = 0;
                        es->stats.isAlive = false;
                    }
                    // Start 3-second corpse timer — sprite hidden after delay
                    ghostDeathTimers_[msg.targetId] = netTime_;
                }
            }
        }

        // If target not found in ghosts AND attacker IS a ghost (mob→player),
        // apply damage prediction to local player. Never apply when isLocalAttack
        // — that would mean our own attack damage hits us if the target ghost is missing.
        if (!foundTarget && !isLocalAttack) {
            scene->world().forEach<PlayerController, CharacterStatsComponent>(
                [&](Entity* entity, PlayerController* ctrl, CharacterStatsComponent* sc) {
                    if (!ctrl->isLocalPlayer || foundTarget) return;
                    auto* t = entity->getComponent<Transform>();
                    if (t) targetPos = t->position;
                    foundTarget = true;

                    // Update local HP prediction for HUD display only.
                    // Death is server-authoritative — only SvDeathNotifyMsg triggers die().
                    if (msg.damage > 0 && sc->stats.isAlive()) {
                        int before = sc->stats.currentHP;
                        sc->stats.currentHP = (std::max)(0, sc->stats.currentHP - msg.damage);
                        LOG_INFO("CombatDbg", "HP prediction: %d -> %d (mob %llu hit us for %d)",
                                 before, sc->stats.currentHP, msg.attackerId, msg.damage);
                    }
                }
            );
        } else if (!foundTarget && isLocalAttack) {
            // Our attack target wasn't found in ghosts — stale/missing ghost entity.
            // Don't apply damage to ourselves. Just log and skip.
            LOG_WARN("CombatDbg", "OUR attack target %llu not in ghosts (dmg=%d) — dropped",
                     msg.targetId, msg.damage);
            return;
        }

        if (!foundTarget) return;

        // Show floating damage text from server-authoritative damage.
        // For local attacks: prediction miss/resist text already spawned;
        // only show server damage (skip 0-damage to avoid double miss text).
        if (isLocalAttack) {
            if (msg.damage > 0) {
                combatSystem_->showDamageText(targetPos, msg.damage, msg.isCrit != 0);
            }
        } else {
            if (msg.damage == 0) {
                combatSystem_->showMissText(targetPos);
            } else {
                combatSystem_->showDamageText(targetPos, msg.damage, msg.isCrit != 0);
            }
        }

        // Process kill: clear target, play death effects
        if (msg.isKill && isLocalAttack) {
            if (combatSystem_) {
                combatSystem_->serverClearTarget();
            }
            LOG_INFO("Combat", "Target killed by server");
        }

        // Audio feedback — local attack audio now plays on the animation hit frame
        // (CombatActionSystem), so only play kill SFX here for local attacks.
        if (isLocalAttack) {
            if (msg.isKill) audioManager_.playSFX("kill");
        } else if (foundTarget) {
            auto& cam = camera();
            audioManager_.playSFXSpatial(
                msg.damage > 0 ? "hit_melee" : "miss",
                targetPos.x, targetPos.y,
                cam.position().x, cam.position().y);
        }
    };

    netClient_.onPlayerState = [this](const SvPlayerStateMsg& msg) {
        // Always store the latest state for zone transition recovery and pending apply
        pendingPlayerState_ = msg;

        // If the local player hasn't been created yet (first frame after connect),
        // mark pending and apply it after the player entity is created.
        if (!localPlayerCreated_) {
            hasPendingPlayerState_ = true;
            return;
        }
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;
        scene->world().forEach<CharacterStatsComponent, PlayerController>(
            [&](Entity* entity, CharacterStatsComponent* stats, PlayerController* ctrl) {
                if (!ctrl->isLocalPlayer) return;
                // Set level and recalculate FIRST, then override with server values.
                // recalculateStats() overwrites maxHP/maxMP, so server values must come after.
                if (stats->stats.level != msg.level) {
                    stats->stats.level = msg.level;
                    stats->stats.recalculateStats();
                    stats->stats.recalculateXPRequirement();
                }
                stats->stats.currentHP = msg.currentHP;
                stats->stats.maxHP = msg.maxHP;
                stats->stats.currentMP = msg.currentMP;
                stats->stats.maxMP = msg.maxMP;
                stats->stats.currentFury = msg.currentFury;
                stats->stats.currentXP = msg.currentXP;
                stats->stats.honor = msg.honor;
                stats->stats.pvpKills = msg.pvpKills;
                stats->stats.pvpDeaths = msg.pvpDeaths;

                // Derived stats — server-authoritative snapshot
                stats->stats.applyServerSnapshot(
                    msg.armor, msg.magicResist, msg.critRate,
                    msg.hitRate, msg.evasion, msg.speed, msg.damageMult);

                // Gold — server-authoritative, set directly (not additive)
                auto* inv = entity->getComponent<InventoryComponent>();
                if (inv) inv->inventory.setGold(msg.gold);
            }
        );
    };

    netClient_.onMovementCorrection = [this](const SvMovementCorrectionMsg& msg) {
        if (!msg.rubberBand) return;
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;
        scene->world().forEach<Transform, PlayerController>(
            [&](Entity*, Transform* t, PlayerController* ctrl) {
                if (!ctrl->isLocalPlayer) return;
                t->position = msg.correctedPosition;
                LOG_WARN("Net", "Rubber-banded to (%.0f, %.0f)",
                         msg.correctedPosition.x, msg.correctedPosition.y);
            }
        );
    };

    netClient_.onChatMessage = [this](const SvChatMessageMsg& msg) {
        if (chatPanel_) {
            chatPanel_->addMessage(msg.channel, msg.senderName, msg.message, msg.faction);
        } else {
            pendingChatMessages_.push_back({msg.channel, msg.senderName, msg.message, msg.faction});
        }
    };

    netClient_.onLootPickup = [this](const SvLootPickupMsg& msg) {
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;

        scene->world().forEach<PlayerController, InventoryComponent>(
            [&](Entity*, PlayerController* ctrl, InventoryComponent* invComp) {
                if (!ctrl->isLocalPlayer) return;

                if (msg.isGold) {
                    invComp->inventory.addGold(msg.goldAmount);
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "Picked up %d gold.", msg.goldAmount);
                    if (chatPanel_) chatPanel_->addMessage(6, "[Loot]", buf, static_cast<uint8_t>(0));
                    audioManager_.playSFX("loot_gold");
                } else {
                    static int lootCounter = 0;
                    char instId[32];
                    std::snprintf(instId, sizeof(instId), "loot_%d", ++lootCounter);

                    ItemInstance item = ItemInstance::createSimple(instId, msg.itemId, msg.quantity);
                    item.displayName = msg.displayName;
                    item.rarity = parseItemRarity(msg.rarity);
                    int usedSlots = 0;
                    for (const auto& s : invComp->inventory.getSlots())
                        if (s.isValid()) ++usedSlots;
                    LOG_INFO("Client", "LootPickup: item='%s' qty=%d valid=%d slots=%d/%d",
                             msg.itemId.c_str(), msg.quantity, item.isValid() ? 1 : 0,
                             usedSlots, (int)invComp->inventory.getSlots().size());
                    if (invComp->inventory.addItem(item)) {
                        char buf[128];
                        std::snprintf(buf, sizeof(buf), "Picked up %s x%d.",
                                      msg.displayName.c_str(), msg.quantity);
                        if (chatPanel_) chatPanel_->addMessage(6, "[Loot]", buf, static_cast<uint8_t>(0));
                        audioManager_.playSFX("loot_item");
                    } else {
                        LOG_WARN("Client", "LootPickup FAILED: addItem returned false for '%s'", msg.itemId.c_str());
                        if (chatPanel_) chatPanel_->addMessage(6, "[Loot]", "Inventory full!", static_cast<uint8_t>(0));
                    }
                }
            }
        );
    };

    netClient_.onTradeUpdate = [this](const SvTradeUpdateMsg& msg) {
        std::string text;
        switch (msg.updateType) {
            case 0: text = msg.otherPlayerName + " invited you to trade."; break;
            case 1: text = "Trade session started."; break;
            case 5: text = "Trade completed!"; break;
            case 6: text = msg.otherPlayerName.empty() ? "Trade cancelled." : msg.otherPlayerName; break;
            default: text = "Trade update."; break;
        }
        if (chatPanel_) chatPanel_->addMessage(6, "[Trade]", text, static_cast<uint8_t>(0));
    };

    netClient_.onMarketResult = [this](const SvMarketResultMsg& msg) {
        if (chatPanel_) chatPanel_->addMessage(6, "[Market]", msg.message, static_cast<uint8_t>(0));
        else pendingChatMessages_.push_back({6, "[Market]", msg.message, 0});
    };

    netClient_.onBountyUpdate = [this](const SvBountyUpdateMsg& msg) {
        if (chatPanel_) chatPanel_->addMessage(6, "[Bounty]", msg.message, static_cast<uint8_t>(0));
        else pendingChatMessages_.push_back({6, "[Bounty]", msg.message, 0});
    };

    netClient_.onGauntletUpdate = [this](const SvGauntletUpdateMsg& msg) {
        if (chatPanel_) chatPanel_->addMessage(6, "[Gauntlet]", msg.message, static_cast<uint8_t>(0));
        else pendingChatMessages_.push_back({6, "[Gauntlet]", msg.message, 0});
    };

    netClient_.onGuildUpdate = [this](const SvGuildUpdateMsg& msg) {
        std::string text = msg.message;
        if (!msg.guildName.empty()) text = "[" + msg.guildName + "] " + text;
        if (chatPanel_) chatPanel_->addMessage(6, "[Guild]", text, static_cast<uint8_t>(0));
        else pendingChatMessages_.push_back({6, "[Guild]", text, 0});
    };

    netClient_.onSocialUpdate = [this](const SvSocialUpdateMsg& msg) {
        if (chatPanel_) chatPanel_->addMessage(6, "[Social]", msg.message, static_cast<uint8_t>(0));
        else pendingChatMessages_.push_back({6, "[Social]", msg.message, 0});
    };

    netClient_.onQuestUpdate = [this](const SvQuestUpdateMsg& msg) {
        if (!localPlayerCreated_) { pendingQuestUpdates_.push_back(msg); return; }
        if (chatPanel_) chatPanel_->addMessage(6, "[Quest]", msg.message, static_cast<uint8_t>(0));

        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;

        scene->world().forEach<PlayerController, QuestComponent>(
            [&](Entity* entity, PlayerController* ctrl, QuestComponent* qc) {
                if (!ctrl->isLocalPlayer) return;

                uint32_t questId = 0;
                try { questId = static_cast<uint32_t>(std::stoul(msg.questId)); }
                catch (const std::exception& e) {
                    LOG_WARN("GameApp", "Invalid quest id '%s': %s", msg.questId.c_str(), e.what());
                    return;
                }

                switch (msg.updateType) {
                    case 0: { // accepted
                        auto* sc = entity->getComponent<CharacterStatsComponent>();
                        int level = sc ? sc->stats.level : 1;
                        qc->quests.acceptQuest(questId, level);
                        break;
                    }
                    case 1: // progressUpdate
                        qc->quests.setProgress(questId, msg.currentCount, msg.targetCount);
                        break;
                    case 2: // completed
                        qc->quests.markCompleted(questId);
                        break;
                    case 3: // abandoned
                        qc->quests.abandonQuest(questId);
                        break;
                    default: break;
                }
            }
        );
    };

    // --- State sync handlers (sent by server on connect) ---

    netClient_.onSkillSync = [this](const SvSkillSyncMsg& msg) {
        if (!localPlayerCreated_) { hasPendingSkillSync_ = true; pendingSkillSync_ = msg; return; }
        auto* sc = SceneManager::instance().currentScene();
        if (!sc) return;
        sc->world().forEach<SkillManagerComponent, PlayerController>(
            [&](Entity*, SkillManagerComponent* sm, PlayerController* ctrl) {
                if (!ctrl->isLocalPlayer) return;
                // Rebuild skill state from server data using setSerializedState
                std::vector<LearnedSkill> skills;
                for (const auto& s : msg.skills) {
                    LearnedSkill ls;
                    ls.skillId = s.skillId;
                    ls.unlockedRank = s.unlockedRank;
                    ls.activatedRank = s.activatedRank;
                    skills.push_back(std::move(ls));
                }
                std::vector<std::string> bar;
                for (int i = 0; i < (int)msg.skillBar.size() && i < 20; ++i) {
                    bar.push_back(msg.skillBar[i]);
                }
                bar.resize(20); // pad to 20 slots
                sm->skills.setSerializedState(std::move(skills), std::move(bar),
                    sm->skills.availablePoints(), sm->skills.earnedPoints(), sm->skills.spentPoints());
            }
        );
    };

    netClient_.onQuestSync = [this](const SvQuestSyncMsg& msg) {
        if (!localPlayerCreated_) { hasPendingQuestSync_ = true; pendingQuestSync_ = msg; return; }
        auto* sc = SceneManager::instance().currentScene();
        if (!sc) return;
        sc->world().forEach<QuestComponent, PlayerController>(
            [&](Entity*, QuestComponent* qc, PlayerController* ctrl) {
                if (!ctrl->isLocalPlayer) return;
                std::vector<uint32_t> completed;
                std::vector<ActiveQuest> active;
                for (const auto& q : msg.quests) {
                    uint32_t qid = 0;
                    try { qid = static_cast<uint32_t>(std::stoul(q.questId)); }
                    catch (const std::exception& e) {
                        LOG_WARN("GameApp", "Invalid quest id '%s' in sync: %s", q.questId.c_str(), e.what());
                        continue;
                    }
                    if (q.state == 1 || q.state == 2) { // completed or failed
                        completed.push_back(qid);
                    } else { // active
                        ActiveQuest aq;
                        aq.questId = qid;
                        for (const auto& [cur, tgt] : q.objectives) {
                            aq.objectiveProgress.push_back(static_cast<uint16_t>(cur));
                        }
                        active.push_back(std::move(aq));
                    }
                }
                qc->quests.setSerializedState(std::move(completed), std::move(active));
            }
        );
    };

    netClient_.onInventorySync = [this](const SvInventorySyncMsg& msg) {
        LOG_INFO("Client", "onInventorySync: %zu slots, %zu equips, localPlayerCreated=%d",
                 msg.slots.size(), msg.equipment.size(), localPlayerCreated_ ? 1 : 0);
        if (!localPlayerCreated_) {
            hasPendingInventorySync_ = true;
            pendingInventorySync_ = msg;
            LOG_INFO("Client", "Buffered pending inventory sync (%zu slots)", msg.slots.size());
            return;
        }
        auto* sc = SceneManager::instance().currentScene();
        if (!sc) return;
        sc->world().forEach<InventoryComponent, PlayerController>(
            [&](Entity*, InventoryComponent* invComp, PlayerController* ctrl) {
                if (!ctrl->isLocalPlayer) return;
                // Build slots vector
                std::vector<ItemInstance> slots;
                for (const auto& s : msg.slots) {
                    if (s.slotIndex < 0) continue;
                    // Ensure vector is large enough
                    if (s.slotIndex >= (int)slots.size()) slots.resize(s.slotIndex + 1);
                    ItemInstance item;
                    item.instanceId = "sync_" + std::to_string(s.slotIndex);
                    item.itemId = s.itemId;
                    item.displayName = s.displayName;
                    item.rarity = parseItemRarity(s.rarity);
                    item.itemType = s.itemType;
                    item.quantity = s.quantity;
                    item.enchantLevel = s.enchantLevel;
                    item.levelReq = s.levelReq;
                    item.damageMin = s.damageMin;
                    item.damageMax = s.damageMax;
                    item.armorValue = s.armor;
                    if (!s.rolledStats.empty()) {
                        item.rolledStats = ItemStatRoller::parseRolledStats(s.rolledStats);
                    }
                    if (!s.socketStat.empty()) {
                        item.socket.statType = ItemStatRoller::getStatType(s.socketStat);
                        item.socket.value = s.socketValue;
                        item.socket.isEmpty = false;
                    }
                    slots[s.slotIndex] = std::move(item);
                }
                // Build equipment map
                std::unordered_map<EquipmentSlot, ItemInstance> equipment;
                for (const auto& e : msg.equipment) {
                    ItemInstance item;
                    item.instanceId = "eq_" + std::to_string(e.slot);
                    item.itemId = e.itemId;
                    item.displayName = e.displayName;
                    item.rarity = parseItemRarity(e.rarity);
                    item.itemType = e.itemType;
                    item.quantity = e.quantity;
                    item.enchantLevel = e.enchantLevel;
                    item.levelReq = e.levelReq;
                    item.damageMin = e.damageMin;
                    item.damageMax = e.damageMax;
                    item.armorValue = e.armor;
                    if (!e.rolledStats.empty()) {
                        item.rolledStats = ItemStatRoller::parseRolledStats(e.rolledStats);
                    }
                    if (!e.socketStat.empty()) {
                        item.socket.statType = ItemStatRoller::getStatType(e.socketStat);
                        item.socket.value = e.socketValue;
                        item.socket.isEmpty = false;
                    }
                    equipment[static_cast<EquipmentSlot>(e.slot)] = std::move(item);
                }
                invComp->inventory.setSerializedState(
                    invComp->inventory.getGold(), std::move(slots), std::move(equipment));
            }
        );
    };

    netClient_.onDeathNotify = [this](const SvDeathNotifyMsg& msg) {
#ifndef FATE_SHIPPING
        if (Editor::instance().isPaused()) return;
#endif
        // If player entity doesn't exist yet (first frame after connect),
        // store pending death and apply after player creation.
        if (!localPlayerCreated_) {
            hasPendingDeathNotify_ = true;
            return;
        }
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;

        scene->world().forEach<PlayerController, CharacterStatsComponent>(
            [&](Entity*, PlayerController* ctrl, CharacterStatsComponent* sc) {
                if (!ctrl->isLocalPlayer) return;
                sc->stats.lifeState = LifeState::Dead;
                sc->stats.isDead = true;
                sc->stats.currentHP = 0;
                sc->stats.respawnTimeRemaining = msg.respawnTimer;
            }
        );

        if (deathOverlay_) deathOverlay_->onDeath(msg.xpLost, msg.honorLost, msg.respawnTimer, msg.deathSource);
        // Show retained-mode death overlay
        if (auto* ds = uiManager().getScreen("death_overlay")) {
            ds->setVisible(true);
            // Aurora deaths: hide spawn-point and phoenix-down buttons (town only)
            bool isAurora = (msg.deathSource == 7);
            if (auto* btnSpawn = dynamic_cast<Button*>(ds->findById("btn_respawn_spawn")))
                btnSpawn->setVisible(!isAurora);
            if (auto* btnPhoenix = dynamic_cast<Button*>(ds->findById("btn_respawn_phoenix")))
                btnPhoenix->setVisible(!isAurora);
        }
        audioManager_.playSFX("death");
        LOG_INFO("Client", "You died! Lost %d XP, %d Honor (source=%u)", msg.xpLost, msg.honorLost, msg.deathSource);
    };

    netClient_.onSkillResult = [this](const SvSkillResultMsg& msg) {
        if (!combatSystem_) return;
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;

        // Resolve optimistic prediction — animation already playing, server confirmed
        combatPredictions_.resolveOldest();

        // Find target entity position for floating text
        Vec2 targetPos{0, 0};
        bool foundTarget = false;

        // Check ghost entities (replicated remote players/mobs from server)
        auto ghostIt = ghostEntities_.find(msg.targetId);
        if (ghostIt != ghostEntities_.end()) {
            Entity* ghost = scene->world().getEntity(ghostIt->second);
            if (ghost) {
                auto* t = ghost->getComponent<Transform>();
                if (t) { targetPos = t->position; foundTarget = true; }
            }
        }

        // If target not found in ghosts, check if it's the local player
        if (!foundTarget) {
            scene->world().forEach<PlayerController, Transform>(
                [&](Entity*, PlayerController* ctrl, Transform* t) {
                    if (!ctrl->isLocalPlayer || foundTarget) return;
                    targetPos = t->position;
                    foundTarget = true;
                }
            );
        }

        if (!foundTarget) return;

        // Trigger skill VFX if the skill has one defined
        const auto* skillDef = ClientSkillDefinitionCache::getSkill(msg.skillId);
        if (skillDef && !skillDef->vfxId.empty()) {
            // Find caster position (fall back to target pos if caster ghost not found)
            Vec2 casterPos = targetPos;
            auto casterGhost = ghostEntities_.find(msg.casterId);
            if (casterGhost != ghostEntities_.end()) {
                Entity* caster = scene->world().getEntity(casterGhost->second);
                if (caster) {
                    auto* ct = caster->getComponent<Transform>();
                    if (ct) casterPos = ct->position;
                }
            }
            SkillVFXPlayer::instance().play(skillDef->vfxId, casterPos, targetPos);
        }

        // Spawn appropriate floating text using hitFlags bitmask
        if (msg.hitFlags & HitFlags::MISS) {
            combatSystem_->showMissText(targetPos);
        } else if (msg.hitFlags & HitFlags::DODGE) {
            combatSystem_->showMissText(targetPos); // TODO: show "Dodge" text
        } else if (msg.hitFlags & HitFlags::BLOCKED) {
            combatSystem_->showMissText(targetPos); // TODO: show "Blocked" text
        } else if (msg.damage > 0) {
            combatSystem_->showDamageText(targetPos, msg.damage,
                                          (msg.hitFlags & HitFlags::CRIT) != 0);
        }

        // Audio feedback
        if (msg.hitFlags & HitFlags::MISS || msg.hitFlags & HitFlags::DODGE) {
            audioManager_.playSFX("miss");
        } else if (msg.hitFlags & HitFlags::CRIT) {
            audioManager_.playSFX("hit_crit");
        } else if (msg.damage > 0) {
            audioManager_.playSFX("hit_skill");
        }
        if (msg.hitFlags & HitFlags::KILLED) {
            audioManager_.playSFX("kill");
        }
    };

    netClient_.onRespawn = [this](const SvRespawnMsg& msg) {
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;

        scene->world().forEach<PlayerController, CharacterStatsComponent>(
            [&](Entity* entity, PlayerController* ctrl, CharacterStatsComponent* sc) {
                if (!ctrl->isLocalPlayer) return;
                sc->stats.respawn();
                // Restore visual
                auto* spr = entity->getComponent<SpriteComponent>();
                if (spr) spr->tint = Color::white();
                auto* t = entity->getComponent<Transform>();
                if (t) {
                    t->rotation = 0.0f;
                    t->position = {msg.spawnX, msg.spawnY};
                }
                auto* anim = entity->getComponent<Animator>();
                if (anim) anim->play("idle");
            }
        );

        if (deathOverlay_) deathOverlay_->respawnPending = false;
        // Hide retained-mode death overlay and restore all respawn buttons
        if (auto* ds = uiManager().getScreen("death_overlay")) {
            ds->setVisible(false);
            if (auto* btnSpawn = dynamic_cast<Button*>(ds->findById("btn_respawn_spawn")))
                btnSpawn->setVisible(true);
            if (auto* btnPhoenix = dynamic_cast<Button*>(ds->findById("btn_respawn_phoenix")))
                btnPhoenix->setVisible(true);
        }
        audioManager_.playSFX("respawn");
        LOG_INFO("Client", "Respawned at (%.0f, %.0f)", msg.spawnX, msg.spawnY);
    };

    // deathOverlay_->onRespawnRequested wired in retainedUILoaded_ block

    // --- Retained-mode NPC panel callbacks (wired after screen load in onUpdate) ---
    // Callbacks are wired once npcDialoguePanel_ etc. are resolved from the
    // npc_panels screen. See the retainedUILoaded_ block in onUpdate.

    // --- Skill bar activation callback ---
    // (Wired to skillArc_->onSkillActivated in retainedUILoaded_ block)

    netClient_.onZoneTransition = [this](const SvZoneTransitionMsg& msg) {
        LOG_INFO("Client", "Zone transition to '%s' at (%.1f, %.1f)",
                 msg.targetScene.c_str(), msg.spawnX, msg.spawnY);

        // Snapshot current player stats before the deferred transition destroys the entity.
        // This ensures level/HP/MP are preserved even if SvPlayerState hasn't arrived recently.
        captureLocalPlayerState();

        // Defer the actual scene load to after poll() completes. Loading a scene
        // destroys all entities in the world, which would crash any code that runs
        // later in the same poll() pass (entity updates, combat events, etc.).
        pendingZoneTransition_ = true;
        pendingZoneScene_ = msg.targetScene;
        pendingZoneSpawn_ = {msg.spawnX, msg.spawnY};
        pendingEntityEnters_.clear(); // clear any stale buffered enters

        // Start zone music (crossfades from current track)
        audioManager_.playMusic("assets/audio/music/" + msg.targetScene + ".ogg");
    };

    netClient_.onDungeonInvite = [this](const SvDungeonInviteMsg& msg) {
        LOG_INFO("Client", "Dungeon invite: '%s' (level %d, %ds)",
                 msg.dungeonName.c_str(), msg.levelReq, msg.timeLimitSeconds);

        if (dungeonInviteDialog_) {
            dungeonInviteDialog_->message = "Ready to start " + msg.dungeonName + " dungeon?";
            dungeonInviteDialog_->confirmText = "Accept";
            dungeonInviteDialog_->cancelText = "Decline";
            dungeonInviteDialog_->onConfirm = [this](const std::string&) {
                netClient_.sendDungeonResponse(1);
                dungeonInviteDialog_->setVisible(false);
            };
            dungeonInviteDialog_->onCancel = [this](const std::string&) {
                netClient_.sendDungeonResponse(0);
                dungeonInviteDialog_->setVisible(false);
            };
            dungeonInviteDialog_->setVisible(true);
        }
    };

    netClient_.onDungeonStart = [this](const SvDungeonStartMsg& msg) {
        LOG_INFO("Client", "Dungeon started: scene '%s', time limit %ds",
                 msg.sceneId.c_str(), msg.timeLimitSeconds);

        if (dungeonInviteDialog_) dungeonInviteDialog_->setVisible(false);
        inDungeon_ = true;

        if (chatPanel_) {
            chatPanel_->addMessage(0, "[System]", "Entering dungeon...", 0);
        }

        captureLocalPlayerState();
        pendingZoneTransition_ = true;
        pendingZoneScene_ = msg.sceneId;
        pendingZoneSpawn_ = {0.0f, 0.0f};
        pendingEntityEnters_.clear();
    };

    netClient_.onDungeonEnd = [this](const SvDungeonEndMsg& msg) {
        const char* reasons[] = {"Boss defeated", "Time expired", "Abandoned"};
        const char* reason = (msg.reason < 3) ? reasons[msg.reason] : "Unknown";
        LOG_INFO("Client", "Dungeon ended: reason=%s (%d)", reason, msg.reason);

        inDungeon_ = false;

        if (chatPanel_) {
            std::string chatMsg = std::string("Dungeon instance has ended (") + reason + ").";
            chatPanel_->addMessage(0, "[System]", chatMsg, 0);
        }
    };

    // Auth callbacks
    netClient_.onConnectRejected = [this](const std::string& reason) {
        LOG_WARN("GameApp", "Connection rejected: %s", reason.c_str());
        if (loginScreenWidget_) {
            loginScreenWidget_->setStatus("Connection rejected: " + reason, true);
            loginScreenWidget_->setVisible(true);
        }
        connState_ = ConnectionState::LoginScreen;
    };

    netClient_.onDisconnected = [this]() {
        if (connState_ == ConnectionState::InGame) {
            // Destroy ghost entities from the world (mobs, players, dropped items)
            auto* scene = SceneManager::instance().currentScene();
            if (scene) {
                for (auto& [pid, handle] : ghostEntities_) {
                    scene->world().destroyEntity(handle);
                }
                // Destroy local player entity
                scene->world().forEach<PlayerController>(
                    [&](Entity* e, PlayerController* ctrl) {
                        if (ctrl->isLocalPlayer) {
                            scene->world().destroyEntity(e->handle());
                        }
                    }
                );
                scene->world().processDestroyQueue();
            }
            ghostEntities_.clear();
            droppedItemPids_.clear();
            ghostDeathTimers_.clear();
            ghostUpdateSeqs_.clear();
            ghostInterpolation_.clear();
            combatPredictions_.clear();
            SkillVFXPlayer::instance().clear();
            connState_ = ConnectionState::LoginScreen;
            localPlayerCreated_ = false;
            retainedUILoaded_ = false;
            npcDialoguePanel_ = nullptr;
            shopPanel_ = nullptr;
            bankPanel_ = nullptr;
            teleporterPanel_ = nullptr;
            inDungeon_ = false;
            dungeonInviteDialog_ = nullptr;
            if (loginScreenWidget_) {
                loginScreenWidget_->reset();
                loginScreenWidget_->setVisible(true);
            }
            LOG_INFO("GameApp", "Disconnected, cleaned up %zu ghost entities", ghostEntities_.size());
        }
    };

    // When auto-reconnect starts (heartbeat timeout), clear stale ghost entities.
    // The server will re-send all entities on successful reconnect.
    netClient_.onReconnectStart = [this]() {
        auto* scene = SceneManager::instance().currentScene();
        if (scene) {
            for (auto& [pid, handle] : ghostEntities_) {
                scene->world().destroyEntity(handle);
            }
            scene->world().processDestroyQueue();
        }
        LOG_INFO("GameApp", "Auto-reconnect: cleared %zu ghost entities", ghostEntities_.size());
        ghostEntities_.clear();
        droppedItemPids_.clear();
        ghostDeathTimers_.clear();
        ghostUpdateSeqs_.clear();
        ghostInterpolation_.clear();
        combatPredictions_.clear();
        SkillVFXPlayer::instance().clear();
    };

    netClient_.onStatEnchantResult = [this](const SvStatEnchantResultMsg& msg) {
        // Show result in ChatPanel on the HUD
        if (chatPanel_) chatPanel_->addMessage(6, "[Enchant]", msg.message, static_cast<uint8_t>(0));

        // Play SFX
        if (msg.success) {
            audioManager_.playSFX("enchant_success");
        } else {
            audioManager_.playSFX("enchant_fail");
        }
    };

    // --- NPC panel network result handlers ---
    netClient_.onShopResult = [this](const SvShopResultMsg& msg) {
        if (shopPanel_ && shopPanel_->isOpen()) {
            if (msg.success) {
                shopPanel_->rebuild();
            } else {
                shopPanel_->errorMessage = msg.reason;
                shopPanel_->errorTimer = 3.0f;
            }
        }
    };

    netClient_.onBankResult = [this](const SvBankResultMsg& msg) {
        if (bankPanel_ && bankPanel_->isOpen()) {
            if (msg.success) {
                bankPanel_->rebuild();
            } else {
                bankPanel_->errorMessage = msg.message;
                bankPanel_->errorTimer = 3.0f;
            }
        }
    };

    netClient_.onTeleportResult = [this](const SvTeleportResultMsg& msg) {
        if (!msg.success) return;
        closeAllNpcPanels();
        if (msg.sceneId.empty()) return; // same-scene teleport, no transition needed

        LOG_INFO("Client", "Teleport to '%s' at (%.1f, %.1f)",
                 msg.sceneId.c_str(), msg.posX, msg.posY);

        // Snapshot current player stats before the deferred transition destroys the entity.
        auto* sc = SceneManager::instance().currentScene();
        if (sc) {
            sc->world().forEach<CharacterStatsComponent, PlayerController>(
                [this](Entity* e, CharacterStatsComponent* cs, PlayerController* ctrl) {
                    if (!ctrl->isLocalPlayer) return;
                    pendingPlayerState_.level = cs->stats.level;
                    pendingPlayerState_.currentHP = cs->stats.currentHP;
                    pendingPlayerState_.maxHP = cs->stats.maxHP;
                    pendingPlayerState_.currentMP = cs->stats.currentMP;
                    pendingPlayerState_.maxMP = cs->stats.maxMP;
                    pendingPlayerState_.currentFury = cs->stats.currentFury;
                    pendingPlayerState_.currentXP = cs->stats.currentXP;
                    pendingPlayerState_.honor = cs->stats.honor;
                    pendingPlayerState_.pvpKills = cs->stats.pvpKills;
                    pendingPlayerState_.pvpDeaths = cs->stats.pvpDeaths;
                    auto* inv = e->getComponent<InventoryComponent>();
                    if (inv) pendingPlayerState_.gold = inv->inventory.getGold();
                }
            );
        }

        pendingZoneTransition_ = true;
        pendingZoneScene_ = msg.sceneId;
        pendingZoneSpawn_ = {msg.posX, msg.posY};
        pendingEntityEnters_.clear();

        audioManager_.playMusic("assets/audio/music/" + msg.sceneId + ".ogg");
    };

    netClient_.onAuroraStatus = [this](const SvAuroraStatusMsg& msg) {
        Faction favored = static_cast<Faction>(msg.favoredFaction);
        auto* def = FactionRegistry::get(favored);
        std::string name = def ? def->displayName : "Unknown";
        LOG_INFO("Game", "Aurora status: %s favored, %u seconds remaining",
                 name.c_str(), msg.secondsRemaining);
    };

    // Initialize audio
    audioManager_.init();
    {
        std::string sfxDir = std::string(FATE_SOURCE_DIR) + "/assets/audio/sfx";
        int loaded = audioManager_.loadSFXDirectory(sfxDir);
        LOG_INFO("Audio", "Loaded %d SFX from %s", loaded, sfxDir.c_str());
    }

    // Initialize SDF text rendering
    SDFText::instance().init("assets/fonts/default.png", "assets/fonts/default.json");

    // Load skill VFX definitions
    SkillVFXPlayer::instance().loadDefinitions("assets/vfx/");

    // Try to load a tilemap (if present, replaces procedural ground)
    tilemap_ = std::make_unique<Tilemap>();
    if (tilemap_->loadFromFile("assets/maps/test_map.json")) {
        tilemap_->origin = {
            -(tilemap_->worldWidth() * 0.5f),
            -(tilemap_->worldHeight() * 0.5f)
        };
    } else {
        tilemap_.reset();
    }

    // Auto-load WhisperingWoods as the default scene
    if (fs::exists("assets/scenes/WhisperingWoods.json")) {
        auto* s = SceneManager::instance().currentScene();
        if (s) {
            Editor::instance().loadScene(&s->world(), "assets/scenes/WhisperingWoods.json");

            // Remove stale player entities baked into the scene JSON —
            // the real player is created on server connect.
            std::vector<EntityHandle> stale;
            s->world().forEach<PlayerController>([&](Entity* e, PlayerController*) {
                stale.push_back(e->handle());
            });
            for (auto h : stale) s->world().destroyEntity(h);

            LOG_INFO("Game", "Auto-loaded WhisperingWoods scene");
        }
    }

    // ========================================================================
    // Register game render passes with the render graph
    // ========================================================================
    auto& graph = renderGraph();

    // Pass: GroundTiles — tilemap rendering into Scene FBO
    graph.addPass({"GroundTiles", true, [this](RenderPassContext& ctx) {
        auto& sceneFbo = ctx.graph->getFBO("Scene", ctx.viewportWidth, ctx.viewportHeight, true);
        sceneFbo.bind();
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        if (tilemap_) {
            Mat4 vp = ctx.camera->getViewProjection();
            ctx.spriteBatch->begin(vp);
            tilemap_->render(*ctx.spriteBatch, *ctx.camera, -10.0f);
            ctx.spriteBatch->end();
        }

        sceneFbo.unbind();
    }});

    // Pass: Entities — sprite rendering (accumulates onto Scene FBO)
    graph.addPass({"Entities", true, [this](RenderPassContext& ctx) {
        auto& sceneFbo = ctx.graph->getFBO("Scene", ctx.viewportWidth, ctx.viewportHeight, true);
        sceneFbo.bind();

        if (renderSystem_ && !isLoading()) {
            renderSystem_->update(0.0f);
        }

        sceneFbo.unbind();
    }});

    // Pass: Particles — particle emitters (accumulates onto Scene FBO)
    graph.addPass({"Particles", true, [this](RenderPassContext& ctx) {
        auto& sceneFbo = ctx.graph->getFBO("Scene", ctx.viewportWidth, ctx.viewportHeight, true);
        sceneFbo.bind();

        if (ctx.world) {
            Mat4 vp = ctx.camera->getViewProjection();

            ctx.world->forEach<ParticleEmitterComponent, Transform>(
                [&](Entity*, ParticleEmitterComponent* emitterComp, Transform*) {
                    const auto& verts = emitterComp->emitter.vertices();
                    if (verts.empty()) return;

                    if (emitterComp->emitter.config().additiveBlend) {
                        ctx.spriteBatch->setBlendMode(BlendMode::Additive);
                    }

                    ctx.spriteBatch->begin(vp);
                    // Draw each particle quad as a colored rect
                    for (size_t i = 0; i + 3 < verts.size(); i += 4) {
                        const auto& v = verts[i];
                        Vec2 center = {
                            (verts[i].x + verts[i+2].x) * 0.5f,
                            (verts[i].y + verts[i+2].y) * 0.5f
                        };
                        float w = verts[i+1].x - verts[i].x;
                        float h = verts[i+2].y - verts[i+1].y;
                        if (w < 0) w = -w;
                        if (h < 0) h = -h;
                        Color c(v.r, v.g, v.b, v.a);
                        ctx.spriteBatch->drawRect(center, {w, h}, c, emitterComp->emitter.config().depth);
                    }
                    ctx.spriteBatch->end();

                    if (emitterComp->emitter.config().additiveBlend) {
                        ctx.spriteBatch->setBlendMode(BlendMode::Alpha);
                    }
                }
            );
        }

        sceneFbo.unbind();
    }});

    // Pass: SkillVFX — skill visual effects (accumulates onto Scene FBO)
    graph.addPass({"SkillVFX", true, [this](RenderPassContext& ctx) {
        auto& sceneFbo = ctx.graph->getFBO("Scene", ctx.viewportWidth, ctx.viewportHeight, true);
        sceneFbo.bind();
        Mat4 vp = ctx.camera->getViewProjection();
        ctx.spriteBatch->begin(vp);
        SkillVFXPlayer::instance().render(*ctx.spriteBatch, SDFText::instance());
        ctx.spriteBatch->end();
        sceneFbo.unbind();
    }});

    // Pass: SDFText — floating damage/XP text (accumulates onto Scene FBO)
    graph.addPass({"SDFText", true, [this](RenderPassContext& ctx) {
        auto& sceneFbo = ctx.graph->getFBO("Scene", ctx.viewportWidth, ctx.viewportHeight, true);
        sceneFbo.bind();

        if (combatSystem_) {
            combatSystem_->renderFloatingTexts(*ctx.spriteBatch, *ctx.camera, SDFText::instance());
        }

        sceneFbo.unbind();
    }});

    // Pass: DebugOverlays — collision debug, aggro radius, spawn zones (editor only)
    graph.addPass({"DebugOverlays", true, [this](RenderPassContext& ctx) {
        if (!Editor::instance().isPaused()) return;

        auto& sceneFbo = ctx.graph->getFBO("Scene", ctx.viewportWidth, ctx.viewportHeight, true);
        sceneFbo.bind();

        if (Editor::instance().showCollisionDebug()) {
            renderCollisionDebug(*ctx.spriteBatch, *ctx.camera);
        }
        renderAggroRadius(*ctx.spriteBatch, *ctx.camera);

        // SpawnSystem debug rendering removed — mobs are server-only

        sceneFbo.unbind();
    }});

    // Invalidate cached widget pointers when a screen is hot-reloaded
    uiManager().addScreenReloadListener([this](const std::string& screenId) {
        if (screenId == "fate_menu_panels") {
            inventoryPanel_ = nullptr;
            retainedUILoaded_ = false;
        } else if (screenId == "fate_hud") {
            skillArc_ = nullptr;
            dungeonInviteDialog_ = nullptr;
            retainedUILoaded_ = false;
        } else if (screenId == "fate_social") {
            chatPanel_ = nullptr;
            retainedUILoaded_ = false;
        } else if (screenId == "death_overlay") {
            deathOverlay_ = nullptr;
            retainedUILoaded_ = false;
        } else if (screenId == "npc_panels") {
            npcDialoguePanel_ = nullptr;
            shopPanel_ = nullptr;
            bankPanel_ = nullptr;
            teleporterPanel_ = nullptr;
            retainedUILoaded_ = false;
        } else if (screenId == "login") {
            loginScreenWidget_ = nullptr;
        }
    });

    // Load retained-mode login screen (visible immediately at startup)
    uiManager().loadScreen("assets/ui/screens/login.json");
    loginScreenWidget_ = dynamic_cast<LoginScreen*>(uiManager().getScreen("login"));
    if (loginScreenWidget_) {
        loginScreenWidget_->loadPreferences();

        loginScreenWidget_->onLogin = [this](const std::string& user, const std::string& pass,
                                              const std::string& server, int port) {
            authClient_.loginAsync(server, static_cast<uint16_t>(port), user, pass);
            connState_ = ConnectionState::Authenticating;
            loginScreenWidget_->setStatus("Authenticating...", false);
        };

        loginScreenWidget_->onRegister = [this](const std::string& user, const std::string& pass,
                                                  const std::string& email,
                                                  const std::string& server, int port) {
            pendingRegUser_ = user; pendingRegPass_ = pass;
            pendingRegEmail_ = email; pendingRegServer_ = server;
            pendingRegPort_ = port;
            // Show character creation, hide login
            loginScreenWidget_->setVisible(false);
            // Load character_creation screen if not already loaded
            if (!uiManager().getScreen("character_creation"))
                uiManager().loadScreen("assets/ui/screens/character_creation.json");
            auto* ccw = dynamic_cast<CharacterCreationScreen*>(uiManager().getScreen("character_creation"));
            if (ccw) {
                ccw->setVisible(true);
                // Wire registration-flow callbacks
                ccw->onNext = [this](const std::string&) {
                    auto* cc = dynamic_cast<CharacterCreationScreen*>(uiManager().getScreen("character_creation"));
                    if (!cc) return;
                    const char* classNames[] = {"Warrior", "Mage", "Archer"};
                    constexpr Faction factions[] = {Faction::Xyros, Faction::Fenor, Faction::Zethos, Faction::Solis};
                    pendingFaction_ = factions[cc->selectedFaction];
                    authClient_.registerAsync(
                        pendingRegServer_, static_cast<uint16_t>(pendingRegPort_),
                        pendingRegUser_, pendingRegPass_, pendingRegEmail_,
                        cc->characterName, classNames[cc->selectedClass]);
                    connState_ = ConnectionState::Authenticating;
                    cc->setVisible(false);
                    if (loginScreenWidget_) {
                        loginScreenWidget_->setVisible(true);
                        loginScreenWidget_->setStatus("Creating account...", false);
                    }
                };
                ccw->onBack = [this](const std::string&) {
                    auto* cc = dynamic_cast<CharacterCreationScreen*>(uiManager().getScreen("character_creation"));
                    if (cc) cc->setVisible(false);
                    if (loginScreenWidget_) loginScreenWidget_->setVisible(true);
                    connState_ = ConnectionState::LoginScreen;
                };
            }
            connState_ = ConnectionState::CharacterCreation;
        };
    }

    LOG_INFO("Game", "Initialized");
}

void GameApp::createPlayer(World& world) {
    Entity* player = EntityFactory::createPlayer(world, "Player", ClassType::Warrior, true, pendingFaction_);

    // Spawn player at origin so they start near the mobs
    auto* transform = player->getComponent<Transform>();
    if (transform) {
        transform->position = {0.0f, 0.0f};
    }

    // Create proper pixel-art player sprite if no texture was loaded by the factory
    auto* sprite = player->getComponent<SpriteComponent>();
    if (sprite && !sprite->texture) {
        std::string playerPath = "assets/sprites/player.png";
        if (!fs::exists(playerPath)) {
            // 20x33 character sprite — chibi warrior
            const int W = 20, H = 33;
            std::vector<unsigned char> pixels(W * H * 4, 0);
            auto sp = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
                setPixelW(pixels, x, y, W, H, r, g, b, a);
            };
            auto outline = [&](int x, int y) { sp(x, y, 20, 20, 25); };

            // -- Hair (rows 0-4) --
            for (int y = 1; y <= 4; y++)
                for (int x = 6; x <= 13; x++) {
                    int n = pixelHash(x, y, 600) % 7 - 3;
                    sp(x, y, clampByte(60+n), clampByte(35+n), clampByte(20+n)); // brown hair
                }
            // Hair outline
            for (int x = 6; x <= 13; x++) outline(x, 0);
            outline(5, 1); outline(5, 2); outline(5, 3);
            outline(14, 1); outline(14, 2); outline(14, 3);

            // -- Head / face (rows 4-10) --
            for (int y = 4; y <= 10; y++)
                for (int x = 7; x <= 12; x++) {
                    int n = pixelHash(x, y, 601) % 5 - 2;
                    sp(x, y, clampByte(230+n), clampByte(190+n), clampByte(150+n)); // skin
                }
            // Eyes
            sp(8, 7, 40, 40, 50); sp(9, 7, 40, 40, 50);
            sp(11, 7, 40, 40, 50); sp(12, 7, 40, 40, 50);
            // Mouth
            sp(9, 9, 180, 120, 100); sp(10, 9, 180, 120, 100); sp(11, 9, 180, 120, 100);
            // Face outline
            for (int y = 4; y <= 10; y++) { outline(6, y); outline(13, y); }
            for (int x = 7; x <= 12; x++) outline(x, 11);

            // -- Body / tunic (rows 11-22) --
            for (int y = 11; y <= 22; y++)
                for (int x = 5; x <= 14; x++) {
                    int n = pixelHash(x, y, 602) % 7 - 3;
                    // Blue tunic with lighter chest highlight
                    int highlight = (x >= 8 && x <= 11 && y >= 13 && y <= 17) ? 20 : 0;
                    sp(x, y, clampByte(50+n+highlight/2), clampByte(80+n+highlight), clampByte(170+n+highlight));
                }
            // Belt
            for (int x = 5; x <= 14; x++) {
                int n = pixelHash(x, 20, 603) % 5 - 2;
                sp(x, 20, clampByte(120+n), clampByte(85+n), clampByte(40+n)); // brown belt
            }
            sp(10, 20, 200, 180, 60); // belt buckle
            // Body outline
            for (int y = 11; y <= 22; y++) { outline(4, y); outline(15, y); }
            for (int x = 5; x <= 14; x++) outline(x, 22);

            // -- Arms (rows 12-20, sides of body) --
            for (int y = 12; y <= 19; y++) {
                int n = pixelHash(3, y, 604) % 5 - 2;
                sp(3, y, clampByte(230+n), clampByte(190+n), clampByte(150+n)); // skin left arm
                sp(4, y, clampByte(50+n), clampByte(80+n), clampByte(170+n)); // sleeve left
                sp(16, y, clampByte(230+n), clampByte(190+n), clampByte(150+n)); // skin right arm
                sp(15, y, clampByte(50+n), clampByte(80+n), clampByte(170+n)); // sleeve right
            }
            for (int y = 12; y <= 19; y++) { outline(2, y); outline(17, y); }
            outline(3, 20); outline(16, 20);

            // -- Sword (right side, rows 8-24) --
            for (int y = 8; y <= 22; y++) {
                sp(18, y, 180, 185, 195); // blade
                if (y >= 8 && y <= 10) sp(19, y, 160, 165, 175); // blade width
            }
            sp(18, 23, 120, 85, 40); sp(18, 24, 120, 85, 40); // hilt
            sp(17, 23, 180, 160, 50); sp(19, 23, 180, 160, 50); // crossguard

            // -- Legs / pants (rows 23-30) --
            for (int y = 23; y <= 30; y++) {
                for (int x = 6; x <= 9; x++) {
                    int n = pixelHash(x, y, 605) % 5 - 2;
                    sp(x, y, clampByte(65+n), clampByte(55+n), clampByte(45+n)); // dark pants left
                }
                for (int x = 11; x <= 14; x++) {
                    int n = pixelHash(x, y, 606) % 5 - 2;
                    sp(x, y, clampByte(65+n), clampByte(55+n), clampByte(45+n)); // dark pants right
                }
            }
            // Leg outline
            for (int y = 23; y <= 30; y++) {
                outline(5, y); outline(10, y);
                outline(10, y); outline(15, y);
            }

            // -- Boots (rows 30-32) --
            for (int y = 31; y <= 32; y++) {
                for (int x = 5; x <= 9; x++) sp(x, y, 80, 55, 30);
                for (int x = 11; x <= 15; x++) sp(x, y, 80, 55, 30);
            }
            for (int x = 5; x <= 9; x++) outline(x, (std::min)(32, H-1));
            for (int x = 11; x <= 15; x++) outline(x, (std::min)(32, H-1));

            fs::create_directories("assets/sprites");
            stbi_write_png(playerPath.c_str(), W, H, 4, pixels.data(), W * 4);
        }
        sprite->texture = TextureCache::instance().load(playerPath);
        sprite->texturePath = playerPath;
        if (sprite->texture) {
            sprite->size = {(float)sprite->texture->width(), (float)sprite->texture->height()};
        }
    }

    // Auto-load animation metadata from .meta.json
    auto* playerAnimator = player->getComponent<Animator>();
    if (sprite && playerAnimator) {
        AnimationLoader::tryAutoLoad(*sprite, *playerAnimator);
    }

    LOG_INFO("Game", "Player entity created at (0, 0)");
}

void GameApp::createTestEntities(World& world) {
    // ---- Generate improved grass tile variants ----
    std::string grassPath = "assets/sprites/grass_tile.png";
    std::string grassDarkPath = "assets/sprites/grass_dark_tile.png";
    std::string dirtPatchPath = "assets/sprites/dirt_patch_tile.png";

    auto grassTex = TextureCache::instance().load(grassPath);
    if (!grassTex) {
        const int SIZE = 32;
        std::vector<unsigned char> pixels(SIZE * SIZE * 4);
        for (int y = 0; y < SIZE; y++) {
            for (int x = 0; x < SIZE; x++) {
                int i = (y * SIZE + x) * 4;
                int h = pixelHash(x, y, 700);
                int n = (h % 21) - 10;
                // Warm green base
                int r = 55 + n/2, g = 115 + n, b = 38 + n/3;
                // Grass blade highlights
                if ((h & 7) == 0) { g += 15; r -= 3; }
                // Subtle darker tufts
                if ((h % 23) == 0) { r -= 8; g -= 12; b -= 5; }
                pixels[i+0] = clampByte(r);
                pixels[i+1] = clampByte(g);
                pixels[i+2] = clampByte(b);
                pixels[i+3] = 255;
            }
        }
        fs::create_directories("assets/sprites");
        stbi_write_png(grassPath.c_str(), SIZE, SIZE, 4, pixels.data(), SIZE * 4);
        grassTex = TextureCache::instance().load(grassPath);
    }

    auto grassDarkTex = TextureCache::instance().load(grassDarkPath);
    if (!grassDarkTex) {
        const int SIZE = 32;
        std::vector<unsigned char> pixels(SIZE * SIZE * 4);
        for (int y = 0; y < SIZE; y++) {
            for (int x = 0; x < SIZE; x++) {
                int i = (y * SIZE + x) * 4;
                int h = pixelHash(x, y, 701);
                int n = (h % 17) - 8;
                int r = 38 + n/2, g = 82 + n, b = 30 + n/3;
                if ((h & 11) == 0) { g += 10; }
                if ((h % 19) == 0) { r += 12; g -= 8; b -= 4; } // leaf litter
                pixels[i+0] = clampByte(r); pixels[i+1] = clampByte(g);
                pixels[i+2] = clampByte(b); pixels[i+3] = 255;
            }
        }
        stbi_write_png(grassDarkPath.c_str(), SIZE, SIZE, 4, pixels.data(), SIZE * 4);
        grassDarkTex = TextureCache::instance().load(grassDarkPath);
    }

    auto dirtTex = TextureCache::instance().load(dirtPatchPath);
    if (!dirtTex) {
        const int SIZE = 32;
        std::vector<unsigned char> pixels(SIZE * SIZE * 4);
        for (int y = 0; y < SIZE; y++) {
            for (int x = 0; x < SIZE; x++) {
                int i = (y * SIZE + x) * 4;
                int h = pixelHash(x, y, 702);
                int n = (h % 21) - 10;
                int r = 140 + n, g = 100 + n*3/4, b = 62 + n/2;
                if ((h % 29) == 0) { r += 18; g += 15; b += 12; } // pebble
                pixels[i+0] = clampByte(r); pixels[i+1] = clampByte(g);
                pixels[i+2] = clampByte(b); pixels[i+3] = 255;
            }
        }
        stbi_write_png(dirtPatchPath.c_str(), SIZE, SIZE, 4, pixels.data(), SIZE * 4);
        dirtTex = TextureCache::instance().load(dirtPatchPath);
    }

    // ---- Lay ground tiles: 48x32 grid centered on origin ----
    int tilesX = 48;
    int tilesY = 32;
    float tileSize = 32.0f;
    float half = tileSize * 0.5f;
    int halfX = tilesX / 2;
    int halfY = tilesY / 2;

    for (int ty = 0; ty < tilesY; ty++) {
        for (int tx = 0; tx < tilesX; tx++) {
            Entity* tile = world.createEntity("Tile");
            tile->setTag("ground");

            auto* transform = tile->addComponent<Transform>(
                (float)(tx - halfX) * tileSize + half,
                (float)(ty - halfY) * tileSize + half
            );
            transform->depth = 0.0f;

            auto* sprite = tile->addComponent<SpriteComponent>();
            sprite->size = {tileSize, tileSize};

            // Choose tile type: mostly grass, occasional dirt patches and dark grass
            int tileH = pixelHash(tx, ty, 800);
            if ((tileH % 17) == 0) {
                // Dirt patch
                sprite->texture = dirtTex;
                sprite->texturePath = dirtPatchPath;
            } else if ((tileH % 7) == 0) {
                // Dark grass (near trees / edges)
                sprite->texture = grassDarkTex;
                sprite->texturePath = grassDarkPath;
            } else {
                sprite->texture = grassTex;
                sprite->texturePath = grassPath;
            }
        }
    }

    // ---- Generate improved tree sprite (32x48) ----
    std::string treePath = "assets/sprites/tree.png";
    auto treeTex = TextureCache::instance().load(treePath);
    if (!treeTex) {
        const int W = 32, H = 48;
        std::vector<unsigned char> pixels(W * H * 4, 0);
        auto sp = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
            setPixelW(pixels, x, y, W, H, r, g, b, a);
        };

        // Trunk (centered, rows 30-47)
        for (int y = 30; y < H; y++) {
            for (int x = 12; x <= 19; x++) {
                int n = pixelHash(x, y, 710) % 11 - 5;
                // Bark texture: lighter on left (light source from left-top)
                int highlight = (x <= 14) ? 12 : ((x >= 18) ? -10 : 0);
                sp(x, y, clampByte(95 + n + highlight), clampByte(65 + n + highlight), clampByte(30 + n));
                // Vertical bark lines
                if ((pixelHash(x, 0, 711) % 4) == 0) {
                    int bn = pixelHash(x, y, 712) % 7 - 3;
                    sp(x, y, clampByte(80 + bn + highlight), clampByte(55 + bn + highlight), clampByte(25 + bn));
                }
            }
        }
        // Trunk outline
        for (int y = 30; y < H; y++) {
            sp(11, y, 30, 22, 12); sp(20, y, 30, 22, 12);
        }

        // Canopy (organic round shape with multiple layers)
        // Main canopy ellipse centered at (16, 16), radii ~14x14
        float cx = 16.0f, cy = 16.0f;
        float rx = 14.5f, ry = 14.0f;
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < W; x++) {
                float dx = x - cx, dy = y - cy;
                float d = (dx*dx)/(rx*rx) + (dy*dy)/(ry*ry);
                if (d < 1.0f) {
                    int n = pixelHash(x, y, 713) % 15 - 7;
                    // Multiple green shades: lighter toward top-left, darker at bottom-right
                    float lightFactor = 1.0f - d * 0.3f;
                    float topBias = (cy - (float)y) / ry; // positive = upper part
                    int baseG = (int)(95.0f + topBias * 25.0f);
                    int baseR = (int)(25.0f + topBias * 10.0f);
                    int baseB = (int)(18.0f + topBias * 5.0f);

                    // Shadow on lower-right
                    if (dx > 3 && dy > 3) { baseG -= 15; baseR -= 5; }
                    // Highlight on upper-left (dappled)
                    if (dx < -2 && dy < -2 && (pixelHash(x, y, 714) % 5) == 0) { baseG += 25; baseR += 8; }
                    // Leaf cluster variation
                    int cluster = pixelHash(x / 3, y / 3, 715) % 20 - 10;

                    sp(x, y, clampByte(baseR + n + cluster/2),
                             clampByte(baseG + n + cluster),
                             clampByte(baseB + n + cluster/3));
                }
            }
        }

        // Canopy dark outline
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < W; x++) {
                float dx = x - cx, dy = y - cy;
                float d = (dx*dx)/(rx*rx) + (dy*dy)/(ry*ry);
                if (d >= 0.85f && d < 1.15f) {
                    // Check if neighbor is transparent
                    bool hasEmpty = false;
                    for (int dy2 = -1; dy2 <= 1; dy2++)
                        for (int dx2 = -1; dx2 <= 1; dx2++) {
                            int nx = x+dx2, ny = y+dy2;
                            if (nx >= 0 && nx < W && ny >= 0 && ny < H) {
                                if (pixels[(ny*W+nx)*4+3] == 0) hasEmpty = true;
                            }
                        }
                    if (hasEmpty && pixels[(y*W+x)*4+3] != 0) {
                        sp(x, y, 15, 40, 10);
                    }
                }
            }
        }

        stbi_write_png(treePath.c_str(), W, H, 4, pixels.data(), W * 4);
        treeTex = TextureCache::instance().load(treePath);
    }

    // ---- Place trees: clustered + scattered for natural feel ----
    // Clusters near edges, some random
    Vec2 treePositions[] = {
        // Cluster NW
        {-320, -200}, {-290, -180}, {-350, -160}, {-310, -140},
        // Cluster NE
        {280, -210}, {310, -190}, {260, -170}, {300, -150},
        // Cluster SW
        {-280, 200}, {-310, 180}, {-260, 220},
        // Cluster SE
        {300, 190}, {330, 210}, {280, 230},
        // Scattered singles
        {-100, 80}, {120, -60}, {-50, -150}, {200, 100},
        {-180, 30}, {180, -120}, {-20, 250}, {60, -240},
        // Near center but offset
        {-140, -50}, {160, 60}, {50, 140}, {-80, -110},
    };

    for (auto& pos : treePositions) {
        Entity* tree = world.createEntity("Tree");
        tree->setTag("obstacle");

        auto* transform = tree->addComponent<Transform>(pos);
        transform->depth = 5.0f;

        auto* sprite = tree->addComponent<SpriteComponent>();
        sprite->texture = treeTex;
        sprite->texturePath = treePath;
        sprite->size = {32.0f, 48.0f};

        auto* collider = tree->addComponent<BoxCollider>();
        collider->size = {12.0f, 18.0f};
        collider->offset = {0.0f, 8.0f};
        collider->isStatic = true;
    }

    // ---- Generate and place rock decorations ----
    std::string rockPath = "assets/sprites/rock_small.png";
    auto rockTex = TextureCache::instance().load(rockPath);
    if (!rockTex) {
        const int SIZE = 16;
        std::vector<unsigned char> pixels(SIZE * SIZE * 4, 0);
        for (int y = 0; y < SIZE; y++) {
            for (int x = 0; x < SIZE; x++) {
                float dx = x - 8.0f, dy = y - 9.0f;
                // Slightly flat ellipse
                if (dx*dx/(6.5f*6.5f) + dy*dy/(5.5f*5.5f) < 1.0f) {
                    int n = pixelHash(x, y, 720) % 15 - 7;
                    int shade = (int)(25.0f * (1.0f - (float)y / SIZE));
                    unsigned char base = clampByte(130 + n + shade);
                    setPixel(pixels, x, y, SIZE, base, clampByte(base - 3), clampByte(base - 8));
                    // Highlight speck
                    if (y < 6 && (pixelHash(x, y, 721) % 7) == 0) {
                        setPixel(pixels, x, y, SIZE, clampByte(base + 30), clampByte(base + 25), clampByte(base + 20));
                    }
                }
            }
        }
        // Outline
        for (int y = 0; y < SIZE; y++)
            for (int x = 0; x < SIZE; x++) {
                if (pixels[(y*SIZE+x)*4+3] == 0) continue;
                bool edge = false;
                for (int d = -1; d <= 1 && !edge; d++)
                    for (int e = -1; e <= 1 && !edge; e++) {
                        int nx = x+e, ny = y+d;
                        if (nx < 0 || nx >= SIZE || ny < 0 || ny >= SIZE || pixels[(ny*SIZE+nx)*4+3] == 0) edge = true;
                    }
                if (edge) setPixel(pixels, x, y, SIZE, 60, 55, 50);
            }
        stbi_write_png(rockPath.c_str(), SIZE, SIZE, 4, pixels.data(), SIZE * 4);
        rockTex = TextureCache::instance().load(rockPath);
    }

    Vec2 rockPositions[] = {
        {-60, 40}, {80, -30}, {-150, 100}, {200, -80},
        {30, 170}, {-100, -130}, {250, 50}, {-220, -40},
    };
    for (auto& pos : rockPositions) {
        Entity* rock = world.createEntity("Rock");
        rock->setTag("decoration");
        auto* transform = rock->addComponent<Transform>(pos);
        transform->depth = 2.0f;
        auto* sprite = rock->addComponent<SpriteComponent>();
        sprite->texture = rockTex;
        sprite->texturePath = rockPath;
        sprite->size = {16.0f, 16.0f};
    }

    LOG_INFO("Game", "Test scene created: %zu entities total (%dx%d tiles + trees + rocks)", world.entityCount(), tilesX, tilesY);
}

void GameApp::spawnTestMobs(World& world) {
    // Create a spawn zone entity
    Entity* zone = world.createEntity("WhisperingWoods_SpawnZone");
    zone->setTag("spawnzone");
    auto* zoneTransform = zone->addComponent<Transform>(0.0f, 0.0f);
    zoneTransform->depth = -5.0f; // Behind everything (invisible zone marker)
    auto* szComp = zone->addComponent<SpawnZoneComponent>();
    szComp->config.zoneName = "Whispering Woods";
    szComp->config.size = {200.0f, 150.0f};

    // Add spawn rules
    MobSpawnRule slimeRule;
    slimeRule.enemyId = "Slime";
    slimeRule.targetCount = 3;
    slimeRule.minLevel = 1; slimeRule.maxLevel = 2;
    slimeRule.baseHP = 30; slimeRule.baseDamage = 5;
    slimeRule.respawnSeconds = 10.0f;
    szComp->config.rules.push_back(slimeRule);

    MobSpawnRule goblinRule;
    goblinRule.enemyId = "Goblin";
    goblinRule.targetCount = 2;
    goblinRule.minLevel = 2; goblinRule.maxLevel = 3;
    goblinRule.baseHP = 50; goblinRule.baseDamage = 8;
    goblinRule.respawnSeconds = 15.0f;
    szComp->config.rules.push_back(goblinRule);

    MobSpawnRule wolfRule;
    wolfRule.enemyId = "Wolf";
    wolfRule.targetCount = 2;
    wolfRule.minLevel = 3; wolfRule.maxLevel = 4;
    wolfRule.baseHP = 70; wolfRule.baseDamage = 12;
    wolfRule.respawnSeconds = 20.0f;
    szComp->config.rules.push_back(wolfRule);

    MobSpawnRule mushroomRule;
    mushroomRule.enemyId = "Mushroom";
    mushroomRule.targetCount = 2;
    mushroomRule.minLevel = 1; mushroomRule.maxLevel = 1;
    mushroomRule.baseHP = 20; mushroomRule.baseDamage = 3;
    mushroomRule.isAggressive = false;
    mushroomRule.respawnSeconds = 10.0f;
    szComp->config.rules.push_back(mushroomRule);

    MobSpawnRule golemRule;
    golemRule.enemyId = "Forest_Golem";
    golemRule.targetCount = 1;
    golemRule.minLevel = 5; golemRule.maxLevel = 5;
    golemRule.baseHP = 200; golemRule.baseDamage = 25;
    golemRule.isBoss = true;
    golemRule.isAggressive = true;
    golemRule.respawnSeconds = 60.0f;
    szComp->config.rules.push_back(golemRule);

    LOG_INFO("Game", "Created spawn zone '%s' with %zu rules",
             szComp->config.zoneName.c_str(), szComp->config.rules.size());
}

void GameApp::spawnTestNPCs(World& world) {
    NPCTemplate testNPC;
    testNPC.name = "Village Elder";
    testNPC.npcId = 1;
    testNPC.position = {256.0f, 256.0f};
    testNPC.isQuestGiver = true;
    testNPC.questIds = {1, 2, 3};
    testNPC.dialogueGreeting = "Welcome, adventurer! I have tasks for you.";
    EntityFactory::createNPC(world, testNPC);

    NPCTemplate merchantNPC;
    merchantNPC.name = "Potion Merchant";
    merchantNPC.npcId = 2;
    merchantNPC.position = {320.0f, 256.0f};
    merchantNPC.isMerchant = true;
    merchantNPC.shopName = "Potion Shop";
    merchantNPC.shopItems = {
        {"potion_hp_small", "Small HP Potion", 50, 12, 0},
        {"potion_mp_small", "Small MP Potion", 50, 12, 0},
        {"potion_hp_medium", "Medium HP Potion", 200, 50, 0}
    };
    merchantNPC.dialogueGreeting = "Welcome to my shop! Take a look around.";
    EntityFactory::createNPC(world, merchantNPC);

    LOG_INFO("Game", "Test NPCs created (Village Elder, Potion Merchant)");
}

void GameApp::onUpdate(float deltaTime) {
    switch (connState_) {
        case ConnectionState::LoginScreen:
            // Callbacks on loginScreenWidget_ drive login/register — nothing to poll
            break;

        case ConnectionState::CharacterCreation:
            // Callbacks on CharacterCreationScreen drive next/back — nothing to poll
            break;

        case ConnectionState::Authenticating: {
            // Poll for auth result
            if (authClient_.hasResult()) {
                AuthResponse resp = authClient_.consumeResult();
                if (resp.success) {
                    pendingAuthToken_ = resp.authToken;
                    pendingCharName_ = resp.characterName;
                    pendingClassName_ = resp.className;
                    pendingLevel_ = resp.level;
                    pendingSpawnPos_ = {resp.spawnX, resp.spawnY};
                    pendingSceneName_ = resp.sceneName;
                    // Store full character state from auth for immediate player init
                    pendingAuthResponse_ = resp;
                    // Connect to game server via UDP with auth token
                    std::string host = loginScreenWidget_ ? loginScreenWidget_->serverHost : "127.0.0.1";
                    netClient_.connectWithToken(host, static_cast<uint16_t>(serverPort_), pendingAuthToken_);
                    connState_ = ConnectionState::UDPConnecting;
                    if (loginScreenWidget_) loginScreenWidget_->setStatus("Connecting to game server...", false);
                } else {
                    if (loginScreenWidget_) loginScreenWidget_->setStatus(resp.errorReason, true);
                    connState_ = ConnectionState::LoginScreen;
                }
            }
            break;
        }

        case ConnectionState::UDPConnecting: {
            // Poll network for ConnectAccept/ConnectReject
            netTime_ += deltaTime;
            netClient_.poll(netTime_);

            if (netClient_.isConnected()) {
                if (loginScreenWidget_) {
                    loginScreenWidget_->setStatus("", false);
                    // Save remember-me preferences on successful connection
                    if (loginScreenWidget_->rememberMe)
                        loginScreenWidget_->savePreferences();
                    loginScreenWidget_->setVisible(false);
                }

                // Defer player creation to next frame by setting flag
                localPlayerCreated_ = false;
                retainedUILoaded_ = false;
                npcDialoguePanel_ = nullptr;
                shopPanel_ = nullptr;
                bankPanel_ = nullptr;
                teleporterPanel_ = nullptr;
                dungeonInviteDialog_ = nullptr;
                inDungeon_ = false;

                // Start async scene load instead of going straight to InGame
                if (!pendingSceneName_.empty()) {
                    std::string jsonPath = "assets/scenes/" + pendingSceneName_ + ".json";
                    asyncLoader_.startLoad(pendingSceneName_, jsonPath);
                    loadingScreen_.begin(pendingSceneName_, windowWidth(), windowHeight());
                    setLoadingScreen(&loadingScreen_);
                    setIsLoading(true);
                    loadingMinTimer_ = 2.0f;
                    loadingDataReady_ = false;
                    connState_ = ConnectionState::LoadingScene;
                    pendingZoneTransition_ = true; // buffer entity enters during loading
                } else {
                    connState_ = ConnectionState::InGame;
                }

                LOG_INFO("GameApp", "Connected to game server, entering game as '%s' (%s)",
                         pendingCharName_.c_str(), pendingClassName_.c_str());
            } else if (!netClient_.isWaitingForConnection()) {
                // Connect timed out or failed — return to login screen
                connState_ = ConnectionState::LoginScreen;
                if (loginScreenWidget_) loginScreenWidget_->setStatus("Connection timed out", true);
            }
            // ConnectReject is handled by the onConnectRejected callback set up in onInit
            break;
        }

        case ConnectionState::LoadingScene: {
            netTime_ += deltaTime;
            netClient_.poll(netTime_);

            // Check disconnect during loading
            if (!netClient_.isConnected() && !netClient_.isWaitingForConnection() &&
                !netClient_.isReconnecting()) {
                LOG_WARN("GameApp", "Disconnected during scene load");
                connState_ = ConnectionState::LoginScreen;
                setIsLoading(false);
                loadingScreen_.end();
                pendingZoneTransition_ = false;
                if (loginScreenWidget_) {
                    loginScreenWidget_->setStatus("Disconnected", true);
                    loginScreenWidget_->setVisible(true);
                }
                break;
            }

            // Check worker failure
            if (asyncLoader_.hasFailed()) {
                LOG_ERROR("GameApp", "Scene load failed: %s", asyncLoader_.errorMessage().c_str());
                connState_ = ConnectionState::LoginScreen;
                setIsLoading(false);
                loadingScreen_.end();
                pendingZoneTransition_ = false;
                if (loginScreenWidget_) {
                    loginScreenWidget_->setStatus("Failed to load scene", true);
                    loginScreenWidget_->setVisible(true);
                }
                break;
            }

            // Tick finalization (entity creation + texture progress)
            auto* sc = SceneManager::instance().currentScene();
            if (sc && asyncLoader_.isWorkerDone() && !loadingDataReady_) {
                bool done = asyncLoader_.tickFinalization(sc->world());
                if (done) loadingDataReady_ = true;
            }

            // Count down minimum display timer
            if (loadingMinTimer_ > 0.0f) loadingMinTimer_ -= deltaTime;

            // Transition to InGame only when data is ready AND min time elapsed
            if (loadingDataReady_ && loadingMinTimer_ <= 0.0f) {
                localPlayerCreated_ = false;
                retainedUILoaded_ = false;
                npcDialoguePanel_ = nullptr;
                shopPanel_ = nullptr;
                bankPanel_ = nullptr;
                teleporterPanel_ = nullptr;
                dungeonInviteDialog_ = nullptr;
                inDungeon_ = false;

                connState_ = ConnectionState::InGame;
                setIsLoading(false);
                loadingScreen_.end();

                // Clear zone transition flag and replay buffered entity enters
                pendingZoneTransition_ = false;
                if (!pendingEntityEnters_.empty()) {
                    LOG_INFO("GameApp", "Replaying %zu buffered entity enters",
                             pendingEntityEnters_.size());
                    for (const auto& enterMsg : pendingEntityEnters_) {
                        if (netClient_.onEntityEnter) {
                            netClient_.onEntityEnter(enterMsg);
                        }
                    }
                    pendingEntityEnters_.clear();
                }

                LOG_INFO("GameApp", "Loading complete, entering game");
            }

            // Show real progress while loading, hold at 100% while waiting for min timer
            setLoadingProgress(loadingDataReady_ ? 1.0f : asyncLoader_.progress());
            break;
        }

        case ConnectionState::InGame: {
            // Deferred player creation — runs on first InGame frame.
            // Scene was already loaded by AsyncSceneLoader before we got here.
            if (!localPlayerCreated_) {
                localPlayerCreated_ = true;

                // Determine if this is a zone transition or initial login
                bool isZoneTransition = !pendingZoneScene_.empty();

                auto* sc = SceneManager::instance().currentScene();
                if (sc) {
                    // Parse class type from pending class name
                    ClassType ct = ClassType::Warrior;
                    if (pendingClassName_ == "Mage") ct = ClassType::Mage;
                    else if (pendingClassName_ == "Archer") ct = ClassType::Archer;

                    // Create full player with all 24 game components
                    Entity* player = EntityFactory::createPlayer(
                        sc->world(), pendingCharName_, ct, true, pendingFaction_);

                    // Set spawn position: zone transition uses pendingZoneSpawn_,
                    // initial login uses pendingSpawnPos_ from auth response
                    auto* t = player->getComponent<Transform>();
                    if (t) t->position = isZoneTransition ? pendingZoneSpawn_ : pendingSpawnPos_;

                    if (isZoneTransition) {
                        // Restore player state from last SvPlayerState (zone transition)
                        auto* cs = player->getComponent<CharacterStatsComponent>();
                        if (cs) {
                            const auto& ps = pendingPlayerState_;
                            cs->stats.level = ps.level;
                            cs->stats.recalculateStats();
                            cs->stats.recalculateXPRequirement();
                            cs->stats.currentHP = ps.currentHP;
                            cs->stats.maxHP = ps.maxHP;
                            cs->stats.currentMP = ps.currentMP;
                            cs->stats.maxMP = ps.maxMP;
                            cs->stats.currentFury = ps.currentFury;
                            cs->stats.currentXP = ps.currentXP;
                            cs->stats.honor = ps.honor;
                            cs->stats.pvpKills = ps.pvpKills;
                            cs->stats.pvpDeaths = ps.pvpDeaths;
                        }
                        auto* inv = player->getComponent<InventoryComponent>();
                        if (inv) inv->inventory.setGold(pendingPlayerState_.gold);
                    } else {
                        // Apply full character state from auth response (initial login)
                        // (avoids blank/default stats until SvPlayerState arrives)
                        const auto& ar = pendingAuthResponse_;
                        auto* cs = player->getComponent<CharacterStatsComponent>();
                        if (cs) {
                            cs->stats.level = ar.level;
                            cs->stats.recalculateStats();
                            cs->stats.recalculateXPRequirement();
                            cs->stats.currentHP = ar.currentHP;
                            cs->stats.maxHP = ar.maxHP;
                            cs->stats.currentMP = ar.currentMP;
                            cs->stats.maxMP = ar.maxMP;
                            cs->stats.currentFury = ar.currentFury;
                            cs->stats.currentXP = ar.currentXP;
                            cs->stats.honor = ar.honor;
                            cs->stats.pvpKills = ar.pvpKills;
                            cs->stats.pvpDeaths = ar.pvpDeaths;
                            cs->stats.isDead = ar.isDead != 0;
                            cs->stats.lifeState = cs->stats.isDead ? LifeState::Dead : LifeState::Alive;
                        }
                        auto* inv = player->getComponent<InventoryComponent>();
                        if (inv) {
                            inv->inventory.setGold(ar.gold); // set, not add — avoid doubling
                        }
                    }

                    // SpawnSystem removed from client — mobs come from server replication only

                    // Apply pending server state (level, HP, XP, etc.) that arrived
                    // before the player entity existed (covers both paths).
                    if (hasPendingPlayerState_ && !isZoneTransition) {
                        hasPendingPlayerState_ = false;
                        auto* cs = player->getComponent<CharacterStatsComponent>();
                        if (cs) {
                            const auto& ps = pendingPlayerState_;
                            // Set level and recalculate first, then override with server values
                            cs->stats.level = ps.level;
                            cs->stats.recalculateStats();
                            cs->stats.recalculateXPRequirement();
                            cs->stats.currentHP = ps.currentHP;
                            cs->stats.maxHP = ps.maxHP;
                            cs->stats.currentMP = ps.currentMP;
                            cs->stats.maxMP = ps.maxMP;
                            cs->stats.currentFury = ps.currentFury;
                            cs->stats.currentXP = ps.currentXP;
                            cs->stats.honor = ps.honor;
                            cs->stats.pvpKills = ps.pvpKills;
                            cs->stats.pvpDeaths = ps.pvpDeaths;
                        }
                        auto* inv = player->getComponent<InventoryComponent>();
                        if (inv) {
                            inv->inventory.setGold(pendingPlayerState_.gold);
                        }
                    } else if (isZoneTransition) {
                        hasPendingPlayerState_ = false; // already applied above
                    }

                    Vec2 spawnPos = isZoneTransition ? pendingZoneSpawn_ : pendingSpawnPos_;
                    std::string sceneName = isZoneTransition ? pendingZoneScene_ : pendingSceneName_;
                    auto* finalStats = player->getComponent<CharacterStatsComponent>();
                    LOG_INFO("GameApp", "Local player created for '%s' (%s Lv%d) at (%.0f, %.0f) in %s",
                             pendingCharName_.c_str(), pendingClassName_.c_str(),
                             finalStats ? finalStats->stats.level : 0,
                             spawnPos.x, spawnPos.y, sceneName.c_str());

                    // Clear zone scene name after use so subsequent frames don't
                    // re-detect as zone transition
                    if (isZoneTransition) pendingZoneScene_.clear();

                    // Compute world bounds from zone entities and set on MovementSystem
                    {
                        auto* moveSys = sc->world().getSystem<MovementSystem>();
                        if (moveSys) {
                            Rect bounds = {0, 0, 0, 0};
                            sc->world().forEach<Transform, ZoneComponent>(
                                [&](Entity*, Transform* zt, ZoneComponent* zc) {
                                    // Use the largest zone as the world boundary
                                    Rect zr = {zt->position.x - zc->size.x * 0.5f,
                                               zt->position.y - zc->size.y * 0.5f,
                                               zc->size.x, zc->size.y};
                                    if (zr.w * zr.h > bounds.w * bounds.h) {
                                        bounds = zr;
                                    }
                                }
                            );
                            // Fallback: if no zones, use a generous default around spawn
                            if (bounds.w <= 0 || bounds.h <= 0) {
                                bounds = {-2000, -2000, 4000, 4000};
                            }
                            moveSys->worldBounds = bounds;
                            LOG_INFO("GameApp", "Movement bounds: (%.0f,%.0f)-(%.0f,%.0f)",
                                     bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h);
                        }
                    }

                    // Build collision grid from collision-layer tiles
                    collisionGrid_.beginBuild();
                    sc->world().forEach<Transform, TileLayerComponent>(
                        [&](Entity*, Transform* t, TileLayerComponent* tlc) {
                            if (tlc->layer == "collision") {
                                int tx = (int)std::floor(t->position.x / 32.0f);
                                int ty = (int)std::floor(t->position.y / 32.0f);
                                collisionGrid_.markBlocked(tx, ty);
                            }
                        }
                    );
                    collisionGrid_.endBuild();
                    if (movementSystem_) movementSystem_->setCollisionGrid(&collisionGrid_);
                    if (mobAISystem_) mobAISystem_->setLocalCollisionGrid(&collisionGrid_);

                    // Apply pending inventory sync that arrived before player existed
                    if (hasPendingInventorySync_) {
                        hasPendingInventorySync_ = false;
                        if (netClient_.onInventorySync)
                            netClient_.onInventorySync(pendingInventorySync_);
                        LOG_INFO("Client", "Applied pending inventory sync on connect");
                    }

                    // Apply pending death notification that arrived before player existed
                    if (hasPendingDeathNotify_) {
                        hasPendingDeathNotify_ = false;
                        if (finalStats) {
                            finalStats->stats.lifeState = LifeState::Dead;
                            finalStats->stats.isDead = true;
                            finalStats->stats.currentHP = 0;
                        }
                        if (deathOverlay_) deathOverlay_->onDeath(0, 0, 0.0f);
                        if (auto* ds = uiManager().getScreen("death_overlay"))
                            ds->setVisible(true);
                        LOG_INFO("Client", "Applied pending death state on connect");
                    }

                    // Replay skill/quest syncs that arrived before player existed
                    if (hasPendingSkillSync_) {
                        hasPendingSkillSync_ = false;
                        if (netClient_.onSkillSync)
                            netClient_.onSkillSync(pendingSkillSync_);
                        LOG_INFO("Client", "Applied pending skill sync on connect");
                    }
                    if (hasPendingQuestSync_) {
                        hasPendingQuestSync_ = false;
                        if (netClient_.onQuestSync)
                            netClient_.onQuestSync(pendingQuestSync_);
                        LOG_INFO("Client", "Applied pending quest sync on connect");
                    }
                    for (auto& qm : pendingQuestUpdates_) {
                        if (netClient_.onQuestUpdate)
                            netClient_.onQuestUpdate(qm);
                    }
                    if (!pendingQuestUpdates_.empty()) {
                        LOG_INFO("Client", "Applied %d pending quest updates on connect",
                                 (int)pendingQuestUpdates_.size());
                        pendingQuestUpdates_.clear();
                    }
                }
                // Load retained-mode UI screens (once, on first InGame frame)
                if (!retainedUILoaded_) {
                    retainedUILoaded_ = true;
                    auto& ui = uiManager();
                    // Only load screens that haven't been loaded yet (hot-reload
                    // sets retainedUILoaded_=false to re-resolve pointers, but the
                    // screen itself is already loaded — reloading it would trigger
                    // the reload listener again, creating an infinite loop)
                    if (!ui.getScreen("death_overlay"))       ui.loadScreen("assets/ui/screens/death_overlay.json");
                    if (!ui.getScreen("fate_hud"))            ui.loadScreen("assets/ui/screens/fate_hud.json");
                    if (!ui.getScreen("fate_menu_panels"))    ui.loadScreen("assets/ui/screens/fate_menu_panels.json");
                    if (!ui.getScreen("character_select"))    ui.loadScreen("assets/ui/screens/character_select.json");
                    if (!ui.getScreen("character_creation"))  ui.loadScreen("assets/ui/screens/character_creation.json");
                    if (!ui.getScreen("fate_social"))         ui.loadScreen("assets/ui/screens/fate_social.json");

                    // Data binding provider — resolves {player.*} and {death.*} paths
                    ui.dataBinding().setProvider([this](const std::string& path) -> std::string {
                        auto* scene = SceneManager::instance().currentScene();
                        if (!scene) return "";

                        // Find local player entity
                        Entity* localPlayer = nullptr;
                        scene->world().forEach<PlayerController>(
                            [&](Entity* e, PlayerController* ctrl) {
                                if (ctrl->isLocalPlayer) localPlayer = e;
                            }
                        );
                        if (!localPlayer) return "";

                        auto* cs = localPlayer->getComponent<CharacterStatsComponent>();
                        auto* inv = localPlayer->getComponent<InventoryComponent>();

                        if (path == "player.hp")        return cs ? std::to_string(cs->stats.currentHP) : "0";
                        if (path == "player.maxHp")     return cs ? std::to_string(cs->stats.maxHP) : "0";
                        if (path == "player.mp")        return cs ? std::to_string(cs->stats.currentMP) : "0";
                        if (path == "player.maxMp")     return cs ? std::to_string(cs->stats.maxMP) : "0";
                        if (path == "player.xp")        return cs ? std::to_string(cs->stats.currentXP) : "0";
                        if (path == "player.xpToLevel") return cs ? std::to_string(cs->stats.xpToNextLevel) : "100";
                        if (path == "player.gold") {
                            if (!inv) return std::string("0");
                            int64_t g = inv->inventory.getGold();
                            // Format with K/M suffixes for large values
                            if (g >= 1000000) return std::to_string(g / 1000000) + "M";
                            if (g >= 1000)    return std::to_string(g / 1000) + "K";
                            return std::to_string(g);
                        }
                        if (path == "player.level")     return cs ? std::to_string(cs->stats.level) : "1";
                        if (path == "player.class")     return cs ? cs->stats.className : "";
                        if (path == "player.str")       return cs ? std::to_string(cs->stats.getStrength()) : "0";
                        if (path == "player.dex")       return cs ? std::to_string(cs->stats.getDexterity()) : "0";
                        if (path == "player.int")       return cs ? std::to_string(cs->stats.getIntelligence()) : "0";
                        if (path == "player.vit")       return cs ? std::to_string(cs->stats.getVitality()) : "0";
                        if (path == "death.xpLoss")     return std::to_string(deathOverlay_ ? deathOverlay_->xpLost : 0);
                        if (path == "death.honorLoss")  return std::to_string(deathOverlay_ ? deathOverlay_->honorLost : 0);
                        if (path == "death.countdown")  return std::to_string(static_cast<int>(deathOverlay_ ? deathOverlay_->countdown : 0.0f));
                        return "";
                    });

                    // Resolve retained-mode widget pointers
                    deathOverlay_ = dynamic_cast<DeathOverlay*>(ui.getScreen("death_overlay"));
                    if (auto* socialScreen = ui.getScreen("fate_social"))
                        chatPanel_ = dynamic_cast<ChatPanel*>(socialScreen->findById("chat_panel"));
                    if (auto* hudScreen = ui.getScreen("fate_hud")) {
                        dungeonInviteDialog_ = dynamic_cast<ConfirmDialog*>(hudScreen->findById("dungeon_invite_dialog"));
                        destroyItemDialog_ = dynamic_cast<ConfirmDialog*>(hudScreen->findById("destroy_item_dialog"));
                        if (destroyItemDialog_) {
                            destroyItemDialog_->onConfirm = [this](const std::string&) {
                                if (destroyItemSlot_ >= 0 && !destroyItemId_.empty()) {
                                    netClient_.sendDestroyItem(destroyItemSlot_, destroyItemId_);
                                    destroyItemSlot_ = -1;
                                    destroyItemId_.clear();
                                }
                                destroyItemDialog_->setVisible(false);
                            };
                            destroyItemDialog_->onCancel = [this](const std::string&) {
                                destroyItemSlot_ = -1;
                                destroyItemId_.clear();
                                destroyItemDialog_->setVisible(false);
                            };
                        }
                    }

                    // Wire ChatPanel send callback
                    if (chatPanel_) {
                        chatPanel_->onSendMessage = [this](uint8_t channel, const std::string& message, const std::string& targetName) {
                            auto filtered = ProfanityFilter::filterChatMessage(message, FilterMode::Censor);
                            netClient_.sendChat(channel, filtered.filteredText, targetName);
                            audioManager_.playSFX("chat_send");
                        };
                        chatPanel_->onClose = [this](const std::string&) {
                            if (chatPanel_) chatPanel_->setFullPanelMode(false);
                            Input::instance().setChatMode(false);
                        };
                        // Replay chat messages that arrived before UI was ready
                        for (auto& cm : pendingChatMessages_)
                            chatPanel_->addMessage(cm.channel, cm.sender, cm.text, cm.faction);
                        if (!pendingChatMessages_.empty()) {
                            LOG_INFO("Client", "Applied %d pending chat messages on connect",
                                     (int)pendingChatMessages_.size());
                            pendingChatMessages_.clear();
                        }
                    }

                    // Wire DeathOverlay respawn callback + button click handlers
                    if (deathOverlay_) {
                        deathOverlay_->onRespawnRequested = [this](uint8_t respawnType) {
                            if (netClient_.isConnected()) {
                                netClient_.sendRespawn(respawnType);
                            }
                        };
                    }
                    auto* deathScreen = ui.getScreen("death_overlay");
                    if (deathScreen) {
                        auto* btnTown = dynamic_cast<Button*>(deathScreen->findById("btn_respawn_town"));
                        if (btnTown) {
                            btnTown->onClick = [this](const std::string&) {
                                if (deathOverlay_ && deathOverlay_->onRespawnRequested)
                                    deathOverlay_->onRespawnRequested(0);
                            };
                        }
                        auto* btnSpawn = dynamic_cast<Button*>(deathScreen->findById("btn_respawn_spawn"));
                        if (btnSpawn) {
                            btnSpawn->onClick = [this](const std::string&) {
                                if (deathOverlay_ && deathOverlay_->onRespawnRequested)
                                    deathOverlay_->onRespawnRequested(1);
                            };
                        }
                        auto* btnPhoenix = dynamic_cast<Button*>(deathScreen->findById("btn_respawn_phoenix"));
                        if (btnPhoenix) {
                            btnPhoenix->onClick = [this](const std::string&) {
                                if (deathOverlay_ && deathOverlay_->onRespawnRequested)
                                    deathOverlay_->onRespawnRequested(2);
                            };
                        }
                    }

                    // Legacy inventory/skill_bar screen wiring removed —
                    // superseded by fate_menu_panels InventoryPanel and fate_hud SkillArc.

                    // Wire Fate HUD widgets
                    auto* hudScreen = ui.getScreen("fate_hud");
                    if (hudScreen) {
                        // FateStatusBar — wire menu + chat callbacks
                        auto* statusBar = dynamic_cast<FateStatusBar*>(hudScreen->findById("status_bar"));
                        if (statusBar) {
                            statusBar->onChatButtonPressed = [this](const std::string&) {
                                if (!chatPanel_) return;
                                bool opening = !chatPanel_->isFullPanelMode();
                                chatPanel_->setFullPanelMode(opening);
                                Input::instance().setChatMode(opening);
                            };
                            statusBar->onMenuItemSelected = [this](const std::string& item) {
                                auto* menuPanels = uiManager().getScreen("fate_menu_panels");
                                if (!menuPanels) return;
                                auto* tabBar = dynamic_cast<MenuTabBar*>(menuPanels->findById("tab_bar"));
                                int tab = -1;
                                if (item == "Status")                       tab = 0;
                                else if (item == "Inventory")               tab = 1;
                                else if (item == "Skills" || item == "Skill") tab = 2;
                                if (tab >= 0) {
                                    bool wasVisible = menuPanels->visible();
                                    menuPanels->setVisible(true);
                                    if (tabBar) tabBar->setActiveTab(tab);
                                }
                            };
                        }

                        // DPad — wire direction changes into ActionMap movement
                        auto* dpad = dynamic_cast<DPad*>(hudScreen->findById("dpad"));
                        if (dpad) {
                            dpad->onDirectionChange = [this](const std::string&) {
                                auto* dp = dynamic_cast<DPad*>(uiManager().getScreen("fate_hud")->findById("dpad"));
                                if (!dp) return;
                                auto& am = Input::instance().actionMap();
                                am.setActionHeld(ActionId::MoveUp,    dp->activeDirection == Direction::Up);
                                am.setActionHeld(ActionId::MoveDown,  dp->activeDirection == Direction::Down);
                                am.setActionHeld(ActionId::MoveLeft,  dp->activeDirection == Direction::Left);
                                am.setActionHeld(ActionId::MoveRight, dp->activeDirection == Direction::Right);
                            };
                        }

                        // SkillArc — resolve pointer and wire callbacks
                        skillArc_ = dynamic_cast<SkillArc*>(hudScreen->findById("skill_arc"));
                        if (skillArc_) {
                            skillArc_->onAttack = [this](const std::string&) {
                                // Inject attack into both ActionMap and InputBuffer
                                // (combat system reads from consumeBuffered, not isPressed)
                                Input::instance().injectAction(ActionId::Attack);
                            };
                            skillArc_->onSkillSlot = [this](int slotIndex) {
                                // Map arc slot index to global skill bar slot using current page
                                int page = skillArc_ ? skillArc_->currentPage : 0;
                                int globalSlot = page * SkillArc::SLOTS_PER_PAGE + slotIndex;
                                // Look up skill in the player's SkillManager
                                auto* sc = SceneManager::instance().currentScene();
                                if (!sc) return;
                                sc->world().forEach<SkillManagerComponent, PlayerController>(
                                    [&](Entity*, SkillManagerComponent* smc, PlayerController* ctrl) {
                                        if (!ctrl->isLocalPlayer) return;
                                        std::string skillId = smc->skills.getSkillInSlot(globalSlot);
                                        if (skillId.empty()) return;
                                        const LearnedSkill* ls = smc->skills.getLearnedSkill(skillId);
                                        int rank = ls ? ls->effectiveRank() : 1;
                                        if (rank > 0 && skillArc_ && skillArc_->onSkillActivated) {
                                            skillArc_->onSkillActivated(skillId, rank);
                                        }
                                    }
                                );
                            };
                            // Wire skill activation to network (moved from onInit SkillBarUI callback)
                            skillArc_->onSkillActivated = [this](const std::string& skillId, int rank) {
                                if (!netClient_.isConnected()) return;

                                uint64_t targetPid = 0;
                                if (combatSystem_ && combatSystem_->hasTarget()) {
                                    EntityId targetEid = combatSystem_->getTargetEntityId();
                                    for (const auto& [pid, handle] : ghostEntities_) {
                                        Entity* ghost = nullptr;
                                        auto* scene = SceneManager::instance().currentScene();
                                        if (scene) ghost = scene->world().getEntity(handle);
                                        if (ghost && ghost->id() == targetEid) {
                                            targetPid = pid;
                                            break;
                                        }
                                    }
                                }

                                if (targetPid != 0 && combatSystem_) {
                                    combatSystem_->triggerAttackWindup();
                                    combatPredictions_.addPrediction(targetPid, netTime_);
                                }

                                netClient_.sendUseSkill(skillId, static_cast<uint8_t>(rank), targetPid);
                            };
                            skillArc_->onPickUp = [this](const std::string&) {
                                // Find nearest dropped item ghost and send pickup
                                auto* sc = SceneManager::instance().currentScene();
                                if (!sc) return;
                                Entity* localPlayer = nullptr;
                                sc->world().forEach<PlayerController>(
                                    [&](Entity* e, PlayerController* ctrl) {
                                        if (ctrl->isLocalPlayer) localPlayer = e;
                                    }
                                );
                                if (!localPlayer) return;
                                auto* playerT = localPlayer->getComponent<Transform>();
                                if (!playerT) return;

                                constexpr float kPickupRange = 48.0f;
                                float bestDist = kPickupRange + 1.0f;
                                uint64_t bestPid = 0;
                                for (uint64_t pid : droppedItemPids_) {
                                    auto it = ghostEntities_.find(pid);
                                    if (it == ghostEntities_.end()) continue;
                                    Entity* ghost = sc->world().getEntity(it->second);
                                    if (!ghost) continue;
                                    auto* t = ghost->getComponent<Transform>();
                                    if (!t) continue;
                                    float dx = t->position.x - playerT->position.x;
                                    float dy = t->position.y - playerT->position.y;
                                    float dist = std::sqrt(dx * dx + dy * dy);
                                    if (dist < bestDist) {
                                        bestDist = dist;
                                        bestPid = pid;
                                    }
                                }
                                if (bestPid != 0) {
                                    netClient_.sendAction(3, bestPid, 0);
                                    // Don't destroy the drop locally — wait for the server
                                    // to confirm via SvEntityExit. If inventory is full the
                                    // server keeps the drop alive and it stays visible.
                                    droppedItemPids_.erase(bestPid);
                                    ghostInterpolation_.removeEntity(bestPid);
                                }
                            };
                        }
                    }

                    // Wire fate_menu_panels — resolve inventoryPanel_ + tab bar navigation
                    auto* menuScreen = ui.getScreen("fate_menu_panels");
                    if (menuScreen) {
                        inventoryPanel_ = dynamic_cast<InventoryPanel*>(menuScreen->findById("inventory_panel"));

                        // Panel IDs indexed by tab (must match tabLabels order)
                        static const char* panelIds[] = {
                            "status_panel", "inventory_panel", "skill_panel",
                            "guild_panel", "social_panel", "settings_panel", "shop_panel"
                        };
                        static constexpr int panelCount = 7;

                        // Wire tab bar — shows/hides panels by index
                        auto* tabBar = dynamic_cast<MenuTabBar*>(menuScreen->findById("tab_bar"));
                        if (tabBar) {
                            tabBar->onTabChanged = [menuScreen](int tab) {
                                for (int i = 0; i < panelCount; ++i) {
                                    auto* panel = menuScreen->findById(panelIds[i]);
                                    if (panel) panel->setVisible(i == tab);
                                }
                            };
                            // Fire once to show the default active tab
                            if (tabBar->onTabChanged) tabBar->onTabChanged(tabBar->activeTab);
                        }

                        // Wire close buttons on each panel to hide the root
                        // Close handler: hide menu + restore game HUD controls
                        auto closeMenu = [this](const std::string&) {
                            auto* ms = uiManager().getScreen("fate_menu_panels");
                            if (ms) ms->setVisible(false);
                            auto* hudScr = uiManager().getScreen("fate_hud");
                            if (hudScr) {
                                auto* dpad = hudScr->findById("dpad");
                                auto* arc  = hudScr->findById("skill_arc");
                                if (dpad) dpad->setVisible(true);
                                if (arc)  arc->setVisible(true);
                            }
                        };

                        auto* invPanel = dynamic_cast<InventoryPanel*>(menuScreen->findById("inventory_panel"));
                        if (invPanel) invPanel->onClose = closeMenu;
                        auto* statusPanel = dynamic_cast<StatusPanel*>(menuScreen->findById("status_panel"));
                        if (statusPanel) statusPanel->onClose = closeMenu;
                        auto* skillPanel = dynamic_cast<SkillPanel*>(menuScreen->findById("skill_panel"));
                        if (skillPanel) skillPanel->onClose = closeMenu;

                        // Wire inventory drag-and-drop callbacks
                        if (invPanel) {
                            invPanel->onStatEnchantRequest = [this](uint8_t targetSlot, const std::string& scrollItemId) {
                                netClient_.sendStatEnchant(targetSlot, scrollItemId);
                            };
                            invPanel->onMoveItemRequest = [this](int32_t sourceSlot, int32_t destSlot) {
                                netClient_.sendMoveItem(sourceSlot, destSlot);
                            };
                            invPanel->onEquipRequest = [this](int32_t inventorySlot, uint8_t equipSlot) {
                                netClient_.sendEquip(0, inventorySlot, equipSlot);
                            };
                            invPanel->onUnequipRequest = [this](uint8_t equipSlot) {
                                netClient_.sendEquip(1, -1, equipSlot);
                            };
                            invPanel->onDestroyItemRequest = [this](int32_t slot, const std::string& itemId, const std::string& displayName) {
                                if (destroyItemDialog_) {
                                    destroyItemSlot_ = slot;
                                    destroyItemId_ = itemId;
                                    destroyItemDialog_->message = "Destroy " + displayName + "?";
                                    destroyItemDialog_->setVisible(true);
                                }
                            };
                        }
                    }

                    // Wire character select screen
                    // TODO (Gap #9): CharacterSelectScreen.slots are never populated.
                    // The current auth flow (AuthResponse) returns a single character and
                    // goes straight to InGame. When the auth protocol supports multi-character
                    // selection (SvCharacterList with a vector of characters), populate
                    // charSelect->slots here from the response before showing the screen.
                    auto* charSelect = dynamic_cast<CharacterSelectScreen*>(
                        ui.getScreen("character_select"));
                    if (charSelect) {
                        charSelect->onEntry = [this](const std::string&) {
                            // Enter game with selected character
                            auto* selectScreen = uiManager().getScreen("character_select");
                            if (selectScreen) selectScreen->setVisible(false);
                        };
                        charSelect->onCreateNew = [this](const std::string&) {
                            // Show character creation screen
                            auto* selectScreen = uiManager().getScreen("character_select");
                            auto* createScreen = uiManager().getScreen("character_creation");
                            if (selectScreen) selectScreen->setVisible(false);
                            if (createScreen) createScreen->setVisible(true);
                        };
                    }

                    // Wire character creation screen
                    auto* charCreate = dynamic_cast<CharacterCreationScreen*>(
                        ui.getScreen("character_creation"));
                    if (charCreate) {
                        charCreate->onBack = [this](const std::string&) {
                            auto* selectScreen = uiManager().getScreen("character_select");
                            auto* createScreen = uiManager().getScreen("character_creation");
                            if (createScreen) createScreen->setVisible(false);
                            if (selectScreen) selectScreen->setVisible(true);
                        };
                        charCreate->onNext = [this](const std::string&) {
                            // Submit character creation using existing registration logic
                            auto* cc = dynamic_cast<CharacterCreationScreen*>(
                                uiManager().getScreen("character_creation"));
                            if (!cc) return;
                            constexpr const char* classNames[] = {"Warrior", "Mage", "Archer"};
                            constexpr Faction factions[] = {Faction::Xyros, Faction::Fenor, Faction::Zethos, Faction::Solis};
                            pendingFaction_ = factions[cc->selectedFaction];
                            pendingClassName_ = classNames[cc->selectedClass];
                            pendingCharName_ = cc->characterName;
                            LOG_INFO("GameApp", "Character creation submitted: '%s' class=%s faction=%d",
                                     cc->characterName.c_str(), classNames[cc->selectedClass], cc->selectedFaction);
                        };
                    }

                    // Wire social/economy screen
                    auto* socialScreen = ui.getScreen("fate_social");
                    if (socialScreen) {
                        // ChatPanel send/close callbacks wired above via chatPanel_ pointer

                        // Wire TradeWindow callbacks
                        auto* tradeWin = dynamic_cast<TradeWindow*>(socialScreen->findById("trade_window"));
                        if (tradeWin) {
                            tradeWin->onCancel = [tradeWin](const std::string&) {
                                tradeWin->setVisible(false);
                            };
                            tradeWin->onLock = [tradeWin](const std::string&) {
                                tradeWin->myLocked = true;
                            };
                            tradeWin->onAccept = [this](const std::string&) {
                                // Confirm trade via TradeComponent on local player
                                auto* scene = SceneManager::instance().currentScene();
                                if (!scene) return;
                                scene->world().forEach<PlayerController, TradeComponent>(
                                    [](Entity*, PlayerController* ctrl, TradeComponent* tc) {
                                        if (!ctrl->isLocalPlayer) return;
                                        tc->trade.confirm();
                                    }
                                );
                            };
                        }

                        // Wire GuildPanel close button
                        auto* guildPanel = dynamic_cast<GuildPanel*>(socialScreen->findById("guild_panel"));
                        if (guildPanel) {
                            guildPanel->onClose = [guildPanel](const std::string&) {
                                guildPanel->setVisible(false);
                            };
                        }
                    }

                    // Load and wire NPC panels screen
                    ui.loadScreen("assets/ui/screens/npc_panels.json");
                    auto* npcScreen = ui.getScreen("npc_panels");
                    if (npcScreen) {
                        npcDialoguePanel_ = dynamic_cast<NpcDialoguePanel*>(npcScreen->findById("npc_dialogue_panel"));
                        shopPanel_ = dynamic_cast<ShopPanel*>(npcScreen->findById("shop_panel"));
                        bankPanel_ = dynamic_cast<BankPanel*>(npcScreen->findById("bank_panel"));
                        teleporterPanel_ = dynamic_cast<TeleporterPanel*>(npcScreen->findById("teleporter_panel"));
                    }

                    // Wire NpcDialoguePanel callbacks
                    if (npcDialoguePanel_) {
                        npcDialoguePanel_->onOpenShop = [this](uint32_t nId) {
                            npcDialoguePanel_->setVisible(false);
                            auto* npc = findNpcById(nId);
                            if (npc && shopPanel_) {
                                auto* shopComp = npc->getComponent<ShopComponent>();
                                if (shopComp) shopPanel_->open(nId, shopComp->shopName, shopComp->inventory);
                            }
                        };
                        npcDialoguePanel_->onOpenBank = [this](uint32_t nId) {
                            npcDialoguePanel_->setVisible(false);
                            if (bankPanel_) bankPanel_->open(nId);
                        };
                        npcDialoguePanel_->onOpenTeleporter = [this](uint32_t nId) {
                            npcDialoguePanel_->setVisible(false);
                            auto* npc = findNpcById(nId);
                            if (npc && teleporterPanel_) {
                                auto* telComp = npc->getComponent<TeleporterComponent>();
                                if (telComp) {
                                    // Get player gold and level
                                    int64_t gold = 0;
                                    uint16_t level = 1;
                                    auto* scene = SceneManager::instance().currentScene();
                                    if (scene) {
                                        scene->world().forEach<PlayerController>(
                                            [&](Entity* e, PlayerController* ctrl) {
                                                if (!ctrl->isLocalPlayer) return;
                                                auto* inv = e->getComponent<InventoryComponent>();
                                                auto* stats = e->getComponent<CharacterStatsComponent>();
                                                if (inv) gold = inv->inventory.getGold();
                                                if (stats) level = stats->stats.level;
                                            }
                                        );
                                    }
                                    teleporterPanel_->open(nId, telComp->destinations, gold, level);
                                }
                            }
                        };
                        npcDialoguePanel_->onOpenGuildCreation = [this](uint32_t nId) {
                            npcDialoguePanel_->setVisible(false);
                            auto* npc = findNpcById(nId);
                            if (!npc) return;
                            auto* guildNPC = npc->getComponent<GuildNPCComponent>();
                            auto* scene = SceneManager::instance().currentScene();
                            if (!guildNPC || !scene) return;
                            scene->world().forEach<PlayerController>(
                                [&](Entity* e, PlayerController* ctrl) {
                                    if (!ctrl->isLocalPlayer) return;
                                    auto* guildComp = e->getComponent<GuildComponent>();
                                    auto* statsComp = e->getComponent<CharacterStatsComponent>();
                                    auto* invComp = e->getComponent<InventoryComponent>();
                                    if (!guildComp || !statsComp) return;
                                    if (statsComp->stats.level < guildNPC->requiredLevel) {
                                        if (chatPanel_) chatPanel_->addMessage(6, "[Guild]", "You don't meet the level requirement.", static_cast<uint8_t>(0));
                                    } else {
                                        int64_t gold = invComp ? invComp->inventory.getGold() : 0;
                                        if (guildComp->guild.createGuild(statsComp->stats.characterName + "'s Guild", gold)) {
                                            if (invComp) {
                                                int64_t spent = invComp->inventory.getGold() - gold;
                                                if (spent > 0) invComp->inventory.removeGold(spent);
                                            }
                                            if (chatPanel_) chatPanel_->addMessage(6, "[Guild]", "Guild created!", static_cast<uint8_t>(0));
                                        }
                                    }
                                }
                            );
                        };
                        npcDialoguePanel_->onOpenDungeon = [this](uint32_t nId) {
                            npcDialoguePanel_->setVisible(false);
                            auto* npc = findNpcById(nId);
                            if (npc) {
                                auto* dungeonComp = npc->getComponent<DungeonNPCComponent>();
                                if (dungeonComp && netClient_.isConnected()) {
                                    netClient_.sendStartDungeon(dungeonComp->dungeonSceneId);
                                }
                            }
                        };
                        npcDialoguePanel_->onClose = [this](const std::string&) {
                            closeAllNpcPanels();
                        };
                    }

                    // Wire sub-panel callbacks
                    auto closeAll = [this](const std::string&) { closeAllNpcPanels(); };

                    if (shopPanel_) {
                        shopPanel_->onBuy = [this](uint32_t nId, const std::string& itemId, uint16_t qty) {
                            netClient_.sendShopBuy(nId, itemId, qty);
                        };
                        shopPanel_->onSell = [this](uint32_t nId, uint8_t slot, uint16_t qty) {
                            netClient_.sendShopSell(nId, slot, qty);
                        };
                        shopPanel_->onClose = closeAll;
                    }

                    if (bankPanel_) {
                        bankPanel_->onDepositItem = [this](uint32_t nId, uint8_t slot) {
                            netClient_.sendBankDepositItem(nId, slot);
                        };
                        bankPanel_->onWithdrawItem = [this](uint32_t nId, uint16_t idx) {
                            netClient_.sendBankWithdrawItem(nId, idx);
                        };
                        bankPanel_->onDepositGold = [this](uint32_t nId, int64_t amt) {
                            netClient_.sendBankDepositGold(nId, amt);
                        };
                        bankPanel_->onWithdrawGold = [this](uint32_t nId, int64_t amt) {
                            netClient_.sendBankWithdrawGold(nId, amt);
                        };
                        bankPanel_->onClose = closeAll;
                    }

                    if (teleporterPanel_) {
                        teleporterPanel_->onTeleport = [this](uint32_t nId, uint8_t idx) {
                            netClient_.sendTeleport(nId, idx);
                        };
                        teleporterPanel_->onClose = closeAll;
                    }

                    LOG_INFO("GameApp", "Retained-mode UI screens loaded and wired");
                }

                break; // Skip rest of first frame
            }

            // Check editor pause state early so all InGame subsystems respect it
            bool editorPaused = Editor::instance().isPaused();

            // Update death overlay countdown timer
            if (deathOverlay_ && !editorPaused) deathOverlay_->update(deltaTime);

            // Update skill visual effects
            if (!editorPaused) SkillVFXPlayer::instance().update(deltaTime);

            // Network: poll for server messages and send movement
            netTime_ += deltaTime;
            if (netClient_.isConnected()) {
                // If play mode was stopped (paused but not in play mode), sever the
                // connection — otherwise server messages keep creating ghost entities
                // in the edit-mode world.
                if (editorPaused && !Editor::instance().inPlayMode()) {
                    netClient_.disconnect();
                    ghostEntities_.clear();
                    droppedItemPids_.clear();
                    ghostUpdateSeqs_.clear();
                    ghostDeathTimers_.clear();
                    localPlayerCreated_ = false;
                    retainedUILoaded_ = false;
                    connState_ = ConnectionState::LoginScreen;
                    LOG_INFO("GameApp", "Disconnected: play mode exited");
                } else if (!editorPaused) {
                    netClient_.poll(netTime_);
                    // Flush entities queued for destruction during poll (onEntityLeave, etc.)
                    if (auto* sc = SceneManager::instance().currentScene())
                        sc->world().processDestroyQueue();
                } else {
                    // Paused during play — poll for heartbeats only
                    netClient_.poll(netTime_);
                }

                // Process deferred zone transition (after poll completes safely)
                if (pendingZoneTransition_ && !editorPaused && connState_ == ConnectionState::InGame) {
                    // Clear ghost state and cached entity pointers before async load
                    // destroys all entities (prevents dangling pointer dereference)
                    ghostEntities_.clear();
                    droppedItemPids_.clear();
                    ghostDeathTimers_.clear();
                    ghostInterpolation_.clear();
                    ghostUpdateSeqs_.clear();
                    combatPredictions_.clear();
                    SkillVFXPlayer::instance().clear();
                    if (npcInteractionSystem_) npcInteractionSystem_->resetCachedPointers();
                    if (combatSystem_) combatSystem_->serverClearTarget();

                    // Start async load for new zone
                    std::string jsonPath = "assets/scenes/" + pendingZoneScene_ + ".json";
                    asyncLoader_.startLoad(pendingZoneScene_, jsonPath);
                    loadingScreen_.begin(pendingZoneScene_, windowWidth(), windowHeight());
                    setLoadingScreen(&loadingScreen_);
                    setIsLoading(true);
                    loadingMinTimer_ = 2.0f;
                    loadingDataReady_ = false;
                    connState_ = ConnectionState::LoadingScene;
                    // pendingZoneTransition_ stays true — entity buffering continues
                    // Entity replay + player creation happen when LoadingScene completes
                }

                // Update audio engine (fades, streaming, etc.)
                audioManager_.update(deltaTime);

                // Interpolate ghost entity positions (skip when paused)
                if (!editorPaused) {
                    auto* sc = SceneManager::instance().currentScene();
                    if (sc) {
                        for (auto& [pid, handle] : ghostEntities_) {
                            bool valid = false;
                            Vec2 pos = ghostInterpolation_.getInterpolatedPosition(pid, deltaTime, &valid);
                            // Never overwrite position with (0,0) from missing interpolation data
                            if (!valid) continue;
                            Entity* ghost = sc->world().getEntity(handle);
                            if (ghost) {
                                auto* t = ghost->getComponent<Transform>();
                                if (t) t->position = pos;
                            }
                        }
                    }
                }

                // Hide dead mob sprites after 3-second corpse delay
                if (!editorPaused && !ghostDeathTimers_.empty()) {
                    auto* sc2 = SceneManager::instance().currentScene();
                    if (sc2) {
                        constexpr float CORPSE_VISIBLE_DURATION = 3.0f;
                        for (auto it = ghostDeathTimers_.begin(); it != ghostDeathTimers_.end(); ) {
                            if (netTime_ - it->second >= CORPSE_VISIBLE_DURATION) {
                                auto git = ghostEntities_.find(it->first);
                                if (git != ghostEntities_.end()) {
                                    Entity* ghost = sc2->world().getEntity(git->second);
                                    if (ghost) {
                                        auto* spr = ghost->getComponent<SpriteComponent>();
                                        if (spr) spr->enabled = false;
                                        auto* np = ghost->getComponent<NameplateComponent>();
                                        if (np) np->visible = false;
                                    }
                                }
                                it = ghostDeathTimers_.erase(it);
                            } else {
                                ++it;
                            }
                        }
                    }
                }

                // Send movement 30 times/sec max (skip when paused, skip if position unchanged)
                if (!editorPaused && netTime_ - lastMoveSendTime_ >= 1.0f / 30.0f) {
                    auto* sc = SceneManager::instance().currentScene();
                    if (sc) {
                        sc->world().forEach<Transform, PlayerController>([&](Entity* entity, Transform* t, PlayerController* pc) {
                            if (pc->isLocalPlayer) {
                                // Only send if position actually changed (avoids spam when stuck)
                                float dx = t->position.x - lastSentPos_.x;
                                float dy = t->position.y - lastSentPos_.y;
                                if (dx * dx + dy * dy > 0.01f) {
                                    netClient_.sendMove(t->position, {0.0f, 0.0f}, netTime_);
                                    lastSentPos_ = t->position;
                                }
                            }
                        });
                    }
                    lastMoveSendTime_ = netTime_;
                }
            }

            // Parallel AI ticking via job system (skip when editor is paused)
            if (mobAISystem_ && !Editor::instance().isPaused()) {
                Counter* aiCounter = mobAISystem_->submitParallelUpdate(deltaTime);
                if (aiCounter) {
                    JobSystem::instance().waitForCounter(aiCounter, 0);
                    mobAISystem_->processDeferredAttacks();
                }
            }

            // ---- Fate HUD per-frame data push ----
            {
                auto* hudScreen = uiManager().getScreen("fate_hud");
                auto* scene = SceneManager::instance().currentScene();
                if (hudScreen && scene) {
                    Entity* localPlayer = nullptr;
                    scene->world().forEach<PlayerController>(
                        [&](Entity* e, PlayerController* ctrl) {
                            if (ctrl->isLocalPlayer) localPlayer = e;
                        }
                    );

                    if (localPlayer) {
                        auto* cs = localPlayer->getComponent<CharacterStatsComponent>();

                        // FateStatusBar — push player stats
                        auto* statusBar = dynamic_cast<FateStatusBar*>(hudScreen->findById("status_bar"));
                        if (statusBar && cs) {
                            statusBar->hp      = static_cast<float>(cs->stats.currentHP);
                            statusBar->maxHp   = static_cast<float>(cs->stats.maxHP);
                            statusBar->mp      = static_cast<float>(cs->stats.currentMP);
                            statusBar->maxMp   = static_cast<float>(cs->stats.maxMP);
                            statusBar->level   = cs->stats.level;
                            statusBar->xp      = static_cast<float>(cs->stats.currentXP);
                            statusBar->xpToLevel = static_cast<float>(cs->stats.xpToNextLevel);
                            statusBar->playerName = localPlayer->name();
                            auto* transform = localPlayer->getComponent<Transform>();
                            if (transform) {
                                statusBar->playerTileX = static_cast<int>(std::floor(transform->position.x / 32.0f));
                                statusBar->playerTileY = static_cast<int>(std::floor(transform->position.y / 32.0f));
                            }
                        }

                        // EXPBar
                        auto* expBar = dynamic_cast<EXPBar*>(hudScreen->findById("exp_bar"));
                        if (expBar && cs) {
                            expBar->xp        = static_cast<float>(cs->stats.currentXP);
                            expBar->xpToLevel = static_cast<float>(cs->stats.xpToNextLevel);
                        }

                        // TargetFrame — show/hide based on combat target
                        auto* tf = dynamic_cast<TargetFrame*>(hudScreen->findById("target_frame"));
                        if (tf && combatSystem_) {
                            if (combatSystem_->hasTarget()) {
                                tf->setVisible(true);
                                tf->targetName = combatSystem_->getTargetName();
                                tf->hp    = static_cast<float>(combatSystem_->getTargetHP());
                                tf->maxHp = static_cast<float>(combatSystem_->getTargetMaxHP());
                            } else {
                                tf->setVisible(false);
                            }
                        }

                        // SkillArc — push skill slot data from SkillManager
                        auto* smc = localPlayer->getComponent<SkillManagerComponent>();
                        if (skillArc_ && smc) {
                            int page = skillArc_->currentPage;
                            skillArc_->slots.resize(skillArc_->slotCount);
                            for (int si = 0; si < skillArc_->slotCount; ++si) {
                                int globalSlot = page * SkillArc::SLOTS_PER_PAGE + si;
                                std::string skillId = smc->skills.getSkillInSlot(globalSlot);
                                auto& slot = skillArc_->slots[si];
                                if (!skillId.empty()) {
                                    const auto* ls = smc->skills.getLearnedSkill(skillId);
                                    slot.skillId = skillId;
                                    slot.level   = ls ? ls->effectiveRank() : 0;
                                    slot.cooldownRemaining = smc->skills.getRemainingCooldown(skillId);
                                    const auto* def = smc->skills.getSkillDefinition(skillId);
                                    slot.cooldownTotal = def ? def->cooldownSeconds : 0.0f;
                                } else {
                                    slot.skillId.clear();
                                    slot.level = 0;
                                    slot.cooldownRemaining = 0.0f;
                                    slot.cooldownTotal = 0.0f;
                                }
                            }
                        }
                    }
                }
            }

            // ---- ChatPanel idle-mode time update ----
            if (chatPanel_) chatPanel_->updateTime(deltaTime);

            // ---- Menu panels per-frame data push ----
            {
                auto* menuScreen = uiManager().getScreen("fate_menu_panels");
                auto* scene2 = SceneManager::instance().currentScene();
                if (menuScreen && menuScreen->visible() && scene2) {
                    Entity* localPlayer = nullptr;
                    scene2->world().forEach<PlayerController>(
                        [&](Entity* e, PlayerController* ctrl) {
                            if (ctrl->isLocalPlayer) localPlayer = e;
                        }
                    );

                    if (localPlayer) {
                        auto* cs  = localPlayer->getComponent<CharacterStatsComponent>();
                        auto* inv = localPlayer->getComponent<InventoryComponent>();

                        // StatusPanel — push stats
                        auto* sp = dynamic_cast<StatusPanel*>(menuScreen->findById("status_panel"));
                        if (sp && cs) {
                            sp->playerName  = localPlayer->name();
                            sp->className   = cs->stats.className;
                            sp->level       = cs->stats.level;
                            sp->xp          = static_cast<float>(cs->stats.currentXP);
                            sp->xpToLevel   = static_cast<float>(cs->stats.xpToNextLevel);
                            sp->str = cs->stats.getStrength();
                            sp->intl = cs->stats.getIntelligence();
                            sp->dex = cs->stats.getDexterity();
                            sp->con = cs->stats.getVitality();
                            sp->wis = cs->stats.getWisdom();
                            sp->arm = cs->stats.getArmor();
                            sp->hit = static_cast<int>(cs->stats.getHitRate() * 100.0f);
                            sp->cri = static_cast<int>(cs->stats.getCritRate() * 100.0f);
                            sp->spd = static_cast<int>(cs->stats.getSpeed() * 100.0f);

                            // Gap #3: factionName from FactionComponent
                            auto* fc = localPlayer->getComponent<FactionComponent>();
                            if (fc) {
                                const auto* fdef = FactionRegistry::get(fc->faction);
                                sp->factionName = fdef ? fdef->displayName : "None";
                            } else {
                                sp->factionName = "None";
                            }
                        }

                        // InventoryPanel — push gold and item data
                        auto* ip = dynamic_cast<InventoryPanel*>(menuScreen->findById("inventory_panel"));
                        if (ip && inv) {
                            ip->gold  = static_cast<int>(inv->inventory.getGold());
                            ip->armorValue = cs ? cs->stats.getArmor() : 0;
                            // Push inventory items
                            const auto& slots = inv->inventory.getSlots();
                            int slotCount = (std::min)(static_cast<int>(slots.size()), InventoryPanel::MAX_SLOTS);
                            auto statName = [](StatType t) -> const char* {
                                switch (t) {
                                    case StatType::Strength:       return "STR";
                                    case StatType::Intelligence:   return "INT";
                                    case StatType::Dexterity:      return "DEX";
                                    case StatType::Vitality:       return "VIT";
                                    case StatType::Wisdom:         return "WIS";
                                    case StatType::MaxHealth:      return "HP";
                                    case StatType::MaxMana:        return "Mana";
                                    case StatType::HealthRegen:    return "HP Regen";
                                    case StatType::ManaRegen:      return "MP Regen";
                                    case StatType::Accuracy:       return "Hit Rate";
                                    case StatType::CriticalChance: return "Crit";
                                    case StatType::CriticalDamage: return "Crit Dmg";
                                    case StatType::Armor:          return "Armor";
                                    case StatType::Evasion:        return "Evasion";
                                    case StatType::MagicResist:    return "Magic Resist";
                                    default:                       return "???";
                                }
                            };
                            auto rarityStr = [](ItemRarity r) -> const char* {
                                switch (r) {
                                    case ItemRarity::Uncommon:  return "Uncommon";
                                    case ItemRarity::Rare:      return "Rare";
                                    case ItemRarity::Epic:      return "Epic";
                                    case ItemRarity::Legendary: return "Legendary";
                                    case ItemRarity::Unique:    return "Legendary";
                                    default:                    return "Common";
                                }
                            };
                            for (int si = 0; si < slotCount; ++si) {
                                if (slots[si].isValid()) {
                                    ip->items[si].itemId      = slots[si].itemId;
                                    ip->items[si].displayName = slots[si].displayName;
                                    ip->items[si].rarity      = rarityStr(slots[si].rarity);
                                    ip->items[si].itemType    = slots[si].itemType;
                                    ip->items[si].quantity    = slots[si].quantity;
                                    ip->items[si].enchantLevel = slots[si].enchantLevel;
                                    ip->items[si].levelReq    = slots[si].levelReq;
                                    ip->items[si].damageMin   = slots[si].damageMin;
                                    ip->items[si].damageMax   = slots[si].damageMax;
                                    ip->items[si].armor       = slots[si].armorValue;
                                    ip->items[si].statLines.clear();
                                    if (slots[si].damageMin > 0 || slots[si].damageMax > 0) {
                                        char buf[64]; snprintf(buf, sizeof(buf), "Attack %d-%d", slots[si].damageMin, slots[si].damageMax);
                                        ip->items[si].statLines.push_back(buf);
                                    }
                                    if (slots[si].armorValue > 0) {
                                        char buf[64]; snprintf(buf, sizeof(buf), "Armor %d", slots[si].armorValue);
                                        ip->items[si].statLines.push_back(buf);
                                    }
                                    for (const auto& rs : slots[si].rolledStats) {
                                        char buf[64]; snprintf(buf, sizeof(buf), "%s +%d",
                                            statName(rs.statType), rs.value);
                                        ip->items[si].statLines.push_back(buf);
                                    }
                                } else {
                                    ip->items[si].itemId.clear();
                                    ip->items[si].displayName.clear();
                                    ip->items[si].rarity.clear();
                                    ip->items[si].itemType.clear();
                                    ip->items[si].quantity = 0;
                                    ip->items[si].enchantLevel = 0;
                                    ip->items[si].levelReq = 0;
                                    ip->items[si].damageMin = 0;
                                    ip->items[si].damageMax = 0;
                                    ip->items[si].armor = 0;
                                    ip->items[si].statLines.clear();
                                }
                            }

                            // Gap #2: push equipment slot data
                            // InventoryPanel equipSlots: 0=Hat, 1=Armor, 2=Weapon, 3=Shield(SubWeapon),
                            //   4=Gloves, 5=Boots(Shoes), 6=Ring, 7=Necklace, 8=Belt, 9=Cloak
                            static const EquipmentSlot equipMap[] = {
                                EquipmentSlot::Hat, EquipmentSlot::Armor, EquipmentSlot::Weapon,
                                EquipmentSlot::SubWeapon, EquipmentSlot::Gloves, EquipmentSlot::Shoes,
                                EquipmentSlot::Ring, EquipmentSlot::Necklace,
                                EquipmentSlot::Belt, EquipmentSlot::Cloak
                            };
                            const auto& equipmentMap = inv->inventory.getEquipmentMap();
                            for (int ei = 0; ei < InventoryPanel::NUM_EQUIP_SLOTS; ++ei) {
                                auto it = equipmentMap.find(equipMap[ei]);
                                if (it != equipmentMap.end() && !it->second.itemId.empty()) {
                                    ip->equipSlots[ei].itemId      = it->second.itemId;
                                    ip->equipSlots[ei].name        = it->second.displayName;
                                    ip->equipSlots[ei].displayName = it->second.displayName;
                                    ip->equipSlots[ei].rarity      = rarityStr(it->second.rarity);
                                    ip->equipSlots[ei].itemType    = it->second.itemType;
                                    ip->equipSlots[ei].enchantLevel = it->second.enchantLevel;
                                    ip->equipSlots[ei].levelReq    = it->second.levelReq;
                                    ip->equipSlots[ei].damageMin   = it->second.damageMin;
                                    ip->equipSlots[ei].damageMax   = it->second.damageMax;
                                    ip->equipSlots[ei].armor       = it->second.armorValue;
                                    ip->equipSlots[ei].statLines.clear();
                                    if (it->second.damageMin > 0 || it->second.damageMax > 0) {
                                        char buf[64]; snprintf(buf, sizeof(buf), "Attack %d-%d", it->second.damageMin, it->second.damageMax);
                                        ip->equipSlots[ei].statLines.push_back(buf);
                                    }
                                    if (it->second.armorValue > 0) {
                                        char buf[64]; snprintf(buf, sizeof(buf), "Armor %d", it->second.armorValue);
                                        ip->equipSlots[ei].statLines.push_back(buf);
                                    }
                                    for (const auto& rs : it->second.rolledStats) {
                                        char buf[64]; snprintf(buf, sizeof(buf), "%s +%d",
                                            statName(rs.statType), rs.value);
                                        ip->equipSlots[ei].statLines.push_back(buf);
                                    }
                                } else {
                                    ip->equipSlots[ei].itemId.clear();
                                    ip->equipSlots[ei].name.clear();
                                    ip->equipSlots[ei].displayName.clear();
                                    ip->equipSlots[ei].rarity.clear();
                                    ip->equipSlots[ei].itemType.clear();
                                    ip->equipSlots[ei].enchantLevel = 0;
                                    ip->equipSlots[ei].levelReq = 0;
                                    ip->equipSlots[ei].damageMin = 0;
                                    ip->equipSlots[ei].damageMax = 0;
                                    ip->equipSlots[ei].armor = 0;
                                    ip->equipSlots[ei].statLines.clear();
                                }
                            }

                            // Paper doll textures
                            auto* sprite = localPlayer->getComponent<SpriteComponent>();
                            if (sprite && sprite->texture) {
                                ip->characterTexture = sprite->texture;
                            }
                            ip->armorTexture = nullptr;  // TODO: wire from equipment art
                            ip->hatTexture = nullptr;    // TODO: wire from equipment art
                        }

                        // Gap #4: SkillPanel — populate classSkills and remainingPoints
                        auto* sklPanel = dynamic_cast<SkillPanel*>(menuScreen->findById("skill_panel"));
                        auto* smcMenu = localPlayer->getComponent<SkillManagerComponent>();
                        if (sklPanel && smcMenu) {
                            sklPanel->remainingPoints = smcMenu->skills.availablePoints();
                            const auto& learned = smcMenu->skills.getLearnedSkills();
                            sklPanel->classSkills.resize(learned.size());
                            for (size_t si = 0; si < learned.size(); ++si) {
                                auto& info = sklPanel->classSkills[si];
                                info.name         = learned[si].skillId;
                                info.currentLevel  = learned[si].effectiveRank();
                                info.unlocked      = true;
                                const auto* def = smcMenu->skills.getSkillDefinition(learned[si].skillId);
                                info.maxLevel = def ? def->maxRank : 3;
                            }
                        }
                    }
                }
            }

            // ---- Social screen per-frame data push ----
            {
                auto* socialScreen = uiManager().getScreen("fate_social");
                auto* scene3 = SceneManager::instance().currentScene();
                if (socialScreen && scene3) {
                    Entity* localPlayer = nullptr;
                    scene3->world().forEach<PlayerController>(
                        [&](Entity* e, PlayerController* ctrl) {
                            if (ctrl->isLocalPlayer) localPlayer = e;
                        }
                    );

                    if (localPlayer) {
                        // Push party member data to PartyFrame
                        auto* partyComp = localPlayer->getComponent<PartyComponent>();
                        auto* pf = dynamic_cast<PartyFrame*>(socialScreen->findById("party_frame"));
                        if (pf) {
                            pf->members.clear();
                            if (partyComp && partyComp->party.isInParty()) {
                                for (const auto& pm : partyComp->party.members) {
                                    PartyFrameMemberInfo info;
                                    info.name     = pm.characterName;
                                    info.hp       = static_cast<float>(pm.currentHP);
                                    info.maxHp    = static_cast<float>(pm.maxHP > 0 ? pm.maxHP : 1);
                                    info.mp       = static_cast<float>(pm.currentMP);
                                    info.maxMp    = static_cast<float>(pm.maxMP > 0 ? pm.maxMP : 1);
                                    info.level    = pm.level;
                                    info.isLeader = pm.isLeader;
                                    pf->members.push_back(std::move(info));
                                    if (static_cast<int>(pf->members.size()) >= 2) break;
                                }
                            }
                        }

                        // Sync ChatPanel party/guild tab enable state
                        auto* chatPanel = dynamic_cast<ChatPanel*>(socialScreen->findById("chat_panel"));
                        auto* guildComp = localPlayer->getComponent<GuildComponent>();
                        if (chatPanel) {
                            chatPanel->isInParty = partyComp && partyComp->party.isInParty();
                            chatPanel->isInGuild = guildComp && guildComp->guild.guildId != 0;
                        }

                        // Gap #7: GuildPanel — push guild info
                        auto* gp = dynamic_cast<GuildPanel*>(socialScreen->findById("guild_panel"));
                        if (gp && guildComp) {
                            gp->guildName   = guildComp->guild.guildName;
                            gp->guildLevel  = guildComp->guild.guildLevel;
                            // TODO: roster API — server does not replicate full member list to client yet.
                            // When guild member list sync is added, populate gp->members here.
                            gp->memberCount = static_cast<int>(gp->members.size());
                        }

                        // Gap #8: TradeWindow — push slot/gold/partner/lock data
                        auto* tw = dynamic_cast<TradeWindow*>(socialScreen->findById("trade_window"));
                        auto* tradeComp = localPlayer->getComponent<TradeComponent>();
                        if (tw && tradeComp && tradeComp->trade.isInTrade()) {
                            tw->partnerName = tradeComp->trade.sessionState.otherPlayerName;
                            tw->myGold      = static_cast<int>(tradeComp->trade.sessionState.myGold);
                            tw->theirGold   = static_cast<int>(tradeComp->trade.sessionState.otherGold);
                            tw->myLocked    = tradeComp->trade.sessionState.myLocked;
                            tw->theirLocked = tradeComp->trade.sessionState.otherLocked;
                            // Push my trade slots
                            for (int ti = 0; ti < 9; ++ti) {
                                if (ti < static_cast<int>(tradeComp->trade.myItems.size()) &&
                                    !tradeComp->trade.myItems[ti].isEmpty()) {
                                    tw->mySlots[ti].itemId   = tradeComp->trade.myItems[ti].itemId;
                                    tw->mySlots[ti].quantity = tradeComp->trade.myItems[ti].quantity;
                                } else {
                                    tw->mySlots[ti].itemId.clear();
                                    tw->mySlots[ti].quantity = 0;
                                }
                            }
                            // Push their trade slots
                            for (int ti = 0; ti < 9; ++ti) {
                                if (ti < static_cast<int>(tradeComp->trade.otherItems.size()) &&
                                    !tradeComp->trade.otherItems[ti].isEmpty()) {
                                    tw->theirSlots[ti].itemId   = tradeComp->trade.otherItems[ti].itemId;
                                    tw->theirSlots[ti].quantity = tradeComp->trade.otherItems[ti].quantity;
                                } else {
                                    tw->theirSlots[ti].itemId.clear();
                                    tw->theirSlots[ti].quantity = 0;
                                }
                            }
                        }
                    }
                }
            }

            // ---- NPC panel per-frame data push ----
            {
                auto* scene = SceneManager::instance().currentScene();
                if (scene) {
                    // NPC dialogue panel: open when NPC interaction system triggers dialogue
                    Entity* interactingNPC = npcInteractionSystem_
                        ? scene->world().getEntity(npcInteractionSystem_->interactingNPCHandle) : nullptr;
                    if (npcInteractionSystem_ && npcInteractionSystem_->dialogueOpen
                        && interactingNPC
                        && npcDialoguePanel_ && !npcDialoguePanel_->isOpen()) {
                        auto* npc = interactingNPC;
                        auto* npcComp = npc->getComponent<NPCComponent>();
                        if (npcComp) {
                            npcDialoguePanel_->npcId = npcComp->npcId;
                            npcDialoguePanel_->npcName = npcComp->displayName;
                            npcDialoguePanel_->greeting = npcComp->dialogueGreeting;
                            npcDialoguePanel_->hasShop = npc->getComponent<ShopComponent>() != nullptr;
                            npcDialoguePanel_->hasBank = npc->getComponent<BankerComponent>() != nullptr;
                            npcDialoguePanel_->hasTeleporter = npc->getComponent<TeleporterComponent>() != nullptr;
                            npcDialoguePanel_->hasGuild = npc->getComponent<GuildNPCComponent>() != nullptr;
                            npcDialoguePanel_->hasDungeon = npc->getComponent<DungeonNPCComponent>() != nullptr;

                            // Populate quests from QuestGiverComponent
                            npcDialoguePanel_->quests.clear();
                            auto* qgComp = npc->getComponent<QuestGiverComponent>();
                            if (qgComp) {
                                for (uint32_t qId : qgComp->questIds) {
                                    NpcDialoguePanel::QuestEntry qe;
                                    qe.questId = qId;
                                    qe.questName = "Quest #" + std::to_string(qId);
                                    npcDialoguePanel_->quests.push_back(std::move(qe));
                                }
                            }

                            // Check story mode
                            auto* storyComp = npc->getComponent<StoryNPCComponent>();
                            npcDialoguePanel_->isStoryMode = (storyComp != nullptr);
                            if (storyComp && !storyComp->dialogueTree.empty()) {
                                // Story dialogue handled by NpcDialoguePanel render
                            }

                            npcDialoguePanel_->open();
                        }
                    }

                    // Push player data to shop panel each frame
                    if (shopPanel_ && shopPanel_->isOpen()) {
                        Entity* localPlayer = nullptr;
                        scene->world().forEach<PlayerController>(
                            [&](Entity* e, PlayerController* ctrl) {
                                if (ctrl->isLocalPlayer) localPlayer = e;
                            }
                        );
                        if (localPlayer) {
                            auto* inv = localPlayer->getComponent<InventoryComponent>();
                            if (inv) {
                                shopPanel_->playerGold = inv->inventory.getGold();
                                const auto& slots = inv->inventory.getSlots();
                                for (int i = 0; i < ShopPanel::MAX_SLOTS && i < static_cast<int>(slots.size()); ++i) {
                                    shopPanel_->playerItems[i].itemId = slots[i].itemId;
                                    shopPanel_->playerItems[i].displayName = slots[i].displayName.empty() ? slots[i].itemId : slots[i].displayName;
                                    shopPanel_->playerItems[i].quantity = slots[i].quantity;
                                    shopPanel_->playerItems[i].sellPrice = 0; // Server computes sell price
                                    shopPanel_->playerItems[i].soulbound = slots[i].isSoulbound;
                                }
                                // Clear remaining slots
                                for (int i = static_cast<int>(slots.size()); i < ShopPanel::MAX_SLOTS; ++i) {
                                    shopPanel_->playerItems[i].itemId.clear();
                                    shopPanel_->playerItems[i].displayName.clear();
                                    shopPanel_->playerItems[i].quantity = 0;
                                    shopPanel_->playerItems[i].sellPrice = 0;
                                    shopPanel_->playerItems[i].soulbound = false;
                                }
                            }
                        }
                    }

                    // Push player data to bank panel each frame
                    if (bankPanel_ && bankPanel_->isOpen()) {
                        Entity* localPlayer = nullptr;
                        scene->world().forEach<PlayerController>(
                            [&](Entity* e, PlayerController* ctrl) {
                                if (ctrl->isLocalPlayer) localPlayer = e;
                            }
                        );
                        if (localPlayer) {
                            auto* inv = localPlayer->getComponent<InventoryComponent>();
                            auto* bankComp = localPlayer->getComponent<BankStorageComponent>();
                            if (inv) {
                                bankPanel_->playerGold = inv->inventory.getGold();
                                const auto& slots = inv->inventory.getSlots();
                                for (int i = 0; i < BankPanel::MAX_SLOTS && i < static_cast<int>(slots.size()); ++i) {
                                    bankPanel_->playerItems[i].itemId = slots[i].itemId;
                                    bankPanel_->playerItems[i].displayName = slots[i].displayName.empty() ? slots[i].itemId : slots[i].displayName;
                                    bankPanel_->playerItems[i].quantity = slots[i].quantity;
                                }
                                for (int i = static_cast<int>(slots.size()); i < BankPanel::MAX_SLOTS; ++i) {
                                    bankPanel_->playerItems[i].itemId.clear();
                                    bankPanel_->playerItems[i].displayName.clear();
                                    bankPanel_->playerItems[i].quantity = 0;
                                }
                            }
                            if (bankComp) {
                                bankPanel_->bankGold = bankComp->storage.getStoredGold();
                                bankPanel_->bankItems.clear();
                                for (const auto& si : bankComp->storage.getItems()) {
                                    BankPanel::BankItem bi;
                                    bi.itemId = si.itemId;
                                    bi.displayName = si.fullItem.displayName.empty() ? si.itemId : si.fullItem.displayName;
                                    bi.count = si.count;
                                    bankPanel_->bankItems.push_back(bi);
                                }
                            }
                        }
                    }
                }
            }

            // F1 HUD toggle removed — HUD is always on
            // F2 collision debug removed — now controlled via editor toolbar toggle
            auto& input = Input::instance();

            // Touch controls are now handled by DPad/SkillArc widgets in
            // the retained-mode UI (see fate_hud.json wiring above).

            // UI toggles — action map suppresses these in Chat context automatically.
            // Keyboard routing in app.cpp already blocks events when editor is paused,
            // so no additional wantsKeyboard() guard needed here.
            if (input.isActionPressed(ActionId::ToggleInventory)) {
                // Toggle the menu panels screen; show last active tab (don't reset)
                auto* menuScr = uiManager().getScreen("fate_menu_panels");
                if (menuScr) {
                    bool opening = !menuScr->visible();
                    menuScr->setVisible(opening);
                    if (opening) {
                        auto* tabBar = dynamic_cast<MenuTabBar*>(menuScr->findById("tab_bar"));
                        if (tabBar) {
                            if (tabBar->onTabChanged) tabBar->onTabChanged(tabBar->activeTab);
                        }
                    }
                    // Hide/show game HUD controls when menu is open/closed
                    auto* hudScr = uiManager().getScreen("fate_hud");
                    if (hudScr) {
                        auto* dpad = hudScr->findById("dpad");
                        auto* arc  = hudScr->findById("skill_arc");
                        if (dpad) dpad->setVisible(!opening);
                        if (arc)  arc->setVisible(!opening);
                    }
                }
            }
            if (input.isActionPressed(ActionId::ToggleSkillBar)) {
                if (skillArc_) skillArc_->setVisible(!skillArc_->visible());
            }
            // Chat toggle (Enter key)
            if (input.isActionPressed(ActionId::OpenChat)) {
                if (!(chatPanel_ && chatPanel_->visible())) {
                    if (chatPanel_) chatPanel_->setVisible(true);
                    input.setChatMode(true);
                }
            }
            if (input.isActionPressed(ActionId::SubmitChat)) {
                // SubmitChat fires in Chat context after Enter on the input field.
                // ChatPanel handles send/hide internally; we just exit chat mode when it hides.
                if (!(chatPanel_ && chatPanel_->visible())) {
                    input.setChatMode(false);
                }
            }
            if (input.isActionPressed(ActionId::Cancel) && chatPanel_ &&
                (chatPanel_->visible() || chatPanel_->isFullPanelMode())) {
                chatPanel_->setFullPanelMode(false);
                chatPanel_->setVisible(true); // stay in idle overlay mode
                input.setChatMode(false);
            }
            // Skill bar page switching
            if (input.isActionPressed(ActionId::SkillPagePrev)) {
                if (skillArc_) skillArc_->prevPage();
            }
            if (input.isActionPressed(ActionId::SkillPageNext)) {
                if (skillArc_) skillArc_->nextPage();
            }

            // Set UI blocking flag — movement + nameplates suppressed while panels are open
            input.setUIBlocking(
                (inventoryPanel_ && inventoryPanel_->visible()) ||
                (shopPanel_ && shopPanel_->isOpen()) ||
                (bankPanel_ && bankPanel_->isOpen()) ||
                (teleporterPanel_ && teleporterPanel_->isOpen()) ||
                (npcDialoguePanel_ && npcDialoguePanel_->isOpen()) ||
                (npcInteractionSystem_ && npcInteractionSystem_->dialogueOpen)
            );
            break;
        }
    }

    // Set retained-mode UI input offset so mouse coords map to widget coords.
    // In the editor, widgets are laid out in FBO space (0,0 to vpW,vpH) but the
    // mouse position is in window space. Subtract the editor viewport origin.
    auto& ed = Editor::instance();
    Vec2 vp = ed.viewportPos();
    uiManager().setInputTransform(vp.x, vp.y, 1.0f, 1.0f);
}

void GameApp::onRender(SpriteBatch& batch, Camera& camera) {
    // Pre-game states: login/character-creation screens render via uiManager automatically.
    // Just skip gameplay rendering when not InGame.
    if (connState_ != ConnectionState::InGame) {
        return;
    }

    // ---- InGame rendering ----

    // Scene rendering (tiles, entities, combat text, debug overlays) is now handled
    // by render graph passes registered in onInit(). This callback only handles
    // ImGui game UI and screen-space overlays that render into the editor viewport FBO.

    // ImGui game UI — suppress when editor is open and paused (no gameplay happening)
    if (!(Editor::instance().isOpen() && Editor::instance().isPaused())) {
        // Set the global game viewport rect — all UI systems read from this
        auto& ed = Editor::instance();
        Vec2 vp = ed.viewportPos();
        Vec2 vs = ed.viewportSize();
        GameViewport::set(vp.x, vp.y, vs.x, vs.y);

        auto* scene = SceneManager::instance().currentScene();
        if (scene) {
            // All HUD/chat/death/NPC/touch UIs now rendered by retained-mode uiManager().
        }
    }

    // Network config panel (debug only — hidden in shipping builds)
#ifndef FATE_SHIPPING
    if (Editor::instance().isOpen()) {
        drawNetworkPanel();
    }
#endif

    // Zone transition fade overlay
    if (zoneSystem_ && zoneSystem_->isTransitioning()) {
        float alpha = zoneSystem_->fadeAlpha();
        Mat4 screenVP = Mat4::ortho(0, (float)windowWidth(), (float)windowHeight(), 0, -1, 1);
        batch.begin(screenVP);
        batch.drawRect({(float)windowWidth() * 0.5f, (float)windowHeight() * 0.5f},
                      {(float)windowWidth(), (float)windowHeight()},
                      Color(0, 0, 0, alpha), 200.0f);
        batch.end();
    }
}

void GameApp::drawNetworkPanel() {
    ImGui::SetNextWindowSize(ImVec2(280, 160), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Network", &showNetPanel_)) {
        ImGui::End();
        return;
    }

    if (netClient_.isConnected()) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Connected");
        ImGui::Text("Client ID: %d", netClient_.clientId());
        ImGui::Text("Ghosts: %zu", ghostEntities_.size());

        if (ImGui::Button("Disconnect")) {
            netClient_.disconnect();
            auto* scene = SceneManager::instance().currentScene();
            if (scene) {
                // Clean up ghost entities
                for (auto& [pid, handle] : ghostEntities_) {
                    scene->world().destroyEntity(handle);
                }
                // Destroy local player entity
                scene->world().forEach<PlayerController>(
                    [&](Entity* e, PlayerController* ctrl) {
                        if (ctrl->isLocalPlayer) {
                            scene->world().destroyEntity(e->handle());
                        }
                    }
                );
                scene->world().processDestroyQueue();
            }
            ghostEntities_.clear();
            ghostDeathTimers_.clear();
            ghostInterpolation_.clear();
            combatPredictions_.clear();
            SkillVFXPlayer::instance().clear();
            localPlayerCreated_ = false;
            retainedUILoaded_ = false;
            npcDialoguePanel_ = nullptr;
            shopPanel_ = nullptr;
            bankPanel_ = nullptr;
            teleporterPanel_ = nullptr;
            dungeonInviteDialog_ = nullptr;
            inDungeon_ = false;
            connState_ = ConnectionState::LoginScreen;
            if (loginScreenWidget_) {
                loginScreenWidget_->reset();
                loginScreenWidget_->setVisible(true);
            }
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Disconnected");
        ImGui::InputText("Host", serverHost_, sizeof(serverHost_));
        ImGui::InputInt("Port", &serverPort_);

        if (ImGui::Button("Connect")) {
            if (netClient_.connect(serverHost_, static_cast<uint16_t>(serverPort_))) {
                LOG_INFO("Net", "Connecting to %s:%d...", serverHost_, serverPort_);
            } else {
                LOG_ERROR("Net", "Failed to connect to %s:%d", serverHost_, serverPort_);
            }
        }
    }

    ImGui::End();
}

// renderHUD removed — controls hint text is not needed in the engine editor

// ============================================================================
// Editor Debug Panel — FPS, position, entities, player stats (F3 only)
// ============================================================================

// renderEditorDebugPanel() removed — now handled by Editor::drawDebugInfoPanel()

void GameApp::renderCollisionDebug(SpriteBatch& batch, Camera& camera) {
    auto* scene = SceneManager::instance().currentScene();
    if (!scene) return;

    Mat4 vp = camera.getViewProjection();
    batch.begin(vp);

    scene->world().forEach<Transform, BoxCollider>(
        [&](Entity* entity, Transform* transform, BoxCollider* collider) {
            Rect bounds = collider->getBounds(transform->position);

            // Green for static (trees/walls), yellow for dynamic (player)
            Color color = collider->isStatic
                ? Color(0.0f, 1.0f, 0.0f, 0.35f)   // green, semi-transparent
                : Color(1.0f, 1.0f, 0.0f, 0.35f);   // yellow, semi-transparent

            // Draw filled rect for the collision area
            batch.drawRect(
                {bounds.x + bounds.w * 0.5f, bounds.y + bounds.h * 0.5f},
                {bounds.w, bounds.h},
                color,
                100.0f  // high depth so it draws on top of everything
            );

            // Draw border lines (4 thin rects for the outline)
            Color border = collider->isStatic
                ? Color(0.0f, 1.0f, 0.0f, 0.9f)
                : Color(1.0f, 1.0f, 0.0f, 0.9f);
            float t = 1.0f; // border thickness

            // Top
            batch.drawRect({bounds.x + bounds.w * 0.5f, bounds.y + bounds.h - t * 0.5f},
                          {bounds.w, t}, border, 101.0f);
            // Bottom
            batch.drawRect({bounds.x + bounds.w * 0.5f, bounds.y + t * 0.5f},
                          {bounds.w, t}, border, 101.0f);
            // Left
            batch.drawRect({bounds.x + t * 0.5f, bounds.y + bounds.h * 0.5f},
                          {t, bounds.h}, border, 101.0f);
            // Right
            batch.drawRect({bounds.x + bounds.w - t * 0.5f, bounds.y + bounds.h * 0.5f},
                          {t, bounds.h}, border, 101.0f);
        }
    );

    // Draw polygon colliders as wireframe outlines
    scene->world().forEach<Transform, PolygonCollider>(
        [&](Entity* entity, Transform* transform, PolygonCollider* poly) {
            if (poly->points.size() < 2) return;

            Color color = poly->isStatic
                ? Color(0.0f, 0.8f, 1.0f, 0.6f)
                : Color(1.0f, 0.5f, 0.0f, 0.6f);

            auto worldPts = poly->getWorldPoints(transform->position);

            // Draw edges as rotated thin rectangles
            for (size_t i = 0; i < worldPts.size(); i++) {
                size_t j = (i + 1) % worldPts.size();
                Vec2 a = worldPts[i];
                Vec2 b = worldPts[j];
                Vec2 mid = (a + b) * 0.5f;
                Vec2 diff = b - a;
                float len = diff.length();
                if (len < 0.1f) continue;

                float angle = std::atan2(diff.y, diff.x);

                SpriteDrawParams params;
                params.position = mid;
                params.size = {len, 1.5f};
                params.color = color;
                params.rotation = angle;
                params.depth = 101.0f;
                batch.drawRect(mid, {len, 1.5f}, color, 101.0f);
                // Actually use the sprite batch with rotation for proper angled lines
                // drawRect doesn't support rotation, so we draw small segments instead
            }

            // Draw filled polygon approximation (connect to center for fill)
            if (worldPts.size() >= 3) {
                Vec2 center = {0, 0};
                for (auto& pt : worldPts) center += pt;
                center = center * (1.0f / worldPts.size());

                Color fill = color;
                fill.a = 0.15f;
                for (size_t i = 0; i < worldPts.size(); i++) {
                    size_t j = (i + 1) % worldPts.size();
                    // Draw thin triangles from center to each edge as small rects
                    Vec2 edgeMid = (worldPts[i] + worldPts[j]) * 0.5f;
                    Vec2 toCenter = center - edgeMid;
                    float dist = toCenter.length();
                    if (dist > 0.1f) {
                        batch.drawRect((edgeMid + center) * 0.5f,
                                      {2.0f, dist}, fill, 100.5f);
                    }
                }
            }

            // Draw edge lines as series of small dots
            for (size_t i = 0; i < worldPts.size(); i++) {
                size_t j = (i + 1) % worldPts.size();
                Vec2 a = worldPts[i];
                Vec2 b = worldPts[j];
                Vec2 diff = b - a;
                float len = diff.length();
                if (len < 0.1f) continue;

                int steps = (int)(len / 2.0f);
                if (steps < 2) steps = 2;
                for (int s = 0; s <= steps; s++) {
                    float t = (float)s / steps;
                    Vec2 pt = a + diff * t;
                    batch.drawRect(pt, {1.5f, 1.5f}, color, 101.0f);
                }
            }

            // Draw vertex handles
            for (auto& pt : worldPts) {
                batch.drawRect(pt, {5.0f, 5.0f}, Color(1, 1, 1, 0.9f), 102.0f);
            }
        }
    );

    batch.end();

    // Zone/Portal debug overlay
    if (zoneSystem_) {
        zoneSystem_->renderDebug(batch, camera);
    }
}

void GameApp::renderAggroRadius(SpriteBatch& batch, Camera& camera) {
    auto* scene = SceneManager::instance().currentScene();
    if (!scene) return;

    Mat4 vp = camera.getViewProjection();
    batch.begin(vp);

    scene->world().forEach<Transform, MobAIComponent>(
        [&](Entity* entity, Transform* transform, MobAIComponent* aiComp) {
            auto& ai = aiComp->ai;

            // Only draw if toggled on for this mob
            if (!ai.showAggroRadius) return;

            // Skip dead mobs
            auto* enemyComp = entity->getComponent<EnemyStatsComponent>();
            if (enemyComp && !enemyComp->stats.isAlive) return;

            Vec2 center = transform->position;

            // Acquire radius (red) — aggro detection range
            if (ai.acquireRadius > 0.0f) {
                constexpr int segments = 48;
                for (int i = 0; i < segments; i++) {
                    float angle = (float)i / (float)segments * 6.2831853f;
                    float px = center.x + std::cos(angle) * ai.acquireRadius;
                    float py = center.y + std::sin(angle) * ai.acquireRadius;
                    batch.drawRect({px, py}, {1.5f, 1.5f}, Color(1.0f, 0.3f, 0.3f, 0.6f), 96.0f);
                }
            }

            // Contact radius / leash (yellow, dimmer) — chase leash range
            if (ai.contactRadius > 0.0f) {
                constexpr int segments = 48;
                for (int i = 0; i < segments; i++) {
                    float angle = (float)i / (float)segments * 6.2831853f;
                    float px = center.x + std::cos(angle) * ai.contactRadius;
                    float py = center.y + std::sin(angle) * ai.contactRadius;
                    batch.drawRect({px, py}, {1.0f, 1.0f}, Color(1.0f, 1.0f, 0.3f, 0.3f), 96.0f);
                }
            }

            // Attack range (green, small) — melee/ranged attack distance
            if (ai.attackRange > 0.0f) {
                constexpr int segments = 32;
                for (int i = 0; i < segments; i++) {
                    float angle = (float)i / (float)segments * 6.2831853f;
                    float px = center.x + std::cos(angle) * ai.attackRange;
                    float py = center.y + std::sin(angle) * ai.attackRange;
                    batch.drawRect({px, py}, {1.0f, 1.0f}, Color(0.3f, 1.0f, 0.3f, 0.5f), 96.0f);
                }
            }
        }
    );

    batch.end();
}

void GameApp::onShutdown() {
    // Disconnect from server before shutdown so the server saves the correct
    // player state immediately (alive, current position, XP). Without this,
    // the server-side player entity stays alive and AFK — mobs kill it before
    // the timeout fires, saving is_dead=true.
    if (netClient_.isConnected()) {
        netClient_.disconnect();
        // Give the OS time to flush the UDP disconnect packets before
        // the process exits and the socket is destroyed.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    audioManager_.shutdown();
    tilemap_.reset();
    SDFText::instance().shutdown();
    delete renderSystem_;
    renderSystem_ = nullptr;

    // Null out raw pointers to systems owned by the World (via addSystem).
    // The World will destroy these when the scene is unloaded; keeping stale
    // pointers here would risk use-after-free.
    gameplaySystem_ = nullptr;
    mobAISystem_ = nullptr;
    combatSystem_ = nullptr;
    zoneSystem_ = nullptr;
    npcInteractionSystem_ = nullptr;
    questSystem_ = nullptr;
    npcDialoguePanel_ = nullptr;
    shopPanel_ = nullptr;
    bankPanel_ = nullptr;
    teleporterPanel_ = nullptr;
    dungeonInviteDialog_ = nullptr;
    inDungeon_ = false;

    LOG_INFO("Game", "Game shutting down...");
}

// ============================================================================
// NPC panel helpers
// ============================================================================

Entity* GameApp::findNpcById(uint32_t npcId) {
    Entity* found = nullptr;
    auto* scene = SceneManager::instance().currentScene();
    if (!scene) return nullptr;
    scene->world().forEach<NPCComponent>([&](Entity* e, NPCComponent* nc) {
        if (nc->npcId == npcId) found = e;
    });
    return found;
}

void GameApp::captureLocalPlayerState() {
    auto* sc = SceneManager::instance().currentScene();
    if (!sc) return;
    sc->world().forEach<CharacterStatsComponent, PlayerController>(
        [this](Entity* e, CharacterStatsComponent* cs, PlayerController* ctrl) {
            if (!ctrl->isLocalPlayer) return;
            pendingPlayerState_.level = cs->stats.level;
            pendingPlayerState_.currentHP = cs->stats.currentHP;
            pendingPlayerState_.maxHP = cs->stats.maxHP;
            pendingPlayerState_.currentMP = cs->stats.currentMP;
            pendingPlayerState_.maxMP = cs->stats.maxMP;
            pendingPlayerState_.currentFury = cs->stats.currentFury;
            pendingPlayerState_.currentXP = cs->stats.currentXP;
            pendingPlayerState_.honor = cs->stats.honor;
            pendingPlayerState_.pvpKills = cs->stats.pvpKills;
            pendingPlayerState_.pvpDeaths = cs->stats.pvpDeaths;
            auto* inv = e->getComponent<InventoryComponent>();
            if (inv) pendingPlayerState_.gold = inv->inventory.getGold();
        }
    );
}

void GameApp::closeAllNpcPanels() {
    if (npcDialoguePanel_) npcDialoguePanel_->close();
    if (shopPanel_) shopPanel_->close();
    if (bankPanel_) bankPanel_->close();
    if (teleporterPanel_) teleporterPanel_->close();
    if (npcInteractionSystem_) npcInteractionSystem_->closeDialogue();
}

} // namespace fate

