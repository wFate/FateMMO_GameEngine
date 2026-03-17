#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/core/types.h"
#include <vector>

namespace fate {

// Polygon collider — custom collision shape defined by a list of vertices
// Vertices are relative to the entity's Transform position
// Supports convex polygons for SAT collision detection
struct PolygonCollider {
    FATE_COMPONENT(PolygonCollider)

    std::vector<Vec2> points;  // vertices in local space (relative to entity center)
    bool isTrigger = false;
    bool isStatic = true;

    // Get world-space points given entity position
    std::vector<Vec2> getWorldPoints(const Vec2& entityPos) const {
        std::vector<Vec2> world;
        world.reserve(points.size());
        for (auto& p : points) {
            world.push_back(entityPos + p);
        }
        return world;
    }

    // Get axis-aligned bounding box of the polygon (world space)
    Rect getBounds(const Vec2& entityPos) const {
        if (points.empty()) return {entityPos.x, entityPos.y, 0, 0};

        float minX = points[0].x, maxX = points[0].x;
        float minY = points[0].y, maxY = points[0].y;
        for (size_t i = 1; i < points.size(); i++) {
            if (points[i].x < minX) minX = points[i].x;
            if (points[i].x > maxX) maxX = points[i].x;
            if (points[i].y < minY) minY = points[i].y;
            if (points[i].y > maxY) maxY = points[i].y;
        }
        return {
            entityPos.x + minX, entityPos.y + minY,
            maxX - minX, maxY - minY
        };
    }

    // Create common shapes as convenience
    static PolygonCollider makeBox(float width, float height, Vec2 offset = {0, 0}) {
        PolygonCollider pc;
        float hw = width * 0.5f, hh = height * 0.5f;
        pc.points = {
            {offset.x - hw, offset.y - hh},
            {offset.x + hw, offset.y - hh},
            {offset.x + hw, offset.y + hh},
            {offset.x - hw, offset.y + hh}
        };
        return pc;
    }

    static PolygonCollider makeCircleApprox(float radius, int segments = 8, Vec2 offset = {0, 0}) {
        PolygonCollider pc;
        for (int i = 0; i < segments; i++) {
            float angle = (float)i / segments * 6.2831853f;
            pc.points.push_back({
                offset.x + radius * std::cos(angle),
                offset.y + radius * std::sin(angle)
            });
        }
        return pc;
    }
};

// Separating Axis Theorem (SAT) collision detection for convex polygons
namespace CollisionUtil {
    // Check if two convex polygons overlap (world-space points)
    bool polygonsOverlap(const std::vector<Vec2>& a, const std::vector<Vec2>& b);

    // Check if a polygon overlaps an AABB rect
    bool polygonOverlapsRect(const std::vector<Vec2>& poly, const Rect& rect);

    // Check if a point is inside a convex polygon
    bool pointInPolygon(const Vec2& point, const std::vector<Vec2>& polygon);
}

} // namespace fate
