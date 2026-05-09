/**************************************************************************/
/*  nameplate_render_system.cpp                                           */
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
#include "engine/gameplay2d/systems/nameplate_render_system.h"
#include "engine/gameplay2d/components/nameplate.h"
#include "engine/gameplay2d/components/health.h"
#include "engine/components/transform.h"
#include "engine/ecs/world.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/camera.h"
#include "engine/render/sdf_text.h"

namespace fate {

void NameplateRenderSystem::render(World& world, SpriteBatch& batch, Camera& camera) {
    Vec2 camPos = camera.position();
    float cullSq = maxDistance_ * maxDistance_;

    // Backend-safe — atlasTextureId() returns 0 on Metal even when the atlas
    // is loaded, which would silently disable nameplate text on iOS.
    bool textReady = SDFText::instance().isReady();

    world.forEach<Transform, Nameplate>([&](Entity* e, Transform* tx, Nameplate* np) {
        if (!np->visible) return;

        if (maxDistance_ > 0.0f) {
            float dx = tx->position.x - camPos.x;
            float dy = tx->position.y - camPos.y;
            if (dx * dx + dy * dy > cullSq) return;
        }

        Vec2 anchor = tx->position + np->worldOffset;

        // Backing rect.
        batch.drawRect(anchor, np->size, np->backgroundColor, 90.0f);

        // HP bar
        if (np->showHealthBar) {
            if (auto* h = e->getComponent<Health>()) {
                float frac = h->fraction();
                float barW = (np->size.x - 4.0f) * frac;
                if (barW < 0.0f) barW = 0.0f;
                Vec2 barCenter{ anchor.x - (np->size.x - 4.0f) * 0.5f + barW * 0.5f,
                                anchor.y - np->size.y * 0.25f };
                Vec2 barSize  { barW, (np->size.y - 4.0f) * 0.5f };
                batch.drawRect(barCenter, barSize, np->healthBarColor, 91.0f);
            }
        }

        if (!textReady) return;

        if (!np->displayName.empty()) {
            Vec2 namePos{ anchor.x, anchor.y + np->size.y * 0.5f + 4.0f };
            SDFText::instance().drawWorld(batch, np->displayName, namePos,
                                          8.0f, np->textColor, 95.0f);
        }
        if (np->showLevel && np->level > 0) {
            std::string lv = "Lv " + std::to_string(np->level);
            Vec2 lvPos{ anchor.x, anchor.y + np->size.y * 0.5f + 14.0f };
            SDFText::instance().drawWorld(batch, lv, lvPos,
                                          7.0f, np->levelColor, 95.0f);
        }
    });
}

} // namespace fate
