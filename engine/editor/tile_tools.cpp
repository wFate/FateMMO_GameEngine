#include "engine/editor/tile_tools.h"
#include <queue>
#include <unordered_set>
#include <cmath>
#include <algorithm>

namespace fate {

TileCoordList floodFill(int startCol, int startRow, int mapW, int mapH,
                        std::function<bool(int, int)> matchesFillTarget) {
    TileCoordList result;
    if (startCol < 0 || startCol >= mapW || startRow < 0 || startRow >= mapH) return result;
    if (!matchesFillTarget(startCol, startRow)) return result;

    auto pack = [mapW](int c, int r) { return r * mapW + c; };
    std::unordered_set<int> visited;
    std::queue<Vec2i> queue;
    queue.push({startCol, startRow});
    visited.insert(pack(startCol, startRow));

    constexpr int dx[] = {0, 0, -1, 1};
    constexpr int dy[] = {-1, 1, 0, 0};

    while (!queue.empty()) {
        auto [cx, cy] = queue.front();
        queue.pop();
        result.push_back({cx, cy});

        for (int i = 0; i < 4; ++i) {
            int nx = cx + dx[i], ny = cy + dy[i];
            if (nx < 0 || nx >= mapW || ny < 0 || ny >= mapH) continue;
            int key = pack(nx, ny);
            if (visited.count(key)) continue;
            if (!matchesFillTarget(nx, ny)) continue;
            visited.insert(key);
            queue.push({nx, ny});
        }
    }
    return result;
}

TileCoordList rectangleFill(int col0, int row0, int col1, int row1) {
    TileCoordList result;
    int minC = std::min(col0, col1), maxC = std::max(col0, col1);
    int minR = std::min(row0, row1), maxR = std::max(row0, row1);
    for (int r = minR; r <= maxR; ++r)
        for (int c = minC; c <= maxC; ++c)
            result.push_back({c, r});
    return result;
}

TileCoordList lineTool(int col0, int row0, int col1, int row1) {
    TileCoordList result;
    int dx = std::abs(col1 - col0), sx = col0 < col1 ? 1 : -1;
    int dy = -std::abs(row1 - row0), sy = row0 < row1 ? 1 : -1;
    int err = dx + dy;
    int x = col0, y = row0;
    for (;;) {
        result.push_back({x, y});
        if (x == col1 && y == row1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
    return result;
}

} // namespace fate
