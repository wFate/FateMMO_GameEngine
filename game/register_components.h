#pragma once

// register_components.h — explicit component registration with trait overrides
// Called once at startup from GameApp::onInit()

#include "engine/ecs/component_meta.h"
#include "engine/ecs/component_traits.h"

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

// --- PlayerController: runtime only (isLocalPlayer is set at spawn) ---
template<> struct component_traits<PlayerController> {
    static constexpr ComponentFlags flags = ComponentFlags::None;
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
            if (j.contains("texturePath")) s->texturePath = j["texturePath"].get<std::string>();
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
            // Note: texture (shared_ptr) is NOT restored here — the asset
            // loader must re-bind it from texturePath after deserialization.
        }
    );

    reg.registerComponent<BoxCollider>();
    reg.registerComponent<PlayerController>();
    reg.registerComponent<Animator>();
    reg.registerComponent<PolygonCollider>();

    // ----- Zone / portal components -----
    reg.registerComponent<ZoneComponent>();
    reg.registerComponent<PortalComponent>();
    reg.registerComponent<SpawnZoneComponent>();

    // ----- Player components -----
    reg.registerComponent<CharacterStatsComponent>();
    reg.registerComponent<CombatControllerComponent>();
    reg.registerComponent<DamageableComponent>();
    reg.registerComponent<InventoryComponent>();
    reg.registerComponent<SkillManagerComponent>();
    reg.registerComponent<StatusEffectComponent>();
    reg.registerComponent<CrowdControlComponent>();
    reg.registerComponent<TargetingComponent>();
    reg.registerComponent<ChatComponent>();
    reg.registerComponent<GuildComponent>();
    reg.registerComponent<PartyComponent>();
    reg.registerComponent<FriendsComponent>();
    reg.registerComponent<MarketComponent>();
    reg.registerComponent<TradeComponent>();
    reg.registerComponent<NameplateComponent>();

    // ----- Mob / Enemy components -----
    reg.registerComponent<EnemyStatsComponent>();
    reg.registerComponent<MobAIComponent>();
    reg.registerComponent<MobNameplateComponent>();

    // ----- NPC components -----
    reg.registerComponent<NPCComponent>();

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
    reg.registerComponent<FactionComponent>();
    reg.registerComponent<PetComponent>();

    // ----- Backward-compat aliases -----
    reg.registerAlias("Sprite", "SpriteComponent");
}

} // namespace fate
