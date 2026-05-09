/**************************************************************************/
/*  collider2d.h                                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                          FateMMO Game Engine                           */
/*                       https://www.FateMMO.com                          */
/**************************************************************************/
/* Copyright (c) 2026-present FateMMO Game Engine contributors.           */
/* Copyright (c) 2026-present Caleb Kious.                                */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/
// engine/gameplay2d/components/collider2d.h
//
// Public 2D colliders for the open-source demo.
//
// Two flavors share a single component because every collider has the same
// trigger/static/layer/mask metadata — Shape just selects how getBounds() and
// the trigger system interpret size/radius. Polygon support is intentionally
// deferred; the demo's MMO use cases (mob hitboxes, NPC interaction radii,
// portal triggers, walls) are all box-or-circle.
//
// Coordinate convention: offset is relative to the entity's Transform position
// (center-based). For Box, size is the full width/height. For Circle, size.x
// is the radius (size.y is ignored, kept for editor symmetry).
//
// Layer/mask are bitmasks. Two colliders interact only when
// (a.layer & b.mask) != 0 OR (b.layer & a.mask) != 0. Layer 0 = default layer.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include "engine/core/types.h"
#include <cstdint>

namespace fate {

enum class Collider2DShape : uint8_t {
    Box    = 0,
    Circle = 1,
};

struct Collider2D {
    FATE_COMPONENT(Collider2D)

    Collider2DShape shape = Collider2DShape::Box;
    Vec2 offset = {0.0f, 0.0f};      // center offset from Transform
    Vec2 size   = {32.0f, 32.0f};    // Box: full w/h; Circle: size.x = radius
    bool isTrigger = false;          // overlap-only, does not block movement
    bool isStatic  = true;           // static colliders never move
    uint32_t layer = 1u;             // which layer THIS collider belongs to
    uint32_t mask  = 0xFFFFFFFFu;    // which layers this collider interacts with
    Color debugColor = {0.2f, 0.9f, 0.4f, 0.4f}; // editor gizmo tint

    Rect getBounds(const Vec2& entityPos) const {
        Vec2 center = entityPos + offset;
        if (shape == Collider2DShape::Circle) {
            float r = size.x;
            return { center.x - r, center.y - r, r * 2.0f, r * 2.0f };
        }
        return { center.x - size.x * 0.5f, center.y - size.y * 0.5f, size.x, size.y };
    }

    bool interactsWith(const Collider2D& other) const {
        return (layer & other.mask) != 0u || (other.layer & mask) != 0u;
    }

    // Shape-aware overlap. getBounds() always returns an AABB and so cannot
    // distinguish a circle's diagonal corners — relying on it for collision
    // makes a Circle collider act like a square. This routine dispatches on
    // shape and does the appropriate geometric test.
    static bool overlaps(const Collider2D& a, const Vec2& aPos,
                         const Collider2D& b, const Vec2& bPos) {
        return shapesOverlap(a.shape, a.offset, a.size, aPos,
                             b.shape, b.offset, b.size, bPos);
    }

    // Geometric kernel — pure function over shape/offset/size/pos pairs. Lives
    // here so any shape carrier (Collider2D, TriggerArea2D) can reuse the same
    // box/circle/mixed dispatch without duplicating the math or copying data
    // through a synthetic temporary.
    static bool shapesOverlap(Collider2DShape sa, const Vec2& oa, const Vec2& sza, const Vec2& pa,
                              Collider2DShape sb, const Vec2& ob, const Vec2& szb, const Vec2& pb) {
        const bool aCircle = (sa == Collider2DShape::Circle);
        const bool bCircle = (sb == Collider2DShape::Circle);
        const Vec2 ac = pa + oa;
        const Vec2 bc = pb + ob;

        if (aCircle && bCircle) {
            const float r = sza.x + szb.x;
            const float dx = ac.x - bc.x;
            const float dy = ac.y - bc.y;
            return (dx * dx + dy * dy) <= (r * r);
        }
        if (!aCircle && !bCircle) {
            const Rect ab{ ac.x - sza.x * 0.5f, ac.y - sza.y * 0.5f, sza.x, sza.y };
            const Rect bb{ bc.x - szb.x * 0.5f, bc.y - szb.y * 0.5f, szb.x, szb.y };
            return ab.overlaps(bb);
        }
        // Mixed: closest point on the AABB to the circle center, then radius.
        const Vec2&  boxCtr = aCircle ? bc  : ac;
        const Vec2&  boxSz  = aCircle ? szb : sza;
        const Vec2&  cCtr   = aCircle ? ac  : bc;
        const float  cR     = aCircle ? sza.x : szb.x;
        const Rect   bx{ boxCtr.x - boxSz.x * 0.5f, boxCtr.y - boxSz.y * 0.5f, boxSz.x, boxSz.y };
        float cx = cCtr.x, cy = cCtr.y;
        if (cx < bx.x)            cx = bx.x;
        else if (cx > bx.x + bx.w) cx = bx.x + bx.w;
        if (cy < bx.y)            cy = bx.y;
        else if (cy > bx.y + bx.h) cy = bx.y + bx.h;
        const float dx = cCtr.x - cx;
        const float dy = cCtr.y - cy;
        return (dx * dx + dy * dy) <= (cR * cR);
    }
};

} // namespace fate

FATE_REFLECT(fate::Collider2D,
    FATE_FIELD(shape, Enum),
    FATE_FIELD(offset, Vec2),
    FATE_FIELD(size, Vec2),
    FATE_FIELD(isTrigger, Bool),
    FATE_FIELD(isStatic, Bool),
    FATE_FIELD(layer, UInt),
    FATE_FIELD(mask, UInt),
    FATE_FIELD(debugColor, Color)
)
