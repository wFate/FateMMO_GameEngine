#pragma once
#include "engine/core/types.h"
#include <vector>
#include <functional>

namespace fate {

using TileCoordList = std::vector<Vec2i>;

// Flood fill (queue-based BFS, 4-connected)
// Bounds are inclusive min, exclusive max: [minCol, maxCol) x [minRow, maxRow)
TileCoordList floodFill(int startCol, int startRow,
                        int minCol, int minRow, int maxCol, int maxRow,
                        std::function<bool(int col, int row)> matchesFillTarget);

// Rectangle fill
TileCoordList rectangleFill(int col0, int row0, int col1, int row1);

// Bresenham line
TileCoordList lineTool(int col0, int row0, int col1, int row1);

} // namespace fate
