#pragma once

// register_components.h — explicit component registration with trait overrides
// Called once at startup from GameApp::onInit()

#include "engine/ecs/component_meta.h"
#include "engine/ecs/component_traits.h"
#include "engine/render/texture.h"

#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "game/components/box_collider.h"
#include "game/components/player_controller.h"
#include "game/components/animator.h"
#include "game/components/polygon_collider.h"
#include "game/components/zone_component.h"
#include "game/components/game_components.h"
#include "game/components/faction_component.h"
#include "game/components/pet_component.h"
#include "game/systems/spawn_system.h"  // SpawnZoneComponent
#include "engine/particle/particle_emitter_component.h"
#include "engine/render/point_light_component.h"
#include "game/components/spawn_point_component.h"

#include <nlohmann/json.hpp>

// ============================================================================
// Component trait specializations (override default Serializable)
// ============================================================================

namespace fate {

// --- Transform: saved to disk ---
template<> struct component_traits<Transform> {
    static constexpr ComponentFlags flags =
        ComponentFlags::Serializable | ComponentFlags::Persistent;
};

// --- SpriteComponent: serialized (has custom toJson/fromJson for texture) ---
template<> struct component_traits<SpriteComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};

// --- BoxCollider: serialized ---
template<> struct component_traits<BoxCollider> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};

// --- PlayerController: serializable (needed for play-in-editor snapshot/restore) ---
template<> struct component_traits<PlayerController> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};

// --- MobAIComponent: runtime only ---
template<> struct component_traits<MobAIComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};

// --- TargetingComponent: runtime only ---
template<> struct component_traits<TargetingComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};

// --- NameplateComponent: runtime only ---
template<> struct component_traits<NameplateComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};

// --- MobNameplateComponent: runtime only ---
template<> struct component_traits<MobNameplateComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};

// --- Social components: runtime only ---
template<> struct component_traits<ChatComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};
template<> struct component_traits<GuildComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};
template<> struct component_traits<PartyComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};
template<> struct component_traits<FriendsComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};
template<> struct component_traits<MarketComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};
template<> struct component_traits<TradeComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};

// --- FactionComponent: saved to DB, replicated ---
template<> struct component_traits<FactionComponent> {
    static constexpr ComponentFlags flags =
        ComponentFlags::Serializable | ComponentFlags::Networked | ComponentFlags::Persistent;
};

// --- PetComponent: saved to DB, replicated ---
template<> struct component_traits<PetComponent> {
    static constexpr ComponentFlags flags =
        ComponentFlags::Serializable | ComponentFlags::Networked | ComponentFlags::Persistent;
};

// --- CharacterStatsComponent: saved to DB, replicated ---
template<> struct component_traits<CharacterStatsComponent> {
    static constexpr ComponentFlags flags =
        ComponentFlags::Serializable | ComponentFlags::Networked | ComponentFlags::Persistent;
};

// --- InventoryComponent: saved to DB ---
template<> struct component_traits<InventoryComponent> {
    static constexpr ComponentFlags flags =
        ComponentFlags::Serializable | ComponentFlags::Persistent;
};

// --- SkillManagerComponent: saved to DB, replicated (skill bar visible to others) ---
template<> struct component_traits<SkillManagerComponent> {
    static constexpr ComponentFlags flags =
        ComponentFlags::Serializable | ComponentFlags::Networked | ComponentFlags::Persistent;
};

// --- StatusEffectComponent: runtime only (effects are transient) ---
template<> struct component_traits<StatusEffectComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};

// --- CrowdControlComponent: runtime only (CC state is transient) ---
template<> struct component_traits<CrowdControlComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
};

// --- EnemyStatsComponent: serialized for spawning ---
template<> struct component_traits<EnemyStatsComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};

// --- DamageableComponent: serialized (marker) ---
template<> struct component_traits<DamageableComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};

// --- CombatControllerComponent: serialized ---
template<> struct component_traits<CombatControllerComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};

// --- ParticleEmitterComponent: serialized (emitter config persisted) ---
template<> struct component_traits<ParticleEmitterComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};

// --- PointLightComponent: serialized (light properties persisted) ---
template<> struct component_traits<PointLightComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};

// --- DroppedItemComponent: networked + serialized (ground loot is transient, NOT persistent) ---
template<> struct component_traits<DroppedItemComponent> {
    static constexpr ComponentFlags flags =
        ComponentFlags::Serializable | ComponentFlags::Networked;
};

// --- BossSpawnPointComponent: saved to disk ---
template<> struct component_traits<BossSpawnPointComponent> {
    static constexpr ComponentFlags flags =
        ComponentFlags::Serializable | ComponentFlags::Persistent;
};

// --- SpawnPointComponent: serialized (placed in scene editor as respawn marker) ---
template<> struct component_traits<SpawnPointComponent> {
    static constexpr ComponentFlags flags = ComponentFlags::Serializable;
};

// All other components keep the default (Serializable).

// ============================================================================
// registerAllComponents() — call once at startup
// ============================================================================

