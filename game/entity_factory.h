#pragma once
#include "engine/ecs/world.h"
#include "engine/render/texture.h"
#include "stb_image_write.h"
#include <filesystem>
#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "game/components/player_controller.h"
#include "game/components/box_collider.h"
#include "game/components/animator.h"
#include "game/components/game_components.h"
#include "game/components/faction_component.h"
#include "game/components/pet_component.h"
#include "game/shared/game_types.h"

namespace fate {

class EntityFactory {
public:
    /// Create a fully-assembled player entity with all game components.
    /// Mirrors the Unity PlayerScene2 prefab (24 MonoBehaviours).
    static Entity* createPlayer(World& world, const std::string& name, ClassType classType, bool isLocal = false, Faction faction = Faction::None) {
        Entity* player = world.createEntity(name);
        player->setTag("player");

        // --- Engine components ---
        auto* transform = player->addComponent<Transform>(0.0f, 0.0f);
        transform->depth = 10.0f;

        auto* sprite = player->addComponent<SpriteComponent>();
        sprite->texturePath = "assets/sprites/player.png";
        sprite->texture = TextureCache::instance().load(sprite->texturePath);
        if (sprite->texture) {
            sprite->size = {(float)sprite->texture->width(), (float)sprite->texture->height()};
        } else {
            sprite->size = {20.0f, 33.0f};  // Trimmed player sprite default
        }

        auto* collider = player->addComponent<BoxCollider>();
        collider->size = {sprite->size.x - 4.0f, sprite->size.y * 0.5f};
        collider->offset = {0.0f, -sprite->size.y * 0.25f}; // Lower half
        collider->isStatic = false;

        auto* controller = player->addComponent<PlayerController>();
        controller->moveSpeed = 96.0f;
        controller->isLocalPlayer = isLocal;

        // --- Game system components (matching Unity prefab) ---

        // Character Stats
        auto* statsComp = player->addComponent<CharacterStatsComponent>();
        statsComp->stats.characterName = name;
        statsComp->stats.className = (classType == ClassType::Warrior) ? "Warrior" :
                                     (classType == ClassType::Mage) ? "Mage" : "Archer";
        // Configure class definition based on type
        auto& cd = statsComp->stats.classDef;
        cd.classType = classType;
        switch (classType) {
            case ClassType::Warrior:
                cd.displayName = "Warrior";
                cd.baseMaxHP = 70; cd.baseMaxMP = 30;
                cd.baseStrength = 14; cd.baseVitality = 12;
                cd.baseIntelligence = 5; cd.baseDexterity = 8; cd.baseWisdom = 5;
                cd.baseHitRate = 4.0f; cd.attackRange = 1.0f;
                cd.primaryResource = ResourceType::Fury;
                cd.hpPerLevel = 7.0f; cd.mpPerLevel = 2.0f;
                cd.strPerLevel = 0.25f; cd.vitPerLevel = 0.25f;
                cd.intPerLevel = 0.0f; cd.dexPerLevel = 0.0f; cd.wisPerLevel = 0.0f;
                break;
            case ClassType::Mage:
                cd.displayName = "Mage";
                cd.baseMaxHP = 50; cd.baseMaxMP = 150;
                cd.baseStrength = 4; cd.baseVitality = 6;
                cd.baseIntelligence = 16; cd.baseDexterity = 6; cd.baseWisdom = 14;
                cd.baseHitRate = 0.0f; cd.attackRange = 7.0f;
                cd.primaryResource = ResourceType::Mana;
                cd.hpPerLevel = 5.0f; cd.mpPerLevel = 10.0f;
                cd.strPerLevel = 0.0f; cd.vitPerLevel = 0.25f;
                cd.intPerLevel = 0.25f; cd.dexPerLevel = 0.0f; cd.wisPerLevel = 0.25f;
                break;
            case ClassType::Archer:
                cd.displayName = "Archer";
                cd.baseMaxHP = 50; cd.baseMaxMP = 40;
                cd.baseStrength = 8; cd.baseVitality = 9;
                cd.baseIntelligence = 7; cd.baseDexterity = 18; cd.baseWisdom = 8;
                cd.baseHitRate = 4.0f; cd.attackRange = 7.0f;
                cd.primaryResource = ResourceType::Fury;
                cd.hpPerLevel = 5.0f; cd.mpPerLevel = 2.0f;
                cd.strPerLevel = 0.0f; cd.vitPerLevel = 0.25f;
                cd.intPerLevel = 0.0f; cd.dexPerLevel = 0.25f; cd.wisPerLevel = 0.5f;
                break;
        }
        statsComp->stats.level = 1;
        statsComp->stats.recalculateStats();
        statsComp->stats.recalculateXPRequirement();
        statsComp->stats.currentHP = statsComp->stats.maxHP;
        statsComp->stats.currentMP = statsComp->stats.maxMP;

        // Combat Controller
        player->addComponent<CombatControllerComponent>();

        // Damageable marker
        player->addComponent<DamageableComponent>();

        // Inventory
        auto* invComp = player->addComponent<InventoryComponent>();
        invComp->inventory.initialize(name, 0);

        // Skill Manager
        auto* skillComp = player->addComponent<SkillManagerComponent>();
        skillComp->skills.initialize(&statsComp->stats);

        // Status Effects
        player->addComponent<StatusEffectComponent>();

        // Crowd Control
        player->addComponent<CrowdControlComponent>();

        // Targeting
        player->addComponent<TargetingComponent>();

        // Social systems
        auto* chatComp = player->addComponent<ChatComponent>();
        chatComp->chat.initialize(name, name);
        chatComp->chat.localFaction = faction;

        player->addComponent<GuildComponent>();
        player->addComponent<PartyComponent>();

        auto* friendsComp = player->addComponent<FriendsComponent>();
        friendsComp->friends.initialize(name);

        player->addComponent<MarketComponent>();
        player->addComponent<TradeComponent>();

        // Quest & Bank
        player->addComponent<QuestComponent>();
        auto* bankComp = player->addComponent<BankStorageComponent>();
        bankComp->storage.initialize(30);

        // Faction
        auto* factionComp = player->addComponent<FactionComponent>();
        factionComp->faction = faction;

        // Pet (empty by default — no pet equipped)
        player->addComponent<PetComponent>();

        // Nameplate
        auto* nameplate = player->addComponent<NameplateComponent>();
        nameplate->displayName = name;
        nameplate->displayLevel = 1;

        return player;
    }

