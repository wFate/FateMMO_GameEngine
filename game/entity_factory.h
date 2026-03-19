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
#include "game/shared/cached_mob_def.h"

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

    /// Create a mob from a database-backed CachedMobDef (73 mob definitions).
    /// Uses all stats from the definition (HP/damage/armor scaled by level, AI ranges, loot, etc).
    static Entity* createMobFromDef(World& world, const CachedMobDef& def, int level,
                                    Vec2 spawnPos) {
        Entity* mob = world.createEntity(def.displayName);
        mob->setTag(def.isBoss ? "boss" : "mob");

        auto* transform = mob->addComponent<Transform>(spawnPos.x, spawnPos.y);
        transform->depth = 8.0f;

        auto* sprite = mob->addComponent<SpriteComponent>();
        sprite->size = def.isBoss ? Vec2{48.0f, 48.0f} : Vec2{32.0f, 32.0f};
        sprite->texturePath = "assets/sprites/mob_" + def.displayName + ".png";
        sprite->texture = TextureCache::instance().load(sprite->texturePath);
        if (!sprite->texture) {
            // Procedural sprite — delegate to the existing name-based generator
            // by falling back to createMob's sprite generation logic.
            // For now, generate a simple colored blob with face.
            const int SZ = def.isBoss ? 32 : 24;
            std::vector<unsigned char> pixels(SZ * SZ * 4, 0);

            auto spMob = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
                if (x < 0 || x >= SZ || y < 0 || y >= SZ) return;
                int i = (y * SZ + x) * 4;
                pixels[i] = r; pixels[i+1] = g; pixels[i+2] = b; pixels[i+3] = a;
            };
            auto outlineMob = [&](int x, int y) { spMob(x, y, 20, 20, 25); };
            auto mobHash = [](int x, int y, int s) -> int {
                int h = x * 374761393 + y * 668265263 + s * 1274126177;
                h = (h ^ (h >> 13)) * 1274126177;
                return h ^ (h >> 16);
            };
            auto clamp = [](int v) -> unsigned char { return (unsigned char)((v<0)?0:(v>255)?255:v); };

            // Color based on monster type
            bool isBossMob = def.isBoss;
            bool isEliteMob = def.isElite;
            float cx2 = SZ/2.0f, cy2 = SZ/2.0f;
            for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) {
                float dx = x - cx2, dy = y - cy2;
                if (dx*dx + dy*dy < (SZ*0.4f)*(SZ*0.4f)) {
                    int n = mobHash(x, y, 900) % 11 - 5;
                    if (isBossMob)
                        spMob(x, y, clamp(200+n), clamp(60+n), clamp(60+n));
                    else if (isEliteMob)
                        spMob(x, y, clamp(180+n), clamp(140+n), clamp(40+n));
                    else
                        spMob(x, y, clamp(60+n), clamp(140+n), clamp(180+n));
                }
            }
            // Eyes
            int eyeY = (int)(cy2 - 2);
            spMob((int)cx2-2, eyeY, 40, 40, 50); spMob((int)cx2+2, eyeY, 40, 40, 50);
            // Outline
            for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) {
                if (pixels[(y*SZ+x)*4+3] == 0) continue;
                for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
                    int nx=x+dx, ny=y+dy;
                    if (nx>=0 && nx<SZ && ny>=0 && ny<SZ && pixels[(ny*SZ+nx)*4+3]==0)
                        { outlineMob(x,y); goto defmob_next; }
                }
                defmob_next:;
            }

            std::filesystem::create_directories("assets/sprites");
            stbi_write_png(sprite->texturePath.c_str(), SZ, SZ, 4, pixels.data(), SZ * 4);
            sprite->texture = TextureCache::instance().load(sprite->texturePath);
        }

        auto* collider = mob->addComponent<BoxCollider>();
        collider->size = sprite->size * 0.8f;
        collider->isStatic = false;
        collider->isTrigger = true;

        // Enemy Stats — all fields from the definition, scaled by level
        auto* enemyComp = mob->addComponent<EnemyStatsComponent>();
        auto& es = enemyComp->stats;
        es.enemyId        = def.mobDefId;
        es.enemyName      = def.displayName;
        es.level          = level;
        es.maxHP          = def.getHPForLevel(level);
        es.currentHP      = es.maxHP;
        es.baseDamage     = def.getDamageForLevel(level);
        es.armor          = def.getArmorForLevel(level);
        es.magicResist    = def.magicResist;
        es.dealsMagicDamage = def.dealsMagicDamage;
        es.mobHitRate     = def.mobHitRate;
        es.critRate       = def.critRate;
        es.attackSpeed    = def.attackSpeed;
        es.moveSpeed      = def.moveSpeed;
        es.xpReward       = def.getXPRewardForLevel(level);
        es.isAggressive   = def.isAggressive;
        es.lootTableId    = def.lootTableId;
        es.minGoldDrop    = def.minGoldDrop;
        es.maxGoldDrop    = def.maxGoldDrop;
        es.goldDropChance = def.goldDropChance;
        es.honorReward    = def.honorReward;
        es.monsterType    = def.monsterType;
        // Don't call initialize() — we already computed scaled values from the def
        es.isAlive        = true;

        // Status Effects (mobs can have DoTs, debuffs)
        mob->addComponent<StatusEffectComponent>();

        // Crowd Control
        mob->addComponent<CrowdControlComponent>();

        // Damageable marker
        mob->addComponent<DamageableComponent>();

        // Mob AI — convert tile-based def ranges to pixel coords
        auto* aiComp = mob->addComponent<MobAIComponent>();
        aiComp->ai.initialize(spawnPos);
        aiComp->ai.acquireRadius   = def.aggroRange * Coords::TILE_SIZE;
        aiComp->ai.attackRange     = def.attackRange * Coords::TILE_SIZE;
        aiComp->ai.contactRadius   = def.leashRadius * Coords::TILE_SIZE;
        aiComp->ai.isPassive       = !def.isAggressive;
        aiComp->ai.baseChaseSpeed  = def.moveSpeed * Coords::TILE_SIZE;
        aiComp->ai.baseReturnSpeed = def.moveSpeed * Coords::TILE_SIZE;
        aiComp->ai.baseRoamSpeed   = def.moveSpeed * 0.6f * Coords::TILE_SIZE;
        aiComp->ai.roamRadius      = 3.0f * Coords::TILE_SIZE;
        aiComp->ai.stuckThreshold  = 0.05f * Coords::TILE_SIZE;
        aiComp->ai.wiggleDistance   = 1.5f * Coords::TILE_SIZE;
        aiComp->ai.attackCooldown  = (def.attackSpeed > 0.0f) ? (1.5f / def.attackSpeed) : 1.5f;

        // Mob nameplate
        auto* nameplate = mob->addComponent<MobNameplateComponent>();
        nameplate->displayName = def.displayName;
        nameplate->level = level;
        nameplate->isBoss = def.isBoss;
        nameplate->isElite = def.isElite;

        return mob;
    }

    /// Create a mob entity with AI and stats (hardcoded fallback).
    /// Used when no MobDefCache is available (client, tests, legacy).
    static Entity* createMob(World& world, const std::string& mobName, int level,
                             int baseHP, int baseDamage, Vec2 spawnPos,
                             bool aggressive = true, bool isBoss = false) {
        Entity* mob = world.createEntity(mobName);
        mob->setTag(isBoss ? "boss" : "mob");

        auto* transform = mob->addComponent<Transform>(spawnPos.x, spawnPos.y);
        transform->depth = 8.0f;

        auto* sprite = mob->addComponent<SpriteComponent>();
        sprite->size = isBoss ? Vec2{48.0f, 48.0f} : Vec2{32.0f, 32.0f};
        sprite->texturePath = "assets/sprites/mob_" + mobName + ".png";
        sprite->texture = TextureCache::instance().load(sprite->texturePath);
        if (!sprite->texture) {
            // Generate mob-specific pixel art based on name
            const int SZ = isBoss ? 32 : 24;
            std::vector<unsigned char> pixels(SZ * SZ * 4, 0);

            auto spMob = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
                if (x < 0 || x >= SZ || y < 0 || y >= SZ) return;
                int i = (y * SZ + x) * 4;
                pixels[i] = r; pixels[i+1] = g; pixels[i+2] = b; pixels[i+3] = a;
            };
            auto outlineMob = [&](int x, int y) { spMob(x, y, 20, 20, 25); };

            // Simple hash for per-pixel noise
            auto mobHash = [](int x, int y, int s) -> int {
                int h = x * 374761393 + y * 668265263 + s * 1274126177;
                h = (h ^ (h >> 13)) * 1274126177;
                return h ^ (h >> 16);
            };
            auto clamp = [](int v) -> unsigned char { return (unsigned char)((v<0)?0:(v>255)?255:v); };

            if (mobName.find("Slime") != std::string::npos) {
                // --- Slime: translucent green/blue blob ---
                float cx = SZ/2.0f, cy = SZ*0.55f;
                float rx = SZ*0.38f, ry = SZ*0.35f;
                for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) {
                    float dx = x - cx, dy = y - cy;
                    // Squished ellipse (wider at bottom)
                    float bottomBulge = (dy > 0) ? 1.15f : 1.0f;
                    float d = (dx*dx)/(rx*rx*bottomBulge*bottomBulge) + (dy*dy)/(ry*ry);
                    if (d < 1.0f) {
                        int n = mobHash(x, y, 100) % 11 - 5;
                        // Green-blue translucent body
                        int r = 30 + n, g = 180 + n - (int)(d * 40), b = 80 + n;
                        unsigned char a = (unsigned char)(180 + (int)(50.0f * (1.0f - d)));
                        spMob(x, y, clamp(r), clamp(g), clamp(b), a);
                        // Shine highlight (upper-left)
                        if (dx < -2 && dy < -3 && d < 0.3f) {
                            spMob(x, y, clamp(200 + n), clamp(240 + n), clamp(220 + n), 220);
                        }
                    }
                }
                // Eyes (two dark dots)
                int ey = (int)(cy - ry*0.2f);
                spMob((int)cx - 3, ey, 30, 30, 40); spMob((int)cx - 2, ey, 30, 30, 40);
                spMob((int)cx + 2, ey, 30, 30, 40); spMob((int)cx + 3, ey, 30, 30, 40);
                // Mouth
                spMob((int)cx - 1, ey + 3, 20, 60, 30); spMob((int)cx, ey + 3, 20, 60, 30); spMob((int)cx + 1, ey + 3, 20, 60, 30);
                // Outline
                for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) {
                    if (pixels[(y*SZ+x)*4+3] == 0) continue;
                    for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
                        int nx=x+dx, ny=y+dy;
                        if (nx>=0 && nx<SZ && ny>=0 && ny<SZ && pixels[(ny*SZ+nx)*4+3]==0)
                            { outlineMob(x,y); goto slime_next; }
                    }
                    slime_next:;
                }

            } else if (mobName.find("Goblin") != std::string::npos) {
                // --- Goblin: small green humanoid ---
                // Head
                for (int y = 2; y <= 8; y++) for (int x = 7; x <= 16; x++) {
                    float dx = x - 11.5f, dy = y - 5.0f;
                    if (dx*dx/(5*5) + dy*dy/(4*4) < 1.0f) {
                        int n = mobHash(x, y, 200) % 7 - 3;
                        spMob(x, y, clamp(80+n), clamp(140+n), clamp(60+n)); // green skin
                    }
                }
                // Pointy ears
                spMob(5, 4, 70, 130, 50); spMob(6, 4, 75, 135, 55); spMob(6, 5, 75, 135, 55);
                spMob(18, 4, 70, 130, 50); spMob(17, 4, 75, 135, 55); spMob(17, 5, 75, 135, 55);
                // Eyes (red, menacing)
                spMob(9, 5, 200, 50, 30); spMob(10, 5, 220, 60, 30);
                spMob(13, 5, 200, 50, 30); spMob(14, 5, 220, 60, 30);
                // Mouth
                for (int x = 10; x <= 13; x++) spMob(x, 7, 60, 30, 25);
                // Body (ragged brown tunic)
                for (int y = 9; y <= 17; y++) for (int x = 8; x <= 15; x++) {
                    int n = mobHash(x, y, 201) % 9 - 4;
                    spMob(x, y, clamp(110+n), clamp(80+n), clamp(40+n));
                }
                // Arms
                for (int y = 10; y <= 15; y++) {
                    int n = mobHash(7, y, 202) % 5 - 2;
                    spMob(6, y, clamp(80+n), clamp(140+n), clamp(60+n));
                    spMob(7, y, clamp(110+n), clamp(80+n), clamp(40+n));
                    spMob(16, y, clamp(110+n), clamp(80+n), clamp(40+n));
                    spMob(17, y, clamp(80+n), clamp(140+n), clamp(60+n));
                }
                // Legs
                for (int y = 18; y <= 22; y++) {
                    int n = mobHash(9, y, 203) % 5 - 2;
                    spMob(9, y, clamp(80+n), clamp(140+n), clamp(60+n));
                    spMob(10, y, clamp(80+n), clamp(140+n), clamp(60+n));
                    spMob(13, y, clamp(80+n), clamp(140+n), clamp(60+n));
                    spMob(14, y, clamp(80+n), clamp(140+n), clamp(60+n));
                }
                // Dagger in right hand
                for (int y = 11; y <= 17; y++) spMob(18, y, 180, 185, 190);
                spMob(18, 10, 120, 85, 40); // hilt
                // Outline pass
                for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) {
                    if (pixels[(y*SZ+x)*4+3] == 0) continue;
                    for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
                        int nx=x+dx, ny=y+dy;
                        if (nx>=0 && nx<SZ && ny>=0 && ny<SZ && pixels[(ny*SZ+nx)*4+3]==0)
                            { outlineMob(x,y); goto goblin_next; }
                    }
                    goblin_next:;
                }

            } else if (mobName.find("Wolf") != std::string::npos) {
                // --- Wolf: grey four-legged silhouette ---
                // Body (horizontal ellipse)
                for (int y = 6; y <= 16; y++) for (int x = 3; x <= 20; x++) {
                    float dx = x - 11.5f, dy = y - 11.0f;
                    if (dx*dx/(9*9) + dy*dy/(5.5f*5.5f) < 1.0f) {
                        int n = mobHash(x, y, 300) % 9 - 4;
                        int shade = (y < 10) ? 10 : -5; // lighter on top
                        spMob(x, y, clamp(120+n+shade), clamp(118+n+shade), clamp(115+n+shade));
                    }
                }
                // Head (right side)
                for (int y = 5; y <= 12; y++) for (int x = 17; x <= 23; x++) {
                    float dx = x - 20.0f, dy = y - 8.5f;
                    if (dx*dx/(4*4) + dy*dy/(4*4) < 1.0f) {
                        int n = mobHash(x, y, 301) % 7 - 3;
                        spMob(x, y, clamp(130+n), clamp(128+n), clamp(125+n));
                    }
                }
                // Ears
                spMob(19, 3, 100, 98, 95); spMob(19, 4, 110, 108, 105);
                spMob(21, 3, 100, 98, 95); spMob(21, 4, 110, 108, 105);
                // Eye
                spMob(21, 8, 200, 180, 40); spMob(22, 8, 200, 180, 40);
                // Nose
                spMob(23, 9, 40, 35, 35);
                // Mouth
                spMob(22, 10, 80, 50, 50); spMob(23, 10, 80, 50, 50);
                // Legs (4 legs)
                for (int y = 16; y <= 22; y++) {
                    int n = mobHash(5, y, 302) % 5 - 2;
                    spMob(5, y, clamp(110+n), clamp(108+n), clamp(105+n));
                    spMob(6, y, clamp(110+n), clamp(108+n), clamp(105+n));
                    spMob(9, y, clamp(115+n), clamp(113+n), clamp(110+n));
                    spMob(10, y, clamp(115+n), clamp(113+n), clamp(110+n));
                    spMob(15, y, clamp(115+n), clamp(113+n), clamp(110+n));
                    spMob(16, y, clamp(115+n), clamp(113+n), clamp(110+n));
                    spMob(19, y, clamp(110+n), clamp(108+n), clamp(105+n));
                    spMob(20, y, clamp(110+n), clamp(108+n), clamp(105+n));
                }
                // Tail (left side, curving up)
                spMob(2, 8, 115, 113, 110); spMob(1, 7, 115, 113, 110);
                spMob(3, 9, 120, 118, 115); spMob(1, 6, 110, 108, 105);
                // Outline
                for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) {
                    if (pixels[(y*SZ+x)*4+3] == 0) continue;
                    for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
                        int nx=x+dx, ny=y+dy;
                        if (nx>=0 && nx<SZ && ny>=0 && ny<SZ && pixels[(ny*SZ+nx)*4+3]==0)
                            { outlineMob(x,y); goto wolf_next; }
                    }
                    wolf_next:;
                }

            } else if (mobName.find("Mushroom") != std::string::npos) {
                // --- Mushroom: red cap with white dots, pale stem ---
                // Cap (top half, dome)
                float capCx = SZ/2.0f, capCy = SZ*0.32f;
                float capRx = SZ*0.42f, capRy = SZ*0.28f;
                for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) {
                    float dx = x - capCx, dy = y - capCy;
                    float d = (dx*dx)/(capRx*capRx) + (dy*dy)/(capRy*capRy);
                    if (d < 1.0f && y <= (int)(capCy + capRy)) {
                        int n = mobHash(x, y, 400) % 9 - 4;
                        // Red cap with darker edges
                        int shade = (int)(30.0f * (1.0f - d));
                        spMob(x, y, clamp(190 + n + shade), clamp(35 + n), clamp(30 + n));
                    }
                }
                // White dots on cap
                int dots[][2] = {{(int)capCx-4,(int)capCy-2}, {(int)capCx+3,(int)capCy-1},
                                 {(int)capCx-1,(int)capCy-4}, {(int)capCx+1,(int)capCy+1},
                                 {(int)capCx-6,(int)capCy+1}, {(int)capCx+5,(int)capCy}};
                for (auto& dot : dots) {
                    spMob(dot[0], dot[1], 240, 235, 230);
                    spMob(dot[0]+1, dot[1], 235, 230, 225);
                    spMob(dot[0], dot[1]+1, 230, 225, 220);
                }
                // Stem
                int stemTop = (int)(capCy + capRy) - 1;
                for (int y = stemTop; y < SZ - 2; y++) {
                    for (int x = (int)capCx - 3; x <= (int)capCx + 2; x++) {
                        int n = mobHash(x, y, 401) % 7 - 3;
                        spMob(x, y, clamp(220+n), clamp(210+n), clamp(190+n)); // pale
                    }
                }
                // Eyes on stem (cute)
                int ey2 = stemTop + 3;
                spMob((int)capCx-2, ey2, 30, 30, 40); spMob((int)capCx-1, ey2, 30, 30, 40);
                spMob((int)capCx+1, ey2, 30, 30, 40); spMob((int)capCx+2, ey2, 30, 30, 40);
                // Outline
                for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) {
                    if (pixels[(y*SZ+x)*4+3] == 0) continue;
                    for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
                        int nx=x+dx, ny=y+dy;
                        if (nx>=0 && nx<SZ && ny>=0 && ny<SZ && pixels[(ny*SZ+nx)*4+3]==0)
                            { outlineMob(x,y); goto mush_next; }
                    }
                    mush_next:;
                }

            } else if (mobName.find("Golem") != std::string::npos || mobName.find("Forest_Golem") != std::string::npos) {
                // --- Forest Golem: large brown/grey rocky humanoid (boss, 32x32) ---
                // Torso (wide)
                for (int y = 8; y <= 22; y++) for (int x = 6; x <= 25; x++) {
                    int n = mobHash(x, y, 500) % 11 - 5;
                    int shade = (y < 14) ? 8 : -5;
                    spMob(x, y, clamp(100+n+shade), clamp(90+n+shade), clamp(75+n+shade));
                    // Moss patches
                    if ((mobHash(x, y, 501) % 11) == 0) spMob(x, y, clamp(50+n), clamp(90+n), clamp(40+n));
                    // Crack lines
                    if ((mobHash(x, y, 502) % 17) == 0) spMob(x, y, clamp(60+n), clamp(55+n), clamp(45+n));
                }
                // Head (smaller, on top)
                for (int y = 2; y <= 9; y++) for (int x = 10; x <= 21; x++) {
                    float dx = x - 15.5f, dy = y - 5.5f;
                    if (dx*dx/(6*6) + dy*dy/(4*4) < 1.0f) {
                        int n = mobHash(x, y, 503) % 9 - 4;
                        spMob(x, y, clamp(110+n), clamp(100+n), clamp(85+n));
                    }
                }
                // Glowing eyes
                spMob(12, 5, 180, 220, 80); spMob(13, 5, 200, 240, 100);
                spMob(18, 5, 180, 220, 80); spMob(19, 5, 200, 240, 100);
                // Arms (thick)
                for (int y = 10; y <= 22; y++) {
                    for (int x = 2; x <= 6; x++) {
                        int n = mobHash(x, y, 504) % 9 - 4;
                        spMob(x, y, clamp(95+n), clamp(85+n), clamp(70+n));
                    }
                    for (int x = 25; x <= 29; x++) {
                        int n = mobHash(x, y, 505) % 9 - 4;
                        spMob(x, y, clamp(95+n), clamp(85+n), clamp(70+n));
                    }
                }
                // Fists
                for (int x = 1; x <= 6; x++) { spMob(x, 23, 85, 80, 65); spMob(x, 24, 85, 80, 65); }
                for (int x = 25; x <= 30; x++) { spMob(x, 23, 85, 80, 65); spMob(x, 24, 85, 80, 65); }
                // Legs (thick pillars)
                for (int y = 22; y <= 30; y++) {
                    for (int x = 8; x <= 13; x++) {
                        int n = mobHash(x, y, 506) % 7 - 3;
                        spMob(x, y, clamp(90+n), clamp(82+n), clamp(68+n));
                    }
                    for (int x = 18; x <= 23; x++) {
                        int n = mobHash(x, y, 507) % 7 - 3;
                        spMob(x, y, clamp(90+n), clamp(82+n), clamp(68+n));
                    }
                }
                // Outline
                for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) {
                    if (pixels[(y*SZ+x)*4+3] == 0) continue;
                    for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
                        int nx=x+dx, ny=y+dy;
                        if (nx>=0 && nx<SZ && ny>=0 && ny<SZ && pixels[(ny*SZ+nx)*4+3]==0)
                            { outlineMob(x,y); goto golem_next; }
                    }
                    golem_next:;
                }

            } else {
                // --- Fallback: generic colored blob with face ---
                float cx2 = SZ/2.0f, cy2 = SZ/2.0f;
                for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) {
                    float dx = x - cx2, dy = y - cy2;
                    if (dx*dx + dy*dy < (SZ*0.4f)*(SZ*0.4f)) {
                        int n = mobHash(x, y, 900) % 11 - 5;
                        spMob(x, y, clamp(isBoss ? 200+n : 180+n), clamp(60+n), clamp(60+n));
                    }
                }
                // Simple eyes
                int eyeY = (int)(cy2 - 2);
                spMob((int)cx2-2, eyeY, 40, 40, 50); spMob((int)cx2+2, eyeY, 40, 40, 50);
                // Outline
                for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) {
                    if (pixels[(y*SZ+x)*4+3] == 0) continue;
                    for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
                        int nx=x+dx, ny=y+dy;
                        if (nx>=0 && nx<SZ && ny>=0 && ny<SZ && pixels[(ny*SZ+nx)*4+3]==0)
                            { outlineMob(x,y); goto fallback_next; }
                    }
                    fallback_next:;
                }
            }

            std::filesystem::create_directories("assets/sprites");
            stbi_write_png(sprite->texturePath.c_str(), SZ, SZ, 4, pixels.data(), SZ * 4);
            sprite->texture = TextureCache::instance().load(sprite->texturePath);
        }

        auto* collider = mob->addComponent<BoxCollider>();
        collider->size = sprite->size * 0.8f;
        collider->isStatic = false;
        collider->isTrigger = true; // Mobs don't block movement, only collide with environment

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

    // Creates a dropped item entity on the ground (server-side)
    static Entity* createDroppedItem(World& world, Vec2 position, bool isGold) {
        Entity* entity = world.createEntity(isGold ? "gold_drop" : "item_drop");
        entity->setTag("dropped_item");

        auto* transform = entity->addComponent<Transform>(position.x, position.y);
        (void)transform;

        auto* sprite = entity->addComponent<SpriteComponent>();
        sprite->size = {16.0f, 16.0f};

        entity->addComponent<DroppedItemComponent>();

        return entity;
    }

    // Creates a ghost dropped item (client-side, for rendering)
    static Entity* createGhostDroppedItem(World& world, const std::string& name, Vec2 position) {
        Entity* entity = world.createEntity(name);
        entity->setTag("dropped_item");

        auto* transform = entity->addComponent<Transform>(position.x, position.y);
        (void)transform;

        auto* sprite = entity->addComponent<SpriteComponent>();
        sprite->size = {16.0f, 16.0f};

        return entity;
    }
};

} // namespace fate
