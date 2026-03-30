#include "game/game_app.h"
#include "engine/core/logger.h"
#include "engine/render/gfx/backend/gl/gl_loader.h"
#include "engine/scene/scene_manager.h"
#include "engine/editor/editor_shim.h"
#include "game/register_components.h"
#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "game/components/player_controller.h"
#include "game/components/animator.h"
#include "game/components/box_collider.h"
#include "game/components/polygon_collider.h"
#include "game/components/game_components.h"
#include "game/components/tile_layer_component.h"
#include "engine/render/sdf_text.h"
#include "engine/render/font_registry.h"
#include "engine/ecs/prefab.h"
#include "game/entity_factory.h"
#include "game/animation_loader.h"
#include "game/systems/movement_system.h"
#include "game/systems/render_system.h"
#include "game/systems/gameplay_system.h"
#include "game/systems/mob_ai_system.h"
#include "game/systems/combat_action_system.h"
#include "game/systems/zone_system.h"
#include "game/systems/spawn_system.h"
#include "game/systems/npc_interaction_system.h"
#include "game/systems/combat_text_config.h"
#include "game/systems/quest_system.h"
#include "engine/job/job_system.h"
#include <thread>
#include <chrono>
#include "engine/particle/particle_system.h"
#include "engine/particle/particle_emitter_component.h"
#include "game/shared/profanity_filter.h"
#include "engine/ui/widgets/button.h"
#include "engine/ui/widgets/window.h"
#include "engine/ui/widgets/fate_status_bar.h"
#include "engine/ui/widgets/target_frame.h"
#include "engine/ui/widgets/exp_bar.h"
#include "engine/ui/widgets/dpad.h"
#include "engine/ui/widgets/skill_arc.h"
#include "engine/ui/widgets/menu_tab_bar.h"
#include "engine/ui/widgets/inventory_panel.h"
#include "engine/ui/widgets/status_panel.h"
#include "engine/ui/widgets/skill_panel.h"
#include "engine/ui/widgets/character_select_screen.h"
#include "engine/ui/widgets/character_creation_screen.h"
#include "engine/ui/widgets/chat_panel.h"
#include "engine/ui/widgets/trade_window.h"
#include "engine/ui/widgets/party_frame.h"
#include "engine/ui/widgets/guild_panel.h"
#include "engine/ui/widgets/npc_dialogue_panel.h"
#include "engine/ui/widgets/shop_panel.h"
#include "engine/ui/widgets/bank_panel.h"
#include "engine/ui/widgets/teleporter_panel.h"
#include "engine/ui/widgets/arena_panel.h"
#include "engine/ui/widgets/battlefield_panel.h"
#include "engine/ui/widgets/pet_panel.h"
#include "engine/ui/widgets/crafting_panel.h"
#include "engine/ui/widgets/collection_panel.h"
#include "engine/ui/widgets/costume_panel.h"
#include "engine/ui/widgets/buff_bar.h"
#include "engine/ui/widgets/settings_panel.h"
#include "engine/ui/widgets/leaderboard_panel.h"
#include "engine/ui/widgets/player_context_menu.h"
#include "engine/ui/widgets/confirm_dialog.h"
#include "game/shared/npc_types.h"
#include "game/shared/item_stat_roller.h"
#include "engine/vfx/skill_vfx_player.h"
#include "game/shared/client_skill_cache.h"
#include "game/components/spawn_point_component.h"
#include "game/components/faction_component.h"
#include "game/procedural_tile_generator.h"
#include "game/data/paper_doll_catalog.h"
#ifndef FATE_SHIPPING
#include "imgui.h"
#endif
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <filesystem>
namespace fs = std::filesystem;  // std::min, std::max (used with parenthesized calls to avoid Windows macro conflict)

namespace fate {

// File-local helper: maps PKStatus to nameplate Color (mirrors GameplaySystem::pkStatusColor)
static Color pkStatusColor(PKStatus status) {
    switch (status) {
        case PKStatus::White:  return Color::white();
        case PKStatus::Purple: return {0.659f, 0.333f, 0.969f};
        case PKStatus::Red:    return Color::red();
        case PKStatus::Black:  return {0.2f, 0.2f, 0.2f};
        default:               return Color::white();
    }
}



void GameApp::onInit() {
    LOG_INFO("Game", "FateMMO Game Engine initializing...");

    // Register all component types with the reflection/meta registry
    fate::registerAllComponents();

    // Generate village tileset (only creates files that don't exist yet)
    generateVillageTiles();

#ifndef FATE_SHIPPING
    // Set up editor and prefab library
    Editor::instance().setAssetRoot("assets");
#ifdef FATE_SOURCE_DIR
    Editor::instance().setSourceDir(FATE_SOURCE_DIR "/assets/scenes");
    PrefabLibrary::instance().setSourceDirectory(FATE_SOURCE_DIR "/assets/prefabs");
#endif
#else
#ifdef FATE_SOURCE_DIR
    PrefabLibrary::instance().setSourceDirectory(FATE_SOURCE_DIR "/assets/prefabs");
#endif
#endif
    PrefabLibrary::instance().setDirectory("assets/prefabs");
    PrefabLibrary::instance().loadAll();

    // Register a default scene so systems have a world to attach to.
    // The editor auto-loads WhisperingWoods.json into it below;
    // on server connect the async loader replaces the contents.
    SceneManager::instance().registerScene("Default", [](Scene&) {});
    SceneManager::instance().switchScene("Default");

    // Add systems (these operate on whatever entities are in the scene)
    auto* scene = SceneManager::instance().currentScene();
    if (scene) {
        World& world = scene->world();

        movementSystem_ = world.addSystem<MovementSystem>();
        world.addSystem<AnimationSystem>();

        auto* cameraFollow = world.addSystem<CameraFollowSystem>();
        cameraFollow->camera = &camera();

        gameplaySystem_ = world.addSystem<GameplaySystem>();
        mobAISystem_ = world.addSystem<MobAISystem>();

        npcInteractionSystem_ = world.addSystem<NPCInteractionSystem>();
        npcInteractionSystem_->camera = &camera();
        npcInteractionSystem_->uiManager_ = &uiManager();

        combatSystem_ = world.addSystem<CombatActionSystem>();
        combatSystem_->camera = &camera();
        combatSystem_->npcSystem_ = npcInteractionSystem_;
        combatSystem_->onSendAttack = [this](Entity* target) {
            if (!netClient_.isConnected() || !target) return;
            // Reverse lookup: find PersistentId for this entity by comparing entity handles.
            // Entity* pointer comparison can fail if getEntity(EntityId) and getEntity(EntityHandle)
            // resolve through different paths, so compare handles directly.
            EntityHandle targetHandle = target->handle();
            for (const auto& [pid, handle] : ghostEntities_) {
                if (handle == targetHandle) {
                    combatPredictions_.addPrediction(pid, netTime_);
                    netClient_.sendAction(0, pid, 0);
                    return;
                }
            }
            LOG_WARN("Combat", "onSendAttack: target '%s' (id=%u) not found in %zu ghosts",
                     target->name().c_str(), target->id(), ghostEntities_.size());
        };
        combatSystem_->onPlaySFX = [this](const std::string& id) {
            audioManager_.playSFX(id);
        };

        questSystem_ = world.addSystem<QuestSystem>();

        zoneSystem_ = world.addSystem<ZoneSystem>();
        zoneSystem_->camera = &camera();
        zoneSystem_->onSceneTransition = [this](const std::string& scene, Vec2 /*spawnPos*/) {
            // Send zone transition request to server for validation (level gate, etc.)
            // Server determines spawn position from portal data
            netClient_.sendZoneTransition(scene);
        };

        // SpawnSystem is server-only — client receives mobs via replication
        world.addSystem<ParticleSystem>();

        renderSystem_ = new SpriteRenderSystem();
        renderSystem_->batch = &spriteBatch();
        renderSystem_->camera = &camera();
        renderSystem_->init(&world);
    }

    // Net client callbacks for ghost entity management
    netClient_.onEntityEnter = [this](const SvEntityEnterMsg& msg) {
        // Buffer enters during pending zone transition — the old scene hasn't
        // been destroyed yet, so creating ghosts now would either duplicate
        // existing ghosts or create entities that get wiped when the scene loads.
        // Buffered messages are replayed after the new scene finishes loading.
        if (pendingZoneTransition_) {
            pendingEntityEnters_.push_back(msg);
            return;
        }

        // Skip our own player — the local entity is already created by GameApp.
        // The server broadcasts SvEntityEnter for all players including ourselves.
        if (msg.entityType == 0 && msg.name == pendingCharName_) {
            localPlayerPid_ = msg.persistentId;
            return;
        }

        // Guard against duplicate entity enter (prevents ghost leak)
        if (ghostEntities_.count(msg.persistentId)) {
            LOG_WARN("Net", "Duplicate SvEntityEnter for PID %llu, ignoring", msg.persistentId);
            return;
        }
        auto* sc = SceneManager::instance().currentScene();
        if (!sc) return;
        auto& world = sc->world();
        Entity* ghost = nullptr;
        // Log ALL entity enters with position to find (0,0) source
        LOG_INFO("Net", "EntityEnter: type=%d pid=%llu name='%s' pos=(%.1f,%.1f)",
                 msg.entityType, msg.persistentId, msg.name.c_str(),
                 msg.position.x, msg.position.y);
        if (msg.entityType == 0) { // player
            ghost = EntityFactory::createGhostPlayer(world, msg.name, msg.position,
                msg.gender, msg.hairstyle);
        } else if (msg.entityType == 3) { // dropped item
            ghost = EntityFactory::createGhostDroppedItem(world, msg.name, msg.position,
                msg.isGold != 0, msg.rarity);
        } else if (msg.entityType == 1) { // mob
            ghost = EntityFactory::createGhostMob(world, msg.name, msg.position,
                msg.mobDefId, msg.level, msg.currentHP, msg.maxHP, msg.isBoss != 0);
        } else { // npc or unknown
            ghost = EntityFactory::createGhostMob(world, msg.name, msg.position);
        }
        if (ghost) {
            ghostEntities_[msg.persistentId] = ghost->handle();
            if (msg.entityType == 3) droppedItemPids_.insert(msg.persistentId);

            // Auto-load animation metadata from .meta.json
            auto* ghostSprite = ghost->getComponent<SpriteComponent>();
            auto* ghostAnimator = ghost->getComponent<Animator>();
            if (ghostSprite && ghostAnimator) {
                AnimationLoader::tryAutoLoad(*ghostSprite, *ghostAnimator);
            }

            // Apply PK status name color and faction for remote players on enter
            if (msg.entityType == 0) {
                auto* nameplate = ghost->getComponent<NameplateComponent>();
                if (nameplate) {
                    nameplate->nameColor = pkStatusColor(static_cast<PKStatus>(msg.pkStatus));
                    if (!msg.guildName.empty()) {
                        nameplate->showGuild = true;
                        nameplate->guildName = msg.guildName;
                        nameplate->guildIconPath = msg.guildIconPath;
                    }
                }
                auto* fc = ghost->getComponent<FactionComponent>();
                if (fc) {
                    fc->faction = static_cast<Faction>(msg.faction);
                }
                if (msg.costumeVisuals != 0) {
                    auto* appearance = ghost->getComponent<AppearanceComponent>();
                    if (appearance) appearance->dirty = true;
                }
            }
            // Seed interpolation buffer with initial position so ghosts don't
            // snap to (0,0) before the first SvEntityUpdate arrives.
            ghostInterpolation_.onEntityUpdate(msg.persistentId, msg.position);
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
            ghostUpdateSeqs_.erase(msg.persistentId);
            ghostDeathTimers_.erase(msg.persistentId);
            droppedItemPids_.erase(msg.persistentId);
            ghostInterpolation_.removeEntity(msg.persistentId);
        }
    };

    netClient_.onEntityUpdate = [this](const SvEntityUpdateMsg& msg) {
        // Reject stale updates via sequence counter (wrapping comparison)
        // First update for any entity is accepted unconditionally (no entry yet).
        auto seqIt = ghostUpdateSeqs_.find(msg.persistentId);
        if (seqIt != ghostUpdateSeqs_.end()) {
            int8_t diff = static_cast<int8_t>(msg.updateSeq - seqIt->second);
            if (diff <= 0) return; // stale or duplicate, discard
        }
        ghostUpdateSeqs_[msg.persistentId] = msg.updateSeq;

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
        // Sync mob/ghost HP from server
        if (msg.fieldMask & 0x08) { // bit 3 = currentHP
            auto* es = ghost->getComponent<EnemyStatsComponent>();
            if (es) {
                es->stats.currentHP = msg.currentHP;
                es->stats.isAlive = msg.currentHP > 0;
                // Hide sprite when mob dies
                if (!es->stats.isAlive) {
                    auto* spr = ghost->getComponent<SpriteComponent>();
                    if (spr) spr->enabled = false;
                }
            }
        }
        if (msg.fieldMask & (1 << 7)) {
            // statusEffectMask received for ghost entity
            // TODO: display buff icons on ghost nameplates
        }
        if (msg.fieldMask & (1 << 14)) { // bit 14 = pkStatus
            auto* nameplate = ghost->getComponent<NameplateComponent>();
            if (nameplate) {
                nameplate->nameColor = pkStatusColor(static_cast<PKStatus>(msg.pkStatus));
            }
        }

        // moveState (bit 5) — set ghost walking state
        if (msg.fieldMask & (1 << 5)) {
            auto* pc = ghost->getComponent<PlayerController>();
            if (pc) {
                pc->isMoving = (msg.moveState == static_cast<uint8_t>(MoveState::Walking));
            }
        }

        // animId (bit 6) — set ghost animation direction + type
        if (msg.fieldMask & (1 << 6)) {
            auto* anim = ghost->getComponent<Animator>();
            auto* pc = ghost->getComponent<PlayerController>();
            auto* spr = ghost->getComponent<SpriteComponent>();
            if (anim && pc) {
                uint8_t animDir, animType;
                decodeAnimId(msg.animId, animDir, animType);

                // Map animDir back to Direction for PlayerController
                if (animDir == 0) pc->facing = Direction::Down;
                else if (animDir == 1) pc->facing = Direction::Up;
                else if (animDir == 2) {
                    // side — use flipX to determine left vs right
                    if (spr) pc->facing = spr->flipX ? Direction::Left : Direction::Right;
                    else pc->facing = Direction::Right;
                }

                // Map animType to animation name
                static const char* typeNames[] = {"idle", "walk", "attack", "cast"};
                static const char* dirNames[]  = {"_down", "_up", "_side"};
                if (animType == 4) {
                    // death (special)
                    anim->currentAnimation = "death";
                } else if (animType < 4 && animDir < 3) {
                    anim->currentAnimation = std::string(typeNames[animType]) + dirNames[animDir];
                }
            }
        }

        // targetEntityId (bit 10) — store on ghost for display
        if (msg.fieldMask & (1 << 10)) {
            auto* tgt = ghost->getComponent<TargetingComponent>();
            if (tgt) {
                tgt->selectedTargetId = msg.targetEntityId;
            }
        }

        // equipVisuals (bit 13) — style name strings
        if (msg.fieldMask & (1 << 13)) {
            auto* appearance = ghost->getComponent<AppearanceComponent>();
            if (appearance) {
                appearance->armorStyle  = msg.armorStyle;
                appearance->hatStyle    = msg.hatStyle;
                appearance->weaponStyle = msg.weaponStyle;
                appearance->dirty = true;
            }
        }

        // costumeVisuals (bit 16)
        if (msg.fieldMask & (1 << 16)) {
            auto* appearance = ghost->getComponent<AppearanceComponent>();
            if (appearance) appearance->dirty = true;
        }
    };

    netClient_.onCombatEvent = [this](const SvCombatEventMsg& msg) {
#ifndef FATE_SHIPPING
        if (Editor::instance().isPaused()) return; // Don't process combat while paused
#endif
        if (!combatSystem_) return;
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;

        // Check if attacker is the local player (not in ghostEntities_ = us)
        // Ghost entities are OTHER players/mobs; the local player is never a ghost.
        bool isLocalAttack = (ghostEntities_.find(msg.attackerId) == ghostEntities_.end());

        // Resolve optimistic prediction — animation already playing, server confirmed
        if (isLocalAttack) {
            combatPredictions_.resolveOldest();
        }

        // Only log combat events that deal damage or kill (skip mob-miss spam)
        if (msg.damage > 0 || msg.isKill) {
            LOG_DEBUG("CombatDbg", "SvCombatEvent: attacker=%llu target=%llu dmg=%d kill=%d isLocal=%d ghosts=%zu",
                      msg.attackerId, msg.targetId, msg.damage, (int)msg.isKill,
                      (int)isLocalAttack, ghostEntities_.size());
        }

        // Find target entity position for floating text
        Vec2 targetPos{0, 0};
        bool foundTarget = false;

        // Check ghost entities (replicated remote players/mobs from server)
        auto it = ghostEntities_.find(msg.targetId);
        if (it != ghostEntities_.end()) {
            Entity* ghost = scene->world().getEntity(it->second);
            if (ghost) {
                auto* t = ghost->getComponent<Transform>();
                if (t) { targetPos = t->position; foundTarget = true; }

                // On kill: set HP to 0 immediately. The server destroys the
                // mob entity before the next SvEntityUpdate (HP=0) can be sent,
                // so we must apply the final HP here from the combat event.
                if (msg.isKill) {
                    auto* es = ghost->getComponent<EnemyStatsComponent>();
                    if (es) {
                        es->stats.currentHP = 0;
                        es->stats.isAlive = false;
                    }
                    // Start 3-second corpse timer — sprite hidden after delay
                    ghostDeathTimers_[msg.targetId] = netTime_;
                }
            }
        }

        // If target not found in ghosts AND attacker IS a ghost (mob→player),
        // apply damage prediction to local player. Never apply when isLocalAttack
        // — that would mean our own attack damage hits us if the target ghost is missing.
        if (!foundTarget && !isLocalAttack) {
            scene->world().forEach<PlayerController, CharacterStatsComponent>(
                [&](Entity* entity, PlayerController* ctrl, CharacterStatsComponent* sc) {
                    if (!ctrl->isLocalPlayer || foundTarget) return;
                    auto* t = entity->getComponent<Transform>();
                    if (t) targetPos = t->position;
                    foundTarget = true;

                    // Update local HP prediction for HUD display only.
                    // Death is server-authoritative — only SvDeathNotifyMsg triggers die().
                    if (msg.damage > 0 && sc->stats.isAlive()) {
                        int before = sc->stats.currentHP;
                        sc->stats.currentHP = (std::max)(0, sc->stats.currentHP - msg.damage);
                        LOG_INFO("CombatDbg", "HP prediction: %d -> %d (mob %llu hit us for %d)",
                                 before, sc->stats.currentHP, msg.attackerId, msg.damage);
                    }
                }
            );
        } else if (!foundTarget && isLocalAttack) {
            // Our attack target wasn't found in ghosts — stale/missing ghost entity.
            // Don't apply damage to ourselves. Just log and skip.
            LOG_WARN("CombatDbg", "OUR attack target %llu not in ghosts (dmg=%d) — dropped",
                     msg.targetId, msg.damage);
            return;
        }

        if (!foundTarget) return;

        // Show floating damage/miss text from server-authoritative result.
        // isMiss flag distinguishes actual misses from zero-damage hits (shields/absorption).
        if (msg.isMiss) {
            combatSystem_->showMissText(targetPos);
        } else {
            combatSystem_->showDamageText(targetPos, msg.damage, msg.isCrit != 0);
        }

        // Process kill: clear target, play death effects
        if (msg.isKill && isLocalAttack) {
            if (combatSystem_) {
                combatSystem_->serverClearTarget();
            }
            LOG_INFO("Combat", "Target killed by server");
        }

        // Audio feedback — local attack audio now plays on the animation hit frame
        // (CombatActionSystem), so only play kill SFX here for local attacks.
        if (isLocalAttack) {
            if (msg.isKill) audioManager_.playSFX("kill");
        } else if (foundTarget) {
            auto& cam = camera();
            audioManager_.playSFXSpatial(
                msg.damage > 0 ? "hit_melee" : "miss",
                targetPos.x, targetPos.y,
                cam.position().x, cam.position().y);
        }
    };

    netClient_.onPlayerState = [this](const SvPlayerStateMsg& msg) {
        // Always store the latest state for zone transition recovery and pending apply
        pendingPlayerState_ = msg;

        // If the local player hasn't been created yet (first frame after connect),
        // mark pending and apply it after the player entity is created.
        if (!localPlayerCreated_) {
            hasPendingPlayerState_ = true;
            return;
        }
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;
        scene->world().forEach<CharacterStatsComponent, PlayerController>(
            [&](Entity* entity, CharacterStatsComponent* stats, PlayerController* ctrl) {
                if (!ctrl->isLocalPlayer) return;
                // Set level and recalculate FIRST, then override with server values.
                // recalculateStats() overwrites maxHP/maxMP, so server values must come after.
                if (stats->stats.level != msg.level) {
                    stats->stats.level = msg.level;
                    stats->stats.recalculateStats();
                    stats->stats.recalculateXPRequirement();
                }
                stats->stats.currentHP = msg.currentHP;
                stats->stats.maxHP = msg.maxHP;
                stats->stats.currentMP = msg.currentMP;
                stats->stats.maxMP = msg.maxMP;
                stats->stats.currentFury = msg.currentFury;
                stats->stats.currentXP = msg.currentXP;
                stats->stats.honor = msg.honor;
                stats->stats.pvpKills = msg.pvpKills;
                stats->stats.pvpDeaths = msg.pvpDeaths;

                // DISABLED: stat allocation removed — stats are fixed per class
                // stats->stats.freeStatPoints = msg.freeStatPoints;
                // stats->stats.allocatedSTR   = msg.allocatedSTR;
                // stats->stats.allocatedINT   = msg.allocatedINT;
                // stats->stats.allocatedDEX   = msg.allocatedDEX;
                // stats->stats.allocatedCON   = msg.allocatedCON;
                // stats->stats.allocatedWIS   = msg.allocatedWIS;

                // Derived stats — server-authoritative snapshot
                stats->stats.applyServerSnapshot(
                    msg.armor, msg.magicResist, msg.critRate,
                    msg.hitRate, msg.evasion, msg.speed, msg.damageMult);

                // Gold — server-authoritative, set directly (not additive)
                auto* inv = entity->getComponent<InventoryComponent>();
                if (inv) inv->inventory.setGold(msg.gold);
            }
        );
    };

    netClient_.onBuffSync = [this](const SvBuffSyncMsg& msg) {
        auto* hud = uiManager().getScreen("fate_hud");
        if (!hud) return;
        auto* bar = dynamic_cast<BuffBar*>(hud->findById("buff_bar"));
        if (!bar) return;

        bar->buffs.clear();
        bar->buffs.reserve(msg.buffs.size());
        for (const auto& b : msg.buffs) {
            BuffDisplayData data;
            data.effectType = b.effectType;
            data.remainingTime = b.remainingTime;
            data.totalDuration = b.totalDuration;
            data.stacks = b.stacks;
            bar->buffs.push_back(data);
        }
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
        if (chatPanel_) {
            chatPanel_->addMessage(msg.channel, msg.senderName, msg.message, msg.faction);
        } else {
            pendingChatMessages_.push_back({msg.channel, msg.senderName, msg.message, msg.faction});
        }
    };

    netClient_.onLootPickup = [this](const SvLootPickupMsg& msg) {
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;

        scene->world().forEach<PlayerController, InventoryComponent>(
            [&](Entity*, PlayerController* ctrl, InventoryComponent* invComp) {
                if (!ctrl->isLocalPlayer) return;

                if (msg.isGold) {
                    invComp->inventory.addGold(msg.goldAmount);
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "Picked up %d gold.", msg.goldAmount);
                    if (chatPanel_) chatPanel_->addMessage(6, "[Loot]", buf, static_cast<uint8_t>(0));
                    audioManager_.playSFX("loot_gold");
                } else {
                    static int lootCounter = 0;
                    char instId[32];
                    std::snprintf(instId, sizeof(instId), "loot_%d", ++lootCounter);

                    ItemInstance item = ItemInstance::createSimple(instId, msg.itemId, msg.quantity);
                    item.displayName = msg.displayName;
                    item.rarity = parseItemRarity(msg.rarity);
                    int usedSlots = 0;
                    for (const auto& s : invComp->inventory.getSlots())
                        if (s.isValid()) ++usedSlots;
                    LOG_INFO("Client", "LootPickup: item='%s' qty=%d valid=%d slots=%d/%d",
                             msg.itemId.c_str(), msg.quantity, item.isValid() ? 1 : 0,
                             usedSlots, (int)invComp->inventory.getSlots().size());
                    if (invComp->inventory.addItem(item)) {
                        char buf[128];
                        std::snprintf(buf, sizeof(buf), "Picked up %s x%d.",
                                      msg.displayName.c_str(), msg.quantity);
                        if (chatPanel_) chatPanel_->addMessage(6, "[Loot]", buf, static_cast<uint8_t>(0));
                        audioManager_.playSFX("loot_item");
                    } else {
                        LOG_WARN("Client", "LootPickup FAILED: addItem returned false for '%s'", msg.itemId.c_str());
                        if (chatPanel_) chatPanel_->addMessage(6, "[Loot]", "Inventory full!", static_cast<uint8_t>(0));
                    }
                }
            }
        );
    };