inline void registerAllComponents() {
    auto& reg = ComponentMetaRegistry::instance();

    // ----- Core engine components -----
    reg.registerComponent<Transform>();

    // SpriteComponent: custom serializer handles texturePath (skips shared_ptr<Texture>)
    reg.registerComponent<SpriteComponent>(
        // toJson
        [](const void* data, nlohmann::json& j) {
            const auto* s = static_cast<const SpriteComponent*>(data);
            j["texturePath"] = s->texturePath;
            j["sourceRect"]  = { s->sourceRect.x, s->sourceRect.y,
                                 s->sourceRect.w, s->sourceRect.h };
            j["size"]        = { s->size.x, s->size.y };
            j["tint"]        = { s->tint.r, s->tint.g, s->tint.b, s->tint.a };
            j["flipX"]       = s->flipX;
            j["flipY"]       = s->flipY;
            j["frameWidth"]  = s->frameWidth;
            j["frameHeight"] = s->frameHeight;
            j["currentFrame"]= s->currentFrame;
            j["totalFrames"] = s->totalFrames;
            j["columns"]     = s->columns;
        },
        // fromJson
        [](const nlohmann::json& j, void* data) {
            auto* s = static_cast<SpriteComponent*>(data);
            // Support both old ("texture") and new ("texturePath") field names
            if (j.contains("texturePath"))
                s->texturePath = j["texturePath"].get<std::string>();
            else if (j.contains("texture"))
                s->texturePath = j["texture"].get<std::string>();
            if (j.contains("sourceRect")) {
                auto& r = j["sourceRect"];
                s->sourceRect = { r[0].get<float>(), r[1].get<float>(),
                                  r[2].get<float>(), r[3].get<float>() };
            }
            if (j.contains("size")) {
                auto& v = j["size"];
                s->size = { v[0].get<float>(), v[1].get<float>() };
            }
            if (j.contains("tint")) {
                auto& c = j["tint"];
                s->tint = Color(c[0].get<float>(), c[1].get<float>(),
                                c[2].get<float>(), c[3].get<float>());
            }
            if (j.contains("flipX"))       s->flipX       = j["flipX"].get<bool>();
            if (j.contains("flipY"))       s->flipY       = j["flipY"].get<bool>();
            if (j.contains("frameWidth"))  s->frameWidth  = j["frameWidth"].get<int>();
            if (j.contains("frameHeight")) s->frameHeight = j["frameHeight"].get<int>();
            if (j.contains("currentFrame"))s->currentFrame= j["currentFrame"].get<int>();
            if (j.contains("totalFrames")) s->totalFrames = j["totalFrames"].get<int>();
            if (j.contains("columns"))     s->columns     = j["columns"].get<int>();
            // Load texture from path after deserialization
            if (!s->texturePath.empty()) {
                s->texture = TextureCache::instance().load(s->texturePath);
            }
        }
    );

    reg.registerComponent<BoxCollider>();
    reg.registerComponent<PlayerController>();
    // Animator: auto-reflect handles currentAnimation/timer/playing,
    // but animations map needs custom serializer
    reg.registerComponent<Animator>(
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const Animator*>(data);
            j["currentAnimation"] = c->currentAnimation;
            j["timer"]            = c->timer;
            j["playing"]          = c->playing;

            auto& animsJ = j["animations"] = nlohmann::json::object();
            for (const auto& [name, anim] : c->animations) {
                nlohmann::json aj = {
                    {"startFrame", anim.startFrame},
                    {"frameCount", anim.frameCount},
                    {"frameRate",  anim.frameRate},
                    {"loop",       anim.loop}
                };
                if (anim.hitFrame >= 0) aj["hitFrame"] = anim.hitFrame;
                animsJ[name] = aj;
            }
        },
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<Animator*>(data);
            if (j.contains("currentAnimation")) c->currentAnimation = j["currentAnimation"].get<std::string>();
            if (j.contains("timer"))            c->timer            = j["timer"].get<float>();
            if (j.contains("playing"))          c->playing          = j["playing"].get<bool>();

            if (j.contains("animations")) {
                c->animations.clear();
                for (auto& [name, aj] : j["animations"].items()) {
                    AnimationDef anim;
                    anim.name = name;
                    if (aj.contains("startFrame")) anim.startFrame = aj["startFrame"].get<int>();
                    if (aj.contains("frameCount")) anim.frameCount = aj["frameCount"].get<int>();
                    if (aj.contains("frameRate"))  anim.frameRate  = aj["frameRate"].get<float>();
                    if (aj.contains("loop"))       anim.loop       = aj["loop"].get<bool>();
                    if (aj.contains("hitFrame"))   anim.hitFrame   = aj["hitFrame"].get<int>();
                    c->animations[name] = anim;
                }
            }
        }
    );
    // PolygonCollider: vector<Vec2> points needs custom serializer
    reg.registerComponent<PolygonCollider>(
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const PolygonCollider*>(data);
            j["isTrigger"] = c->isTrigger;
            j["isStatic"]  = c->isStatic;
            auto& pts = j["points"] = nlohmann::json::array();
            for (const auto& p : c->points) {
                pts.push_back({p.x, p.y});
            }
        },
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<PolygonCollider*>(data);
            if (j.contains("isTrigger")) c->isTrigger = j["isTrigger"].get<bool>();
            if (j.contains("isStatic"))  c->isStatic  = j["isStatic"].get<bool>();
            if (j.contains("points")) {
                c->points.clear();
                for (const auto& pt : j["points"]) {
                    c->points.push_back({pt[0].get<float>(), pt[1].get<float>()});
                }
            }
        }
    );

    // ----- Zone / portal components -----
    reg.registerComponent<ZoneComponent>();
    reg.registerComponent<PortalComponent>();
    // SpawnZoneComponent registered below with custom serializer

    // ----- Player components -----

    // CharacterStatsComponent: wraps CharacterStats with many public fields
    reg.registerComponent<CharacterStatsComponent>(
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const CharacterStatsComponent*>(data);
            const auto& s = c->stats;
            j["characterId"]   = s.characterId;
            j["characterName"] = s.characterName;
            j["className"]     = s.className;
            j["classType"]     = static_cast<uint8_t>(s.classDef.classType);
            j["level"]         = s.level;
            j["currentHP"]     = s.currentHP;
            j["maxHP"]         = s.maxHP;
            j["currentMP"]     = s.currentMP;
            j["maxMP"]         = s.maxMP;
            j["currentFury"]   = s.currentFury;
            j["maxFury"]       = s.maxFury;
            j["currentXP"]     = s.currentXP;
            j["xpToNextLevel"] = s.xpToNextLevel;
            j["honor"]         = s.honor;
            j["pvpKills"]      = s.pvpKills;
            j["pvpDeaths"]     = s.pvpDeaths;
            j["pkStatus"]      = static_cast<uint8_t>(s.pkStatus);
            j["isDead"]        = s.isDead;
        },
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<CharacterStatsComponent*>(data);
            auto& s = c->stats;
            if (j.contains("characterId"))   s.characterId   = j["characterId"].get<std::string>();
            if (j.contains("characterName")) s.characterName = j["characterName"].get<std::string>();
            if (j.contains("className"))     s.className     = j["className"].get<std::string>();
            if (j.contains("classType"))     s.classDef.classType = static_cast<ClassType>(j["classType"].get<uint8_t>());
            if (j.contains("level"))         s.level         = j["level"].get<int>();
            if (j.contains("currentHP"))     s.currentHP     = j["currentHP"].get<int>();
            if (j.contains("maxHP"))         s.maxHP         = j["maxHP"].get<int>();
            if (j.contains("currentMP"))     s.currentMP     = j["currentMP"].get<int>();
            if (j.contains("maxMP"))         s.maxMP         = j["maxMP"].get<int>();
            if (j.contains("currentFury"))   s.currentFury   = j["currentFury"].get<float>();
            if (j.contains("maxFury"))       s.maxFury       = j["maxFury"].get<int>();
            if (j.contains("currentXP"))     s.currentXP     = j["currentXP"].get<int64_t>();
            if (j.contains("xpToNextLevel")) s.xpToNextLevel = j["xpToNextLevel"].get<int64_t>();
            if (j.contains("honor"))         s.honor         = j["honor"].get<int>();
            if (j.contains("pvpKills"))      s.pvpKills      = j["pvpKills"].get<int>();
            if (j.contains("pvpDeaths"))     s.pvpDeaths     = j["pvpDeaths"].get<int>();
            if (j.contains("pkStatus"))      s.pkStatus      = static_cast<PKStatus>(j["pkStatus"].get<uint8_t>());
            if (j.contains("isDead"))        s.isDead        = j["isDead"].get<bool>();
        }
    );

    reg.registerComponent<CombatControllerComponent>();
    reg.registerComponent<DamageableComponent>();

    // InventoryComponent: wraps Inventory with private slots/equipment/gold
    reg.registerComponent<InventoryComponent>(
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const InventoryComponent*>(data);
            j["gold"] = c->inventory.getGold();

            auto& slotsJ = j["slots"] = nlohmann::json::array();
            for (const auto& item : c->inventory.getSlots()) {
                if (!item.isValid()) { slotsJ.push_back(nullptr); continue; }
                nlohmann::json ij;
                ij["instanceId"]   = item.instanceId;
                ij["itemId"]       = item.itemId;
                ij["quantity"]     = item.quantity;
                ij["enchantLevel"] = item.enchantLevel;
                ij["isProtected"]  = item.isProtected;
                ij["isSoulbound"]  = item.isSoulbound;
                ij["boundTo"]      = item.boundToCharacterId;
                ij["statEnchantType"]  = static_cast<uint8_t>(item.statEnchantType);
                ij["statEnchantValue"] = item.statEnchantValue;
                slotsJ.push_back(ij);
            }

            auto& equipJ = j["equipment"] = nlohmann::json::object();
            for (const auto& [slot, item] : c->inventory.getEquipmentMap()) {
                if (!item.isValid()) continue;
                nlohmann::json ij;
                ij["instanceId"]   = item.instanceId;
                ij["itemId"]       = item.itemId;
                ij["quantity"]     = item.quantity;
                ij["enchantLevel"] = item.enchantLevel;
                ij["isProtected"]  = item.isProtected;
                ij["isSoulbound"]  = item.isSoulbound;
                ij["statEnchantType"]  = static_cast<uint8_t>(item.statEnchantType);
                ij["statEnchantValue"] = item.statEnchantValue;
                equipJ[std::to_string(static_cast<uint8_t>(slot))] = ij;
            }
        },
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<InventoryComponent*>(data);
            int64_t gold = 0;
            if (j.contains("gold")) gold = j["gold"].get<int64_t>();

            std::vector<ItemInstance> slots;
            if (j.contains("slots")) {
                for (const auto& ij : j["slots"]) {
                    if (ij.is_null()) { slots.emplace_back(); continue; }
                    ItemInstance item;
                    if (ij.contains("instanceId"))   item.instanceId   = ij["instanceId"].get<std::string>();
                    if (ij.contains("itemId"))       item.itemId       = ij["itemId"].get<std::string>();
                    if (ij.contains("quantity"))      item.quantity     = ij["quantity"].get<int>();
                    if (ij.contains("enchantLevel"))  item.enchantLevel = ij["enchantLevel"].get<int>();
                    if (ij.contains("isProtected"))   item.isProtected  = ij["isProtected"].get<bool>();
                    if (ij.contains("isSoulbound"))   item.isSoulbound  = ij["isSoulbound"].get<bool>();
                    if (ij.contains("boundTo"))       item.boundToCharacterId = ij["boundTo"].get<std::string>();
                    if (ij.contains("statEnchantType"))  item.statEnchantType  = static_cast<StatType>(ij["statEnchantType"].get<uint8_t>());
                    if (ij.contains("statEnchantValue")) item.statEnchantValue = ij["statEnchantValue"].get<int>();
                    slots.push_back(std::move(item));
                }
            }

            std::unordered_map<EquipmentSlot, ItemInstance> equipment;
            if (j.contains("equipment")) {
                for (auto& [key, ij] : j["equipment"].items()) {
                    auto slot = static_cast<EquipmentSlot>(std::stoi(key));
                    ItemInstance item;
                    if (ij.contains("instanceId"))   item.instanceId   = ij["instanceId"].get<std::string>();
                    if (ij.contains("itemId"))       item.itemId       = ij["itemId"].get<std::string>();
                    if (ij.contains("quantity"))      item.quantity     = ij["quantity"].get<int>();
                    if (ij.contains("enchantLevel"))  item.enchantLevel = ij["enchantLevel"].get<int>();
                    if (ij.contains("isProtected"))   item.isProtected  = ij["isProtected"].get<bool>();
                    if (ij.contains("isSoulbound"))   item.isSoulbound  = ij["isSoulbound"].get<bool>();
                    if (ij.contains("statEnchantType"))  item.statEnchantType  = static_cast<StatType>(ij["statEnchantType"].get<uint8_t>());
                    if (ij.contains("statEnchantValue")) item.statEnchantValue = ij["statEnchantValue"].get<int>();
                    equipment[slot] = std::move(item);
                }
            }
            c->inventory.setSerializedState(gold, std::move(slots), std::move(equipment));
        }
    );

    // SkillManagerComponent: wraps SkillManager with private learned skills and bar
    reg.registerComponent<SkillManagerComponent>(
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const SkillManagerComponent*>(data);
            j["availablePoints"] = c->skills.availablePoints();
            j["earnedPoints"]    = c->skills.earnedPoints();
            j["spentPoints"]     = c->skills.spentPoints();

            auto& skillsJ = j["learnedSkills"] = nlohmann::json::array();
            for (const auto& sk : c->skills.getLearnedSkills()) {
                skillsJ.push_back({
                    {"skillId",       sk.skillId},
                    {"unlockedRank",  sk.unlockedRank},
                    {"activatedRank", sk.activatedRank}
                });
            }
            j["skillBar"] = c->skills.getSkillBarSlots();
        },
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<SkillManagerComponent*>(data);
            int avail = 0, earned = 0, spent = 0;
            if (j.contains("availablePoints")) avail  = j["availablePoints"].get<int>();
            if (j.contains("earnedPoints"))    earned = j["earnedPoints"].get<int>();
            if (j.contains("spentPoints"))     spent  = j["spentPoints"].get<int>();

            std::vector<LearnedSkill> skills;
            if (j.contains("learnedSkills")) {
                for (const auto& sk : j["learnedSkills"]) {
                    LearnedSkill ls;
                    if (sk.contains("skillId"))       ls.skillId       = sk["skillId"].get<std::string>();
                    if (sk.contains("unlockedRank"))   ls.unlockedRank  = sk["unlockedRank"].get<int>();
                    if (sk.contains("activatedRank"))  ls.activatedRank = sk["activatedRank"].get<int>();
                    skills.push_back(std::move(ls));
                }
            }
            std::vector<std::string> bar;
            if (j.contains("skillBar")) bar = j["skillBar"].get<std::vector<std::string>>();

            c->skills.setSerializedState(std::move(skills), std::move(bar), avail, earned, spent);
        }
    );

    // StatusEffectComponent & CrowdControlComponent: runtime-only transient state
    // (effects expire, not meaningful to persist across saves)
    reg.registerComponent<StatusEffectComponent>();
    reg.registerComponent<CrowdControlComponent>();
    reg.registerComponent<TargetingComponent>();
    reg.registerComponent<EquipVisualsComponent>();
    reg.registerComponent<ChatComponent>();
    reg.registerComponent<GuildComponent>();
    reg.registerComponent<PartyComponent>();
    reg.registerComponent<FriendsComponent>();
    reg.registerComponent<MarketComponent>();
    reg.registerComponent<TradeComponent>();
    reg.registerComponent<NameplateComponent>();

    // ----- Mob / Enemy components -----
    reg.registerComponent<EnemyStatsComponent>(
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const EnemyStatsComponent*>(data);
            const auto& s = c->stats;
            j["enemyId"]         = s.enemyId;
            j["enemyName"]       = s.enemyName;
            j["monsterType"]     = s.monsterType;
            j["level"]           = s.level;
            j["currentHP"]       = s.currentHP;
            j["maxHP"]           = s.maxHP;
            j["baseDamage"]      = s.baseDamage;
            j["armor"]           = s.armor;
            j["magicResist"]     = s.magicResist;
            j["critRate"]        = s.critRate;
            j["attackSpeed"]     = s.attackSpeed;
            j["moveSpeed"]       = s.moveSpeed;
            j["xpReward"]        = s.xpReward;
            j["isAggressive"]    = s.isAggressive;
            j["honorReward"]     = s.honorReward;
            j["lootTableId"]     = s.lootTableId;
            j["minGoldDrop"]     = s.minGoldDrop;
            j["maxGoldDrop"]     = s.maxGoldDrop;
            j["goldDropChance"]  = s.goldDropChance;
        },
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<EnemyStatsComponent*>(data);
            auto& s = c->stats;
            if (j.contains("enemyId"))        s.enemyId        = j["enemyId"].get<std::string>();
            if (j.contains("enemyName"))      s.enemyName      = j["enemyName"].get<std::string>();
            if (j.contains("monsterType"))    s.monsterType    = j["monsterType"].get<std::string>();
            if (j.contains("level"))          s.level          = j["level"].get<int>();
            if (j.contains("currentHP"))      s.currentHP      = j["currentHP"].get<int>();
            if (j.contains("maxHP"))          s.maxHP          = j["maxHP"].get<int>();
            if (j.contains("baseDamage"))     s.baseDamage     = j["baseDamage"].get<int>();
            if (j.contains("armor"))          s.armor          = j["armor"].get<int>();
            if (j.contains("magicResist"))    s.magicResist    = j["magicResist"].get<int>();
            if (j.contains("critRate"))       s.critRate       = j["critRate"].get<float>();
            if (j.contains("attackSpeed"))    s.attackSpeed    = j["attackSpeed"].get<float>();
            if (j.contains("moveSpeed"))      s.moveSpeed      = j["moveSpeed"].get<float>();
            if (j.contains("xpReward"))       s.xpReward       = j["xpReward"].get<int>();
            if (j.contains("isAggressive"))   s.isAggressive   = j["isAggressive"].get<bool>();
            if (j.contains("honorReward"))    s.honorReward    = j["honorReward"].get<int>();
            if (j.contains("lootTableId"))    s.lootTableId    = j["lootTableId"].get<std::string>();
            if (j.contains("minGoldDrop"))    s.minGoldDrop    = j["minGoldDrop"].get<int>();
            if (j.contains("maxGoldDrop"))    s.maxGoldDrop    = j["maxGoldDrop"].get<int>();
            if (j.contains("goldDropChance")) s.goldDropChance = j["goldDropChance"].get<float>();
        }
    );
    reg.registerComponent<MobAIComponent>();
    reg.registerComponent<MobNameplateComponent>();

    // ----- NPC components -----
    reg.registerComponent<NPCComponent>(
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const NPCComponent*>(data);
            j["npcId"]              = c->npcId;
            j["displayName"]       = c->displayName;
            j["dialogueGreeting"]  = c->dialogueGreeting;
            j["sceneId"]           = c->sceneId;
            j["interactionRadius"] = c->interactionRadius;
            j["faceDirection"]     = static_cast<uint8_t>(c->faceDirection);
        },
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<NPCComponent*>(data);
            if (j.contains("npcId"))              c->npcId              = j["npcId"].get<uint32_t>();
            if (j.contains("displayName"))        c->displayName        = j["displayName"].get<std::string>();
            if (j.contains("dialogueGreeting"))   c->dialogueGreeting   = j["dialogueGreeting"].get<std::string>();
            if (j.contains("sceneId"))            c->sceneId            = j["sceneId"].get<std::string>();
            if (j.contains("interactionRadius"))  c->interactionRadius  = j["interactionRadius"].get<float>();
            if (j.contains("faceDirection"))       c->faceDirection      = static_cast<FaceDirection>(j["faceDirection"].get<uint8_t>());
        }
    );

    // QuestGiverComponent: vector<uint32_t> questIds
    reg.registerComponent<QuestGiverComponent>(
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const QuestGiverComponent*>(data);
            j["questIds"] = c->questIds;
        },
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<QuestGiverComponent*>(data);
            if (j.contains("questIds"))
                c->questIds = j["questIds"].get<std::vector<uint32_t>>();
        }
    );

    // QuestMarkerComponent: runtime-computed state, minimal serializer
    reg.registerComponent<QuestMarkerComponent>(
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const QuestMarkerComponent*>(data);
            j["currentState"] = static_cast<uint8_t>(c->currentState);
            j["highestTier"]  = static_cast<uint8_t>(c->highestTier);
        },
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<QuestMarkerComponent*>(data);
            if (j.contains("currentState"))
                c->currentState = static_cast<MarkerState>(j["currentState"].get<uint8_t>());
            if (j.contains("highestTier"))
                c->highestTier = static_cast<QuestTier>(j["highestTier"].get<uint8_t>());
        }
    );

    // ShopComponent: string shopName, vector<ShopItem> inventory
    reg.registerComponent<ShopComponent>(
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const ShopComponent*>(data);
            j["shopName"] = c->shopName;
            auto& inv = j["inventory"] = nlohmann::json::array();
            for (const auto& item : c->inventory) {
                inv.push_back({
                    {"itemId",    item.itemId},
                    {"itemName",  item.itemName},
                    {"buyPrice",  item.buyPrice},
                    {"sellPrice", item.sellPrice},
                    {"stock",     item.stock}
                });
            }
        },
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<ShopComponent*>(data);
            if (j.contains("shopName")) c->shopName = j["shopName"].get<std::string>();
            if (j.contains("inventory")) {
                c->inventory.clear();
                for (const auto& item : j["inventory"]) {
                    ShopItem si;
                    if (item.contains("itemId"))    si.itemId    = item["itemId"].get<std::string>();
                    if (item.contains("itemName"))  si.itemName  = item["itemName"].get<std::string>();
                    if (item.contains("buyPrice"))  si.buyPrice  = item["buyPrice"].get<int64_t>();
                    if (item.contains("sellPrice")) si.sellPrice = item["sellPrice"].get<int64_t>();
                    if (item.contains("stock"))     si.stock     = item["stock"].get<uint16_t>();
                    c->inventory.push_back(std::move(si));
                }
            }
        }
    );

    // SkillTrainerComponent: ClassType trainerClass, vector<TrainableSkill> skills
    reg.registerComponent<SkillTrainerComponent>(
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const SkillTrainerComponent*>(data);
            j["trainerClass"] = static_cast<uint8_t>(c->trainerClass);
            auto& sk = j["skills"] = nlohmann::json::array();
            for (const auto& s : c->skills) {
                sk.push_back({
                    {"skillId",        s.skillId},
                    {"requiredLevel",  s.requiredLevel},
                    {"goldCost",       s.goldCost},
                    {"skillPointCost", s.skillPointCost},
                    {"requiredClass",  static_cast<uint8_t>(s.requiredClass)}
                });
            }
        },
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<SkillTrainerComponent*>(data);
            if (j.contains("trainerClass"))
                c->trainerClass = static_cast<ClassType>(j["trainerClass"].get<uint8_t>());
            if (j.contains("skills")) {
                c->skills.clear();
                for (const auto& s : j["skills"]) {
                    TrainableSkill ts;
                    if (s.contains("skillId"))        ts.skillId        = s["skillId"].get<std::string>();
                    if (s.contains("requiredLevel"))  ts.requiredLevel  = s["requiredLevel"].get<uint16_t>();
                    if (s.contains("goldCost"))       ts.goldCost       = s["goldCost"].get<int64_t>();
                    if (s.contains("skillPointCost")) ts.skillPointCost = s["skillPointCost"].get<uint16_t>();
                    if (s.contains("requiredClass"))  ts.requiredClass  = static_cast<ClassType>(s["requiredClass"].get<uint8_t>());
                    c->skills.push_back(std::move(ts));
                }
            }
        }
    );

    // BankerComponent: simple fields, auto-serialized via FATE_REFLECT
    reg.registerComponent<BankerComponent>();

    // GuildNPCComponent: simple fields, auto-serialized via FATE_REFLECT
    reg.registerComponent<GuildNPCComponent>();

    // TeleporterComponent: vector<TeleportDestination> destinations
    reg.registerComponent<TeleporterComponent>(
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const TeleporterComponent*>(data);
            auto& dests = j["destinations"] = nlohmann::json::array();
            for (const auto& d : c->destinations) {
                dests.push_back({
                    {"destinationName", d.destinationName},
                    {"sceneId",         d.sceneId},
                    {"targetPosition",  {d.targetPosition.x, d.targetPosition.y}},
                    {"cost",            d.cost},
                    {"requiredLevel",   d.requiredLevel}
                });
            }
        },
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<TeleporterComponent*>(data);
            if (j.contains("destinations")) {
                c->destinations.clear();
                for (const auto& d : j["destinations"]) {
                    TeleportDestination td;
                    if (d.contains("destinationName")) td.destinationName = d["destinationName"].get<std::string>();
                    if (d.contains("sceneId"))         td.sceneId         = d["sceneId"].get<std::string>();
                    if (d.contains("targetPosition")) {
                        auto& p = d["targetPosition"];
                        td.targetPosition = { p[0].get<float>(), p[1].get<float>() };
                    }
                    if (d.contains("cost"))          td.cost          = d["cost"].get<int64_t>();
                    if (d.contains("requiredLevel")) td.requiredLevel = d["requiredLevel"].get<uint16_t>();
                    c->destinations.push_back(std::move(td));
                }
            }
        }
    );

    // StoryNPCComponent: vector<DialogueNode> dialogueTree, uint32_t rootNodeId
    reg.registerComponent<StoryNPCComponent>(
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const StoryNPCComponent*>(data);
            j["rootNodeId"] = c->rootNodeId;
            auto& tree = j["dialogueTree"] = nlohmann::json::array();
            for (const auto& node : c->dialogueTree) {
                nlohmann::json nodeJ;
                nodeJ["nodeId"]  = node.nodeId;
                nodeJ["npcText"] = node.npcText;
                auto& choices = nodeJ["choices"] = nlohmann::json::array();
                for (const auto& ch : node.choices) {
                    choices.push_back({
                        {"buttonText", ch.buttonText},
                        {"nextNodeId", ch.nextNodeId},
                        {"onSelect", {
                            {"action",   static_cast<uint8_t>(ch.onSelect.action)},
                            {"targetId", ch.onSelect.targetId},
                            {"value",    ch.onSelect.value}
                        }}
                    });
                }
                nodeJ["condition"] = {
                    {"condition", static_cast<uint8_t>(node.condition.condition)},
                    {"targetId",  node.condition.targetId},
                    {"value",     node.condition.value}
                };
                tree.push_back(std::move(nodeJ));
            }
        },
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<StoryNPCComponent*>(data);
            if (j.contains("rootNodeId")) c->rootNodeId = j["rootNodeId"].get<uint32_t>();
            if (j.contains("dialogueTree")) {
                c->dialogueTree.clear();
                for (const auto& nodeJ : j["dialogueTree"]) {
                    DialogueNode node;
                    if (nodeJ.contains("nodeId"))  node.nodeId  = nodeJ["nodeId"].get<uint32_t>();
                    if (nodeJ.contains("npcText")) node.npcText = nodeJ["npcText"].get<std::string>();
                    if (nodeJ.contains("choices")) {
                        for (const auto& chJ : nodeJ["choices"]) {
                            DialogueChoice ch;
                            if (chJ.contains("buttonText")) ch.buttonText = chJ["buttonText"].get<std::string>();
                            if (chJ.contains("nextNodeId")) ch.nextNodeId = chJ["nextNodeId"].get<uint32_t>();
                            if (chJ.contains("onSelect")) {
                                auto& sel = chJ["onSelect"];
                                if (sel.contains("action"))   ch.onSelect.action   = static_cast<DialogueAction>(sel["action"].get<uint8_t>());
                                if (sel.contains("targetId")) ch.onSelect.targetId = sel["targetId"].get<std::string>();
                                if (sel.contains("value"))    ch.onSelect.value    = sel["value"].get<int32_t>();
                            }
                            node.choices.push_back(std::move(ch));
                        }
                    }
                    if (nodeJ.contains("condition")) {
                        auto& cond = nodeJ["condition"];
                        if (cond.contains("condition")) node.condition.condition = static_cast<DialogueCondition>(cond["condition"].get<uint8_t>());
                        if (cond.contains("targetId"))  node.condition.targetId  = cond["targetId"].get<std::string>();
                        if (cond.contains("value"))     node.condition.value     = cond["value"].get<int32_t>();
                    }
                    c->dialogueTree.push_back(std::move(node));
                }
            }
        }
    );

    // ----- Player quest / bank -----

    // QuestComponent: QuestManager quests (active quests + completed IDs)
    reg.registerComponent<QuestComponent>(
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const QuestComponent*>(data);
            j["completedQuestIds"] = c->quests.getCompletedQuestIds();
            auto& active = j["activeQuests"] = nlohmann::json::array();
            for (const auto& aq : c->quests.getActiveQuests()) {
                nlohmann::json aqJ;
                aqJ["questId"] = aq.questId;
                aqJ["progress"] = aq.objectiveProgress;
                active.push_back(std::move(aqJ));
            }
        },
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<QuestComponent*>(data);
            std::vector<uint32_t> completed;
            std::vector<ActiveQuest> active;
            if (j.contains("completedQuestIds"))
                completed = j["completedQuestIds"].get<std::vector<uint32_t>>();
            if (j.contains("activeQuests")) {
                for (const auto& aqJ : j["activeQuests"]) {
                    ActiveQuest aq;
                    if (aqJ.contains("questId"))  aq.questId = aqJ["questId"].get<uint32_t>();
                    if (aqJ.contains("progress")) aq.objectiveProgress = aqJ["progress"].get<std::vector<uint16_t>>();
                    active.push_back(std::move(aq));
                }
            }
            c->quests.setSerializedState(std::move(completed), std::move(active));
        }
    );

    // BankStorageComponent: BankStorage (storedGold, maxSlots, items)
    reg.registerComponent<BankStorageComponent>(
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const BankStorageComponent*>(data);
            j["storedGold"] = c->storage.getStoredGold();
            j["maxSlots"]   = c->storage.getMaxSlots();
            auto& items = j["items"] = nlohmann::json::array();
            for (const auto& item : c->storage.getItems()) {
                items.push_back({
                    {"itemId", item.itemId},
                    {"count",  item.count}
                });
            }
        },
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<BankStorageComponent*>(data);
            int64_t gold = 0;
            uint16_t maxSlots = 30;
            std::vector<StoredItem> items;
            if (j.contains("storedGold")) gold     = j["storedGold"].get<int64_t>();
            if (j.contains("maxSlots"))   maxSlots = j["maxSlots"].get<uint16_t>();
            if (j.contains("items")) {
                for (const auto& itemJ : j["items"]) {
                    StoredItem si;
                    if (itemJ.contains("itemId")) si.itemId = itemJ["itemId"].get<std::string>();
                    if (itemJ.contains("count"))  si.count  = itemJ["count"].get<uint16_t>();
                    items.push_back(std::move(si));
                }
            }
            c->storage.setSerializedState(gold, maxSlots, std::move(items));
        }
    );

    // ----- Faction & Pet components -----
    reg.registerComponent<FactionComponent>(
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const FactionComponent*>(data);
            j["faction"] = static_cast<uint8_t>(c->faction);
        },
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<FactionComponent*>(data);
            if (j.contains("faction"))
                c->faction = static_cast<Faction>(j["faction"].get<uint8_t>());
        }
    );

    reg.registerComponent<PetComponent>(
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const PetComponent*>(data);
            j["autoLootRadius"] = c->autoLootRadius;
            if (c->hasPet()) {
                const auto& pet = c->equippedPet;
                j["pet"] = {
                    {"instanceId",       pet.instanceId},
                    {"petDefinitionId",  pet.petDefinitionId},
                    {"petName",          pet.petName},
                    {"level",            pet.level},
                    {"currentXP",        pet.currentXP},
                    {"xpToNextLevel",    pet.xpToNextLevel},
                    {"autoLootEnabled",  pet.autoLootEnabled},
                    {"isSoulbound",      pet.isSoulbound}
                };
            }
        },
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<PetComponent*>(data);
            if (j.contains("autoLootRadius")) c->autoLootRadius = j["autoLootRadius"].get<float>();
            if (j.contains("pet")) {
                const auto& p = j["pet"];
                auto& pet = c->equippedPet;
                if (p.contains("instanceId"))       pet.instanceId       = p["instanceId"].get<std::string>();
                if (p.contains("petDefinitionId"))   pet.petDefinitionId  = p["petDefinitionId"].get<std::string>();
                if (p.contains("petName"))           pet.petName          = p["petName"].get<std::string>();
                if (p.contains("level"))             pet.level            = p["level"].get<int>();
                if (p.contains("currentXP"))         pet.currentXP        = p["currentXP"].get<int64_t>();
                if (p.contains("xpToNextLevel"))     pet.xpToNextLevel    = p["xpToNextLevel"].get<int64_t>();
                if (p.contains("autoLootEnabled"))   pet.autoLootEnabled  = p["autoLootEnabled"].get<bool>();
                if (p.contains("isSoulbound"))       pet.isSoulbound      = p["isSoulbound"].get<bool>();
            }
        }
    );

    // ----- SpawnZoneComponent -----
    reg.registerComponent<SpawnZoneComponent>(
        // toJson
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const SpawnZoneComponent*>(data);
            j["zoneName"]          = c->config.zoneName;
            j["size"]              = { c->config.size.x, c->config.size.y };
            j["minSpawnDistance"]   = c->config.minSpawnDistance;
            j["maxSpawnAttempts"]   = c->config.maxSpawnAttempts;
            j["serverTickInterval"] = c->config.serverTickInterval;
            j["showBounds"]        = c->showBounds;

            nlohmann::json rulesJ = nlohmann::json::array();
            for (const auto& r : c->config.rules) {
                nlohmann::json rj;
                rj["enemyId"]        = r.enemyId;
                rj["targetCount"]    = r.targetCount;
                rj["minLevel"]       = r.minLevel;
                rj["maxLevel"]       = r.maxLevel;
                rj["baseHP"]         = r.baseHP;
                rj["baseDamage"]     = r.baseDamage;
                rj["isAggressive"]   = r.isAggressive;
                rj["isBoss"]         = r.isBoss;
                rj["respawnSeconds"] = r.respawnSeconds;
                rulesJ.push_back(rj);
            }
            j["rules"] = rulesJ;
        },
        // fromJson
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<SpawnZoneComponent*>(data);
            if (j.contains("zoneName"))          c->config.zoneName          = j["zoneName"].get<std::string>();
            if (j.contains("size"))            { auto& v = j["size"]; c->config.size = { v[0].get<float>(), v[1].get<float>() }; }
            if (j.contains("minSpawnDistance"))   c->config.minSpawnDistance   = j["minSpawnDistance"].get<float>();
            if (j.contains("maxSpawnAttempts"))   c->config.maxSpawnAttempts   = j["maxSpawnAttempts"].get<int>();
            if (j.contains("serverTickInterval")) c->config.serverTickInterval = j["serverTickInterval"].get<float>();
            if (j.contains("showBounds"))        c->showBounds               = j["showBounds"].get<bool>();

            if (j.contains("rules")) {
                c->config.rules.clear();
                for (const auto& rj : j["rules"]) {
                    MobSpawnRule r;
                    if (rj.contains("enemyId"))        r.enemyId        = rj["enemyId"].get<std::string>();
                    if (rj.contains("targetCount"))    r.targetCount    = rj["targetCount"].get<int>();
                    if (rj.contains("minLevel"))       r.minLevel       = rj["minLevel"].get<int>();
                    if (rj.contains("maxLevel"))       r.maxLevel       = rj["maxLevel"].get<int>();
                    if (rj.contains("baseHP"))         r.baseHP         = rj["baseHP"].get<int>();
                    if (rj.contains("baseDamage"))     r.baseDamage     = rj["baseDamage"].get<int>();
                    if (rj.contains("isAggressive"))   r.isAggressive   = rj["isAggressive"].get<bool>();
                    if (rj.contains("isBoss"))         r.isBoss         = rj["isBoss"].get<bool>();
                    if (rj.contains("respawnSeconds")) r.respawnSeconds = rj["respawnSeconds"].get<float>();
                    c->config.rules.push_back(r);
                }
            }
        }
    );

    // ----- Particle & Lighting components -----

    // ParticleEmitterComponent: EmitterConfig fields serialized; texture as path string
    reg.registerComponent<ParticleEmitterComponent>(
        // toJson
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const ParticleEmitterComponent*>(data);
            const auto& cfg = c->emitter.config();
            j["spawnRate"]         = cfg.spawnRate;
            j["burstCount"]        = cfg.burstCount;
            j["velocityMin"]       = { cfg.velocityMin.x, cfg.velocityMin.y };
            j["velocityMax"]       = { cfg.velocityMax.x, cfg.velocityMax.y };
            j["lifetimeMin"]       = cfg.lifetimeMin;
            j["lifetimeMax"]       = cfg.lifetimeMax;
            j["sizeMin"]           = cfg.sizeMin;
            j["sizeMax"]           = cfg.sizeMax;
            j["rotationSpeedMin"]  = cfg.rotationSpeedMin;
            j["rotationSpeedMax"]  = cfg.rotationSpeedMax;
            j["colorStart"]        = { cfg.colorStart.r, cfg.colorStart.g, cfg.colorStart.b, cfg.colorStart.a };
            j["colorEnd"]          = { cfg.colorEnd.r,   cfg.colorEnd.g,   cfg.colorEnd.b,   cfg.colorEnd.a   };
            j["gravity"]           = { cfg.gravity.x, cfg.gravity.y };
            j["depth"]             = cfg.depth;
            j["worldSpace"]        = cfg.worldSpace;
            j["additiveBlend"]     = cfg.additiveBlend;
            j["autoDestroy"]       = c->autoDestroy;
        },
        // fromJson
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<ParticleEmitterComponent*>(data);
            EmitterConfig cfg;
            if (j.contains("spawnRate"))    cfg.spawnRate    = j["spawnRate"].get<float>();
            if (j.contains("burstCount"))   cfg.burstCount   = j["burstCount"].get<int>();
            if (j.contains("velocityMin")) { auto& v = j["velocityMin"]; cfg.velocityMin = { v[0].get<float>(), v[1].get<float>() }; }
            if (j.contains("velocityMax")) { auto& v = j["velocityMax"]; cfg.velocityMax = { v[0].get<float>(), v[1].get<float>() }; }
            if (j.contains("lifetimeMin"))  cfg.lifetimeMin  = j["lifetimeMin"].get<float>();
            if (j.contains("lifetimeMax"))  cfg.lifetimeMax  = j["lifetimeMax"].get<float>();
            if (j.contains("sizeMin"))      cfg.sizeMin      = j["sizeMin"].get<float>();
            if (j.contains("sizeMax"))      cfg.sizeMax      = j["sizeMax"].get<float>();
            if (j.contains("rotationSpeedMin")) cfg.rotationSpeedMin = j["rotationSpeedMin"].get<float>();
            if (j.contains("rotationSpeedMax")) cfg.rotationSpeedMax = j["rotationSpeedMax"].get<float>();
            if (j.contains("colorStart")) { auto& c2 = j["colorStart"]; cfg.colorStart = Color(c2[0].get<float>(), c2[1].get<float>(), c2[2].get<float>(), c2[3].get<float>()); }
            if (j.contains("colorEnd"))   { auto& c2 = j["colorEnd"];   cfg.colorEnd   = Color(c2[0].get<float>(), c2[1].get<float>(), c2[2].get<float>(), c2[3].get<float>()); }
            if (j.contains("gravity"))    { auto& g = j["gravity"]; cfg.gravity = { g[0].get<float>(), g[1].get<float>() }; }
            if (j.contains("depth"))        cfg.depth        = j["depth"].get<float>();
            if (j.contains("worldSpace"))   cfg.worldSpace   = j["worldSpace"].get<bool>();
            if (j.contains("additiveBlend")) cfg.additiveBlend = j["additiveBlend"].get<bool>();
            if (j.contains("autoDestroy"))  c->autoDestroy   = j["autoDestroy"].get<bool>();
            c->emitter.init(cfg);
        }
    );

    // PointLightComponent: all PointLight fields serialized
    reg.registerComponent<PointLightComponent>(
        // toJson
        [](const void* data, nlohmann::json& j) {
            const auto* c = static_cast<const PointLightComponent*>(data);
            j["position"]  = { c->light.position.x, c->light.position.y };
            j["color"]     = { c->light.color.r, c->light.color.g, c->light.color.b, c->light.color.a };
            j["radius"]    = c->light.radius;
            j["intensity"] = c->light.intensity;
            j["falloff"]   = c->light.falloff;
        },
        // fromJson
        [](const nlohmann::json& j, void* data) {
            auto* c = static_cast<PointLightComponent*>(data);
            if (j.contains("position")) { auto& p = j["position"]; c->light.position = { p[0].get<float>(), p[1].get<float>() }; }
            if (j.contains("color"))    { auto& col = j["color"];   c->light.color = Color(col[0].get<float>(), col[1].get<float>(), col[2].get<float>(), col[3].get<float>()); }
            if (j.contains("radius"))    c->light.radius    = j["radius"].get<float>();
            if (j.contains("intensity")) c->light.intensity = j["intensity"].get<float>();
            if (j.contains("falloff"))   c->light.falloff   = j["falloff"].get<float>();
        }
    );

    // ----- Dropped item component -----
    reg.registerComponent<DroppedItemComponent>();

    // ----- Boss spawn point component -----
    reg.registerComponent<BossSpawnPointComponent>();

    // ----- Player respawn point component -----
    reg.registerComponent<SpawnPointComponent>();

    // ----- Backward-compat aliases -----
    reg.registerAlias("Sprite", "SpriteComponent");
}

} // namespace fate

FATE_REFLECT_EMPTY(fate::DroppedItemComponent)
FATE_REFLECT_EMPTY(fate::BossSpawnPointComponent)
