#include "game/components/polygon_collider.h"
#include <cmath>
#include <cfloat>

namespace fate {
namespace CollisionUtil {

// Project all points onto an axis, return min/max
static void projectPolygon(const std::vector<Vec2>& poly, const Vec2& axis, float& outMin, float& outMax) {
    outMin = outMax = poly[0].dot(axis);
    for (size_t i = 1; i < poly.size(); i++) {
        float proj = poly[i].dot(axis);
        if (proj < outMin) outMin = proj;
        if (proj > outMax) outMax = proj;
    }
}

// Get perpendicular axes (edge normals) for a polygon
static std::vector<Vec2> getAxes(const std::vector<Vec2>& poly) {
    std::vector<Vec2> axes;
    for (size_t i = 0; i < poly.size(); i++) {
        size_t j = (i + 1) % poly.size();
        Vec2 edge = poly[j] - poly[i];
        // Perpendicular (left normal)
        Vec2 normal = Vec2(-edge.y, edge.x).normalized();
        axes.push_back(normal);
    }
    return axes;
}

bool polygonsOverlap(const std::vector<Vec2>& a, const std::vector<Vec2>& b) {
    if (a.size() < 3 || b.size() < 3) return false;

    // Test all axes from polygon A
    auto axesA = getAxes(a);
    for (auto& axis : axesA) {
        float minA, maxA, minB, maxB;
        projectPolygon(a, axis, minA, maxA);
        projectPolygon(b, axis, minB, maxB);
        if (maxA < minB || maxB < minA) return false; // separating axis found
    }

    // Test all axes from polygon B
    auto axesB = getAxes(b);
    for (auto& axis : axesB) {
        float minA, maxA, minB, maxB;
        projectPolygon(a, axis, minA, maxA);
        projectPolygon(b, axis, minB, maxB);
        if (maxA < minB || maxB < minA) return false;
    }

    return true; // no separating axis = overlap
}

bool polygonOverlapsRect(const std::vector<Vec2>& poly, const Rect& rect) {
    // Convert rect to polygon
    std::vector<Vec2> rectPoly = {
        {rect.x, rect.y},
        {rect.x + rect.w, rect.y},
        {rect.x + rect.w, rect.y + rect.h},
        {rect.x, rect.y + rect.h}
    };
    return polygonsOverlap(poly, rectPoly);
}

bool pointInPolygon(const Vec2& point, const std::vector<Vec2>& polygon) {
    bool inside = false;
    size_t n = polygon.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        if (((polygon[i].y > point.y) != (polygon[j].y > point.y)) &&
            (point.x < (polygon[j].x - polygon[i].x) * (point.y - polygon[i].y) /
             (polygon[j].y - polygon[i].y) + polygon[i].x)) {
            inside = !inside;
        }
    }
    return inside;
}

} // namespace CollisionUtil
} // namespace fate