    /// Create a mob entity with AI and stats.
    /// mobDefId would normally load from MobDefinitionCache (PostgreSQL).
    /// For now, takes parameters directly.
    static Entity* createMob(World& world, const std::string& mobName, int level,
                             int baseHP, int baseDamage, Vec2 spawnPos,
                             bool aggressive = true, bool isBoss = false) {
        Entity* mob = world.createEntity(mobName);
        mob->setTag(isBoss ? "boss" : "mob");

        auto* transform = mob->addComponent<Transform>(spawnPos.x, spawnPos.y);
        transform->depth = 8.0f;

        auto* sprite = mob->addComponent<SpriteComponent>();
        // Placeholder mob sprite - will be loaded from assets
        sprite->size = isBoss ? Vec2{48.0f, 48.0f} : Vec2{32.0f, 32.0f};
        // Try to load mob texture, fall back to generating and saving placeholder
        sprite->texturePath = "assets/sprites/mob_" + mobName + ".png";
        sprite->texture = TextureCache::instance().load(sprite->texturePath);
        if (!sprite->texture) {
            // Generate a colored placeholder and save to disk
            const int SZ = 16;
            std::vector<unsigned char> pixels(SZ * SZ * 4, 0);
            for (int y = 0; y < SZ; y++) {
                for (int x = 0; x < SZ; x++) {
                    float dx = x - 7.5f, dy = y - 7.5f;
                    if (dx*dx + dy*dy < 50.0f) {
                        int i = (y * SZ + x) * 4;
                        pixels[i+0] = isBoss ? 200 : 180;
                        pixels[i+1] = 60;
                        pixels[i+2] = 60;
                        pixels[i+3] = 255;
                    }
                }
            }
            std::filesystem::create_directories("assets/sprites");
            stbi_write_png(sprite->texturePath.c_str(), SZ, SZ, 4, pixels.data(), SZ * 4);
            sprite->texture = TextureCache::instance().load(sprite->texturePath);
        }

        auto* collider = mob->addComponent<BoxCollider>();
        collider->size = sprite->size * 0.8f;
        collider->isStatic = false;
        collider->isTrigger = true; // Mobs don't block each other (TWOM-style)

        // Enemy Stats
        auto* enemyComp = mob->addComponent<EnemyStatsComponent>();
        auto& es = enemyComp->stats;
        es.enemyName = mobName;
        es.level = level;
        es.maxHP = baseHP;
        es.baseDamage = baseDamage;
        es.isAggressive = aggressive;
        es.xpReward = level * 10;
        es.moveSpeed = 1.0f;
        es.monsterType = isBoss ? "Boss" : "Normal";
        es.mobHitRate = isBoss ? 16 : 10;
        es.honorReward = isBoss ? 50 : 0;
        es.minGoldDrop = level * 2;
        es.maxGoldDrop = level * 5;
        es.goldDropChance = 1.0f;  // Always drop gold for testing
        es.initialize();

        // Status Effects (mobs can have DoTs, debuffs too)
        mob->addComponent<StatusEffectComponent>();

        // Damageable marker
        mob->addComponent<DamageableComponent>();

        // Mob AI — all distances and speeds in PIXELS (engine uses pixel coords)
        auto* aiComp = mob->addComponent<MobAIComponent>();
        aiComp->ai.initialize(spawnPos);
        aiComp->ai.acquireRadius   = aggressive ? 4.0f * Coords::TILE_SIZE : 0.0f;  // 4 tiles = 128px
        aiComp->ai.contactRadius   = 6.0f * Coords::TILE_SIZE;                       // 6 tiles = 192px
        aiComp->ai.attackRange     = 1.0f * Coords::TILE_SIZE;                       // 1 tile = 32px
        aiComp->ai.roamRadius      = 3.0f * Coords::TILE_SIZE;                       // 3 tiles = 96px
        aiComp->ai.baseChaseSpeed  = 1.5f * Coords::TILE_SIZE;                       // 1.5 tiles/s = 48px/s
        aiComp->ai.baseReturnSpeed = 1.5f * Coords::TILE_SIZE;                       // 1.5 tiles/s = 48px/s
        aiComp->ai.baseRoamSpeed   = 0.8f * Coords::TILE_SIZE;                       // 0.8 tiles/s = 25.6px/s
        aiComp->ai.stuckThreshold  = 0.05f * Coords::TILE_SIZE;                      // ~1.6px
        aiComp->ai.wiggleDistance  = 1.5f * Coords::TILE_SIZE;                       // 48px
        aiComp->ai.isPassive = !aggressive;
        aiComp->ai.attackCooldown = 1.5f / es.attackSpeed;

        // Mob nameplate
        auto* nameplate = mob->addComponent<MobNameplateComponent>();
        nameplate->displayName = mobName;
        nameplate->level = level;
        nameplate->isBoss = isBoss;

        return mob;
    }