    netClient_.onTradeUpdate = [this](const SvTradeUpdateMsg& msg) {
        std::string text;
        switch (msg.updateType) {
            case 0: text = msg.otherPlayerName + " invited you to trade."; break;
            case 1: text = "Trade session started."; break;
            case 5: text = "Trade completed!"; break;
            case 6: text = msg.otherPlayerName.empty() ? "Trade cancelled." : msg.otherPlayerName; break;
            default: text = "Trade update."; break;
        }
        if (chatPanel_) chatPanel_->addMessage(6, "[Trade]", text, static_cast<uint8_t>(0));
    };

    netClient_.onMarketResult = [this](const SvMarketResultMsg& msg) {
        if (chatPanel_) chatPanel_->addMessage(6, "[Market]", msg.message, static_cast<uint8_t>(0));
        else pendingChatMessages_.push_back({6, "[Market]", msg.message, 0});
    };

    netClient_.onBountyUpdate = [this](const SvBountyUpdateMsg& msg) {
        if (chatPanel_) chatPanel_->addMessage(6, "[Bounty]", msg.message, static_cast<uint8_t>(0));
        else pendingChatMessages_.push_back({6, "[Bounty]", msg.message, 0});
    };

    netClient_.onGauntletUpdate = [this](const SvGauntletUpdateMsg& msg) {
        if (chatPanel_) chatPanel_->addMessage(6, "[Gauntlet]", msg.message, static_cast<uint8_t>(0));
        else pendingChatMessages_.push_back({6, "[Gauntlet]", msg.message, 0});
    };

    netClient_.onGuildUpdate = [this](const SvGuildUpdateMsg& msg) {
        std::string text = msg.message;
        if (!msg.guildName.empty()) text = "[" + msg.guildName + "] " + text;
        if (chatPanel_) chatPanel_->addMessage(6, "[Guild]", text, static_cast<uint8_t>(0));
        else pendingChatMessages_.push_back({6, "[Guild]", text, 0});
    };

    netClient_.onSocialUpdate = [this](const SvSocialUpdateMsg& msg) {
        if (chatPanel_) chatPanel_->addMessage(6, "[Social]", msg.message, static_cast<uint8_t>(0));
        else pendingChatMessages_.push_back({6, "[Social]", msg.message, 0});
    };

    netClient_.onConsumeResult = [this](const SvConsumeResultMsg& msg) {
        if (chatPanel_) chatPanel_->addMessage(6, "[Item]", msg.message, static_cast<uint8_t>(0));
        else pendingChatMessages_.push_back({6, "[Item]", msg.message, 0});
    };

    netClient_.onQuestUpdate = [this](const SvQuestUpdateMsg& msg) {
        if (!localPlayerCreated_) { pendingQuestUpdates_.push_back(msg); return; }
        if (chatPanel_) chatPanel_->addMessage(6, "[Quest]", msg.message, static_cast<uint8_t>(0));

        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;

        scene->world().forEach<PlayerController, QuestComponent>(
            [&](Entity* entity, PlayerController* ctrl, QuestComponent* qc) {
                if (!ctrl->isLocalPlayer) return;

                uint32_t questId = 0;
                try { questId = static_cast<uint32_t>(std::stoul(msg.questId)); }
                catch (const std::exception& e) {
                    LOG_WARN("GameApp", "Invalid quest id '%s': %s", msg.questId.c_str(), e.what());
                    return;
                }

                switch (msg.updateType) {
                    case 0: { // accepted
                        auto* sc = entity->getComponent<CharacterStatsComponent>();
                        int level = sc ? sc->stats.level : 1;
                        qc->quests.acceptQuest(questId, level);
                        break;
                    }
                    case 1: // progressUpdate
                        qc->quests.setProgress(questId, msg.currentCount, msg.targetCount);
                        break;
                    case 2: // completed
                        qc->quests.markCompleted(questId);
                        break;
                    case 3: // abandoned
                        qc->quests.abandonQuest(questId);
                        break;
                    default: break;
                }
            }
        );
    };

    // --- State sync handlers (sent by server on connect) ---

    netClient_.onSkillSync = [this](const SvSkillSyncMsg& msg) {
        if (!localPlayerCreated_) { hasPendingSkillSync_ = true; pendingSkillSync_ = msg; return; }
        auto* sc = SceneManager::instance().currentScene();
        if (!sc) return;
        sc->world().forEach<SkillManagerComponent, PlayerController>(
            [&](Entity*, SkillManagerComponent* sm, PlayerController* ctrl) {
                if (!ctrl->isLocalPlayer) return;
                // Rebuild skill state from server data using setSerializedState
                std::vector<LearnedSkill> skills;
                for (const auto& s : msg.skills) {
                    LearnedSkill ls;
                    ls.skillId = s.skillId;
                    ls.unlockedRank = s.unlockedRank;
                    ls.activatedRank = s.activatedRank;
                    skills.push_back(std::move(ls));
                }
                std::vector<std::string> bar;
                for (int i = 0; i < (int)msg.skillBar.size() && i < 20; ++i) {
                    bar.push_back(msg.skillBar[i]);
                }
                bar.resize(20); // pad to 20 slots
                sm->skills.setSerializedState(std::move(skills), std::move(bar),
                    msg.availablePoints, msg.earnedPoints, msg.spentPoints);
            }
        );
    };

    netClient_.onQuestSync = [this](const SvQuestSyncMsg& msg) {
        if (!localPlayerCreated_) { hasPendingQuestSync_ = true; pendingQuestSync_ = msg; return; }
        auto* sc = SceneManager::instance().currentScene();
        if (!sc) return;
        sc->world().forEach<QuestComponent, PlayerController>(
            [&](Entity*, QuestComponent* qc, PlayerController* ctrl) {
                if (!ctrl->isLocalPlayer) return;
                std::vector<uint32_t> completed;
                std::vector<ActiveQuest> active;
                for (const auto& q : msg.quests) {
                    uint32_t qid = 0;
                    try { qid = static_cast<uint32_t>(std::stoul(q.questId)); }
                    catch (const std::exception& e) {
                        LOG_WARN("GameApp", "Invalid quest id '%s' in sync: %s", q.questId.c_str(), e.what());
                        continue;
                    }
                    if (q.state == 1 || q.state == 2) { // completed or failed
                        completed.push_back(qid);
                    } else { // active
                        ActiveQuest aq;
                        aq.questId = qid;
                        for (const auto& [cur, tgt] : q.objectives) {
                            aq.objectiveProgress.push_back(static_cast<uint16_t>(cur));
                        }
                        active.push_back(std::move(aq));
                    }
                }
                qc->quests.setSerializedState(std::move(completed), std::move(active));
            }
        );
    };

    netClient_.onInventorySync = [this](const SvInventorySyncMsg& msg) {
        LOG_INFO("Client", "onInventorySync: %zu slots, %zu equips, localPlayerCreated=%d",
                 msg.slots.size(), msg.equipment.size(), localPlayerCreated_ ? 1 : 0);
        if (!localPlayerCreated_) {
            hasPendingInventorySync_ = true;
            pendingInventorySync_ = msg;
            LOG_INFO("Client", "Buffered pending inventory sync (%zu slots)", msg.slots.size());
            return;
        }
        auto* sc = SceneManager::instance().currentScene();
        if (!sc) return;
        sc->world().forEach<InventoryComponent, PlayerController>(
            [&](Entity* entity, InventoryComponent* invComp, PlayerController* ctrl) {
                if (!ctrl->isLocalPlayer) return;
                // Build slots vector
                std::vector<ItemInstance> slots;
                for (const auto& s : msg.slots) {
                    if (s.slotIndex < 0) continue;
                    // Ensure vector is large enough
                    if (s.slotIndex >= (int)slots.size()) slots.resize(s.slotIndex + 1);
                    ItemInstance item;
                    item.instanceId = "sync_" + std::to_string(s.slotIndex);
                    item.itemId = s.itemId;
                    item.displayName = s.displayName;
                    item.rarity = parseItemRarity(s.rarity);
                    item.itemType = s.itemType;
                    item.quantity = s.quantity;
                    item.enchantLevel = s.enchantLevel;
                    item.levelReq = s.levelReq;
                    item.damageMin = s.damageMin;
                    item.damageMax = s.damageMax;
                    item.armorValue = s.armor;
                    if (!s.rolledStats.empty()) {
                        item.rolledStats = ItemStatRoller::parseRolledStats(s.rolledStats);
                    }
                    if (!s.socketStat.empty()) {
                        item.socket.statType = ItemStatRoller::getStatType(s.socketStat);
                        item.socket.value = s.socketValue;
                        item.socket.isEmpty = false;
                    }
                    slots[s.slotIndex] = std::move(item);
                }
                // Build equipment map
                std::unordered_map<EquipmentSlot, ItemInstance> equipment;
                for (const auto& e : msg.equipment) {
                    ItemInstance item;
                    item.instanceId = "eq_" + std::to_string(e.slot);
                    item.itemId = e.itemId;
                    item.displayName = e.displayName;
                    item.rarity = parseItemRarity(e.rarity);
                    item.itemType = e.itemType;
                    item.quantity = e.quantity;
                    item.enchantLevel = e.enchantLevel;
                    item.levelReq = e.levelReq;
                    item.damageMin = e.damageMin;
                    item.damageMax = e.damageMax;
                    item.armorValue = e.armor;
                    if (!e.rolledStats.empty()) {
                        item.rolledStats = ItemStatRoller::parseRolledStats(e.rolledStats);
                    }
                    if (!e.socketStat.empty()) {
                        item.socket.statType = ItemStatRoller::getStatType(e.socketStat);
                        item.socket.value = e.socketValue;
                        item.socket.isEmpty = false;
                    }
                    equipment[static_cast<EquipmentSlot>(e.slot)] = std::move(item);
                }
                invComp->inventory.setSerializedState(
                    invComp->inventory.getGold(), std::move(slots), std::move(equipment));

                // Update paper-doll style names from equipped items
                auto* appearance = entity->getComponent<AppearanceComponent>();
                if (appearance) {
                    appearance->armorStyle.clear();
                    appearance->hatStyle.clear();
                    appearance->weaponStyle.clear();
                    for (const auto& e : msg.equipment) {
                        auto eqSlot = static_cast<EquipmentSlot>(e.slot);
                        if (eqSlot == EquipmentSlot::Weapon)      appearance->weaponStyle = e.visualStyle;
                        else if (eqSlot == EquipmentSlot::Armor)   appearance->armorStyle  = e.visualStyle;
                        else if (eqSlot == EquipmentSlot::Hat)     appearance->hatStyle    = e.visualStyle;
                    }
                    appearance->dirty = true;
                }
            }
        );
    };

    netClient_.onDeathNotify = [this](const SvDeathNotifyMsg& msg) {
        LOG_INFO("GameApp", "Death notification received: xpLost=%d honorLost=%d timer=%.1f source=%d",
                 msg.xpLost, msg.honorLost, msg.respawnTimer, msg.deathSource);

        if (!localPlayerCreated_) {
            LOG_WARN("GameApp", "Death notification deferred — local player not yet created");
            hasPendingDeathNotify_ = true;
            return;
        }
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) {
            LOG_WARN("GameApp", "Death notification ignored — no current scene");
            return;
        }

        bool foundLocal = false;
        scene->world().forEach<PlayerController, CharacterStatsComponent>(
            [&](Entity*, PlayerController* ctrl, CharacterStatsComponent* sc) {
                if (!ctrl->isLocalPlayer) return;
                foundLocal = true;
                sc->stats.lifeState = LifeState::Dead;
                sc->stats.isDead = true;
                sc->stats.currentHP = 0;
                sc->stats.respawnTimeRemaining = msg.respawnTimer;
            }
        );
        if (!foundLocal) {
            LOG_WARN("GameApp", "Death notification: no local player entity found!");
        }

        LOG_INFO("GameApp", "Death overlay ptr: %s, screen: %s",
                 deathOverlay_ ? "SET" : "NULL",
                 uiManager().getScreen("death_overlay") ? "LOADED" : "NOT LOADED");

