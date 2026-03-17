#pragma once
#include "engine/ecs/world.h"
#include "engine/input/input.h"
#include "engine/render/camera.h"
#include "game/components/game_components.h"
#include "game/components/transform.h"
#include "game/components/player_controller.h"
#include "game/components/sprite_component.h"
#include "engine/core/logger.h"
#include "imgui.h"

#include "game/systems/quest_system.h"
#include <cmath>

namespace fate {

// ============================================================================
// NPCInteractionSystem — click-to-interact with NPCs
//
// Runs before CombatActionSystem. When the player clicks on an NPC entity,
// this system sets clickConsumed = true on TargetingComponent so that
// CombatActionSystem does not also process the same click.
// ============================================================================
class NPCInteractionSystem : public System {
public:
    const char* name() const override { return "NPCInteractionSystem"; }

    Camera* camera = nullptr;

    // Public state
    bool dialogueOpen = false;
    Entity* interactingNPC = nullptr;
    Entity* localPlayer = nullptr;

    void update(float dt) override {
        if (!world_ || !camera) return;

        // Find local player if not cached
        if (!localPlayer) {
            world_->forEach<PlayerController>([&](Entity* e, PlayerController* pc) {
                if (pc->isLocalPlayer) localPlayer = e;
            });
            if (!localPlayer) return;
        }

        // Reset click consumed flag each frame
        auto* targeting = localPlayer->getComponent<TargetingComponent>();
        if (targeting) targeting->clickConsumed = false;

        // Close dialogue if player walked out of range
        if (dialogueOpen && interactingNPC) {
            auto* playerTransform = localPlayer->getComponent<Transform>();
            auto* npcTransform = interactingNPC->getComponent<Transform>();
            auto* npcComp = interactingNPC->getComponent<NPCComponent>();
            if (playerTransform && npcTransform && npcComp) {
                float dx = playerTransform->position.x - npcTransform->position.x;
                float dy = playerTransform->position.y - npcTransform->position.y;
                float dist = std::sqrt(dx * dx + dy * dy) / Coords::TILE_SIZE;
                if (dist > npcComp->interactionRadius + 1.0f) {
                    closeDialogue();
                }
            }
        }

        // Don't process new clicks while dialogue is open
        if (dialogueOpen) return;

        auto& input = Input::instance();
        bool clicked = input.isMousePressed(SDL_BUTTON_LEFT);
        bool touched = input.isTouchPressed(0);
        if (!clicked && !touched) return;
        if (ImGui::GetIO().WantCaptureMouse) return;

        // Screen-to-world conversion
        Vec2 screenPos = touched ? input.touchPosition(0) : input.mousePosition();
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        Vec2 worldClick = camera->screenToWorld(
            screenPos, (int)displaySize.x, (int)displaySize.y);

        // Find NPC under click via sprite bounds check
        Entity* hitNPC = nullptr;
        world_->forEach<NPCComponent, Transform>(
            [&](Entity* e, NPCComponent* npc, Transform* t) {
                if (hitNPC) return;  // already found one
                auto* spr = e->getComponent<SpriteComponent>();
                if (!spr || !spr->enabled) return;
                Vec2 half = spr->size * 0.5f;
                if (worldClick.x >= t->position.x - half.x &&
                    worldClick.x <= t->position.x + half.x &&
                    worldClick.y >= t->position.y - half.y &&
                    worldClick.y <= t->position.y + half.y) {
                    hitNPC = e;
                }
            }
        );

        if (!hitNPC) return;

        // Consume click regardless of range (prevents combat system from processing)
        if (targeting) {
            targeting->clickConsumed = true;
            targeting->targetType = TargetType::NPC;
        }

        // Check range
        auto* playerTransform = localPlayer->getComponent<Transform>();
        auto* npcTransform = hitNPC->getComponent<Transform>();
        auto* npcComp = hitNPC->getComponent<NPCComponent>();
        if (!playerTransform || !npcTransform || !npcComp) return;

        float dx = playerTransform->position.x - npcTransform->position.x;
        float dy = playerTransform->position.y - npcTransform->position.y;
        float distTiles = std::sqrt(dx * dx + dy * dy) / Coords::TILE_SIZE;

        if (distTiles > npcComp->interactionRadius) {
            // TODO: Show "Too far away" floating text
            LOG_INFO("NPC", "Too far from %s (%.1f tiles > %.1f)",
                     npcComp->displayName.c_str(), distTiles,
                     npcComp->interactionRadius);
            return;
        }

        // Open dialogue
        openDialogue(hitNPC);
    }

    void openDialogue(Entity* npc) {
        interactingNPC = npc;
        dialogueOpen = true;
        auto* npcComp = npc->getComponent<NPCComponent>();
        LOG_INFO("NPC", "Opened dialogue with %s",
                 npcComp ? npcComp->displayName.c_str() : "unknown");

        // Notify quest system for TalkTo objectives
        if (npcComp && world_) {
            auto* questSys = world_->getSystem<QuestSystem>();
            if (questSys) {
                questSys->onNPCInteraction(npcComp->npcId);
            }
        }
    }

    void closeDialogue() {
        if (interactingNPC) {
            auto* npcComp = interactingNPC->getComponent<NPCComponent>();
            LOG_INFO("NPC", "Closed dialogue with %s",
                     npcComp ? npcComp->displayName.c_str() : "unknown");
        }
        interactingNPC = nullptr;
        dialogueOpen = false;
    }

private:
};

} // namespace fate
