#include "game/game_app.h"
#include "engine/core/logger.h"
#include "engine/render/gfx/backend/gl/gl_loader.h"
#include "engine/scene/scene_manager.h"
#include "engine/editor/editor.h"
#include "game/register_components.h"
#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "game/components/player_controller.h"
#include "game/components/animator.h"
#include "game/components/box_collider.h"
#include "game/components/polygon_collider.h"
#include "game/components/game_components.h"
#include "engine/render/sdf_text.h"
#include "engine/ecs/prefab.h"
#include "game/entity_factory.h"
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
#include "engine/particle/particle_system.h"
#include "engine/particle/particle_emitter_component.h"
#include "game/ui/inventory_ui.h"
#include "game/ui/skill_bar_ui.h"
#include "game/ui/hud_bars_ui.h"
#include "game/shared/npc_types.h"
#include "imgui.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include "stb_image_write.h"
namespace fs = std::filesystem;  // std::min, std::max (used with parenthesized calls to avoid Windows macro conflict)

namespace fate {

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

static void generateVillageTiles() {
    fs::create_directories("assets/tiles");
    fs::create_directories("assets/sprites");

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

    // Set up editor and prefab library
    Editor::instance().setAssetRoot("assets");
#ifdef FATE_SOURCE_DIR
    Editor::instance().setSourceDir(FATE_SOURCE_DIR "/assets/scenes");
    PrefabLibrary::instance().setSourceDirectory(FATE_SOURCE_DIR "/assets/prefabs");
#endif
    PrefabLibrary::instance().setDirectory("assets/prefabs");
    PrefabLibrary::instance().loadAll();

    // Register fallback scene factory (used if no saved scene exists)
    SceneManager::instance().registerScene("TestScene", [this](Scene& scene) {
        createPlayer(scene.world());
        createTestEntities(scene.world());
        spawnTestMobs(scene.world());
        spawnTestNPCs(scene.world());
    });

    SceneManager::instance().switchScene("TestScene");

    // Add systems (these operate on whatever entities are in the scene)
    auto* scene = SceneManager::instance().currentScene();
    if (scene) {
        World& world = scene->world();

        world.addSystem<MovementSystem>();
        world.addSystem<AnimationSystem>();

        auto* cameraFollow = world.addSystem<CameraFollowSystem>();
        cameraFollow->camera = &camera();

        gameplaySystem_ = world.addSystem<GameplaySystem>();
        mobAISystem_ = world.addSystem<MobAISystem>();

        npcInteractionSystem_ = world.addSystem<NPCInteractionSystem>();
        npcInteractionSystem_->camera = &camera();

        combatSystem_ = world.addSystem<CombatActionSystem>();
        combatSystem_->camera = &camera();

        questSystem_ = world.addSystem<QuestSystem>();

        zoneSystem_ = world.addSystem<ZoneSystem>();
        zoneSystem_->camera = &camera();

        world.addSystem<SpawnSystem>();
        world.addSystem<ParticleSystem>();

        renderSystem_ = new SpriteRenderSystem();
        renderSystem_->batch = &spriteBatch();
        renderSystem_->camera = &camera();
        renderSystem_->init(&world);
    }

    // Net client callbacks for ghost entity management
    netClient_.onEntityEnter = [this](const SvEntityEnterMsg& msg) {
        auto* sc = SceneManager::instance().currentScene();
        if (!sc) return;
        auto& world = sc->world();
        Entity* ghost = nullptr;
        if (msg.entityType == 0) { // player
            ghost = EntityFactory::createGhostPlayer(world, msg.name, msg.position);
        } else if (msg.entityType == 3) { // dropped item
            ghost = EntityFactory::createGhostDroppedItem(world, msg.name, msg.position);
        } else { // mob or npc
            ghost = EntityFactory::createGhostMob(world, msg.name, msg.position);
        }
        if (ghost) {
            ghostEntities_[msg.persistentId] = ghost->handle();
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
        }
        ghostInterpolation_.removeEntity(msg.persistentId);
    };

    netClient_.onEntityUpdate = [this](const SvEntityUpdateMsg& msg) {
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
    };

    netClient_.onCombatEvent = [this](const SvCombatEventMsg& msg) {
        LOG_INFO("Combat", "Damage: %d to entity %llu%s%s",
                 msg.damage, (unsigned long long)msg.targetId,
                 msg.isCrit ? " (CRIT)" : "",
                 msg.isKill ? " (KILL)" : "");
    };

    netClient_.onPlayerState = [this](const SvPlayerStateMsg& msg) {
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;
        scene->world().forEach<CharacterStatsComponent, PlayerController>(
            [&](Entity*, CharacterStatsComponent* stats, PlayerController* ctrl) {
                if (!ctrl->isLocalPlayer) return;
                stats->stats.currentHP = msg.currentHP;
                stats->stats.maxHP = msg.maxHP;
                stats->stats.currentMP = msg.currentMP;
                stats->stats.maxMP = msg.maxMP;
                stats->stats.currentFury = msg.currentFury;
                stats->stats.currentXP = msg.currentXP;
                stats->stats.level = msg.level;
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
        LOG_INFO("Chat", "[%s] %s", msg.senderName.c_str(), msg.message.c_str());
    };

    netClient_.onLootPickup = [this](const SvLootPickupMsg& msg) {
        LOG_INFO("Client", "Picked up: %s x%d", msg.displayName.c_str(),
                 msg.isGold ? msg.goldAmount : msg.quantity);
    };

    // Auth callbacks
    netClient_.onConnectRejected = [this](const std::string& reason) {
        LOG_WARN("GameApp", "Connection rejected: %s", reason.c_str());
        loginScreen_.statusMessage = "Connection rejected: " + reason;
        loginScreen_.isError = true;
        connState_ = ConnectionState::LoginScreen;
    };

    netClient_.onDisconnected = [this]() {
        if (connState_ == ConnectionState::InGame) {
            connState_ = ConnectionState::LoginScreen;
            localPlayerCreated_ = false;
            loginScreen_.reset();
            LOG_INFO("GameApp", "Disconnected, returning to login screen");
        }
    };

    // Initialize SDF text rendering
    SDFText::instance().init("assets/fonts/default.png", "assets/fonts/default.json");

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

    // Auto-load last saved scene (replaces procedural entities, keeps systems)
    if (fs::exists("assets/scenes/scene.json")) {
        auto* s = SceneManager::instance().currentScene();
        if (s) {
            Editor::instance().loadScene(&s->world(), "assets/scenes/scene.json");
            LOG_INFO("Game", "Auto-loaded saved scene");
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

        if (renderSystem_) {
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

    // Pass: SDFText — floating damage/XP text (accumulates onto Scene FBO)
    graph.addPass({"SDFText", true, [this](RenderPassContext& ctx) {
        auto& sceneFbo = ctx.graph->getFBO("Scene", ctx.viewportWidth, ctx.viewportHeight, true);
        sceneFbo.bind();

        if (combatSystem_) {
            combatSystem_->renderFloatingTexts(*ctx.spriteBatch, *ctx.camera);
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

        auto* scene = SceneManager::instance().currentScene();
        if (scene) {
            if (auto* spawnSys = scene->world().getSystem<SpawnSystem>()) {
                spawnSys->renderDebug(*ctx.spriteBatch, *ctx.camera);
            }
        }

        sceneFbo.unbind();
    }});

    LOG_INFO("Game", "Initialized");
}

void GameApp::createPlayer(World& world) {
    // TODO: faction selection UI — hardcoded to Xyros for now
    Faction playerFaction = Faction::Xyros;
    Entity* player = EntityFactory::createPlayer(world, "Player", ClassType::Warrior, true, playerFaction);

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
            // 20x33 character sprite — TWOM-style warrior
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
        case ConnectionState::LoginScreen: {
            // Only process login screen — no gameplay
            if (loginScreen_.loginSubmitted) {
                loginScreen_.loginSubmitted = false;
                authClient_.loginAsync(loginScreen_.serverHost, static_cast<uint16_t>(loginScreen_.serverPort),
                                       loginScreen_.username, loginScreen_.password);
                connState_ = ConnectionState::Authenticating;
                loginScreen_.statusMessage = "Authenticating...";
                loginScreen_.isError = false;
            }
            if (loginScreen_.registerSubmitted) {
                loginScreen_.registerSubmitted = false;
                const char* classNames[] = {"Warrior", "Mage", "Archer"};
                authClient_.registerAsync(loginScreen_.serverHost, static_cast<uint16_t>(loginScreen_.serverPort),
                                          loginScreen_.username, loginScreen_.password,
                                          loginScreen_.email,
                                          loginScreen_.characterName,
                                          classNames[loginScreen_.selectedClass]);
                connState_ = ConnectionState::Authenticating;
                loginScreen_.statusMessage = "Creating account...";
                loginScreen_.isError = false;
            }
            break;
        }

        case ConnectionState::Authenticating: {
            // Poll for auth result
            if (authClient_.hasResult()) {
                AuthResponse resp = authClient_.consumeResult();
                if (resp.success) {
                    pendingAuthToken_ = resp.authToken;
                    pendingCharName_ = resp.characterName;
                    pendingClassName_ = resp.className;
                    // Connect to game server via UDP with auth token
                    std::string host = loginScreen_.serverHost;
                    netClient_.connectWithToken(host, static_cast<uint16_t>(serverPort_), pendingAuthToken_);
                    connState_ = ConnectionState::UDPConnecting;
                    loginScreen_.statusMessage = "Connecting to game server...";
                } else {
                    loginScreen_.statusMessage = resp.errorReason;
                    loginScreen_.isError = true;
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
                connState_ = ConnectionState::InGame;
                loginScreen_.statusMessage = "";

                // Defer player creation to next frame by setting flag
                localPlayerCreated_ = false;

                LOG_INFO("GameApp", "Connected to game server, entering game as '%s' (%s)",
                         pendingCharName_.c_str(), pendingClassName_.c_str());
            }
            // ConnectReject is handled by the onConnectRejected callback set up in onInit
            break;
        }

        case ConnectionState::InGame: {
            // Deferred player creation — runs on first InGame frame
            if (!localPlayerCreated_) {
                localPlayerCreated_ = true;
                auto* sc = SceneManager::instance().currentScene();
                if (sc) {
                    // Parse class type from pending class name
                    ClassType ct = ClassType::Warrior;
                    if (pendingClassName_ == "Mage") ct = ClassType::Mage;
                    else if (pendingClassName_ == "Archer") ct = ClassType::Archer;

                    // Create full player with all 24 game components
                    Entity* player = EntityFactory::createPlayer(
                        sc->world(), pendingCharName_, ct, true, Faction::Xyros);

                    LOG_INFO("GameApp", "Local player created for '%s' (%s) with %zu components",
                             pendingCharName_.c_str(), pendingClassName_.c_str(),
                             player->componentCount());
                }
                break; // Skip rest of first frame
            }

            // Network: poll for server messages and send movement
            netTime_ += deltaTime;
            if (netClient_.isConnected()) {
                netClient_.poll(netTime_);

                // Interpolate ghost entity positions
                {
                    auto* sc = SceneManager::instance().currentScene();
                    if (sc) {
                        for (auto& [pid, handle] : ghostEntities_) {
                            Vec2 pos = ghostInterpolation_.getInterpolatedPosition(pid, deltaTime);
                            Entity* ghost = sc->world().getEntity(handle);
                            if (ghost) {
                                auto* t = ghost->getComponent<Transform>();
                                if (t) t->position = pos;
                            }
                        }
                    }
                }

                // Send movement 30 times/sec max
                if (netTime_ - lastMoveSendTime_ >= 1.0f / 30.0f) {
                    auto* sc = SceneManager::instance().currentScene();
                    if (sc) {
                        sc->world().forEach<Transform, PlayerController>([&](Entity* entity, Transform* t, PlayerController* pc) {
                            if (pc->isLocalPlayer) {
                                netClient_.sendMove(t->position, {0.0f, 0.0f}, netTime_);
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

            // F1 HUD toggle removed — HUD is always on
            // F2 collision debug removed — now controlled via editor toolbar toggle
            auto& input = Input::instance();

            // UI toggles — action map suppresses these in Chat context automatically
            if (input.isActionPressed(ActionId::ToggleInventory) && !Editor::instance().wantsKeyboard()) {
                InventoryUI::instance().toggle();
            }
            if (input.isActionPressed(ActionId::ToggleSkillBar) && !Editor::instance().wantsKeyboard()) {
                SkillBarUI::instance().toggle();
            }
            if (input.isActionPressed(ActionId::ToggleQuestLog) && !Editor::instance().wantsKeyboard()) {
                questLogUI_.toggle();
            }
            // Skill bar page switching
            if (input.isActionPressed(ActionId::SkillPagePrev) && !Editor::instance().wantsKeyboard()) {
                SkillBarUI::instance().prevPage();
            }
            if (input.isActionPressed(ActionId::SkillPageNext) && !Editor::instance().wantsKeyboard()) {
                SkillBarUI::instance().nextPage();
            }
            break;
        }
    }
}

void GameApp::onRender(SpriteBatch& batch, Camera& camera) {
    // Draw login screen in all pre-game states
    if (connState_ != ConnectionState::InGame) {
        loginScreen_.draw();
        return;
    }

    // ---- InGame rendering ----

    // Scene rendering (tiles, entities, combat text, debug overlays) is now handled
    // by render graph passes registered in onInit(). This callback only handles
    // ImGui game UI and screen-space overlays that render into the editor viewport FBO.

    // ImGui game UI — suppress when editor is open and paused (no gameplay happening)
    if (!(Editor::instance().isOpen() && Editor::instance().isPaused())) {
        // Position HUD bars relative to the viewport panel
        auto& ed = Editor::instance();
        Vec2 vp = ed.viewportPos();
        Vec2 vs = ed.viewportSize();
        HudBarsUI::instance().setViewportRect(vp.x, vp.y, vs.x, vs.y);

        auto* scene = SceneManager::instance().currentScene();
        if (scene) {
            World* w = &scene->world();
            HudBarsUI::instance().draw(w);
            SkillBarUI::instance().draw(w);
            InventoryUI::instance().draw(w);

            // NPC dialogue UI
            if (npcInteractionSystem_ && npcInteractionSystem_->dialogueOpen
                && npcInteractionSystem_->interactingNPC) {
                npcDialogueUI_.render(
                    npcInteractionSystem_->interactingNPC,
                    npcInteractionSystem_->localPlayer,
                    npcInteractionSystem_,
                    questSystem_);
            }

            // Quest log UI
            if (questLogUI_.isOpen && npcInteractionSystem_ && npcInteractionSystem_->localPlayer) {
                questLogUI_.render(npcInteractionSystem_->localPlayer);
            }
        }
    }

    // Network config panel (always visible in editor)
    if (Editor::instance().isOpen()) {
        drawNetworkPanel();
    }

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
            // Clean up ghost entities
            auto* scene = SceneManager::instance().currentScene();
            if (scene) {
                for (auto& [pid, handle] : ghostEntities_) {
                    scene->world().destroyEntity(handle);
                }
                scene->world().processDestroyQueue();
            }
            ghostEntities_.clear();
            ghostInterpolation_.clear();
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

    LOG_INFO("Game", "Game shutting down...");
}

} // namespace fate