    /// Create an NPC entity from an NPCTemplate.
    /// Adds role-specific components based on template flags.
    static Entity* createNPC(World& world, const NPCTemplate& tmpl) {
        Entity* npc = world.createEntity(tmpl.name);
        npc->setTag("npc");

        // --- Engine components ---
        auto* transform = npc->addComponent<Transform>(tmpl.position.x, tmpl.position.y);
        transform->depth = 9.0f;

        auto* sprite = npc->addComponent<SpriteComponent>();
        sprite->texturePath = tmpl.spriteSheet.empty()
            ? "assets/sprites/npc_default.png"
            : tmpl.spriteSheet;
        sprite->texture = TextureCache::instance().load(sprite->texturePath);
        if (sprite->texture) {
            sprite->size = {(float)sprite->texture->width(), (float)sprite->texture->height()};
        } else {
            sprite->size = {32.0f, 48.0f};  // Default NPC sprite size
        }

        // --- NPC identity ---
        auto* npcComp = npc->addComponent<NPCComponent>();
        npcComp->npcId = tmpl.npcId;
        npcComp->displayName = tmpl.name;
        npcComp->dialogueGreeting = tmpl.dialogueGreeting;
        npcComp->interactionRadius = tmpl.interactionRadius;
        npcComp->faceDirection = tmpl.facing;

        // --- Nameplate with role subtitle ---
        auto* nameplate = npc->addComponent<NameplateComponent>();
        nameplate->displayName = tmpl.name;
        nameplate->showLevel = false;

        // Role subtitle based on priority: quest > merchant > skill trainer > banker > guild > teleporter
        if (tmpl.isQuestGiver)          nameplate->roleSubtitle = "[Quest]";
        else if (tmpl.isMerchant)       nameplate->roleSubtitle = "[Merchant]";
        else if (tmpl.isSkillTrainer)   nameplate->roleSubtitle = "[Skill Trainer]";
        else if (tmpl.isBanker)         nameplate->roleSubtitle = "[Banker]";
        else if (tmpl.isGuildNPC)       nameplate->roleSubtitle = "[Guild]";
        else if (tmpl.isTeleporter)     nameplate->roleSubtitle = "[Teleporter]";

        // --- Conditional role components ---
        if (tmpl.isQuestGiver) {
            auto* questGiver = npc->addComponent<QuestGiverComponent>();
            questGiver->questIds = tmpl.questIds;

            npc->addComponent<QuestMarkerComponent>();
        }

        if (tmpl.isMerchant) {
            auto* shop = npc->addComponent<ShopComponent>();
            shop->shopName = tmpl.shopName;
            shop->inventory = tmpl.shopItems;
        }

        if (tmpl.isSkillTrainer) {
            auto* trainer = npc->addComponent<SkillTrainerComponent>();
            trainer->trainerClass = tmpl.trainerClass;
            trainer->skills = tmpl.trainableSkills;
        }

        if (tmpl.isBanker) {
            auto* banker = npc->addComponent<BankerComponent>();
            banker->storageSlots = tmpl.bankSlots;
            banker->depositFeePercent = tmpl.bankFeePercent;
        }

        if (tmpl.isGuildNPC) {
            auto* guildNpc = npc->addComponent<GuildNPCComponent>();
            guildNpc->creationCost = tmpl.guildCreationCost;
            guildNpc->requiredLevel = tmpl.guildRequiredLevel;
        }

        if (tmpl.isTeleporter) {
            auto* teleporter = npc->addComponent<TeleporterComponent>();
            teleporter->destinations = tmpl.destinations;
        }

        if (tmpl.isStoryNPC) {
            auto* story = npc->addComponent<StoryNPCComponent>();
            story->dialogueTree = tmpl.dialogueTree;
            story->rootNodeId = tmpl.dialogueRootNodeId;
        }

        return npc;
    }

    /// Create a ghost (remote) player entity — minimal visual representation.
    static Entity* createGhostPlayer(World& world, const std::string& name, Vec2 position) {
        Entity* entity = world.createEntity(name);
        entity->setTag("ghost");
        auto* t = entity->addComponent<Transform>(position);
        t->depth = 1.0f;
        auto* sprite = entity->addComponent<SpriteComponent>();
        sprite->size = {32.0f, 32.0f}; // default player size
        auto* np = entity->addComponent<NameplateComponent>();
        np->displayName = name;
        np->visible = true;
        return entity;
    }

    /// Create a ghost (remote) mob entity — minimal visual representation.
    static Entity* createGhostMob(World& world, const std::string& name, Vec2 position) {
        Entity* entity = world.createEntity(name);
        entity->setTag("ghost");
        auto* t = entity->addComponent<Transform>(position);
        t->depth = 1.0f;
        auto* sprite = entity->addComponent<SpriteComponent>();
        sprite->size = {32.0f, 32.0f};
        auto* np = entity->addComponent<MobNameplateComponent>();
        np->displayName = name;
        np->visible = true;
        return entity;
    }
};

} // namespace fate
