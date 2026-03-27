#include "game/procedural_tile_generator.h"
#include "engine/core/logger.h"
#include <vector>
#include <functional>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include "stb_image_write.h"
namespace fs = std::filesystem;

namespace fate {

// --- Pixel helpers -----------------------------------------------------------

void setPixel(std::vector<unsigned char>& px, int x, int y, int sz,
              unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    if (x < 0 || x >= sz || y < 0 || y >= sz) return;
    int i = (y * sz + x) * 4;
    px[i] = r; px[i+1] = g; px[i+2] = b; px[i+3] = a;
}

// setPixel for arbitrary-stride buffers (width != height)
void setPixelW(std::vector<unsigned char>& px, int x, int y, int w, int h,
               unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
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
int pixelHash(int x, int y, int seed) {
    int h = x * 374761393 + y * 668265263 + seed * 1274126177;
    h = (h ^ (h >> 13)) * 1274126177;
    return h ^ (h >> 16);
}

// Clamp int to 0..255
int clampByte(int v) {
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

void generateVillageTiles() {
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

} // namespace fate
