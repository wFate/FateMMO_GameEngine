#pragma once
#include <vector>

namespace fate {

/// Generate all village tiles / sprites (only creates files that don't already exist).
void generateVillageTiles();

// Pixel helpers shared with game_app.cpp procedural sprite generation
void setPixel(std::vector<unsigned char>& px, int x, int y, int sz,
              unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255);
void setPixelW(std::vector<unsigned char>& px, int x, int y, int w, int h,
               unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255);
int pixelHash(int x, int y, int seed = 0);
int clampByte(int v);

} // namespace fate