        if (deathOverlay_) deathOverlay_->onDeath(msg.xpLost, msg.honorLost, msg.respawnTimer, msg.deathSource);
        // Show retained-mode death overlay
        if (auto* ds = uiManager().getScreen("death_overlay")) {
            ds->setVisible(true);
            // Aurora deaths: hide spawn-point and phoenix-down buttons (town only)
            bool isAurora = (msg.deathSource == 7);
            if (auto* btnSpawn = dynamic_cast<Button*>(ds->findById("btn_respawn_spawn")))
                btnSpawn->setVisible(!isAurora);
            if (auto* btnPhoenix = dynamic_cast<Button*>(ds->findById("btn_respawn_phoenix")))
                btnPhoenix->setVisible(!isAurora);
        }
        audioManager_.playSFX("death");
        LOG_INFO("Client", "You died! Lost %d XP, %d Honor (source=%u)", msg.xpLost, msg.honorLost, msg.deathSource);
    };

    netClient_.onSkillResult = [this](const SvSkillResultMsg& msg) {
        if (!combatSystem_) return;
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;

        // Resolve optimistic prediction — animation already playing, server confirmed
        combatPredictions_.resolveOldest();

        // Find target entity position for floating text
        Vec2 targetPos{0, 0};
        bool foundTarget = false;

        // Check ghost entities (replicated remote players/mobs from server)
        auto ghostIt = ghostEntities_.find(msg.targetId);
        if (ghostIt != ghostEntities_.end()) {
            Entity* ghost = scene->world().getEntity(ghostIt->second);
            if (ghost) {
                auto* t = ghost->getComponent<Transform>();
                if (t) { targetPos = t->position; foundTarget = true; }
            }
        }

        // If target not found in ghosts, check if it's the local player
        if (!foundTarget) {
            scene->world().forEach<PlayerController, Transform>(
                [&](Entity*, PlayerController* ctrl, Transform* t) {
                    if (!ctrl->isLocalPlayer || foundTarget) return;
                    targetPos = t->position;
                    foundTarget = true;
                }
            );
        }

        if (!foundTarget) return;

        // Trigger skill VFX if the skill has one defined
        const auto* skillDef = ClientSkillDefinitionCache::getSkill(msg.skillId);
        if (skillDef && !skillDef->vfxId.empty()) {
            // Find caster position (fall back to target pos if caster ghost not found)
            Vec2 casterPos = targetPos;
            auto casterGhost = ghostEntities_.find(msg.casterId);
            if (casterGhost != ghostEntities_.end()) {
                Entity* caster = scene->world().getEntity(casterGhost->second);
                if (caster) {
                    auto* ct = caster->getComponent<Transform>();
                    if (ct) casterPos = ct->position;
                }
            }
            SkillVFXPlayer::instance().play(skillDef->vfxId, casterPos, targetPos);
        }

        // Spawn appropriate floating text using hitFlags bitmask
        if (msg.hitFlags & HitFlags::RESIST) {
            combatSystem_->showResistText(targetPos);
        } else if (msg.hitFlags & HitFlags::MISS) {
            combatSystem_->showMissText(targetPos);
        } else if (msg.hitFlags & HitFlags::DODGE) {
            combatSystem_->showMissText(targetPos); // TODO: show "Dodge" text
        } else if (msg.hitFlags & HitFlags::BLOCKED) {
            combatSystem_->showMissText(targetPos); // TODO: show "Blocked" text
        } else if (msg.damage > 0) {
            combatSystem_->showDamageText(targetPos, msg.damage,
                                          (msg.hitFlags & HitFlags::CRIT) != 0);
        }

        // Audio feedback
        if (msg.hitFlags & HitFlags::RESIST) {
            audioManager_.playSFX("resist");
        } else if (msg.hitFlags & HitFlags::MISS || msg.hitFlags & HitFlags::DODGE) {
            audioManager_.playSFX("miss");
        } else if (msg.hitFlags & HitFlags::CRIT) {
            audioManager_.playSFX("hit_crit");
        } else if (msg.damage > 0) {
            audioManager_.playSFX("hit_skill");
        }
        if (msg.hitFlags & HitFlags::KILLED) {
            audioManager_.playSFX("kill");
        }

        // Start client-side cooldown from server-authoritative duration
        if (msg.cooldownMs > 0) {
            scene->world().forEach<SkillManagerComponent, PlayerController>(
                [&](Entity*, SkillManagerComponent* smc, PlayerController* ctrl) {
                    if (!ctrl->isLocalPlayer) return;
                    smc->skills.startCooldown(msg.skillId, msg.cooldownMs / 1000.0f);
                }
            );
        }
    };

    netClient_.onRespawn = [this](const SvRespawnMsg& msg) {
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;

        scene->world().forEach<PlayerController, CharacterStatsComponent>(
            [&](Entity* entity, PlayerController* ctrl, CharacterStatsComponent* sc) {
                if (!ctrl->isLocalPlayer) return;
                sc->stats.respawn();
                // Restore visual
                auto* spr = entity->getComponent<SpriteComponent>();
                if (spr) spr->tint = Color::white();
                auto* t = entity->getComponent<Transform>();
                if (t) {
                    t->rotation = 0.0f;
                    t->position = {msg.spawnX, msg.spawnY};
                }
                auto* anim = entity->getComponent<Animator>();
                if (anim) anim->play("idle");
            }
        );

        if (deathOverlay_) deathOverlay_->respawnPending = false;
        // Hide retained-mode death overlay and restore all respawn buttons
        if (auto* ds = uiManager().getScreen("death_overlay")) {
            ds->setVisible(false);
            if (auto* btnSpawn = dynamic_cast<Button*>(ds->findById("btn_respawn_spawn")))
                btnSpawn->setVisible(true);
            if (auto* btnPhoenix = dynamic_cast<Button*>(ds->findById("btn_respawn_phoenix")))
                btnPhoenix->setVisible(true);
        }
        audioManager_.playSFX("respawn");
        LOG_INFO("Client", "Respawned at (%.0f, %.0f)", msg.spawnX, msg.spawnY);
    };

    // deathOverlay_->onRespawnRequested wired in retainedUILoaded_ block

    // --- Retained-mode NPC panel callbacks (wired after screen load in onUpdate) ---
    // Callbacks are wired once npcDialoguePanel_ etc. are resolved from the
    // npc_panels screen. See the retainedUILoaded_ block in onUpdate.

    // --- Skill bar activation callback ---
    // (Wired to skillArc_->onSkillActivated in retainedUILoaded_ block)

    netClient_.onZoneTransition = [this](const SvZoneTransitionMsg& msg) {
        LOG_INFO("Client", "Zone transition to '%s' at (%.1f, %.1f)",
                 msg.targetScene.c_str(), msg.spawnX, msg.spawnY);

        // Snapshot current player stats before the deferred transition destroys the entity.
        // This ensures level/HP/MP are preserved even if SvPlayerState hasn't arrived recently.
        captureLocalPlayerState();

        // Defer the actual scene load to after poll() completes. Loading a scene
        // destroys all entities in the world, which would crash any code that runs
        // later in the same poll() pass (entity updates, combat events, etc.).
        pendingZoneTransition_ = true;
        pendingZoneScene_ = msg.targetScene;
        pendingZoneSpawn_ = {msg.spawnX, msg.spawnY};
        pendingEntityEnters_.clear(); // clear any stale buffered enters

        // Start zone music (crossfades from current track)
        audioManager_.playMusic("assets/audio/music/" + msg.targetScene + ".ogg");
    };

    netClient_.onDungeonInvite = [this](const SvDungeonInviteMsg& msg) {
        LOG_INFO("Client", "Dungeon invite: '%s' (level %d, %ds)",
                 msg.dungeonName.c_str(), msg.levelReq, msg.timeLimitSeconds);

        if (dungeonInviteDialog_) {
            dungeonInviteDialog_->message = "Ready to start " + msg.dungeonName + " dungeon?";
            dungeonInviteDialog_->confirmText = "Accept";
            dungeonInviteDialog_->cancelText = "Decline";
            dungeonInviteDialog_->onConfirm = [this](const std::string&) {
                netClient_.sendDungeonResponse(1);
                dungeonInviteDialog_->setVisible(false);
            };
            dungeonInviteDialog_->onCancel = [this](const std::string&) {
                netClient_.sendDungeonResponse(0);
                dungeonInviteDialog_->setVisible(false);
            };
            dungeonInviteDialog_->setVisible(true);
        }
    };

    netClient_.onDungeonStart = [this](const SvDungeonStartMsg& msg) {
        LOG_INFO("Client", "Dungeon started: scene '%s', time limit %ds",
                 msg.sceneId.c_str(), msg.timeLimitSeconds);

        if (dungeonInviteDialog_) dungeonInviteDialog_->setVisible(false);
        inDungeon_ = true;

        if (chatPanel_) {
            chatPanel_->addMessage(0, "[System]", "Entering dungeon...", 0);
        }

        captureLocalPlayerState();
        pendingZoneTransition_ = true;
        pendingZoneScene_ = msg.sceneId;
        pendingZoneSpawn_ = {0.0f, 0.0f};
        pendingEntityEnters_.clear();
    };

    netClient_.onDungeonEnd = [this](const SvDungeonEndMsg& msg) {
        const char* reasons[] = {"Boss defeated", "Time expired", "Abandoned"};
        const char* reason = (msg.reason < 3) ? reasons[msg.reason] : "Unknown";
        LOG_INFO("Client", "Dungeon ended: reason=%s (%d)", reason, msg.reason);

        inDungeon_ = false;

        if (chatPanel_) {
            std::string chatMsg = std::string("Dungeon instance has ended (") + reason + ").";
            chatPanel_->addMessage(0, "[System]", chatMsg, 0);
        }
    };

    netClient_.onSkillDefs = [this](const SvSkillDefsMsg& msg) {
        if (!localPlayerCreated_) { hasPendingSkillDefs_ = true; pendingSkillDefs_ = msg; return; }
        applySkillDefs(msg);
    };

    // Auth callbacks
    netClient_.onConnectRejected = [this](const std::string& reason) {
        LOG_WARN("GameApp", "Connection rejected: %s", reason.c_str());
        authClient_.disconnectAuth();
        if (loadingPanel_) loadingPanel_->hide();
        if (loginScreenWidget_) {
            loginScreenWidget_->setStatus("Connection rejected: " + reason, true);
            loginScreenWidget_->setVisible(true);
        }
        connState_ = ConnectionState::LoginScreen;
    };

    netClient_.onDisconnected = [this]() {
        if (connState_ == ConnectionState::InGame) {
            authClient_.disconnectAuth();
            // Destroy ghost entities from the world (mobs, players, dropped items)
            auto* scene = SceneManager::instance().currentScene();
            if (scene) {
                for (auto& [pid, handle] : ghostEntities_) {
                    scene->world().destroyEntity(handle);
                }
                // Destroy local player entity
                scene->world().forEach<PlayerController>(
                    [&](Entity* e, PlayerController* ctrl) {
                        if (ctrl->isLocalPlayer) {
                            scene->world().destroyEntity(e->handle());
                        }
                    }
                );
                scene->world().processDestroyQueue();
            }
            ghostEntities_.clear();
            droppedItemPids_.clear();
            ghostDeathTimers_.clear();
            ghostUpdateSeqs_.clear();
            ghostInterpolation_.clear();
            combatPredictions_.clear();
            SkillVFXPlayer::instance().clear();
            connState_ = ConnectionState::LoginScreen;
            localPlayerCreated_ = false;
            retainedUILoaded_ = false;
            npcDialoguePanel_ = nullptr;
            shopPanel_ = nullptr;
            bankPanel_ = nullptr;
            teleporterPanel_ = nullptr;
            arenaPanel_ = nullptr;
            battlefieldPanel_ = nullptr;
            costumePanel_ = nullptr;
            inDungeon_ = false;
            dungeonInviteDialog_ = nullptr;
            // Hide all in-game screens before showing login
            auto& ui = uiManager();
            if (auto* s = ui.getScreen("fate_hud"))         s->setVisible(false);
            if (auto* s = ui.getScreen("fate_menu_panels")) s->setVisible(false);
            if (auto* s = ui.getScreen("fate_social"))      s->setVisible(false);
            if (auto* s = ui.getScreen("npc_panels"))       s->setVisible(false);
            if (auto* s = ui.getScreen("death_overlay"))    s->setVisible(false);
            if (auto* s = ui.getScreen("character_select")) s->setVisible(false);
            if (auto* s = ui.getScreen("character_creation")) s->setVisible(false);

            if (loginScreenWidget_) {
                loginScreenWidget_->reset();
                loginScreenWidget_->setVisible(true);
            }

            // Dismiss loading panel now that login is visible
            if (loadingPanel_) loadingPanel_->hide();
            setIsLoading(false);
            LOG_INFO("GameApp", "Disconnected, cleaned up and returned to login");
        }
    };

    // When auto-reconnect starts (heartbeat timeout), clear stale ghost entities.
    // The server will re-send all entities on successful reconnect.
    netClient_.onReconnectStart = [this]() {
        // Show loading panel to hide the frozen game state (no isLoading_ — we
        // still need onUpdate() to run so the reconnect timeout state machine
        // advances via netClient_.poll()).
        if (loadingPanel_) { loadingPanel_->show("Reconnecting..."); loadingPanel_->setProgress(0.0f); }
        auto* scene = SceneManager::instance().currentScene();
        if (scene) {
            for (auto& [pid, handle] : ghostEntities_) {
                scene->world().destroyEntity(handle);
            }
            scene->world().processDestroyQueue();
        }
        LOG_INFO("GameApp", "Auto-reconnect: cleared %zu ghost entities", ghostEntities_.size());
        ghostEntities_.clear();
        droppedItemPids_.clear();
        ghostDeathTimers_.clear();
        ghostUpdateSeqs_.clear();
        ghostInterpolation_.clear();
        combatPredictions_.clear();
        SkillVFXPlayer::instance().clear();
    };

    netClient_.onStatEnchantResult = [this](const SvStatEnchantResultMsg& msg) {
        // Show result in ChatPanel on the HUD
        if (chatPanel_) chatPanel_->addMessage(6, "[Enchant]", msg.message, static_cast<uint8_t>(0));

        // Play SFX
        if (msg.success) {
            audioManager_.playSFX("enchant_success");
        } else {
            audioManager_.playSFX("enchant_fail");
        }
    };

    // --- NPC panel network result handlers ---
    netClient_.onShopResult = [this](const SvShopResultMsg& msg) {
        if (shopPanel_ && shopPanel_->isOpen()) {
            if (msg.success) {
                shopPanel_->rebuild();
            } else {
                shopPanel_->errorMessage = msg.reason;
                shopPanel_->errorTimer = 3.0f;
            }
        }
    };

    netClient_.onBankResult = [this](const SvBankResultMsg& msg) {
        if (bankPanel_ && bankPanel_->isOpen()) {
            if (msg.success) {
                bankPanel_->rebuild();
            } else {
                bankPanel_->errorMessage = msg.message;
                bankPanel_->errorTimer = 3.0f;
            }
        }
    };

    netClient_.onTeleportResult = [this](const SvTeleportResultMsg& msg) {
        if (!msg.success) return;
        closeAllNpcPanels();
        if (msg.sceneId.empty()) return; // same-scene teleport, no transition needed

        LOG_INFO("Client", "Teleport to '%s' at (%.1f, %.1f)",
                 msg.sceneId.c_str(), msg.posX, msg.posY);

        // Snapshot current player stats before the deferred transition destroys the entity.
        auto* sc = SceneManager::instance().currentScene();
        if (sc) {
            sc->world().forEach<CharacterStatsComponent, PlayerController>(
                [this](Entity* e, CharacterStatsComponent* cs, PlayerController* ctrl) {
                    if (!ctrl->isLocalPlayer) return;
                    pendingPlayerState_.level = cs->stats.level;
                    pendingPlayerState_.currentHP = cs->stats.currentHP;
                    pendingPlayerState_.maxHP = cs->stats.maxHP;
                    pendingPlayerState_.currentMP = cs->stats.currentMP;
                    pendingPlayerState_.maxMP = cs->stats.maxMP;
                    pendingPlayerState_.currentFury = cs->stats.currentFury;
                    pendingPlayerState_.currentXP = cs->stats.currentXP;
                    pendingPlayerState_.honor = cs->stats.honor;
                    pendingPlayerState_.pvpKills = cs->stats.pvpKills;
                    pendingPlayerState_.pvpDeaths = cs->stats.pvpDeaths;
                    auto* inv = e->getComponent<InventoryComponent>();
                    if (inv) pendingPlayerState_.gold = inv->inventory.getGold();
                }
            );
        }

        pendingZoneTransition_ = true;
        pendingZoneScene_ = msg.sceneId;
        pendingZoneSpawn_ = {msg.posX, msg.posY};
        pendingEntityEnters_.clear();

        audioManager_.playMusic("assets/audio/music/" + msg.sceneId + ".ogg");
    };

    netClient_.onRankingResult = [this](const SvRankingResultMsg& msg) {
        if (!leaderboardPanel_ || !leaderboardPanel_->isOpen()) return;
        auto j = nlohmann::json::parse(msg.entriesJson, nullptr, false);
        if (j.is_discarded()) return;

        std::vector<LeaderboardPanel::Entry> entries;
        auto category = static_cast<RankingCategory>(msg.category);

        for (const auto& item : j) {
            LeaderboardPanel::Entry entry;
            entry.rank = item.value("rank", 0);
            entry.name = item.value("characterName", "");
            entry.classType = item.value("classType", "");
            entry.level = item.value("level", 0);

            if (category == RankingCategory::Honor) {
                entry.valueDisplay = "Honor: " + std::to_string(item.value("honor", 0));
            } else if (category == RankingCategory::MobsKilled) {
                entry.valueDisplay = "Kills: " + std::to_string(item.value("totalMobKills", 0));
            } else if (category == RankingCategory::CollectionProgress) {
                float pct = item.value("percentage", 0.0f);
                char buf[16];
                snprintf(buf, sizeof(buf), "%.1f%%", pct);
                entry.valueDisplay = buf;
            } else if (category == RankingCategory::Guilds) {
                entry.name = item.value("guildName", "");
                entry.classType = item.value("ownerName", "");
                entry.valueDisplay = "Lv." + std::to_string(item.value("guildLevel", 0)) +
                                     " (" + std::to_string(item.value("memberCount", 0)) + " members)";
            } else {
                entry.valueDisplay = "Lv." + std::to_string(entry.level);
            }

            entries.push_back(entry);
        }
        leaderboardPanel_->populateEntries(entries, msg.totalEntries, msg.page);
    };

    netClient_.onAuroraStatus = [this](const SvAuroraStatusMsg& msg) {
        Faction favored = static_cast<Faction>(msg.favoredFaction);
        auto* def = FactionRegistry::get(favored);
        std::string name = def ? def->displayName : "Unknown";
        LOG_INFO("Game", "Aurora status: %s favored, %u seconds remaining",
                 name.c_str(), msg.secondsRemaining);
    };

    // --- System result handlers (enchant, repair, extract, craft, socket, pet, arena, battlefield) ---
    netClient_.onEnchantResult = [this](const SvEnchantResultMsg& msg) {
        if (chatPanel_) chatPanel_->addMessage(6, "[Enchant]", msg.message, 0);
    };

    netClient_.onRepairResult = [this](const SvRepairResultMsg& msg) {
        if (chatPanel_) chatPanel_->addMessage(6, "[Repair]", msg.message, 0);
    };

    netClient_.onExtractResult = [this](const SvExtractResultMsg& msg) {
        if (chatPanel_) chatPanel_->addMessage(6, "[Extract]", msg.message, 0);
    };

    netClient_.onCraftResult = [this](const SvCraftResultMsg& msg) {
        if (chatPanel_) chatPanel_->addMessage(6, "[Craft]", msg.message, 0);
        if (craftingPanel_) craftingPanel_->resultMessage = msg.message;
    };

    netClient_.onSocketResult = [this](const SvSocketResultMsg& msg) {
        if (chatPanel_) chatPanel_->addMessage(6, "[Socket]", msg.message, 0);
    };

    netClient_.onPetUpdate = [this](const SvPetUpdateMsg& msg) {
        if (petPanel_) {
            petPanel_->petEquipped = msg.equipped;
            petPanel_->petName = msg.petName;
            petPanel_->petDefId = msg.petDefId;
            petPanel_->petLevel = msg.level;
            petPanel_->petXP = msg.currentXP;
            petPanel_->hasPet = msg.equipped;
        }
        if (chatPanel_) {
            std::string text = msg.equipped ? "Pet " + msg.petName + " equipped!" : "Pet unequipped.";
            chatPanel_->addMessage(6, "[Pet]", text, 0);
        }
    };

    netClient_.onArenaUpdate = [this](const SvArenaUpdateMsg& msg) {
        if (arenaPanel_ && arenaPanel_->isOpen()) {
            if (msg.state == 1) {  // queued
                arenaPanel_->isRegistered = true;
                arenaPanel_->statusMessage = "Searching for opponents...";
            } else if (msg.state == 0) {  // cancelled/unregistered
                arenaPanel_->isRegistered = false;
                arenaPanel_->statusMessage = "";
            }
        }
        if (chatPanel_) {
            std::string text = "Arena status updated.";
            if (msg.result == 1) text = "Victory! +" + std::to_string(msg.honorReward) + " honor";
            else if (msg.result == 2) text = "Defeat. Better luck next time.";
            chatPanel_->addMessage(6, "[Arena]", text, 0);
        }
    };

    netClient_.onBattlefieldUpdate = [this](const SvBattlefieldUpdateMsg& msg) {
        if (battlefieldPanel_ && battlefieldPanel_->isOpen()) {
            if (msg.state == 1) {
                battlefieldPanel_->isRegistered = true;
                battlefieldPanel_->timeUntilStart = msg.timeRemaining;
                battlefieldPanel_->statusMessage = "Registered. Waiting for battle...";
            } else if (msg.state == 0) {
                battlefieldPanel_->isRegistered = false;
                battlefieldPanel_->statusMessage = "";
            }
        }
        if (chatPanel_) {
            std::string text = "Battlefield status updated.";
            if (msg.result == 1) text = "Victory! Your faction prevails!";
            else if (msg.result == 2) text = "Defeat. Your faction has fallen.";
            chatPanel_->addMessage(6, "[Battlefield]", text, 0);
        }
    };

    netClient_.onCollectionDefs = [this](const SvCollectionDefsMsg& msg) {
        if (collectionPanel_) {
            collectionPanel_->entries.clear();
            collectionPanel_->totalCount = static_cast<int>(msg.defs.size());
            for (const auto& def : msg.defs) {
                CollectionPanel::CollectionEntry entry;
                entry.collectionId = def.collectionId;
                entry.name = def.name;
                entry.description = def.description;
                entry.category = def.category;
                entry.rewardType = def.rewardType;
                entry.rewardValue = def.rewardValue;
                entry.completed = false;
                collectionPanel_->entries.push_back(std::move(entry));
            }
        }
    };

    netClient_.onCollectionSync = [this](const SvCollectionSyncMsg& msg) {
        if (collectionPanel_) {
            std::unordered_set<uint32_t> completed(msg.completedIds.begin(), msg.completedIds.end());
            for (auto& entry : collectionPanel_->entries) {
                entry.completed = completed.count(entry.collectionId) > 0;
            }
            collectionPanel_->completedCount = static_cast<int>(msg.completedIds.size());
            collectionPanel_->bonusSTR = msg.bonusSTR;
            collectionPanel_->bonusINT = msg.bonusINT;
            collectionPanel_->bonusDEX = msg.bonusDEX;
            collectionPanel_->bonusCON = msg.bonusCON;
            collectionPanel_->bonusWIS = msg.bonusWIS;
            collectionPanel_->bonusHP = msg.bonusHP;
            collectionPanel_->bonusMP = msg.bonusMP;
            collectionPanel_->bonusDamage = msg.bonusDamage;
            collectionPanel_->bonusArmor = msg.bonusArmor;
            collectionPanel_->bonusCritRate = msg.bonusCritRate;
            collectionPanel_->bonusMoveSpeed = msg.bonusMoveSpeed;
        }
    };

    netClient_.onCostumeDefs = [this](const SvCostumeDefsMsg& msg) {
        costumeDefCache_.clear();
        for (const auto& d : msg.defs) {
            costumeDefCache_[d.costumeDefId] = d;
        }
        // Re-enrich existing panel entries (handles defs arriving after sync)
        if (costumePanel_) {
            for (auto& entry : costumePanel_->ownedCostumes) {
                enrichCostumeEntry(entry);
            }
        }
    };

    netClient_.onCostumeSync = [this](const SvCostumeSyncMsg& msg) {
        if (!costumePanel_) return;
        auto* scene = SceneManager::instance().currentScene();
        if (scene) {
            scene->world().forEach<PlayerController, CostumeComponent>(
                [&](Entity*, PlayerController* ctrl, CostumeComponent* costume) {
                    if (!ctrl->isLocalPlayer) return;
                    costume->ownedCostumes.clear();
                    for (const auto& id : msg.ownedCostumeIds) costume->ownedCostumes.insert(id);
                    costume->equippedBySlot.clear();
                    for (const auto& [slot, id] : msg.equipped) costume->equippedBySlot[slot] = id;
                    costume->showCostumes = (msg.showCostumes != 0);
                }
            );
        }
        costumePanel_->ownedCostumes.clear();
        for (const auto& id : msg.ownedCostumeIds) {
            CostumeEntry entry;
            entry.costumeDefId = id;
            entry.displayName  = id;
            enrichCostumeEntry(entry);
            costumePanel_->ownedCostumes.push_back(std::move(entry));
        }
        costumePanel_->equippedBySlot.clear();
        for (const auto& [slot, id] : msg.equipped) costumePanel_->equippedBySlot[slot] = id;
        costumePanel_->showCostumes = (msg.showCostumes != 0);
    };

    netClient_.onCostumeUpdate = [this](const SvCostumeUpdateMsg& msg) {
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;
        CostumeComponent* costume = nullptr;
        scene->world().forEach<PlayerController, CostumeComponent>(
            [&](Entity*, PlayerController* ctrl, CostumeComponent* c) {
                if (ctrl->isLocalPlayer) costume = c;
            }
        );
        if (!costume) return;

        switch (msg.updateType) {
            case 0: // obtained
                costume->ownedCostumes.insert(msg.costumeDefId);
                if (costumePanel_) {
                    CostumeEntry entry;
                    entry.costumeDefId = msg.costumeDefId;
                    entry.displayName  = msg.costumeDefId;
                    enrichCostumeEntry(entry);
                    costumePanel_->ownedCostumes.push_back(std::move(entry));
                }
                break;
            case 1: // equipped
                costume->equippedBySlot[msg.slotType] = msg.costumeDefId;
                if (costumePanel_) costumePanel_->equippedBySlot[msg.slotType] = msg.costumeDefId;
                break;
            case 2: // unequipped
                costume->equippedBySlot.erase(msg.slotType);
                if (costumePanel_) costumePanel_->equippedBySlot.erase(msg.slotType);
                break;
            case 3: // toggleChanged
                costume->showCostumes = (msg.show != 0);
                if (costumePanel_) costumePanel_->showCostumes = (msg.show != 0);
                break;
        }
    };

    // Initialize audio
    audioManager_.init();
    {
        std::string sfxDir = std::string(FATE_SOURCE_DIR) + "/assets/audio/sfx";
        int loaded = audioManager_.loadSFXDirectory(sfxDir);
        LOG_INFO("Audio", "Loaded %d SFX from %s", loaded, sfxDir.c_str());
    }

    // Initialize SDF text rendering
    SDFText::instance().init("assets/fonts/default.png", "assets/fonts/default.json");

    // Load font registry
    {
        auto& fontRegistry = fate::FontRegistry::instance();
        if (fontRegistry.parseManifest("assets/fonts/fonts.json")) {
            fontRegistry.loadAtlases();
            fate::SDFText::instance().setFontRegistry(&fontRegistry);
            LOG_INFO("GameApp", "Font registry loaded with %zu fonts",
                     fontRegistry.fontNames().size());
        }
    }

    // Load skill VFX definitions
    SkillVFXPlayer::instance().loadDefinitions("assets/vfx/");

    CombatTextConfig::instance().load(CombatTextConfig::kDefaultPath);

    // ========================================================================
    // Register game render passes with the render graph
    // ========================================================================
    auto& graph = renderGraph();

    // Pass: GroundTiles — clear Scene FBO for subsequent passes
    graph.addPass({"GroundTiles", true, [this](RenderPassContext& ctx) {
        auto& sceneFbo = ctx.graph->getFBO("Scene", ctx.viewportWidth, ctx.viewportHeight, true);
        sceneFbo.bind();
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        sceneFbo.unbind();
    }});

    // Pass: Entities — sprite rendering (accumulates onto Scene FBO)
    graph.addPass({"Entities", true, [this](RenderPassContext& ctx) {
        auto& sceneFbo = ctx.graph->getFBO("Scene", ctx.viewportWidth, ctx.viewportHeight, true);
        sceneFbo.bind();

        if (renderSystem_ && !isLoading()) {
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

    // Pass: SkillVFX — skill visual effects (accumulates onto Scene FBO)
    graph.addPass({"SkillVFX", true, [this](RenderPassContext& ctx) {
        auto& sceneFbo = ctx.graph->getFBO("Scene", ctx.viewportWidth, ctx.viewportHeight, true);
        sceneFbo.bind();
        Mat4 vp = ctx.camera->getViewProjection();
        ctx.spriteBatch->begin(vp);
        SkillVFXPlayer::instance().render(*ctx.spriteBatch, SDFText::instance());
        ctx.spriteBatch->end();
        sceneFbo.unbind();
    }});

    // Pass: SDFText — floating damage/XP text (accumulates onto Scene FBO)
    graph.addPass({"SDFText", true, [this](RenderPassContext& ctx) {
        auto& sceneFbo = ctx.graph->getFBO("Scene", ctx.viewportWidth, ctx.viewportHeight, true);
        sceneFbo.bind();

        if (combatSystem_) {
            combatSystem_->renderFloatingTexts(*ctx.spriteBatch, *ctx.camera, SDFText::instance());
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
        renderAttackRange(*ctx.spriteBatch, *ctx.camera);

        // SpawnSystem debug rendering removed — mobs are server-only

        sceneFbo.unbind();
    }});

    // Invalidate cached widget pointers when a screen is hot-reloaded
    uiManager().addScreenReloadListener([this](const std::string& screenId) {
        if (screenId == "fate_menu_panels") {
            inventoryPanel_ = nullptr;
            petPanel_ = nullptr;
            craftingPanel_ = nullptr;
            collectionPanel_ = nullptr;
            costumePanel_ = nullptr;
            retainedUILoaded_ = false;
        } else if (screenId == "fate_hud") {
            skillArc_ = nullptr;
            dungeonInviteDialog_ = nullptr;
            retainedUILoaded_ = false;
        } else if (screenId == "fate_social") {
            chatPanel_ = nullptr;
            retainedUILoaded_ = false;
        } else if (screenId == "death_overlay") {
            deathOverlay_ = nullptr;
            retainedUILoaded_ = false;
        } else if (screenId == "npc_panels") {
            npcDialoguePanel_ = nullptr;
            shopPanel_ = nullptr;
            bankPanel_ = nullptr;
            teleporterPanel_ = nullptr;
            arenaPanel_ = nullptr;
            battlefieldPanel_ = nullptr;
            retainedUILoaded_ = false;
        } else if (screenId == "login") {
            loginScreenWidget_ = nullptr;
        } else if (screenId == "loading_screen") {
            loadingPanel_ = dynamic_cast<LoadingPanel*>(uiManager().getScreen("loading_screen"));
        }
    });

    // Load retained-mode login screen (visible immediately at startup)
    uiManager().loadScreen("assets/ui/screens/login.json");

    // Load loading screen (always available, starts hidden)
    uiManager().loadScreen("assets/ui/screens/loading_screen.json");
    loadingPanel_ = dynamic_cast<LoadingPanel*>(uiManager().getScreen("loading_screen"));

#ifdef EDITOR_BUILD
    // Pre-load all UI screens so the editor hierarchy always shows everything.
    // All start hidden — game state still controls visibility as normal.
    {
        auto& ui = uiManager();
        const char* screens[] = {
            "assets/ui/screens/character_creation.json",
            "assets/ui/screens/character_select.json",
            "assets/ui/screens/death_overlay.json",
            "assets/ui/screens/fate_hud.json",
            "assets/ui/screens/fate_menu_panels.json",
            "assets/ui/screens/fate_social.json",
            "assets/ui/screens/npc_panels.json",
        };
        for (auto* path : screens) {
            ui.loadScreen(path);
        }
        // Force all roots hidden — game state enables them as needed
        for (auto& id : ui.screenIds()) {
            if (id == "login") continue;
            if (auto* root = ui.getScreen(id))
                root->setVisible(false);
        }
    }
#endif

    loginScreenWidget_ = dynamic_cast<LoginScreen*>(uiManager().getScreen("login"));
    if (loginScreenWidget_) {
        loginScreenWidget_->loadPreferences();

        loginScreenWidget_->onLogin = [this](const std::string& user, const std::string& pass,
                                              const std::string& server, int port) {
            authClient_.loginAsync(server, static_cast<uint16_t>(port), user, pass);
            if (!authClient_.isBusy()) {
                loginScreenWidget_->setStatus("Login failed — try again", true);
                return;
            }
            connState_ = ConnectionState::Authenticating;
            loginScreenWidget_->setStatus("Authenticating...", false);
        };

        loginScreenWidget_->onRegister = [this](const std::string& user, const std::string& pass,
                                                  const std::string& email,
                                                  const std::string& server, int port) {
            pendingRegUser_ = user; pendingRegPass_ = pass;
            pendingRegEmail_ = email; pendingRegServer_ = server;
            pendingRegPort_ = port;
            // Show character creation, hide login
            loginScreenWidget_->setVisible(false);
            // Load character_creation screen if not already loaded
            if (!uiManager().getScreen("character_creation"))
                uiManager().loadScreen("assets/ui/screens/character_creation.json");
            auto* ccw = dynamic_cast<CharacterCreationScreen*>(uiManager().getScreen("character_creation"));
            if (ccw) {
                ccw->setVisible(true);
                // Wire registration-flow callbacks
                ccw->onNext = [this](const std::string&) {
                    auto* cc = dynamic_cast<CharacterCreationScreen*>(uiManager().getScreen("character_creation"));
                    if (!cc) return;
                    const char* classNames[] = {"Warrior", "Mage", "Archer"};
                    constexpr Faction factions[] = {Faction::Xyros, Faction::Fenor, Faction::Zethos, Faction::Solis};
                    pendingFaction_ = factions[cc->selectedFaction];
                    authClient_.registerAsync(
                        pendingRegServer_, static_cast<uint16_t>(pendingRegPort_),
                        pendingRegUser_, pendingRegPass_, pendingRegEmail_,
                        cc->characterName, classNames[cc->selectedClass]);
                    connState_ = ConnectionState::Authenticating;
                    cc->setVisible(false);
                    if (loginScreenWidget_) {
                        loginScreenWidget_->setVisible(true);
                        loginScreenWidget_->setStatus("Creating account...", false);
                    }
                };
                ccw->onBack = [this](const std::string&) {
                    auto* cc = dynamic_cast<CharacterCreationScreen*>(uiManager().getScreen("character_creation"));
                    if (cc) cc->setVisible(false);
                    if (loginScreenWidget_) loginScreenWidget_->setVisible(true);
                    connState_ = ConnectionState::LoginScreen;
                };
            }
            connState_ = ConnectionState::CharacterCreation;
        };
    }

    // When the editor undo/redo replaces a UI screen tree via loadScreenFromString(),
    // all cached widget pointers into that screen become dangling.  Re-resolve them
    // immediately and re-wire essential callbacks.  (The retainedUILoaded_ block can't
    // help here because it sits inside the localPlayerCreated_ guard which is already
    // true during normal gameplay.)
    uiManager().addScreenReloadListener([this](const std::string& screenId) {
        if (connState_ != ConnectionState::InGame) return;
        auto& ui = uiManager();

        if (screenId == "fate_social") {
            chatPanel_ = nullptr;
            if (auto* s = ui.getScreen("fate_social"))
                chatPanel_ = dynamic_cast<ChatPanel*>(s->findById("chat_panel"));
            if (chatPanel_) {
                chatPanel_->onSendMessage = [this](uint8_t channel, const std::string& message, const std::string& targetName) {
                    auto filtered = ProfanityFilter::filterChatMessage(message, FilterMode::Censor);
                    netClient_.sendChat(channel, filtered.filteredText, targetName);
                    audioManager_.playSFX("chat_send");
                };
                chatPanel_->onClose = [this](const std::string&) {
                    if (chatPanel_) chatPanel_->setFullPanelMode(false);
                    uiManager().clearFocus();
                    Input::instance().setChatMode(false);
                };
            }
        } else if (screenId == "fate_hud") {
            skillArc_ = nullptr;
            dungeonInviteDialog_ = nullptr;
            destroyItemDialog_ = nullptr;
            playerContextMenu_ = nullptr;
            if (auto* h = ui.getScreen("fate_hud")) {
                skillArc_ = dynamic_cast<SkillArc*>(h->findById("skill_arc"));
                dungeonInviteDialog_ = dynamic_cast<ConfirmDialog*>(h->findById("dungeon_invite_dialog"));
                destroyItemDialog_ = dynamic_cast<ConfirmDialog*>(h->findById("destroy_item_dialog"));
                playerContextMenu_ = dynamic_cast<PlayerContextMenu*>(h->findById("player_context_menu"));
            }
        } else if (screenId == "fate_menu_panels") {
            inventoryPanel_ = nullptr;
            petPanel_ = nullptr;
            craftingPanel_ = nullptr;
            collectionPanel_ = nullptr;
            costumePanel_ = nullptr;
            if (auto* m = ui.getScreen("fate_menu_panels")) {
                inventoryPanel_ = dynamic_cast<InventoryPanel*>(m->findById("inventory_panel"));
                petPanel_ = dynamic_cast<PetPanel*>(m->findById("pet_panel"));
                craftingPanel_ = dynamic_cast<CraftingPanel*>(m->findById("crafting_panel"));
                collectionPanel_ = dynamic_cast<CollectionPanel*>(m->findById("collection_panel"));
                costumePanel_ = dynamic_cast<CostumePanel*>(m->findById("costume_panel"));
            }
        } else if (screenId == "npc_panels") {
            npcDialoguePanel_ = nullptr;
            shopPanel_ = nullptr;
            bankPanel_ = nullptr;
            teleporterPanel_ = nullptr;
            arenaPanel_ = nullptr;
            battlefieldPanel_ = nullptr;
            leaderboardPanel_ = nullptr;
            if (auto* n = ui.getScreen("npc_panels")) {
                npcDialoguePanel_ = dynamic_cast<NpcDialoguePanel*>(n->findById("npc_dialogue_panel"));
                shopPanel_ = dynamic_cast<ShopPanel*>(n->findById("shop_panel"));
                bankPanel_ = dynamic_cast<BankPanel*>(n->findById("bank_panel"));
                teleporterPanel_ = dynamic_cast<TeleporterPanel*>(n->findById("teleporter_panel"));
                arenaPanel_ = dynamic_cast<ArenaPanel*>(n->findById("arena_panel"));
                battlefieldPanel_ = dynamic_cast<BattlefieldPanel*>(n->findById("battlefield_panel"));
                leaderboardPanel_ = dynamic_cast<LeaderboardPanel*>(n->findById("leaderboard_panel"));
            }
        } else if (screenId == "death_overlay") {
            deathOverlay_ = nullptr;
            deathOverlay_ = dynamic_cast<DeathOverlay*>(ui.getScreen("death_overlay"));
        }
    });

    PaperDollCatalog::instance().load("assets/paper_doll.json");

    LOG_INFO("Game", "Initialized");
}

void GameApp::onLoadingUpdate(float deltaTime) {
    // isLoading_ is set during scene loads. If the server drops during a
    // scene load and reconnect starts, we still need to poll the net client
    // so the reconnect timeout fires and returns to login.
    if (netClient_.isReconnecting()) {
        netTime_ += deltaTime;
        netClient_.poll(netTime_);
    }
}

void GameApp::onUpdate(float deltaTime) {
    switch (connState_) {
        case ConnectionState::LoginScreen:
            // Callbacks on loginScreenWidget_ drive login/register — nothing to poll
            break;

        case ConnectionState::CharacterCreation: {
            // Poll for create result while on creation screen
            if (authClient_.hasResult()) {
                auto result = authClient_.consumeResult();
                if (result.type == AuthResultType::Create) {
                    auto* ccw = dynamic_cast<CharacterCreationScreen*>(
                        uiManager().getScreen("character_creation"));
                    if (result.success) {
                        pendingCharacterList_ = result.characters;
                        auto* charSelect = dynamic_cast<CharacterSelectScreen*>(
                            uiManager().getScreen("character_select"));
                        if (charSelect) {
                            populateCharacterSlots(charSelect, pendingCharacterList_);
                            charSelect->setVisible(true);
                        }
                        if (ccw) ccw->setVisible(false);
                        connState_ = ConnectionState::CharacterSelect;
                    } else {
                        if (ccw) {
                            ccw->statusMessage = result.errorMessage;
                            ccw->isError = true;
                        }
                    }
                }
            }
            break;
        }

        case ConnectionState::CharacterSelect: {
            // Poll for create/delete results while on character select screen
            if (authClient_.hasResult()) {
                auto result = authClient_.consumeResult();
                if (result.type == AuthResultType::Create || result.type == AuthResultType::Delete) {
                    auto* charSelect = dynamic_cast<CharacterSelectScreen*>(
                        uiManager().getScreen("character_select"));
                    if (result.success && charSelect) {
                        pendingCharacterList_ = result.characters;
                        populateCharacterSlots(charSelect, pendingCharacterList_);
                    }
                    if (result.type == AuthResultType::Delete && !result.success) {
                        LOG_WARN("GameApp", "Character delete failed: %s", result.errorMessage.c_str());
                    }
                }
            }
            break;
        }

        case ConnectionState::Authenticating: {
            // Poll for auth result — handles Login and Select result types
            if (authClient_.hasResult()) {
                AuthClientResult result = authClient_.consumeResult();
                if (result.type == AuthResultType::Login) {
                    if (result.success) {
                        pendingCharacterList_ = result.characters;
                        // Store server host for later UDP connect
                        if (loginScreenWidget_)
                            pendingRegServer_ = loginScreenWidget_->serverHost;
                        // Load and show character select screen
                        if (!uiManager().getScreen("character_select"))
                            uiManager().loadScreen("assets/ui/screens/character_select.json");
                        if (!uiManager().getScreen("character_creation"))
                            uiManager().loadScreen("assets/ui/screens/character_creation.json");
                        auto* charSelect = dynamic_cast<CharacterSelectScreen*>(
                            uiManager().getScreen("character_select"));
                        if (charSelect) {
                            wireCharacterSelectCallbacks(charSelect);
                            populateCharacterSlots(charSelect, pendingCharacterList_);
                            charSelect->setVisible(true);
                        }
                        if (loginScreenWidget_) loginScreenWidget_->setVisible(false);
                        connState_ = ConnectionState::CharacterSelect;
                        LOG_INFO("GameApp", "Login success, showing character select (%zu characters)",
                                 pendingCharacterList_.size());
                    } else {
                        if (loginScreenWidget_)
                            loginScreenWidget_->setStatus(result.errorMessage, true);
                        connState_ = ConnectionState::LoginScreen;
                    }
                }
                if (result.type == AuthResultType::Select) {
                    if (result.success) {
                        const auto& sel = result.selectData;
                        pendingAuthToken_ = sel.authToken;
                        pendingCharName_ = sel.characterName;
                        pendingClassName_ = sel.className;
                        pendingSceneName_ = sel.sceneName;
                        pendingSpawnPos_ = Coords::toPixel({sel.spawnX, sel.spawnY});
                        pendingLevel_ = sel.level;
                        pendingFaction_ = static_cast<Faction>(sel.faction);
                        pendingAuthResponse_ = result;
                        // Connect UDP with the real token from SelectCharResponse
                        std::string host = pendingRegServer_.empty()
                            ? (loginScreenWidget_ ? loginScreenWidget_->serverHost : "127.0.0.1")
                            : pendingRegServer_;
                        netClient_.connectWithToken(host, static_cast<uint16_t>(serverPort_), pendingAuthToken_);
                        connState_ = ConnectionState::UDPConnecting;
                        if (loginScreenWidget_) loginScreenWidget_->setStatus("Connecting to game server...", false);
                        LOG_INFO("GameApp", "Character selected, connecting to game server");
                    } else {
                        // Show error, go back to select
                        auto* cs = dynamic_cast<CharacterSelectScreen*>(uiManager().getScreen("character_select"));
                        if (cs) cs->setVisible(true);
                        connState_ = ConnectionState::CharacterSelect;
                        LOG_WARN("GameApp", "Character select failed: %s", result.errorMessage.c_str());
                    }
                }
            }
            break;
        }

        case ConnectionState::UDPConnecting: {
            // Poll network for ConnectAccept/ConnectReject
            netTime_ += deltaTime;
            netClient_.poll(netTime_);

            if (netClient_.isConnected()) {
                if (loginScreenWidget_) {
                    loginScreenWidget_->setStatus("", false);
                    // Save remember-me preferences on successful connection
                    if (loginScreenWidget_->rememberMe)
                        loginScreenWidget_->savePreferences();
                    loginScreenWidget_->setVisible(false);
                }

                // Defer player creation to next frame by setting flag
                localPlayerCreated_ = false;
                retainedUILoaded_ = false;
                npcDialoguePanel_ = nullptr;
                shopPanel_ = nullptr;
                bankPanel_ = nullptr;
                teleporterPanel_ = nullptr;
                arenaPanel_ = nullptr;
                battlefieldPanel_ = nullptr;
                costumePanel_ = nullptr;
                dungeonInviteDialog_ = nullptr;
                inDungeon_ = false;

                // Start async scene load instead of going straight to InGame
                if (!pendingSceneName_.empty()) {
                    std::string jsonPath = "assets/scenes/" + pendingSceneName_ + ".json";
                    asyncLoader_.startLoad(pendingSceneName_, jsonPath);
#ifndef FATE_SHIPPING
                    // Tell the editor which scene file is loaded so Ctrl+S works
                    Editor::instance().setCurrentScenePath(jsonPath);
#endif
                    if (loadingPanel_) loadingPanel_->show(pendingSceneName_);
                    setIsLoading(true);
                    loadingMinTimer_ = 2.0f;
                    loadingDataReady_ = false;
                    connState_ = ConnectionState::LoadingScene;
                    pendingZoneTransition_ = true; // buffer entity enters during loading
                } else {
                    connState_ = ConnectionState::InGame;
                }

                LOG_INFO("GameApp", "Connected to game server, entering game as '%s' (%s)",
                         pendingCharName_.c_str(), pendingClassName_.c_str());
            } else if (!netClient_.isWaitingForConnection()) {
                // Connect timed out or failed — return to login screen
                connState_ = ConnectionState::LoginScreen;
                if (loginScreenWidget_) loginScreenWidget_->setStatus("Connection timed out", true);
            }
            // ConnectReject is handled by the onConnectRejected callback set up in onInit
            break;
        }

        case ConnectionState::LoadingScene: {
            netTime_ += deltaTime;
            netClient_.poll(netTime_);

            // Check disconnect during loading
            if (!netClient_.isConnected() && !netClient_.isWaitingForConnection() &&
                !netClient_.isReconnecting()) {
                LOG_WARN("GameApp", "Disconnected during scene load");
                connState_ = ConnectionState::LoginScreen;
                setIsLoading(false);
                if (loadingPanel_) loadingPanel_->hide();
                pendingZoneTransition_ = false;
                if (loginScreenWidget_) {
                    loginScreenWidget_->setStatus("Disconnected", true);
                    loginScreenWidget_->setVisible(true);
                }
                break;
            }

            // Check worker failure
            if (asyncLoader_.hasFailed()) {
                LOG_ERROR("GameApp", "Scene load failed: %s", asyncLoader_.errorMessage().c_str());
                connState_ = ConnectionState::LoginScreen;
                setIsLoading(false);
                if (loadingPanel_) loadingPanel_->hide();
                pendingZoneTransition_ = false;
                if (loginScreenWidget_) {
                    loginScreenWidget_->setStatus("Failed to load scene", true);
                    loginScreenWidget_->setVisible(true);
                }
                break;
            }

            // Tick finalization (entity creation + texture progress)
            auto* sc = SceneManager::instance().currentScene();
            if (sc && asyncLoader_.isWorkerDone() && !loadingDataReady_) {
                bool done = asyncLoader_.tickFinalization(sc->world());
                if (done) loadingDataReady_ = true;
            }

            // Count down minimum display timer
            if (loadingMinTimer_ > 0.0f) loadingMinTimer_ -= deltaTime;

            // Transition to InGame only when data is ready AND min time elapsed
            if (loadingDataReady_ && loadingMinTimer_ <= 0.0f) {
                localPlayerCreated_ = false;
                retainedUILoaded_ = false;
                npcDialoguePanel_ = nullptr;
                shopPanel_ = nullptr;
                bankPanel_ = nullptr;
                teleporterPanel_ = nullptr;
                arenaPanel_ = nullptr;
                battlefieldPanel_ = nullptr;
                costumePanel_ = nullptr;
                dungeonInviteDialog_ = nullptr;
                inDungeon_ = false;

                connState_ = ConnectionState::InGame;
                setIsLoading(false);
                if (loadingPanel_) loadingPanel_->hide();

                // Clear zone transition flag and replay buffered entity enters
                pendingZoneTransition_ = false;
                if (!pendingEntityEnters_.empty()) {
                    LOG_INFO("GameApp", "Replaying %zu buffered entity enters",
                             pendingEntityEnters_.size());
                    for (const auto& enterMsg : pendingEntityEnters_) {
                        if (netClient_.onEntityEnter) {
                            netClient_.onEntityEnter(enterMsg);
                        }
                    }
                    pendingEntityEnters_.clear();
                }

                LOG_INFO("GameApp", "Loading complete, entering game");
            }

            // Show real progress while loading, hold at 100% while waiting for min timer
            if (loadingPanel_) loadingPanel_->setProgress(loadingDataReady_ ? 1.0f : asyncLoader_.progress());
            break;
        }

        case ConnectionState::InGame: {
            // Pending logout: count down then disconnect
            if (logoutTimer_ > 0.0f) {
                logoutTimer_ -= deltaTime;
                if (loadingPanel_) loadingPanel_->setProgress(1.0f - logoutTimer_ / 1.5f);
                if (logoutTimer_ <= 0.0f) {
                    logoutTimer_ = 0.0f;
                    authClient_.disconnectAuth();
                    netClient_.disconnect();
                }
                break;
            }

            // Deferred player creation — runs on first InGame frame.
            // Scene was already loaded by AsyncSceneLoader before we got here.
            if (!localPlayerCreated_) {
                localPlayerCreated_ = true;

                // Determine if this is a zone transition or initial login
                bool isZoneTransition = !pendingZoneScene_.empty();

                auto* sc = SceneManager::instance().currentScene();
                if (sc) {
                    // Parse class type from pending class name
                    ClassType ct = ClassType::Warrior;
                    if (pendingClassName_ == "Mage") ct = ClassType::Mage;
                    else if (pendingClassName_ == "Archer") ct = ClassType::Archer;

                    // Create full player with all 24 game components
                    Entity* player = EntityFactory::createPlayer(
                        sc->world(), pendingCharName_, ct, true, pendingFaction_,
                        pendingGender_, pendingHairstyle_);

                    // Set spawn position: zone transition uses pendingZoneSpawn_,
                    // initial login uses pendingSpawnPos_ from auth response
                    auto* t = player->getComponent<Transform>();
                    if (t) t->position = isZoneTransition ? pendingZoneSpawn_ : pendingSpawnPos_;

                    // Prevent portal overlap from triggering an instant zone transition
                    if (zoneSystem_) zoneSystem_->setSpawnGrace(1.0f);

                    if (isZoneTransition) {
                        // Restore player state from last SvPlayerState (zone transition)
                        auto* cs = player->getComponent<CharacterStatsComponent>();
                        if (cs) {
                            const auto& ps = pendingPlayerState_;
                            cs->stats.level = ps.level;
                            cs->stats.recalculateStats();
                            cs->stats.recalculateXPRequirement();
                            cs->stats.currentHP = ps.currentHP;
                            cs->stats.maxHP = ps.maxHP;
                            cs->stats.currentMP = ps.currentMP;
                            cs->stats.maxMP = ps.maxMP;
                            cs->stats.currentFury = ps.currentFury;
                            cs->stats.currentXP = ps.currentXP;
                            cs->stats.honor = ps.honor;
                            cs->stats.pvpKills = ps.pvpKills;
                            cs->stats.pvpDeaths = ps.pvpDeaths;
                        }
                        auto* inv = player->getComponent<InventoryComponent>();
                        if (inv) inv->inventory.setGold(pendingPlayerState_.gold);
                    } else {
                        // Apply character snapshot from SelectCharResponse
                        auto* cs = player->getComponent<CharacterStatsComponent>();
                        if (cs) {
                            const auto& sel = pendingAuthResponse_.selectData;
                            cs->stats.level = sel.level;
                            cs->stats.recalculateStats();
                            cs->stats.recalculateXPRequirement();
                            cs->stats.currentXP = sel.currentXP;
                            cs->stats.currentHP = sel.currentHP;
                            cs->stats.maxHP = sel.maxHP;
                            cs->stats.currentMP = sel.currentMP;
                            cs->stats.maxMP = sel.maxMP;
                            cs->stats.currentFury = sel.currentFury;
                        }
                        auto* inv = player->getComponent<InventoryComponent>();
                        if (inv) inv->inventory.setGold(pendingAuthResponse_.selectData.gold);
                    }

                    // SpawnSystem removed from client — mobs come from server replication only

                    // Apply pending server state (level, HP, XP, etc.) that arrived
                    // before the player entity existed (covers both paths).
                    if (hasPendingPlayerState_ && !isZoneTransition) {
                        hasPendingPlayerState_ = false;
                        auto* cs = player->getComponent<CharacterStatsComponent>();
                        if (cs) {
                            const auto& ps = pendingPlayerState_;
                            // Set level and recalculate first, then override with server values
                            cs->stats.level = ps.level;
                            cs->stats.recalculateStats();
                            cs->stats.recalculateXPRequirement();
                            cs->stats.currentHP = ps.currentHP;
                            cs->stats.maxHP = ps.maxHP;
                            cs->stats.currentMP = ps.currentMP;
                            cs->stats.maxMP = ps.maxMP;
                            cs->stats.currentFury = ps.currentFury;
                            cs->stats.currentXP = ps.currentXP;
                            cs->stats.honor = ps.honor;
                            cs->stats.pvpKills = ps.pvpKills;
                            cs->stats.pvpDeaths = ps.pvpDeaths;
                        }
                        auto* inv = player->getComponent<InventoryComponent>();
                        if (inv) {
                            inv->inventory.setGold(pendingPlayerState_.gold);
                        }
                    } else if (isZoneTransition) {
                        hasPendingPlayerState_ = false; // already applied above
                    }

                    Vec2 spawnPos = isZoneTransition ? pendingZoneSpawn_ : pendingSpawnPos_;
                    std::string sceneName = isZoneTransition ? pendingZoneScene_ : pendingSceneName_;
                    auto* finalStats = player->getComponent<CharacterStatsComponent>();
                    auto* appearance = player->getComponent<AppearanceComponent>();
                    LOG_INFO("GameApp", "Local player created for '%s' (%s Lv%d) at (%.0f, %.0f) in %s — gender=%d hairstyle=%d",
                             pendingCharName_.c_str(), pendingClassName_.c_str(),
                             finalStats ? finalStats->stats.level : 0,
                             spawnPos.x, spawnPos.y, sceneName.c_str(),
                             appearance ? appearance->gender : -1,
                             appearance ? appearance->hairstyle : -1);

                    // Clear zone scene name after use so subsequent frames don't
                    // re-detect as zone transition
                    if (isZoneTransition) pendingZoneScene_.clear();

                    // Compute world bounds from zone entities and set on MovementSystem
                    {
                        auto* moveSys = sc->world().getSystem<MovementSystem>();
                        if (moveSys) {
                            Rect bounds = {0, 0, 0, 0};
                            sc->world().forEach<Transform, ZoneComponent>(
                                [&](Entity*, Transform* zt, ZoneComponent* zc) {
                                    // Use the largest zone as the world boundary
                                    Rect zr = {zt->position.x - zc->size.x * 0.5f,
                                               zt->position.y - zc->size.y * 0.5f,
                                               zc->size.x, zc->size.y};
                                    if (zr.w * zr.h > bounds.w * bounds.h) {
                                        bounds = zr;
                                    }
                                }
                            );
                            // Fallback: if no zones, use a generous default around spawn
                            if (bounds.w <= 0 || bounds.h <= 0) {
                                bounds = {-2000, -2000, 4000, 4000};
                            }
                            moveSys->worldBounds = bounds;
                            LOG_INFO("GameApp", "Movement bounds: (%.0f,%.0f)-(%.0f,%.0f)",
                                     bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h);
                        }
                    }

                    // Build collision grid from collision-layer tiles
                    collisionGrid_.beginBuild();
                    sc->world().forEach<Transform, TileLayerComponent>(
                        [&](Entity*, Transform* t, TileLayerComponent* tlc) {
                            if (tlc->layer == "collision") {
                                int tx = (int)std::floor(t->position.x / 32.0f);
                                int ty = (int)std::floor(t->position.y / 32.0f);
                                collisionGrid_.markBlocked(tx, ty);
                            }
                        }
                    );
                    collisionGrid_.endBuild();
                    if (movementSystem_) movementSystem_->setCollisionGrid(&collisionGrid_);
                    if (mobAISystem_) mobAISystem_->setLocalCollisionGrid(&collisionGrid_);

                    // Apply pending inventory sync that arrived before player existed
                    if (hasPendingInventorySync_) {
                        hasPendingInventorySync_ = false;
                        if (netClient_.onInventorySync)
                            netClient_.onInventorySync(pendingInventorySync_);
                        LOG_INFO("Client", "Applied pending inventory sync on connect");
                    }

                    // Apply pending death notification that arrived before player existed
                    if (hasPendingDeathNotify_) {
                        hasPendingDeathNotify_ = false;
                        if (finalStats) {
                            finalStats->stats.lifeState = LifeState::Dead;
                            finalStats->stats.isDead = true;
                            finalStats->stats.currentHP = 0;
                        }
                        if (deathOverlay_) deathOverlay_->onDeath(0, 0, 0.0f);
                        if (auto* ds = uiManager().getScreen("death_overlay"))
                            ds->setVisible(true);
                        LOG_INFO("Client", "Applied pending death state on connect");
                    }

                    // Replay skill/quest syncs that arrived before player existed
                    if (hasPendingSkillSync_) {
                        hasPendingSkillSync_ = false;
                        if (netClient_.onSkillSync)
                            netClient_.onSkillSync(pendingSkillSync_);
                        LOG_INFO("Client", "Applied pending skill sync on connect");
                    }
                    if (hasPendingSkillDefs_) {
                        hasPendingSkillDefs_ = false;
                        applySkillDefs(pendingSkillDefs_);
                        LOG_INFO("Client", "Applied pending skill defs on connect");
                    }
                    if (hasPendingQuestSync_) {
                        hasPendingQuestSync_ = false;
                        if (netClient_.onQuestSync)
                            netClient_.onQuestSync(pendingQuestSync_);
                        LOG_INFO("Client", "Applied pending quest sync on connect");
                    }
                    for (auto& qm : pendingQuestUpdates_) {
                        if (netClient_.onQuestUpdate)
                            netClient_.onQuestUpdate(qm);
                    }
                    if (!pendingQuestUpdates_.empty()) {
                        LOG_INFO("Client", "Applied %d pending quest updates on connect",
                                 (int)pendingQuestUpdates_.size());
                        pendingQuestUpdates_.clear();
                    }
                }
                // Load retained-mode UI screens (once, on first InGame frame)
                if (!retainedUILoaded_) {
                    retainedUILoaded_ = true;
                    auto& ui = uiManager();
                    // Only load screens that haven't been loaded yet (hot-reload
                    // sets retainedUILoaded_=false to re-resolve pointers, but the
                    // screen itself is already loaded — reloading it would trigger
                    // the reload listener again, creating an infinite loop)
                    if (!ui.getScreen("death_overlay"))       ui.loadScreen("assets/ui/screens/death_overlay.json");
                    if (!ui.getScreen("fate_hud"))            ui.loadScreen("assets/ui/screens/fate_hud.json");
                    if (!ui.getScreen("fate_menu_panels"))    ui.loadScreen("assets/ui/screens/fate_menu_panels.json");
                    if (!ui.getScreen("character_select"))    ui.loadScreen("assets/ui/screens/character_select.json");
                    if (!ui.getScreen("character_creation"))  ui.loadScreen("assets/ui/screens/character_creation.json");
                    if (!ui.getScreen("fate_social"))         ui.loadScreen("assets/ui/screens/fate_social.json");

                    // Screens pre-loaded by editor start hidden — make InGame roots visible
                    if (auto* hud = ui.getScreen("fate_hud")) hud->setVisible(true);
                    if (auto* social = ui.getScreen("fate_social")) social->setVisible(true);

                    // Data binding provider — resolves {player.*} and {death.*} paths
                    ui.dataBinding().setProvider([this](const std::string& path) -> std::string {
                        auto* scene = SceneManager::instance().currentScene();
                        if (!scene) return "";

                        // Find local player entity
                        Entity* localPlayer = nullptr;
                        scene->world().forEach<PlayerController>(
                            [&](Entity* e, PlayerController* ctrl) {
                                if (ctrl->isLocalPlayer) localPlayer = e;
                            }
                        );
                        if (!localPlayer) return "";

                        auto* cs = localPlayer->getComponent<CharacterStatsComponent>();
                        auto* inv = localPlayer->getComponent<InventoryComponent>();

                        if (path == "player.hp")        return cs ? std::to_string(cs->stats.currentHP) : "0";
                        if (path == "player.maxHp")     return cs ? std::to_string(cs->stats.maxHP) : "0";
                        if (path == "player.mp")        return cs ? std::to_string(cs->stats.currentMP) : "0";
                        if (path == "player.maxMp")     return cs ? std::to_string(cs->stats.maxMP) : "0";
                        if (path == "player.xp")        return cs ? std::to_string(cs->stats.currentXP) : "0";
                        if (path == "player.xpToLevel") return cs ? std::to_string(cs->stats.xpToNextLevel) : "100";
                        if (path == "player.gold") {
                            if (!inv) return std::string("0");
                            int64_t g = inv->inventory.getGold();
                            // Format with K/M suffixes for large values
                            if (g >= 1000000) return std::to_string(g / 1000000) + "M";
                            if (g >= 1000)    return std::to_string(g / 1000) + "K";
                            return std::to_string(g);
                        }
                        if (path == "player.level")     return cs ? std::to_string(cs->stats.level) : "1";
                        if (path == "player.class")     return cs ? cs->stats.className : "";
                        if (path == "player.str")       return cs ? std::to_string(cs->stats.getStrength()) : "0";
                        if (path == "player.dex")       return cs ? std::to_string(cs->stats.getDexterity()) : "0";
                        if (path == "player.int")       return cs ? std::to_string(cs->stats.getIntelligence()) : "0";
                        if (path == "player.vit")       return cs ? std::to_string(cs->stats.getVitality()) : "0";
                        if (path == "death.xpLoss")     return std::to_string(deathOverlay_ ? deathOverlay_->xpLost : 0);
                        if (path == "death.honorLoss")  return std::to_string(deathOverlay_ ? deathOverlay_->honorLost : 0);
                        if (path == "death.countdown")  return std::to_string(static_cast<int>(deathOverlay_ ? deathOverlay_->countdown : 0.0f));
                        return "";
                    });

                    // Resolve retained-mode widget pointers
                    deathOverlay_ = dynamic_cast<DeathOverlay*>(ui.getScreen("death_overlay"));
                    if (auto* socialScreen = ui.getScreen("fate_social"))
                        chatPanel_ = dynamic_cast<ChatPanel*>(socialScreen->findById("chat_panel"));
                    if (auto* hudScreen = ui.getScreen("fate_hud")) {
                        dungeonInviteDialog_ = dynamic_cast<ConfirmDialog*>(hudScreen->findById("dungeon_invite_dialog"));
                        destroyItemDialog_ = dynamic_cast<ConfirmDialog*>(hudScreen->findById("destroy_item_dialog"));
                        if (destroyItemDialog_) {
                            destroyItemDialog_->onConfirm = [this](const std::string&) {
                                if (destroyItemSlot_ >= 0 && !destroyItemId_.empty()) {
                                    netClient_.sendDestroyItem(destroyItemSlot_, destroyItemId_);
                                    destroyItemSlot_ = -1;
                                    destroyItemId_.clear();
                                }
                                destroyItemDialog_->setVisible(false);
                                if (inventoryPanel_) inventoryPanel_->setEnabled(true);
                            };
                            destroyItemDialog_->onCancel = [this](const std::string&) {
                                destroyItemSlot_ = -1;
                                destroyItemId_.clear();
                                destroyItemDialog_->setVisible(false);
                                if (inventoryPanel_) inventoryPanel_->setEnabled(true);
                            };
                        }
                        // Wire PlayerContextMenu
                        playerContextMenu_ = dynamic_cast<PlayerContextMenu*>(hudScreen->findById("player_context_menu"));
                        if (playerContextMenu_) {
                            playerContextMenu_->onTrade = [this](const std::string& charId) {
                                playerContextMenu_->hide();
                                netClient_.sendTradeAction(TradeAction::Initiate, charId);
                            };
                            playerContextMenu_->onPartyInvite = [this](const std::string& charId) {
                                playerContextMenu_->hide();
                                // Party invite not yet implemented on server — log intent
                                if (chatPanel_) {
                                    chatPanel_->addMessage(6, "[System]", "Party invite sent to " + charId, 0);
                                }
                            };
                            playerContextMenu_->onWhisper = [this](const std::string& charId) {
                                playerContextMenu_->hide();
                                if (chatPanel_) {
                                    chatPanel_->addMessage(6, "[System]", "Whisper target set to " + charId, 0);
                                    // Switch chat to whisper channel with target pre-filled
                                    chatPanel_->setFullPanelMode(true);
                                    uiManager().setFocus(chatPanel_);
                                    Input::instance().setChatMode(true);
                                }
                            };
                        }
                    }

                    // Wire ChatPanel send callback
                    if (chatPanel_) {
                        chatPanel_->onSendMessage = [this](uint8_t channel, const std::string& message, const std::string& targetName) {
                            auto filtered = ProfanityFilter::filterChatMessage(message, FilterMode::Censor);
                            netClient_.sendChat(channel, filtered.filteredText, targetName);
                            audioManager_.playSFX("chat_send");
                        };
                        chatPanel_->onClose = [this](const std::string&) {
                            if (chatPanel_) chatPanel_->setFullPanelMode(false);
                            uiManager().clearFocus();
                            Input::instance().setChatMode(false);
                        };
                        // Replay chat messages that arrived before UI was ready
                        for (auto& cm : pendingChatMessages_)
                            chatPanel_->addMessage(cm.channel, cm.sender, cm.text, cm.faction);
                        if (!pendingChatMessages_.empty()) {
                            LOG_INFO("Client", "Applied %d pending chat messages on connect",
                                     (int)pendingChatMessages_.size());
                            pendingChatMessages_.clear();
                        }
                    }

                    // Wire DeathOverlay respawn callback + button click handlers
                    if (deathOverlay_) {
                        deathOverlay_->onRespawnRequested = [this](uint8_t respawnType) {
                            if (netClient_.isConnected()) {
                                netClient_.sendRespawn(respawnType);
                            }
                        };
                    }
                    auto* deathScreen = ui.getScreen("death_overlay");
                    if (deathScreen) {
                        auto* btnTown = dynamic_cast<Button*>(deathScreen->findById("btn_respawn_town"));
                        if (btnTown) {
                            btnTown->onClick = [this](const std::string&) {
                                netClient_.sendRespawn(0); // town
                            };
                        }
                        auto* btnSpawn = dynamic_cast<Button*>(deathScreen->findById("btn_respawn_spawn"));
                        if (btnSpawn) {
                            btnSpawn->onClick = [this](const std::string&) {
                                netClient_.sendRespawn(1); // spawn point
                            };
                        }
                        auto* btnPhoenix = dynamic_cast<Button*>(deathScreen->findById("btn_respawn_phoenix"));
                        if (btnPhoenix) {
                            btnPhoenix->onClick = [this](const std::string&) {
                                netClient_.sendRespawn(2); // phoenix down
                            };
                        }
                    }

                    // Legacy inventory/skill_bar screen wiring removed —
                    // superseded by fate_menu_panels InventoryPanel and fate_hud SkillArc.

                    // Wire Fate HUD widgets
                    auto* hudScreen = ui.getScreen("fate_hud");
                    if (hudScreen) {
                        // FateStatusBar — wire menu + chat callbacks
                        auto* statusBar = dynamic_cast<FateStatusBar*>(hudScreen->findById("status_bar"));
                        if (statusBar) {
                            statusBar->onChatButtonPressed = [this](const std::string&) {
                                if (!chatPanel_) return;
                                bool opening = !chatPanel_->isFullPanelMode();
                                chatPanel_->setFullPanelMode(opening);
                                if (opening)
                                    uiManager().setFocus(chatPanel_);
                                else
                                    uiManager().clearFocus();
                                Input::instance().setChatMode(opening);
                            };
                            statusBar->onMenuItemSelected = [this](const std::string& item) {
                                auto* menuPanels = uiManager().getScreen("fate_menu_panels");
                                if (!menuPanels) return;
                                auto* tabBar = dynamic_cast<MenuTabBar*>(menuPanels->findById("tab_bar"));
                                int tab = -1;
                                if (item == "Status")                       tab = 0;
                                else if (item == "Inventory")               tab = 1;
                                else if (item == "Skills" || item == "Skill") tab = 2;
                                else if (item == "Settings")                tab = 5;
                                if (tab >= 0) {
                                    bool wasVisible = menuPanels->visible();
                                    menuPanels->setVisible(true);
                                    if (tabBar) tabBar->setActiveTab(tab);
                                    // Hide skill arc / dpad / menu+chat buttons when menu opens
                                    if (!wasVisible) {
                                        auto* hudScr = uiManager().getScreen("fate_hud");
                                        if (hudScr) {
                                            auto* dpad = hudScr->findById("dpad");
                                            auto* arc  = hudScr->findById("skill_arc");
                                            if (dpad) dpad->setVisible(false);
                                            if (arc)  arc->setVisible(false);
                                            auto* sb = hudScr->findById("status_bar");
                                            if (sb) {
                                                auto* fsb = dynamic_cast<FateStatusBar*>(sb);
                                                if (fsb) {
                                                    fsb->showMenuButton = false;
                                                    fsb->showChatButton = false;
                                                }
                                            }
                                        }
                                    }
                                }
                            };
                        }

                        // DPad — wire direction changes into ActionMap movement
                        auto* dpad = dynamic_cast<DPad*>(hudScreen->findById("dpad"));
                        if (dpad) {
                            dpad->onDirectionChange = [this](const std::string&) {
                                auto* dp = dynamic_cast<DPad*>(uiManager().getScreen("fate_hud")->findById("dpad"));
                                if (!dp) return;
                                auto& am = Input::instance().actionMap();
                                am.setActionHeld(ActionId::MoveUp,    dp->activeDirection == Direction::Up);
                                am.setActionHeld(ActionId::MoveDown,  dp->activeDirection == Direction::Down);
                                am.setActionHeld(ActionId::MoveLeft,  dp->activeDirection == Direction::Left);
                                am.setActionHeld(ActionId::MoveRight, dp->activeDirection == Direction::Right);
                            };
                        }

                        // SkillArc — resolve pointer and wire callbacks
                        skillArc_ = dynamic_cast<SkillArc*>(hudScreen->findById("skill_arc"));
                        if (skillArc_) {
                            skillArc_->onAttack = [this](const std::string&) {
                                // Inject attack into both ActionMap and InputBuffer
                                // (combat system reads from consumeBuffered, not isPressed)
                                Input::instance().injectAction(ActionId::Attack);
                            };
                            skillArc_->onSkillSlot = [this](int slotIndex) {
                                // Map arc slot index to global skill bar slot using current page
                                int page = skillArc_ ? skillArc_->currentPage : 0;
                                int globalSlot = page * SkillArc::SLOTS_PER_PAGE + slotIndex;
                                LOG_INFO("GameApp", "SkillArc slot %d pressed (page %d, global %d)", slotIndex, page, globalSlot);
                                // Look up skill in the player's SkillManager
                                auto* sc = SceneManager::instance().currentScene();
                                if (!sc) return;
                                sc->world().forEach<SkillManagerComponent, PlayerController>(
                                    [&](Entity*, SkillManagerComponent* smc, PlayerController* ctrl) {
                                        if (!ctrl->isLocalPlayer) return;
                                        std::string skillId = smc->skills.getSkillInSlot(globalSlot);
                                        LOG_INFO("GameApp", "  slot %d -> skillId='%s'", globalSlot, skillId.c_str());
                                        if (skillId.empty()) return;
                                        const LearnedSkill* ls = smc->skills.getLearnedSkill(skillId);
                                        int rank = ls ? ls->effectiveRank() : 1;
                                        LOG_INFO("GameApp", "  rank=%d (learned=%s)", rank, ls ? "yes" : "no");
                                        if (rank > 0 && skillArc_ && skillArc_->onSkillActivated) {
                                            skillArc_->onSkillActivated(skillId, rank);
                                        }
                                    }
                                );
                            };
                            // Wire skill activation to network (moved from onInit SkillBarUI callback)
                            skillArc_->onSkillActivated = [this](const std::string& skillId, int rank) {
                                if (!netClient_.isConnected()) return;

                                uint64_t targetPid = 0;
                                if (combatSystem_ && combatSystem_->hasTarget()) {
                                    EntityId targetEid = combatSystem_->getTargetEntityId();
                                    for (const auto& [pid, handle] : ghostEntities_) {
                                        Entity* ghost = nullptr;
                                        auto* scene = SceneManager::instance().currentScene();
                                        if (scene) ghost = scene->world().getEntity(handle);
                                        if (ghost && ghost->id() == targetEid) {
                                            targetPid = pid;
                                            break;
                                        }
                                    }
                                }

                                if (targetPid != 0 && combatSystem_) {
                                    combatSystem_->triggerAttackWindup();
                                    combatPredictions_.addPrediction(targetPid, netTime_);
                                }

                                netClient_.sendUseSkill(skillId, static_cast<uint8_t>(rank), targetPid);
                            };
                            skillArc_->onPickUp = [this](const std::string&) {
                                // Find nearest dropped item ghost and send pickup
                                auto* sc = SceneManager::instance().currentScene();
                                if (!sc) return;
                                Entity* localPlayer = nullptr;
                                sc->world().forEach<PlayerController>(
                                    [&](Entity* e, PlayerController* ctrl) {
                                        if (ctrl->isLocalPlayer) localPlayer = e;
                                    }
                                );
                                if (!localPlayer) return;
                                auto* playerT = localPlayer->getComponent<Transform>();
                                if (!playerT) return;

                                constexpr float kPickupRange = 48.0f;
                                float bestDist = kPickupRange + 1.0f;
                                uint64_t bestPid = 0;
                                for (uint64_t pid : droppedItemPids_) {
                                    auto it = ghostEntities_.find(pid);
                                    if (it == ghostEntities_.end()) continue;
                                    Entity* ghost = sc->world().getEntity(it->second);
                                    if (!ghost) continue;
                                    auto* t = ghost->getComponent<Transform>();
                                    if (!t) continue;
                                    float dx = t->position.x - playerT->position.x;
                                    float dy = t->position.y - playerT->position.y;
                                    float dist = std::sqrt(dx * dx + dy * dy);
                                    if (dist < bestDist) {
                                        bestDist = dist;
                                        bestPid = pid;
                                    }
                                }
                                if (bestPid != 0) {
                                    netClient_.sendAction(3, bestPid, 0);
                                    // Don't destroy the drop locally — wait for the server
                                    // to confirm via SvEntityExit. If inventory is full the
                                    // server keeps the drop alive and it stays visible.
                                    droppedItemPids_.erase(bestPid);
                                    ghostInterpolation_.removeEntity(bestPid);
                                }
                            };
                        }
                    }

                    // Wire fate_menu_panels — resolve inventoryPanel_ + tab bar navigation
                    auto* menuScreen = ui.getScreen("fate_menu_panels");
                    if (menuScreen) {
                        inventoryPanel_ = dynamic_cast<InventoryPanel*>(menuScreen->findById("inventory_panel"));

                        // Panel IDs indexed by tab (must match tabLabels order)
                        static const char* panelIds[] = {
                            "status_panel", "inventory_panel", "skill_panel",
                            "guild_panel", "social_panel", "settings_panel", "shop_panel",
                            "pet_panel", "crafting_panel", "collection_panel", "costume_panel"
                        };
                        static constexpr int panelCount = 11;

                        // Wire tab bar — shows/hides panels by index
                        auto* tabBar = dynamic_cast<MenuTabBar*>(menuScreen->findById("tab_bar"));
                        if (tabBar) {
                            tabBar->onTabChanged = [menuScreen](int tab) {
                                for (int i = 0; i < panelCount; ++i) {
                                    auto* panel = menuScreen->findById(panelIds[i]);
                                    if (panel) panel->setVisible(i == tab);
                                }
                            };
                            // Fire once to show the default active tab
                            if (tabBar->onTabChanged) tabBar->onTabChanged(tabBar->activeTab);
                        }

                        // Wire close buttons on each panel to hide the root
                        // Close handler: hide menu + restore game HUD controls
                        auto closeMenu = [this](const std::string&) {
                            auto* ms = uiManager().getScreen("fate_menu_panels");
                            if (ms) ms->setVisible(false);
                            auto* hudScr = uiManager().getScreen("fate_hud");
                            if (hudScr) {
                                auto* dpad = hudScr->findById("dpad");
                                auto* arc  = hudScr->findById("skill_arc");
                                if (dpad) dpad->setVisible(true);
                                if (arc)  arc->setVisible(true);
                            }
                        };

                        auto* invPanel = dynamic_cast<InventoryPanel*>(menuScreen->findById("inventory_panel"));
                        if (invPanel) invPanel->onClose = closeMenu;
                        auto* statusPanel = dynamic_cast<StatusPanel*>(menuScreen->findById("status_panel"));
                        if (statusPanel) {
                            statusPanel->onClose = closeMenu;
                            // DISABLED: stat allocation removed — stats are fixed per class
                            // statusPanel->onAllocateStat = [this](uint8_t statType) {
                            //     netClient_.sendAllocateStat(statType, 1);
                            // };
                        }
                        auto* skillPanel = dynamic_cast<SkillPanel*>(menuScreen->findById("skill_panel"));
                        if (skillPanel) {
                            skillPanel->onClose = closeMenu;
                            skillPanel->onLevelUp = [this](int skillIndex) {
                                auto* ms = uiManager().getScreen("fate_menu_panels");
                                auto* sp = ms ? dynamic_cast<SkillPanel*>(ms->findById("skill_panel")) : nullptr;
                                if (!sp || skillIndex < 0 || skillIndex >= static_cast<int>(sp->classSkills.size())) return;
                                const auto& info = sp->classSkills[static_cast<size_t>(skillIndex)];
                                if (info.skillId.empty()) return;
                                if (info.currentLevel >= info.unlockedLevel) return;
                                netClient_.sendActivateSkillRank(info.skillId);
                            };
                            skillPanel->onAssignSkill = [this](const std::string& skillId, int globalSlot) {
                                if (globalSlot < 0 || globalSlot >= 20) return;
                                if (skillId.empty()) {
                                    // action=1 = clear slot
                                    netClient_.sendAssignSkillSlot(1, "",
                                        static_cast<uint8_t>(globalSlot));
                                } else {
                                    // action=0 = assign skill to slot
                                    netClient_.sendAssignSkillSlot(0, skillId,
                                        static_cast<uint8_t>(globalSlot));
                                }
                            };
                        }

                        petPanel_ = dynamic_cast<PetPanel*>(menuScreen->findById("pet_panel"));
                        if (petPanel_) {
                            petPanel_->onEquipPet = [this](int32_t petDbId) {
                                netClient_.sendPetCommand(0, petDbId);  // 0 = Equip
                            };
                            petPanel_->onUnequipPet = [this]() {
                                netClient_.sendPetCommand(1, 0);  // 1 = Unequip
                            };
                            petPanel_->onClose = closeMenu;
                        }

                        craftingPanel_ = dynamic_cast<CraftingPanel*>(menuScreen->findById("crafting_panel"));
                        if (craftingPanel_) {
                            craftingPanel_->onCraft = [this](const std::string& recipeId) {
                                netClient_.sendCraft(recipeId);
                            };
                            craftingPanel_->onClose = closeMenu;
                        }

                        collectionPanel_ = dynamic_cast<CollectionPanel*>(menuScreen->findById("collection_panel"));
                        if (collectionPanel_) {
                            collectionPanel_->onClose = [this](const std::string&) {
                                if (collectionPanel_) collectionPanel_->close();
                            };
                        }

                        costumePanel_ = dynamic_cast<CostumePanel*>(menuScreen->findById("costume_panel"));
                        if (costumePanel_) {
                            costumePanel_->onEquipCostume = [this](const std::string& costumeDefId) {
                                netClient_.sendEquipCostume(costumeDefId);
                            };
                            costumePanel_->onUnequipCostume = [this](uint8_t slotType) {
                                netClient_.sendUnequipCostume(slotType);
                            };
                            costumePanel_->onToggleCostumes = [this](bool show) {
                                netClient_.sendToggleCostumes(show);
                            };
                            costumePanel_->onClose = [this](const std::string&) {
                                // no-op — panel hides itself
                            };
                        }

                        // SettingsPanel — wire logout
                        auto* settingsPanel = dynamic_cast<SettingsPanel*>(menuScreen->findById("settings_panel"));
                        if (settingsPanel) {
                            settingsPanel->onLogout = [this]() {
                                LOG_INFO("GameApp", "Logout requested via Settings panel");
                                if (loadingPanel_) loadingPanel_->show("Logging out...");
                                logoutTimer_ = 1.5f;
                            };
                        }

                        // Wire inventory drag-and-drop callbacks
                        if (invPanel) {
                            invPanel->onStatEnchantRequest = [this](uint8_t targetSlot, const std::string& scrollItemId) {
                                netClient_.sendStatEnchant(targetSlot, scrollItemId);
                            };
                            invPanel->onMoveItemRequest = [this](int32_t sourceSlot, int32_t destSlot) {
                                netClient_.sendMoveItem(sourceSlot, destSlot);
                            };
                            invPanel->onEquipRequest = [this](int32_t inventorySlot, uint8_t equipSlot) {
                                netClient_.sendEquip(0, inventorySlot, equipSlot);
                            };
                            invPanel->onUnequipRequest = [this](uint8_t equipSlot) {
                                netClient_.sendEquip(1, -1, equipSlot);
                            };
                            invPanel->onDestroyItemRequest = [this](int32_t slot, const std::string& itemId, const std::string& displayName) {
                                if (destroyItemDialog_) {
                                    destroyItemSlot_ = slot;
                                    destroyItemId_ = itemId;
                                    destroyItemDialog_->message = "Destroy " + displayName + "?";
                                    destroyItemDialog_->setVisible(true);
                                    if (inventoryPanel_) inventoryPanel_->setEnabled(false);
                                }
                            };
                            invPanel->onUseItemRequest = [this](int32_t slot) {
                                if (slot >= 0 && slot < 256)
                                    netClient_.sendUseConsumable(static_cast<uint8_t>(slot));
                            };
                            invPanel->onEnchantRequest = [this](uint8_t slot, bool useProt) {
                                netClient_.sendEnchant(slot, useProt ? 1 : 0);
                            };
                            invPanel->onRepairRequest = [this](uint8_t slot) {
                                netClient_.sendRepair(slot);
                            };
                            invPanel->onExtractCoreRequest = [this](uint8_t itemSlot, uint8_t scrollSlot) {
                                netClient_.sendExtractCore(itemSlot, scrollSlot);
                            };
                            invPanel->onSocketRequest = [this](uint8_t equipSlot, const std::string& scrollId) {
                                netClient_.sendSocketItem(equipSlot, scrollId);
                            };
                        }
                    }

                    // Wire character select screen
                    // TODO (Gap #9): CharacterSelectScreen.slots are never populated.
                    // The current auth flow (AuthResponse) returns a single character and
                    // goes straight to InGame. When the auth protocol supports multi-character
                    // selection (SvCharacterList with a vector of characters), populate
                    // charSelect->slots here from the response before showing the screen.
                    auto* charSelect = dynamic_cast<CharacterSelectScreen*>(
                        ui.getScreen("character_select"));
                    if (charSelect) {
                        charSelect->onEntry = [this](const std::string&) {
                            // Enter game with selected character
                            auto* selectScreen = uiManager().getScreen("character_select");
                            if (selectScreen) selectScreen->setVisible(false);
                        };
                        charSelect->onCreateNew = [this](const std::string&) {
                            // Show character creation screen
                            auto* selectScreen = uiManager().getScreen("character_select");
                            auto* createScreen = uiManager().getScreen("character_creation");
                            if (selectScreen) selectScreen->setVisible(false);
                            if (createScreen) createScreen->setVisible(true);
                        };
                    }

                    // Wire character creation screen
                    auto* charCreate = dynamic_cast<CharacterCreationScreen*>(
                        ui.getScreen("character_creation"));
                    if (charCreate) {
                        charCreate->onBack = [this](const std::string&) {
                            auto* selectScreen = uiManager().getScreen("character_select");
                            auto* createScreen = uiManager().getScreen("character_creation");
                            if (createScreen) createScreen->setVisible(false);
                            if (selectScreen) selectScreen->setVisible(true);
                        };
                        charCreate->onNext = [this](const std::string&) {
                            // Submit character creation using existing registration logic
                            auto* cc = dynamic_cast<CharacterCreationScreen*>(
                                uiManager().getScreen("character_creation"));
                            if (!cc) return;
                            constexpr const char* classNames[] = {"Warrior", "Mage", "Archer"};
                            constexpr Faction factions[] = {Faction::Xyros, Faction::Fenor, Faction::Zethos, Faction::Solis};
                            pendingFaction_ = factions[cc->selectedFaction];
                            pendingClassName_ = classNames[cc->selectedClass];
                            pendingCharName_ = cc->characterName;
                            LOG_INFO("GameApp", "Character creation submitted: '%s' class=%s faction=%d",
                                     cc->characterName.c_str(), classNames[cc->selectedClass], cc->selectedFaction);
                        };
                    }

                    // Wire social/economy screen
                    auto* socialScreen = ui.getScreen("fate_social");
                    if (socialScreen) {
                        // ChatPanel send/close callbacks wired above via chatPanel_ pointer

                        // Wire TradeWindow callbacks
                        auto* tradeWin = dynamic_cast<TradeWindow*>(socialScreen->findById("trade_window"));
                        if (tradeWin) {
                            tradeWin->onCancel = [tradeWin](const std::string&) {
                                tradeWin->setVisible(false);
                            };
                            tradeWin->onLock = [tradeWin](const std::string&) {
                                tradeWin->myLocked = true;
                            };
                            tradeWin->onAccept = [this](const std::string&) {
                                // Confirm trade via TradeComponent on local player
                                auto* scene = SceneManager::instance().currentScene();
                                if (!scene) return;
                                scene->world().forEach<PlayerController, TradeComponent>(
                                    [](Entity*, PlayerController* ctrl, TradeComponent* tc) {
                                        if (!ctrl->isLocalPlayer) return;
                                        tc->trade.confirm();
                                    }
                                );
                            };
                        }

                        // Wire GuildPanel close button
                        auto* guildPanel = dynamic_cast<GuildPanel*>(socialScreen->findById("guild_panel"));
                        if (guildPanel) {
                            guildPanel->onClose = [guildPanel](const std::string&) {
                                guildPanel->setVisible(false);
                            };
                        }
                    }

                    // Load and wire NPC panels screen
                    if (!ui.getScreen("npc_panels")) ui.loadScreen("assets/ui/screens/npc_panels.json");
                    auto* npcScreen = ui.getScreen("npc_panels");
                    if (npcScreen) {
                        npcScreen->setVisible(true);
                        npcDialoguePanel_ = dynamic_cast<NpcDialoguePanel*>(npcScreen->findById("npc_dialogue_panel"));
                        shopPanel_ = dynamic_cast<ShopPanel*>(npcScreen->findById("shop_panel"));
                        bankPanel_ = dynamic_cast<BankPanel*>(npcScreen->findById("bank_panel"));
                        teleporterPanel_ = dynamic_cast<TeleporterPanel*>(npcScreen->findById("teleporter_panel"));
                        arenaPanel_ = dynamic_cast<ArenaPanel*>(npcScreen->findById("arena_panel"));
                        battlefieldPanel_ = dynamic_cast<BattlefieldPanel*>(npcScreen->findById("battlefield_panel"));
                        leaderboardPanel_ = dynamic_cast<LeaderboardPanel*>(npcScreen->findById("leaderboard_panel"));
                    }

                    // Wire NpcDialoguePanel callbacks
                    if (npcDialoguePanel_) {
                        npcDialoguePanel_->onOpenShop = [this](uint32_t nId) {
                            npcDialoguePanel_->setVisible(false);
                            auto* npc = findNpcById(nId);
                            if (npc && shopPanel_) {
                                auto* shopComp = npc->getComponent<ShopComponent>();
                                if (shopComp) shopPanel_->open(nId, shopComp->shopName, shopComp->inventory);
                            }
                        };
                        npcDialoguePanel_->onOpenBank = [this](uint32_t nId) {
                            npcDialoguePanel_->setVisible(false);
                            if (bankPanel_) bankPanel_->open(nId);
                        };
                        npcDialoguePanel_->onOpenTeleporter = [this](uint32_t nId) {
                            npcDialoguePanel_->setVisible(false);
                            auto* npc = findNpcById(nId);
                            if (npc && teleporterPanel_) {
                                auto* telComp = npc->getComponent<TeleporterComponent>();
                                if (telComp) {
                                    // Get player gold and level
                                    int64_t gold = 0;
                                    uint16_t level = 1;
                                    auto* scene = SceneManager::instance().currentScene();
                                    if (scene) {
                                        scene->world().forEach<PlayerController>(
                                            [&](Entity* e, PlayerController* ctrl) {
                                                if (!ctrl->isLocalPlayer) return;
                                                auto* inv = e->getComponent<InventoryComponent>();
                                                auto* stats = e->getComponent<CharacterStatsComponent>();
                                                if (inv) gold = inv->inventory.getGold();
                                                if (stats) level = stats->stats.level;
                                            }
                                        );
                                    }
                                    teleporterPanel_->open(nId, telComp->destinations, gold, level);
                                }
                            }
                        };
                        npcDialoguePanel_->onOpenGuildCreation = [this](uint32_t nId) {
                            npcDialoguePanel_->setVisible(false);
                            auto* npc = findNpcById(nId);
                            if (!npc) return;
                            auto* guildNPC = npc->getComponent<GuildNPCComponent>();
                            auto* scene = SceneManager::instance().currentScene();
                            if (!guildNPC || !scene) return;
                            scene->world().forEach<PlayerController>(
                                [&](Entity* e, PlayerController* ctrl) {
                                    if (!ctrl->isLocalPlayer) return;
                                    auto* guildComp = e->getComponent<GuildComponent>();
                                    auto* statsComp = e->getComponent<CharacterStatsComponent>();
                                    auto* invComp = e->getComponent<InventoryComponent>();
                                    if (!guildComp || !statsComp) return;
                                    if (statsComp->stats.level < guildNPC->requiredLevel) {
                                        if (chatPanel_) chatPanel_->addMessage(6, "[Guild]", "You don't meet the level requirement.", static_cast<uint8_t>(0));
                                    } else {
                                        int64_t gold = invComp ? invComp->inventory.getGold() : 0;
                                        if (guildComp->guild.createGuild(statsComp->stats.characterName + "'s Guild", gold)) {
                                            if (invComp) {
                                                int64_t spent = invComp->inventory.getGold() - gold;
                                                if (spent > 0) invComp->inventory.removeGold(spent);
                                            }
                                            if (chatPanel_) chatPanel_->addMessage(6, "[Guild]", "Guild created!", static_cast<uint8_t>(0));
                                        }
                                    }
                                }
                            );
                        };
                        npcDialoguePanel_->onOpenDungeon = [this](uint32_t nId) {
                            npcDialoguePanel_->setVisible(false);
                            auto* npc = findNpcById(nId);
                            if (npc) {
                                auto* dungeonComp = npc->getComponent<DungeonNPCComponent>();
                                if (dungeonComp && netClient_.isConnected()) {
                                    netClient_.sendStartDungeon(dungeonComp->dungeonSceneId);
                                }
                            }
                        };
                        npcDialoguePanel_->onOpenArena = [this](uint32_t nId) {
                            npcDialoguePanel_->setVisible(false);
                            if (arenaPanel_) arenaPanel_->open(nId);
                        };
                        npcDialoguePanel_->onOpenBattlefield = [this](uint32_t nId) {
                            npcDialoguePanel_->setVisible(false);
                            if (battlefieldPanel_) battlefieldPanel_->open(nId);
                        };
                        npcDialoguePanel_->onOpenMarketplace = [this](uint32_t npcId) {
                            // Marketplace panel opening will be wired when marketplace UI is built
                            LOG_INFO("Game", "Marketplace NPC interaction - marketplace UI not yet implemented");
                        };
                        npcDialoguePanel_->onOpenLeaderboard = [this](uint32_t npcId) {
                            npcDialoguePanel_->setVisible(false);
                            if (leaderboardPanel_) leaderboardPanel_->open();
                        };
                        npcDialoguePanel_->onClose = [this](const std::string&) {
                            closeAllNpcPanels();
                        };
                    }

                    // Wire sub-panel callbacks
                    auto closeAll = [this](const std::string&) { closeAllNpcPanels(); };

                    if (shopPanel_) {
                        shopPanel_->onBuy = [this](uint32_t nId, const std::string& itemId, uint16_t qty) {
                            netClient_.sendShopBuy(nId, itemId, qty);
                        };
                        shopPanel_->onSell = [this](uint32_t nId, uint8_t slot, uint16_t qty) {
                            netClient_.sendShopSell(nId, slot, qty);
                        };
                        shopPanel_->onClose = closeAll;
                    }

                    if (bankPanel_) {
                        bankPanel_->onDepositItem = [this](uint32_t nId, uint8_t slot) {
                            netClient_.sendBankDepositItem(nId, slot);
                        };
                        bankPanel_->onWithdrawItem = [this](uint32_t nId, uint16_t idx) {
                            netClient_.sendBankWithdrawItem(nId, idx);
                        };
                        bankPanel_->onDepositGold = [this](uint32_t nId, int64_t amt) {
                            netClient_.sendBankDepositGold(nId, amt);
                        };
                        bankPanel_->onWithdrawGold = [this](uint32_t nId, int64_t amt) {
                            netClient_.sendBankWithdrawGold(nId, amt);
                        };
                        bankPanel_->onClose = closeAll;
                    }

                    if (teleporterPanel_) {
                        teleporterPanel_->onTeleport = [this](uint32_t nId, uint8_t idx) {
                            netClient_.sendTeleport(nId, idx);
                        };
                        teleporterPanel_->onClose = closeAll;
                    }

                    if (arenaPanel_) {
                        arenaPanel_->onRegister = [this](uint32_t nId, uint8_t mode) {
                            netClient_.sendArena(0, mode);
                        };
                        arenaPanel_->onUnregister = [this](uint32_t nId) {
                            netClient_.sendArena(1, 0);
                        };
                        arenaPanel_->onClose = [this](const std::string&) { closeAllNpcPanels(); };
                    }

                    if (battlefieldPanel_) {
                        battlefieldPanel_->onRegister = [this](uint32_t nId) {
                            netClient_.sendBattlefield(0);  // 0 = Register
                        };
                        battlefieldPanel_->onUnregister = [this](uint32_t nId) {
                            netClient_.sendBattlefield(1);  // 1 = Unregister
                        };
                        battlefieldPanel_->onClose = [this](const std::string&) { closeAllNpcPanels(); };
                    }

                    if (leaderboardPanel_) {
                        leaderboardPanel_->onQueryRankings = [this](uint8_t cat, uint8_t page, uint8_t faction) {
                            CmdRankingQueryMsg msg;
                            msg.category = cat;
                            msg.page = page;
                            msg.factionFilter = faction;
                            netClient_.sendRankingQuery(msg);
                        };
                        leaderboardPanel_->onClose = [this](const std::string&) { closeAllNpcPanels(); };
                    }

                    LOG_INFO("GameApp", "Retained-mode UI screens loaded and wired");
                }

                break; // Skip rest of first frame
            }

            // Check editor pause state early so all InGame subsystems respect it
            bool editorPaused = Editor::instance().isPaused();

            // Notify server when editor pause state changes (makes player untargetable by mobs)
            if (editorPaused != lastEditorPaused_ && netClient_.isConnected()) {
                netClient_.sendEditorPause(editorPaused);
                lastEditorPaused_ = editorPaused;
            }

            // Update death overlay countdown timer
            if (deathOverlay_ && !editorPaused) deathOverlay_->update(deltaTime);

            // Update skill visual effects
            if (!editorPaused) SkillVFXPlayer::instance().update(deltaTime);

            // Network: poll for server messages and send movement
            netTime_ += deltaTime;
            // Poll during reconnect so the timeout state machine advances
            if (!netClient_.isConnected() && netClient_.isReconnecting()) {
                netClient_.poll(netTime_);
            }
            // Reconnect succeeded — dismiss the "Reconnecting..." overlay
            if (netClient_.isConnected() && !netClient_.isReconnecting() &&
                loadingPanel_ && loadingPanel_->visibleSelf()) {
                loadingPanel_->hide();
                LOG_INFO("GameApp", "Reconnect succeeded, dismissed loading panel");
            }
            if (netClient_.isConnected()) {
                // If play mode was stopped (paused but not in play mode), sever the
                // connection — otherwise server messages keep creating ghost entities
                // in the edit-mode world.
                if (editorPaused && !Editor::instance().inPlayMode()) {
                    netClient_.disconnect();
                    ghostEntities_.clear();
                    droppedItemPids_.clear();
                    ghostUpdateSeqs_.clear();
                    ghostDeathTimers_.clear();
                    localPlayerCreated_ = false;
                    retainedUILoaded_ = false;
                    connState_ = ConnectionState::LoginScreen;
                    LOG_INFO("GameApp", "Disconnected: play mode exited");
                } else if (!editorPaused) {
                    netClient_.poll(netTime_);
                    // Flush entities queued for destruction during poll (onEntityLeave, etc.)
                    if (auto* sc = SceneManager::instance().currentScene())
                        sc->world().processDestroyQueue();
                } else {
                    // Paused during play — poll for heartbeats only
                    netClient_.poll(netTime_);
                }

                // Process deferred zone transition (after poll completes safely)
                if (pendingZoneTransition_ && !editorPaused && connState_ == ConnectionState::InGame) {
                    // Snapshot skill state from the current player entity before the
                    // async loader destroys the world.  The server does NOT re-send
                    // SvSkillSync on zone transitions, so we must preserve it locally.
                    {
                        auto* sc2 = SceneManager::instance().currentScene();
                        if (sc2) {
                            sc2->world().forEach<SkillManagerComponent, PlayerController>(
                                [&](Entity*, SkillManagerComponent* smc, PlayerController* ctrl) {
                                    if (!ctrl->isLocalPlayer) return;
                                    pendingSkillSync_ = {};
                                    for (const auto& ls : smc->skills.getLearnedSkills()) {
                                        SkillSyncEntry e;
                                        e.skillId = ls.skillId;
                                        e.unlockedRank = ls.unlockedRank;
                                        e.activatedRank = ls.activatedRank;
                                        pendingSkillSync_.skills.push_back(std::move(e));
                                    }
                                    pendingSkillSync_.skillBar = smc->skills.getSkillBarSlots();
                                    pendingSkillSync_.availablePoints = static_cast<int16_t>(smc->skills.availablePoints());
                                    pendingSkillSync_.earnedPoints = static_cast<int16_t>(smc->skills.earnedPoints());
                                    pendingSkillSync_.spentPoints = static_cast<int16_t>(smc->skills.spentPoints());
                                    hasPendingSkillSync_ = true;
                                }
                            );
                        }
                    }

                    // Clear ghost state and cached entity pointers before async load
                    // destroys all entities (prevents dangling pointer dereference)
                    ghostEntities_.clear();
                    droppedItemPids_.clear();
                    ghostDeathTimers_.clear();
                    ghostInterpolation_.clear();
                    ghostUpdateSeqs_.clear();
                    combatPredictions_.clear();
                    SkillVFXPlayer::instance().clear();
                    if (npcInteractionSystem_) npcInteractionSystem_->resetCachedPointers();
                    if (combatSystem_) combatSystem_->serverClearTarget();
                    lastContextMenuTargetId_ = INVALID_ENTITY;
                    if (playerContextMenu_) playerContextMenu_->hide();

                    // Start async load for new zone
                    std::string jsonPath = "assets/scenes/" + pendingZoneScene_ + ".json";
                    asyncLoader_.startLoad(pendingZoneScene_, jsonPath);
#ifndef FATE_SHIPPING
                    Editor::instance().setCurrentScenePath(jsonPath);
#endif
                    if (loadingPanel_) loadingPanel_->show(pendingZoneScene_);
                    setIsLoading(true);
                    loadingMinTimer_ = 2.0f;
                    loadingDataReady_ = false;
                    connState_ = ConnectionState::LoadingScene;
                    // pendingZoneTransition_ stays true — entity buffering continues
                    // Entity replay + player creation happen when LoadingScene completes
                }

                // Update audio engine (fades, streaming, etc.)
                audioManager_.update(deltaTime);

                // Interpolate ghost entity positions (skip when paused)
                if (!editorPaused) {
                    auto* sc = SceneManager::instance().currentScene();
                    if (sc) {
                        for (auto& [pid, handle] : ghostEntities_) {
                            bool valid = false;
                            Vec2 pos = ghostInterpolation_.getInterpolatedPosition(pid, deltaTime, &valid);
                            // Never overwrite position with (0,0) from missing interpolation data
                            if (!valid) continue;
                            Entity* ghost = sc->world().getEntity(handle);
                            if (ghost) {
                                auto* t = ghost->getComponent<Transform>();
                                if (t) t->position = pos;
                            }
                        }
                    }
                }

                // Hide dead mob sprites after 3-second corpse delay
                if (!editorPaused && !ghostDeathTimers_.empty()) {
                    auto* sc2 = SceneManager::instance().currentScene();
                    if (sc2) {
                        constexpr float CORPSE_VISIBLE_DURATION = 3.0f;
                        for (auto it = ghostDeathTimers_.begin(); it != ghostDeathTimers_.end(); ) {
                            if (netTime_ - it->second >= CORPSE_VISIBLE_DURATION) {
                                auto git = ghostEntities_.find(it->first);
                                if (git != ghostEntities_.end()) {
                                    Entity* ghost = sc2->world().getEntity(git->second);
                                    if (ghost) {
                                        auto* spr = ghost->getComponent<SpriteComponent>();
                                        if (spr) spr->enabled = false;
                                        auto* np = ghost->getComponent<NameplateComponent>();
                                        if (np) np->visible = false;
                                    }
                                }
                                it = ghostDeathTimers_.erase(it);
                            } else {
                                ++it;
                            }
                        }
                    }
                }

                // Send movement 30 times/sec max (skip when paused, skip if position unchanged)
                if (!editorPaused && netTime_ - lastMoveSendTime_ >= 1.0f / 30.0f) {
                    auto* sc = SceneManager::instance().currentScene();
                    if (sc) {
                        sc->world().forEach<Transform, PlayerController>([&](Entity* entity, Transform* t, PlayerController* pc) {
                            if (pc->isLocalPlayer) {
                                // Only send if position actually changed (avoids spam when stuck)
                                float dx = t->position.x - lastSentPos_.x;
                                float dy = t->position.y - lastSentPos_.y;
                                if (dx * dx + dy * dy > 0.01f) {
                                    netClient_.sendMove(t->position, {0.0f, 0.0f}, netTime_);
                                    lastSentPos_ = t->position;
                                }
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

            // ---- Fate HUD per-frame data push ----
            {
                auto* hudScreen = uiManager().getScreen("fate_hud");
                auto* scene = SceneManager::instance().currentScene();
                if (hudScreen && scene) {
                    Entity* localPlayer = nullptr;
                    scene->world().forEach<PlayerController>(
                        [&](Entity* e, PlayerController* ctrl) {
                            if (ctrl->isLocalPlayer) localPlayer = e;
                        }
                    );

                    if (localPlayer) {
                        auto* cs = localPlayer->getComponent<CharacterStatsComponent>();

                        // FateStatusBar — push player stats
                        auto* statusBar = dynamic_cast<FateStatusBar*>(hudScreen->findById("status_bar"));
                        if (statusBar && cs) {
                            statusBar->hp      = static_cast<float>(cs->stats.currentHP);
                            statusBar->maxHp   = static_cast<float>(cs->stats.maxHP);
                            statusBar->mp      = static_cast<float>(cs->stats.currentMP);
                            statusBar->maxMp   = static_cast<float>(cs->stats.maxMP);
                            statusBar->level   = cs->stats.level;
                            statusBar->xp      = static_cast<float>(cs->stats.currentXP);
                            statusBar->xpToLevel = static_cast<float>(cs->stats.xpToNextLevel);
                            statusBar->playerName = localPlayer->name();
                            auto* transform = localPlayer->getComponent<Transform>();
                            if (transform) {
                                statusBar->playerTileX = static_cast<int>(std::floor(transform->position.x / 32.0f));
                                statusBar->playerTileY = static_cast<int>(std::floor(transform->position.y / 32.0f));
                            }
                        }

                        // EXPBar
                        auto* expBar = dynamic_cast<EXPBar*>(hudScreen->findById("exp_bar"));
                        if (expBar && cs) {
                            expBar->xp        = static_cast<float>(cs->stats.currentXP);
                            expBar->xpToLevel = static_cast<float>(cs->stats.xpToNextLevel);
                        }

                        // BuffBar — tick down timers between server syncs
                        auto* buffBar = dynamic_cast<BuffBar*>(hudScreen->findById("buff_bar"));
                        if (buffBar) {
                            buffBar->tickTimers(deltaTime);
                        }

                        // TargetFrame — show/hide based on combat target
                        auto* tf = dynamic_cast<TargetFrame*>(hudScreen->findById("target_frame"));
                        if (tf && combatSystem_) {
                            if (combatSystem_->hasTarget()) {
                                tf->setVisible(true);
                                tf->targetName = combatSystem_->getTargetName();
                                tf->hp    = static_cast<float>(combatSystem_->getTargetHP());
                                tf->maxHp = static_cast<float>(combatSystem_->getTargetMaxHP());
                            } else {
                                tf->setVisible(false);
                            }
                        }

                        // PlayerContextMenu — show when a new player target is selected
                        if (playerContextMenu_ && combatSystem_) {
                            EntityId targetEid = combatSystem_->getTargetEntityId();
                            if (targetEid != INVALID_ENTITY && targetEid != lastContextMenuTargetId_) {
                                // New target selected — check if it's a player (not a mob)
                                Entity* targetEnt = scene->world().getEntity(targetEid);
                                if (targetEnt
                                    && targetEnt->getComponent<CharacterStatsComponent>()
                                    && !targetEnt->getComponent<EnemyStatsComponent>()) {
                                    // It's a player ghost — show context menu
                                    auto* tgtCs = targetEnt->getComponent<CharacterStatsComponent>();
                                    auto* tgtFc = targetEnt->getComponent<FactionComponent>();
                                    auto* localFc = localPlayer->getComponent<FactionComponent>();

                                    bool sameFaction = false;
                                    if (tgtFc && localFc) {
                                        sameFaction = FactionRegistry::isSameFaction(localFc->faction, tgtFc->faction);
                                    }
                                    bool safeZone = !scene->metadata().pvpEnabled;

                                    // Convert target world position to screen for menu placement
                                    auto* tgtTransform = targetEnt->getComponent<Transform>();
                                    Vec2 screenPos = {0, 0};
                                    if (tgtTransform) {
                                        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
                                        screenPos = camera().worldToScreen(
                                            tgtTransform->position,
                                            static_cast<int>(displaySize.x),
                                            static_cast<int>(displaySize.y));
                                        // Offset slightly so menu doesn't cover the target
                                        screenPos.y -= 20.0f;
                                    }

                                    // Find persistent ID for this ghost (needed for trade target)
                                    uint64_t pid = 0;
                                    EntityHandle tgtHandle = targetEnt->handle();
                                    for (const auto& [ghostPid, handle] : ghostEntities_) {
                                        if (handle == tgtHandle) {
                                            pid = ghostPid;
                                            break;
                                        }
                                    }

                                    playerContextMenu_->show(
                                        screenPos,
                                        tgtCs->stats.characterName,
                                        tgtCs->stats.characterName, // charId = name for now
                                        pid,
                                        sameFaction,
                                        safeZone);
                                }
                                lastContextMenuTargetId_ = targetEid;
                            } else if (targetEid == INVALID_ENTITY) {
                                lastContextMenuTargetId_ = INVALID_ENTITY;
                            }
                        }

                        // SkillArc — push skill slot data from SkillManager
                        auto* smc = localPlayer->getComponent<SkillManagerComponent>();
                        if (skillArc_ && smc) {
                            int page = skillArc_->currentPage;
                            skillArc_->slots.resize(skillArc_->slotCount);
                            for (int si = 0; si < skillArc_->slotCount; ++si) {
                                int globalSlot = page * SkillArc::SLOTS_PER_PAGE + si;
                                std::string skillId = smc->skills.getSkillInSlot(globalSlot);
                                auto& slot = skillArc_->slots[si];
                                if (!skillId.empty()) {
                                    const auto* ls = smc->skills.getLearnedSkill(skillId);
                                    slot.skillId = skillId;
                                    slot.level   = ls ? ls->effectiveRank() : 0;
                                    slot.cooldownRemaining = smc->skills.getRemainingCooldown(skillId);
                                    const auto* def = smc->skills.getSkillDefinition(skillId);
                                    slot.cooldownTotal = def ? def->cooldownSeconds : 0.0f;
                                } else {
                                    slot.skillId.clear();
                                    slot.level = 0;
                                    slot.cooldownRemaining = 0.0f;
                                    slot.cooldownTotal = 0.0f;
                                }
                            }
                        }
                    }
                }
            }

            // ---- ChatPanel idle-mode time update ----
            if (chatPanel_) chatPanel_->updateTime(deltaTime);

            // ---- Menu panels per-frame data push ----
            {
                auto* menuScreen = uiManager().getScreen("fate_menu_panels");
                auto* scene2 = SceneManager::instance().currentScene();
                if (menuScreen && menuScreen->visible() && scene2) {
                    Entity* localPlayer = nullptr;
                    scene2->world().forEach<PlayerController>(
                        [&](Entity* e, PlayerController* ctrl) {
                            if (ctrl->isLocalPlayer) localPlayer = e;
                        }
                    );

                    if (localPlayer) {
                        auto* cs  = localPlayer->getComponent<CharacterStatsComponent>();
                        auto* inv = localPlayer->getComponent<InventoryComponent>();

                        // StatusPanel — push stats
                        auto* sp = dynamic_cast<StatusPanel*>(menuScreen->findById("status_panel"));
                        if (sp && cs) {
                            sp->playerName  = localPlayer->name();
                            sp->className   = cs->stats.className;
                            sp->level       = cs->stats.level;
                            sp->xp          = static_cast<float>(cs->stats.currentXP);
                            sp->xpToLevel   = static_cast<float>(cs->stats.xpToNextLevel);
                            sp->str = cs->stats.getStrength();
                            sp->intl = cs->stats.getIntelligence();
                            sp->dex = cs->stats.getDexterity();
                            sp->con = cs->stats.getVitality();
                            sp->wis = cs->stats.getWisdom();
                            sp->arm = cs->stats.getArmor();
                            sp->hit = static_cast<int>(cs->stats.getHitRate() * 100.0f);
                            sp->cri = static_cast<int>(cs->stats.getCritRate() * 100.0f);
                            sp->spd = static_cast<int>(cs->stats.getSpeed() * 100.0f);
                            // DISABLED: stat allocation removed — stats are fixed per class
                            // sp->freeStatPoints = cs->stats.freeStatPoints;
                            // sp->allocatedSTR   = cs->stats.allocatedSTR;
                            // sp->allocatedINT   = cs->stats.allocatedINT;
                            // sp->allocatedDEX   = cs->stats.allocatedDEX;
                            // sp->allocatedCON   = cs->stats.allocatedCON;
                            // sp->allocatedWIS   = cs->stats.allocatedWIS;

                            // Gap #3: factionName from FactionComponent
                            auto* fc = localPlayer->getComponent<FactionComponent>();
                            if (fc) {
                                const auto* fdef = FactionRegistry::get(fc->faction);
                                sp->factionName = fdef ? fdef->displayName : "None";
                            } else {
                                sp->factionName = "None";
                            }
                        }

                        // InventoryPanel — push gold and item data
                        auto* ip = dynamic_cast<InventoryPanel*>(menuScreen->findById("inventory_panel"));
                        if (ip && inv) {
                            ip->gold  = static_cast<int>(inv->inventory.getGold());
                            ip->armorValue = cs ? cs->stats.getArmor() : 0;
                            // Push inventory items
                            const auto& slots = inv->inventory.getSlots();
                            int slotCount = (std::min)(static_cast<int>(slots.size()), InventoryPanel::MAX_SLOTS);
                            auto statName = [](StatType t) -> const char* {
                                switch (t) {
                                    case StatType::Strength:       return "STR";
                                    case StatType::Intelligence:   return "INT";
                                    case StatType::Dexterity:      return "DEX";
                                    case StatType::Vitality:       return "VIT";
                                    case StatType::Wisdom:         return "WIS";
                                    case StatType::MaxHealth:      return "HP";
                                    case StatType::MaxMana:        return "Mana";
                                    case StatType::HealthRegen:    return "HP Regen";
                                    case StatType::ManaRegen:      return "MP Regen";
                                    case StatType::Accuracy:       return "Hit Rate";
                                    case StatType::CriticalChance: return "Crit";
                                    case StatType::CriticalDamage: return "Crit Dmg";
                                    case StatType::Armor:          return "Armor";
                                    case StatType::Evasion:        return "Evasion";
                                    case StatType::MagicResist:    return "Magic Resist";
                                    default:                       return "???";
                                }
                            };
                            auto rarityStr = [](ItemRarity r) -> const char* {
                                switch (r) {
                                    case ItemRarity::Uncommon:  return "Uncommon";
                                    case ItemRarity::Rare:      return "Rare";
                                    case ItemRarity::Epic:      return "Epic";
                                    case ItemRarity::Legendary: return "Legendary";
                                    case ItemRarity::Unique:    return "Legendary";
                                    default:                    return "Common";
                                }
                            };
                            for (int si = 0; si < slotCount; ++si) {
                                if (slots[si].isValid()) {
                                    ip->items[si].itemId      = slots[si].itemId;
                                    ip->items[si].displayName = slots[si].displayName;
                                    ip->items[si].rarity      = rarityStr(slots[si].rarity);
                                    ip->items[si].itemType    = slots[si].itemType;
                                    ip->items[si].quantity    = slots[si].quantity;
                                    ip->items[si].enchantLevel = slots[si].enchantLevel;
                                    ip->items[si].levelReq    = slots[si].levelReq;
                                    ip->items[si].damageMin   = slots[si].damageMin;
                                    ip->items[si].damageMax   = slots[si].damageMax;
                                    ip->items[si].armor       = slots[si].armorValue;
                                    ip->items[si].statLines.clear();
                                    if (slots[si].damageMin > 0 || slots[si].damageMax > 0) {
                                        char buf[64]; snprintf(buf, sizeof(buf), "Attack %d-%d", slots[si].damageMin, slots[si].damageMax);
                                        ip->items[si].statLines.push_back(buf);
                                    }
                                    if (slots[si].armorValue > 0) {
                                        char buf[64]; snprintf(buf, sizeof(buf), "Armor %d", slots[si].armorValue);
                                        ip->items[si].statLines.push_back(buf);
                                    }
                                    for (const auto& rs : slots[si].rolledStats) {
                                        char buf[64]; snprintf(buf, sizeof(buf), "%s +%d",
                                            statName(rs.statType), rs.value);
                                        ip->items[si].statLines.push_back(buf);
                                    }
                                } else {
                                    ip->items[si].itemId.clear();
                                    ip->items[si].displayName.clear();
                                    ip->items[si].rarity.clear();
                                    ip->items[si].itemType.clear();
                                    ip->items[si].quantity = 0;
                                    ip->items[si].enchantLevel = 0;
                                    ip->items[si].levelReq = 0;
                                    ip->items[si].damageMin = 0;
                                    ip->items[si].damageMax = 0;
                                    ip->items[si].armor = 0;
                                    ip->items[si].statLines.clear();
                                }
                            }

                            // Gap #2: push equipment slot data
                            // InventoryPanel equipSlots: 0=Hat, 1=Armor, 2=Weapon, 3=Shield(SubWeapon),
                            //   4=Gloves, 5=Boots(Shoes), 6=Ring, 7=Necklace, 8=Belt, 9=Cloak
                            static const EquipmentSlot equipMap[] = {
                                EquipmentSlot::Hat, EquipmentSlot::Armor, EquipmentSlot::Weapon,
                                EquipmentSlot::SubWeapon, EquipmentSlot::Gloves, EquipmentSlot::Shoes,
                                EquipmentSlot::Ring, EquipmentSlot::Necklace,
                                EquipmentSlot::Belt, EquipmentSlot::Cloak
                            };
                            const auto& equipmentMap = inv->inventory.getEquipmentMap();
                            for (int ei = 0; ei < InventoryPanel::NUM_EQUIP_SLOTS; ++ei) {
                                auto it = equipmentMap.find(equipMap[ei]);
                                if (it != equipmentMap.end() && !it->second.itemId.empty()) {
                                    ip->equipSlots[ei].itemId      = it->second.itemId;
                                    ip->equipSlots[ei].name        = it->second.displayName;
                                    ip->equipSlots[ei].displayName = it->second.displayName;
                                    ip->equipSlots[ei].rarity      = rarityStr(it->second.rarity);
                                    ip->equipSlots[ei].itemType    = it->second.itemType;
                                    ip->equipSlots[ei].enchantLevel = it->second.enchantLevel;
                                    ip->equipSlots[ei].levelReq    = it->second.levelReq;
                                    ip->equipSlots[ei].damageMin   = it->second.damageMin;
                                    ip->equipSlots[ei].damageMax   = it->second.damageMax;
                                    ip->equipSlots[ei].armor       = it->second.armorValue;
                                    ip->equipSlots[ei].statLines.clear();
                                    if (it->second.damageMin > 0 || it->second.damageMax > 0) {
                                        char buf[64]; snprintf(buf, sizeof(buf), "Attack %d-%d", it->second.damageMin, it->second.damageMax);
                                        ip->equipSlots[ei].statLines.push_back(buf);
                                    }
                                    if (it->second.armorValue > 0) {
                                        char buf[64]; snprintf(buf, sizeof(buf), "Armor %d", it->second.armorValue);
                                        ip->equipSlots[ei].statLines.push_back(buf);
                                    }
                                    for (const auto& rs : it->second.rolledStats) {
                                        char buf[64]; snprintf(buf, sizeof(buf), "%s +%d",
                                            statName(rs.statType), rs.value);
                                        ip->equipSlots[ei].statLines.push_back(buf);
                                    }
                                } else {
                                    ip->equipSlots[ei].itemId.clear();
                                    ip->equipSlots[ei].name.clear();
                                    ip->equipSlots[ei].displayName.clear();
                                    ip->equipSlots[ei].rarity.clear();
                                    ip->equipSlots[ei].itemType.clear();
                                    ip->equipSlots[ei].enchantLevel = 0;
                                    ip->equipSlots[ei].levelReq = 0;
                                    ip->equipSlots[ei].damageMin = 0;
                                    ip->equipSlots[ei].damageMax = 0;
                                    ip->equipSlots[ei].armor = 0;
                                    ip->equipSlots[ei].statLines.clear();
                                }
                            }

                            // Paper doll textures (layered from AppearanceComponent)
                            auto* appearance = localPlayer->getComponent<AppearanceComponent>();
                            if (appearance) {
                                ip->dollBodyTex   = appearance->body.front;
                                ip->dollHairTex   = appearance->hair.front;
                                ip->dollArmorTex  = appearance->armor.front;
                                ip->dollHatTex    = appearance->hat.front;
                                ip->dollWeaponTex = nullptr; // weapon not shown in idle pose
                                auto& catalog = PaperDollCatalog::instance();
                                if (catalog.isLoaded()) {
                                    ip->dollFrameW = catalog.frameWidth();
                                    ip->dollFrameH = catalog.frameHeight();
                                }
                            }
                            // Legacy fallback
                            auto* sprite = localPlayer->getComponent<SpriteComponent>();
                            if (!appearance && sprite && sprite->texture) {
                                ip->characterTexture = sprite->texture;
                            }
                        }

                        // SkillPanel — populate from ClientSkillDefinitionCache (all class skills)
                        auto* sklPanel = dynamic_cast<SkillPanel*>(menuScreen->findById("skill_panel"));
                        auto* smcMenu = localPlayer->getComponent<SkillManagerComponent>();
                        if (sklPanel && smcMenu) {
                            sklPanel->remainingPoints = smcMenu->skills.availablePoints();

                            const auto& allDefs = ClientSkillDefinitionCache::getAllSkills();
                            sklPanel->classSkills.resize(allDefs.size());
                            for (size_t si = 0; si < allDefs.size(); ++si) {
                                auto& info = sklPanel->classSkills[si];
                                const auto& def = allDefs[si];
                                info.skillId       = def.skillId;
                                info.name          = def.skillName;
                                info.maxLevel      = 3;
                                info.levelRequired = def.levelRequired;
                                info.isPassive     = def.isPassive;

                                const LearnedSkill* ls = smcMenu->skills.getLearnedSkill(def.skillId);
                                if (ls) {
                                    info.currentLevel  = ls->effectiveRank();
                                    info.unlockedLevel = ls->unlockedRank;
                                    info.unlocked      = true;
                                } else {
                                    info.currentLevel  = 0;
                                    info.unlockedLevel = 0;
                                    info.unlocked      = false;
                                }
                            }

                            // Populate skill bar slot assignments for the wheel display
                            sklPanel->skillBarSlots.resize(20);
                            sklPanel->skillBarNames.resize(20);
                            for (int si = 0; si < 20; ++si) {
                                std::string slotSkillId = smcMenu->skills.getSkillInSlot(si);
                                sklPanel->skillBarSlots[si] = slotSkillId;
                                if (!slotSkillId.empty()) {
                                    const auto* sDef = ClientSkillDefinitionCache::getSkill(slotSkillId);
                                    sklPanel->skillBarNames[si] = sDef ? sDef->skillName : slotSkillId;
                                } else {
                                    sklPanel->skillBarNames[si].clear();
                                }
                            }
                        }
                    }
                }
            }

            // ---- Social screen per-frame data push ----
            {
                auto* socialScreen = uiManager().getScreen("fate_social");
                auto* scene3 = SceneManager::instance().currentScene();
                if (socialScreen && scene3) {
                    Entity* localPlayer = nullptr;
                    scene3->world().forEach<PlayerController>(
                        [&](Entity* e, PlayerController* ctrl) {
                            if (ctrl->isLocalPlayer) localPlayer = e;
                        }
                    );

                    if (localPlayer) {
                        // Push party member data to PartyFrame
                        auto* partyComp = localPlayer->getComponent<PartyComponent>();
                        auto* pf = dynamic_cast<PartyFrame*>(socialScreen->findById("party_frame"));
                        if (pf) {
                            pf->members.clear();
                            if (partyComp && partyComp->party.isInParty()) {
                                for (const auto& pm : partyComp->party.members) {
                                    PartyFrameMemberInfo info;
                                    info.name     = pm.characterName;
                                    info.hp       = static_cast<float>(pm.currentHP);
                                    info.maxHp    = static_cast<float>(pm.maxHP > 0 ? pm.maxHP : 1);
                                    info.mp       = static_cast<float>(pm.currentMP);
                                    info.maxMp    = static_cast<float>(pm.maxMP > 0 ? pm.maxMP : 1);
                                    info.level    = pm.level;
                                    info.isLeader = pm.isLeader;
                                    pf->members.push_back(std::move(info));
                                    if (static_cast<int>(pf->members.size()) >= 2) break;
                                }
                            }
                        }

                        // Sync ChatPanel party/guild tab enable state
                        auto* chatPanel = dynamic_cast<ChatPanel*>(socialScreen->findById("chat_panel"));
                        auto* guildComp = localPlayer->getComponent<GuildComponent>();
                        if (chatPanel) {
                            chatPanel->isInParty = partyComp && partyComp->party.isInParty();
                            chatPanel->isInGuild = guildComp && guildComp->guild.guildId != 0;
                        }

                        // Gap #7: GuildPanel — push guild info
                        auto* gp = dynamic_cast<GuildPanel*>(socialScreen->findById("guild_panel"));
                        if (gp && guildComp) {
                            gp->guildName   = guildComp->guild.guildName;
                            gp->guildLevel  = guildComp->guild.guildLevel;
                            // TODO: roster API — server does not replicate full member list to client yet.
                            // When guild member list sync is added, populate gp->members here.
                            gp->memberCount = static_cast<int>(gp->members.size());
                        }

                        // Gap #8: TradeWindow — push slot/gold/partner/lock data
                        auto* tw = dynamic_cast<TradeWindow*>(socialScreen->findById("trade_window"));
                        auto* tradeComp = localPlayer->getComponent<TradeComponent>();
                        if (tw && tradeComp && tradeComp->trade.isInTrade()) {
                            tw->partnerName = tradeComp->trade.sessionState.otherPlayerName;
                            tw->myGold      = static_cast<int>(tradeComp->trade.sessionState.myGold);
                            tw->theirGold   = static_cast<int>(tradeComp->trade.sessionState.otherGold);
                            tw->myLocked    = tradeComp->trade.sessionState.myLocked;
                            tw->theirLocked = tradeComp->trade.sessionState.otherLocked;
                            // Push my trade slots
                            for (int ti = 0; ti < 9; ++ti) {
                                if (ti < static_cast<int>(tradeComp->trade.myItems.size()) &&
                                    !tradeComp->trade.myItems[ti].isEmpty()) {
                                    tw->mySlots[ti].itemId   = tradeComp->trade.myItems[ti].itemId;
                                    tw->mySlots[ti].quantity = tradeComp->trade.myItems[ti].quantity;
                                } else {
                                    tw->mySlots[ti].itemId.clear();
                                    tw->mySlots[ti].quantity = 0;
                                }
                            }
                            // Push their trade slots
                            for (int ti = 0; ti < 9; ++ti) {
                                if (ti < static_cast<int>(tradeComp->trade.otherItems.size()) &&
                                    !tradeComp->trade.otherItems[ti].isEmpty()) {
                                    tw->theirSlots[ti].itemId   = tradeComp->trade.otherItems[ti].itemId;
                                    tw->theirSlots[ti].quantity = tradeComp->trade.otherItems[ti].quantity;
                                } else {
                                    tw->theirSlots[ti].itemId.clear();
                                    tw->theirSlots[ti].quantity = 0;
                                }
                            }
                        }
                    }
                }
            }

            // ---- NPC panel per-frame data push ----
            {
                auto* scene = SceneManager::instance().currentScene();
                if (scene) {
                    // NPC dialogue panel: open when NPC interaction system triggers dialogue
                    Entity* interactingNPC = npcInteractionSystem_
                        ? scene->world().getEntity(npcInteractionSystem_->interactingNPCHandle) : nullptr;
                    if (npcInteractionSystem_ && npcInteractionSystem_->dialogueOpen
                        && interactingNPC
                        && npcDialoguePanel_ && !npcDialoguePanel_->isOpen()) {
                        auto* npc = interactingNPC;
                        auto* npcComp = npc->getComponent<NPCComponent>();
                        if (npcComp) {
                            npcDialoguePanel_->npcId = npcComp->npcId;
                            npcDialoguePanel_->npcName = npcComp->displayName;
                            npcDialoguePanel_->greeting = npcComp->dialogueGreeting;
                            npcDialoguePanel_->hasShop = npc->getComponent<ShopComponent>() != nullptr;
                            npcDialoguePanel_->hasBank = npc->getComponent<BankerComponent>() != nullptr;
                            npcDialoguePanel_->hasTeleporter = npc->getComponent<TeleporterComponent>() != nullptr;
                            npcDialoguePanel_->hasGuild = npc->getComponent<GuildNPCComponent>() != nullptr;
                            npcDialoguePanel_->hasDungeon = npc->getComponent<DungeonNPCComponent>() != nullptr;
                            npcDialoguePanel_->hasArena = npc->getComponent<ArenaNPCComponent>() != nullptr;
                            npcDialoguePanel_->hasBattlefield = npc->getComponent<BattlefieldNPCComponent>() != nullptr;
                            npcDialoguePanel_->hasMarketplace = npc->getComponent<MarketplaceNPCComponent>() != nullptr;
                            npcDialoguePanel_->hasLeaderboard = npc->getComponent<LeaderboardNPCComponent>() != nullptr;

                            // Populate quests from QuestGiverComponent
                            npcDialoguePanel_->quests.clear();
                            auto* qgComp = npc->getComponent<QuestGiverComponent>();
                            if (qgComp) {
                                for (uint32_t qId : qgComp->questIds) {
                                    NpcDialoguePanel::QuestEntry qe;
                                    qe.questId = qId;
                                    qe.questName = "Quest #" + std::to_string(qId);
                                    npcDialoguePanel_->quests.push_back(std::move(qe));
                                }
                            }

                            // Check story mode
                            auto* storyComp = npc->getComponent<StoryNPCComponent>();
                            npcDialoguePanel_->isStoryMode = (storyComp != nullptr);
                            if (storyComp && !storyComp->dialogueTree.empty()) {
                                // Story dialogue handled by NpcDialoguePanel render
                            }

                            npcDialoguePanel_->open();
                        }
                    }

                    // Push player data to shop panel each frame
                    if (shopPanel_ && shopPanel_->isOpen()) {
                        Entity* localPlayer = nullptr;
                        scene->world().forEach<PlayerController>(
                            [&](Entity* e, PlayerController* ctrl) {
                                if (ctrl->isLocalPlayer) localPlayer = e;
                            }
                        );
                        if (localPlayer) {
                            auto* inv = localPlayer->getComponent<InventoryComponent>();
                            if (inv) {
                                shopPanel_->playerGold = inv->inventory.getGold();
                                const auto& slots = inv->inventory.getSlots();
                                for (int i = 0; i < ShopPanel::MAX_SLOTS && i < static_cast<int>(slots.size()); ++i) {
                                    shopPanel_->playerItems[i].itemId = slots[i].itemId;
                                    shopPanel_->playerItems[i].displayName = slots[i].displayName.empty() ? slots[i].itemId : slots[i].displayName;
                                    shopPanel_->playerItems[i].quantity = slots[i].quantity;
                                    shopPanel_->playerItems[i].sellPrice = 0; // Server computes sell price
                                    shopPanel_->playerItems[i].soulbound = slots[i].isSoulbound;
                                }
                                // Clear remaining slots
                                for (int i = static_cast<int>(slots.size()); i < ShopPanel::MAX_SLOTS; ++i) {
                                    shopPanel_->playerItems[i].itemId.clear();
                                    shopPanel_->playerItems[i].displayName.clear();
                                    shopPanel_->playerItems[i].quantity = 0;
                                    shopPanel_->playerItems[i].sellPrice = 0;
                                    shopPanel_->playerItems[i].soulbound = false;
                                }
                            }
                        }
                    }

                    // Push player data to bank panel each frame
                    if (bankPanel_ && bankPanel_->isOpen()) {
                        Entity* localPlayer = nullptr;
                        scene->world().forEach<PlayerController>(
                            [&](Entity* e, PlayerController* ctrl) {
                                if (ctrl->isLocalPlayer) localPlayer = e;
                            }
                        );
                        if (localPlayer) {
                            auto* inv = localPlayer->getComponent<InventoryComponent>();
                            auto* bankComp = localPlayer->getComponent<BankStorageComponent>();
                            if (inv) {
                                bankPanel_->playerGold = inv->inventory.getGold();
                                const auto& slots = inv->inventory.getSlots();
                                for (int i = 0; i < BankPanel::MAX_SLOTS && i < static_cast<int>(slots.size()); ++i) {
                                    bankPanel_->playerItems[i].itemId = slots[i].itemId;
                                    bankPanel_->playerItems[i].displayName = slots[i].displayName.empty() ? slots[i].itemId : slots[i].displayName;
                                    bankPanel_->playerItems[i].quantity = slots[i].quantity;
                                }
                                for (int i = static_cast<int>(slots.size()); i < BankPanel::MAX_SLOTS; ++i) {
                                    bankPanel_->playerItems[i].itemId.clear();
                                    bankPanel_->playerItems[i].displayName.clear();
                                    bankPanel_->playerItems[i].quantity = 0;
                                }
                            }
                            if (bankComp) {
                                bankPanel_->bankGold = bankComp->storage.getStoredGold();
                                bankPanel_->bankItems.clear();
                                for (const auto& si : bankComp->storage.getItems()) {
                                    BankPanel::BankItem bi;
                                    bi.itemId = si.itemId;
                                    bi.displayName = si.fullItem.displayName.empty() ? si.itemId : si.fullItem.displayName;
                                    bi.count = si.count;
                                    bankPanel_->bankItems.push_back(bi);
                                }
                            }
                        }
                    }
                }
            }

            // F1 HUD toggle removed — HUD is always on
            // F2 collision debug removed — now controlled via editor toolbar toggle
            auto& input = Input::instance();

            // Touch controls are now handled by DPad/SkillArc widgets in
            // the retained-mode UI (see fate_hud.json wiring above).

            // UI toggles — action map suppresses these in Chat context automatically.
            // Keyboard routing in app.cpp already blocks events when editor is paused,
            // so no additional wantsKeyboard() guard needed here.
            if (input.isActionPressed(ActionId::ToggleInventory)) {
                // Toggle the menu panels screen; show last active tab (don't reset)
                auto* menuScr = uiManager().getScreen("fate_menu_panels");
                if (menuScr) {
                    bool opening = !menuScr->visible();
                    menuScr->setVisible(opening);
                    if (opening) {
                        auto* tabBar = dynamic_cast<MenuTabBar*>(menuScr->findById("tab_bar"));
                        if (tabBar) {
                            if (tabBar->onTabChanged) tabBar->onTabChanged(tabBar->activeTab);
                        }
                    }
                    // Hide/show game HUD controls when menu is open/closed
                    auto* hudScr = uiManager().getScreen("fate_hud");
                    if (hudScr) {
                        auto* dpad = hudScr->findById("dpad");
                        auto* arc  = hudScr->findById("skill_arc");
                        auto* sb   = hudScr->findById("status_bar");
                        if (dpad) dpad->setVisible(!opening);
                        if (arc)  arc->setVisible(!opening);
                        if (sb) {
                            auto* fsb = dynamic_cast<FateStatusBar*>(sb);
                            if (fsb) {
                                fsb->showMenuButton = !opening;
                                fsb->showChatButton = !opening;
                            }
                        }
                    }
                }
            }
            if (input.isActionPressed(ActionId::ToggleSkillBar)) {
                if (skillArc_) skillArc_->setVisible(!skillArc_->visible());
            }
            // Chat toggle (Enter key)
            if (input.isActionPressed(ActionId::OpenChat)) {
                if (!(chatPanel_ && chatPanel_->visible())) {
                    if (chatPanel_) chatPanel_->setVisible(true);
                    input.setChatMode(true);
                }
            }
            if (input.isActionPressed(ActionId::SubmitChat)) {
                // SubmitChat fires in Chat context after Enter on the input field.
                // ChatPanel handles send/hide internally; we just exit chat mode when it hides.
                if (!(chatPanel_ && chatPanel_->visible())) {
                    input.setChatMode(false);
                }
            }
            if (input.isActionPressed(ActionId::Cancel) && chatPanel_ &&
                (chatPanel_->visible() || chatPanel_->isFullPanelMode())) {
                chatPanel_->setFullPanelMode(false);
                chatPanel_->setVisible(true); // stay in idle overlay mode
                uiManager().clearFocus();
                input.setChatMode(false);
            }
            // Skill bar page switching
            if (input.isActionPressed(ActionId::SkillPagePrev)) {
                if (skillArc_) skillArc_->prevPage();
            }
            if (input.isActionPressed(ActionId::SkillPageNext)) {
                if (skillArc_) skillArc_->nextPage();
            }

            // Set UI blocking flag — movement + nameplates suppressed while panels are open
            input.setUIBlocking(
                (inventoryPanel_ && inventoryPanel_->visible()) ||
                (petPanel_ && petPanel_->isOpen()) ||
                (craftingPanel_ && craftingPanel_->isOpen()) ||
                (collectionPanel_ && collectionPanel_->isOpen()) ||
                (shopPanel_ && shopPanel_->isOpen()) ||
                (bankPanel_ && bankPanel_->isOpen()) ||
                (teleporterPanel_ && teleporterPanel_->isOpen()) ||
                (arenaPanel_ && arenaPanel_->isOpen()) ||
                (battlefieldPanel_ && battlefieldPanel_->isOpen()) ||
                (npcDialoguePanel_ && npcDialoguePanel_->isOpen()) ||
                (npcInteractionSystem_ && npcInteractionSystem_->dialogueOpen)
            );
            break;
        }
    }

    // Set retained-mode UI input offset so mouse coords map to widget coords.
    // In the editor, widgets are laid out in FBO space (0,0 to vpW,vpH) but the
    // mouse position is in window space. Subtract the editor viewport origin.
    auto& ed = Editor::instance();
    Vec2 vp = ed.viewportPos();
    uiManager().setInputTransform(vp.x, vp.y, 1.0f, 1.0f);
}

void GameApp::onRender(SpriteBatch& batch, Camera& camera) {
    // Pre-game states: login/character-creation screens render via uiManager automatically.
    // Just skip gameplay rendering when not InGame.
    if (connState_ != ConnectionState::InGame) {
        return;
    }

    // ---- InGame rendering ----

    // Scene rendering (tiles, entities, combat text, debug overlays) is now handled
    // by render graph passes registered in onInit(). This callback only handles
    // ImGui game UI and screen-space overlays that render into the editor viewport FBO.

    // ImGui game UI — suppress when editor is open and paused (no gameplay happening)
    if (!(Editor::instance().isOpen() && Editor::instance().isPaused())) {
        // Set the global game viewport rect — all UI systems read from this
        auto& ed = Editor::instance();
        Vec2 vp = ed.viewportPos();
        Vec2 vs = ed.viewportSize();
        GameViewport::set(vp.x, vp.y, vs.x, vs.y);

        auto* scene = SceneManager::instance().currentScene();
        if (scene) {
            // All HUD/chat/death/NPC/touch UIs now rendered by retained-mode uiManager().
        }
    }

    // Network config panel (debug only — hidden in shipping builds)
#ifndef FATE_SHIPPING
    if (Editor::instance().isOpen()) {
        drawNetworkPanel();
    }
#endif

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
            auto* scene = SceneManager::instance().currentScene();
            if (scene) {
                // Clean up ghost entities
                for (auto& [pid, handle] : ghostEntities_) {
                    scene->world().destroyEntity(handle);
                }
                // Destroy local player entity
                scene->world().forEach<PlayerController>(
                    [&](Entity* e, PlayerController* ctrl) {
                        if (ctrl->isLocalPlayer) {
                            scene->world().destroyEntity(e->handle());
                        }
                    }
                );
                scene->world().processDestroyQueue();
            }
            ghostEntities_.clear();
            ghostDeathTimers_.clear();
            ghostInterpolation_.clear();
            combatPredictions_.clear();
            SkillVFXPlayer::instance().clear();
            localPlayerCreated_ = false;
            retainedUILoaded_ = false;
            npcDialoguePanel_ = nullptr;
            shopPanel_ = nullptr;
            bankPanel_ = nullptr;
            teleporterPanel_ = nullptr;
            arenaPanel_ = nullptr;
            battlefieldPanel_ = nullptr;
            costumePanel_ = nullptr;
            dungeonInviteDialog_ = nullptr;
            inDungeon_ = false;
            connState_ = ConnectionState::LoginScreen;
            if (loginScreenWidget_) {
                loginScreenWidget_->reset();
                loginScreenWidget_->setVisible(true);
            }
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

void GameApp::renderAttackRange(SpriteBatch& batch, Camera& camera) {
    auto* scene = SceneManager::instance().currentScene();
    if (!scene) return;

    batch.begin(camera.getViewProjection());

    scene->world().forEach<Transform, CombatControllerComponent>(
        [&](Entity* entity, Transform* transform, CombatControllerComponent* combat) {
            if (!combat->showDisengageRange) return;
            if (combat->disengageRange <= 0.0f) return;

            float radiusPixels = combat->disengageRange * 32.0f; // tiles → pixels
            batch.drawRing(transform->position, radiusPixels, 1.5f,
                           Color(0.2f, 0.6f, 1.0f, 0.7f), 97.0f, 48);
        }
    );

    batch.end();
}

void GameApp::onShutdown() {
    // Disconnect from server before shutdown so the server saves the correct
    // player state immediately (alive, current position, XP). Without this,
    // the server-side player entity stays alive and AFK — mobs kill it before
    // the timeout fires, saving is_dead=true.
    if (netClient_.isConnected()) {
        netClient_.disconnect();
        // Give the OS time to flush the UDP disconnect packets before
        // the process exits and the socket is destroyed.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    audioManager_.shutdown();
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
    npcDialoguePanel_ = nullptr;
    shopPanel_ = nullptr;
    bankPanel_ = nullptr;
    teleporterPanel_ = nullptr;
    arenaPanel_ = nullptr;
    battlefieldPanel_ = nullptr;
    costumePanel_ = nullptr;
    dungeonInviteDialog_ = nullptr;
    inDungeon_ = false;

    LOG_INFO("Game", "Game shutting down...");
}

// ============================================================================
// NPC panel helpers
// ============================================================================

Entity* GameApp::findNpcById(uint32_t npcId) {
    Entity* found = nullptr;
    auto* scene = SceneManager::instance().currentScene();
    if (!scene) return nullptr;
    scene->world().forEach<NPCComponent>([&](Entity* e, NPCComponent* nc) {
        if (nc->npcId == npcId) found = e;
    });
    return found;
}

void GameApp::captureLocalPlayerState() {
    auto* sc = SceneManager::instance().currentScene();
    if (!sc) return;
    sc->world().forEach<CharacterStatsComponent, PlayerController>(
        [this](Entity* e, CharacterStatsComponent* cs, PlayerController* ctrl) {
            if (!ctrl->isLocalPlayer) return;
            pendingPlayerState_.level = cs->stats.level;
            pendingPlayerState_.currentHP = cs->stats.currentHP;
            pendingPlayerState_.maxHP = cs->stats.maxHP;
            pendingPlayerState_.currentMP = cs->stats.currentMP;
            pendingPlayerState_.maxMP = cs->stats.maxMP;
            pendingPlayerState_.currentFury = cs->stats.currentFury;
            pendingPlayerState_.currentXP = cs->stats.currentXP;
            pendingPlayerState_.honor = cs->stats.honor;
            pendingPlayerState_.pvpKills = cs->stats.pvpKills;
            pendingPlayerState_.pvpDeaths = cs->stats.pvpDeaths;
            auto* inv = e->getComponent<InventoryComponent>();
            if (inv) pendingPlayerState_.gold = inv->inventory.getGold();
        }
    );
}

void GameApp::populateCharacterSlots(CharacterSelectScreen* screen,
                                     const std::vector<CharacterPreview>& chars) {
    screen->slots.clear();
    for (const auto& c : chars) {
        CharacterSlot slot;
        slot.name = c.characterName;
        slot.className = c.className;
        slot.level = c.level;
        slot.empty = false;
        slot.characterId = c.characterId;
        slot.gender = c.gender;
        slot.hairstyle = c.hairstyle;
        slot.faction = c.faction;
        slot.weaponStyle = c.weaponStyle;
        slot.armorStyle  = c.armorStyle;
        slot.hatStyle    = c.hatStyle;
        screen->slots.push_back(slot);
    }
    while (screen->slots.size() < 3) {
        CharacterSlot empty;
        empty.empty = true;
        screen->slots.push_back(empty);
    }
    screen->selectedSlot = -1;
    for (int i = 0; i < (int)screen->slots.size(); ++i) {
        if (!screen->slots[i].empty) {
            screen->selectedSlot = i;
            selectedCharacterId_ = screen->slots[i].characterId;
            pendingGender_ = screen->slots[i].gender;
            pendingHairstyle_ = screen->slots[i].hairstyle;
            break;
        }
    }
}

void GameApp::wireCharacterSelectCallbacks(CharacterSelectScreen* charSelect) {
    if (!charSelect) return;

    charSelect->onEntry = [this](const std::string&) {
        if (selectedCharacterId_.empty()) {
            LOG_WARN("GameApp", "Entry pressed but no character selected");
            return;
        }
        LOG_INFO("GameApp", "Selecting character '%s' for entry", selectedCharacterId_.c_str());
        authClient_.selectCharacterAsync(selectedCharacterId_);
        connState_ = ConnectionState::Authenticating;
        auto* cs = dynamic_cast<CharacterSelectScreen*>(uiManager().getScreen("character_select"));
        if (cs) cs->setVisible(false);
        if (loginScreenWidget_) {
            loginScreenWidget_->setVisible(true);
            loginScreenWidget_->setStatus("Entering world...", false);
        }
    };

    charSelect->onSlotSelected = [this](int index, const std::string& charId) {
        selectedCharacterId_ = charId;
        // Store appearance from the preview data for createPlayer
        std::string dbArmor, dbWeapon, dbHat;
        for (const auto& c : pendingCharacterList_) {
            if (c.characterId == charId) {
                pendingGender_ = c.gender;
                pendingHairstyle_ = c.hairstyle;
                dbArmor = c.armorStyle;
                dbWeapon = c.weaponStyle;
                dbHat = c.hatStyle;
                break;
            }
        }
        LOG_INFO("GameApp", "Selected slot %d, character '%s' (gender=%d hairstyle=%d armor='%s' weapon='%s' hat='%s')",
                 index, charId.c_str(), pendingGender_, pendingHairstyle_,
                 dbArmor.c_str(), dbWeapon.c_str(), dbHat.c_str());
    };

    charSelect->onCreateNew = [this](const std::string&) {
        auto* cs = dynamic_cast<CharacterSelectScreen*>(uiManager().getScreen("character_select"));
        if (cs) cs->setVisible(false);
        if (!uiManager().getScreen("character_creation"))
            uiManager().loadScreen("assets/ui/screens/character_creation.json");
        auto* ccw = dynamic_cast<CharacterCreationScreen*>(uiManager().getScreen("character_creation"));
        if (ccw) {
            ccw->setVisible(true);
            ccw->characterName.clear();
            ccw->cursorPos = 0;
            ccw->selectedClass = 0;
            ccw->selectedFaction = 0;
            ccw->statusMessage.clear();
        }
        connState_ = ConnectionState::CharacterCreation;
    };

    charSelect->onDelete = [this](const std::string&) {
        auto* cs = dynamic_cast<CharacterSelectScreen*>(uiManager().getScreen("character_select"));
        if (!cs) return;
        LOG_INFO("GameApp", "Deleting character '%s'", cs->deleteTargetId.c_str());
        authClient_.deleteCharacterAsync(cs->deleteTargetId);
        cs->showDeleteConfirm = false;
    };

    // Also wire creation screen callbacks
    auto* charCreate = dynamic_cast<CharacterCreationScreen*>(
        uiManager().getScreen("character_creation"));
    if (charCreate) {
        charCreate->onNext = [this](const std::string&) {
            auto* cc = dynamic_cast<CharacterCreationScreen*>(
                uiManager().getScreen("character_creation"));
            if (!cc) return;
            if (cc->characterName.empty()) {
                cc->statusMessage = "Enter a character name";
                cc->isError = true;
                return;
            }
            if (!AuthValidation::isValidCharacterName(cc->characterName)) {
                cc->statusMessage = "Name must be 1-10 alphanumeric characters";
                cc->isError = true;
                return;
            }
            const char* classNames[] = {"Warrior", "Mage", "Archer"};
            LOG_INFO("GameApp", "Creating character '%s' class=%s",
                     cc->characterName.c_str(), classNames[cc->selectedClass]);
            authClient_.createCharacterAsync(
                cc->characterName,
                classNames[cc->selectedClass],
                static_cast<uint8_t>(cc->selectedFaction),
                cc->selectedGender,
                cc->selectedHairstyle
            );
            cc->statusMessage = "Creating character...";
            cc->isError = false;
        };

        charCreate->onBack = [this](const std::string&) {
            auto* cc = dynamic_cast<CharacterCreationScreen*>(uiManager().getScreen("character_creation"));
            if (cc) cc->setVisible(false);
            auto* cs = dynamic_cast<CharacterSelectScreen*>(uiManager().getScreen("character_select"));
            if (cs) cs->setVisible(true);
            connState_ = ConnectionState::CharacterSelect;
        };
    }
}

void GameApp::closeAllNpcPanels() {
    // Use setVisible(false) instead of close() to avoid infinite recursion:
    // close() fires onClose callbacks which call closeAllNpcPanels() again.
    if (npcDialoguePanel_) npcDialoguePanel_->setVisible(false);
    if (shopPanel_) shopPanel_->setVisible(false);
    if (bankPanel_) bankPanel_->setVisible(false);
    if (teleporterPanel_) teleporterPanel_->setVisible(false);
    if (arenaPanel_) arenaPanel_->setVisible(false);
    if (battlefieldPanel_) battlefieldPanel_->setVisible(false);
    if (leaderboardPanel_) leaderboardPanel_->setVisible(false);
    if (npcInteractionSystem_) npcInteractionSystem_->closeDialogue();
}

void GameApp::enrichCostumeEntry(CostumeEntry& entry) {
    auto it = costumeDefCache_.find(entry.costumeDefId);
    if (it != costumeDefCache_.end()) {
        entry.displayName  = it->second.displayName;
        entry.slotType     = it->second.slotType;
        entry.visualIndex  = it->second.visualIndex;
        entry.rarity       = it->second.rarity;
    }
}

void GameApp::applySkillDefs(const SvSkillDefsMsg& msg) {
    // Determine the local player's class name
    std::string className;
    auto* sc = SceneManager::instance().currentScene();
    if (sc) {
        sc->world().forEach<CharacterStatsComponent, PlayerController>(
            [&](Entity*, CharacterStatsComponent* cs, PlayerController* ctrl) {
                if (ctrl->isLocalPlayer) className = cs->stats.className;
            });
    }
    if (className.empty()) className = "Unknown";

    // Map target type enum to string
    static const char* targetTypeNames[] = {
        "Self", "SingleEnemy", "SingleAlly", "AreaAroundSelf",
        "AreaAtTarget", "Cone", "Line"
    };

    std::vector<ClientSkillDef> defs;
    defs.reserve(msg.defs.size());
    for (const auto& entry : msg.defs) {
        ClientSkillDef d;
        d.skillId = entry.skillId;
        d.skillName = entry.skillName;
        d.description = entry.description;
        d.skillType = (entry.skillType == 1) ? "Passive" : "Active";
        if (entry.resourceType == 1) d.resourceType = "Fury";
        else if (entry.resourceType == 2) d.resourceType = "Mana";
        else d.resourceType = "None";
        d.targetType = (entry.targetType < 7) ? targetTypeNames[entry.targetType] : "Self";
        d.levelRequired = entry.levelRequired;
        d.range = entry.range;
        d.aoeRadius = entry.aoeRadius;
        d.isUltimate = (entry.isUltimate != 0);
        d.isPassive = (entry.skillType == 1);
        d.consumesAllResource = (entry.consumesAllResource != 0);
        d.vfxId = entry.vfxId;

        for (int i = 0; i < 3; ++i) {
            const auto& r = entry.ranks[i];
            d.ranks[i].resourceCost = static_cast<int>(r.resourceCost);
            d.ranks[i].cooldownSeconds = static_cast<float>(r.cooldownMs) / 1000.0f;
            d.ranks[i].damagePercent = static_cast<int>(r.damagePercent);
            d.ranks[i].maxTargets = static_cast<int>(r.maxTargets);
            d.ranks[i].effectDuration = static_cast<float>(r.effectDurationMs) / 1000.0f;
            d.ranks[i].effectValue = static_cast<float>(r.effectValue);
            d.ranks[i].stunDuration = static_cast<float>(r.stunDurationMs) / 1000.0f;
            d.ranks[i].passiveDamageReduction = static_cast<float>(r.passiveDamageReduction) / 100.0f;
            d.ranks[i].passiveCritBonus = static_cast<float>(r.passiveCritBonus) / 100.0f;
            d.ranks[i].passiveSpeedBonus = static_cast<float>(r.passiveSpeedBonus) / 100.0f;
            d.ranks[i].passiveHPBonus = static_cast<int>(r.passiveHPBonus);
            d.ranks[i].passiveStatBonus = static_cast<int>(r.passiveStatBonus);
        }
        defs.push_back(std::move(d));
    }

    ClientSkillDefinitionCache::populate(className, defs);
    LOG_INFO("Client", "Populated skill definition cache: %d skills for class '%s'",
             (int)defs.size(), className.c_str());
}

} // namespace fate

