// examples/demo_app.cpp -- FateEngine open-source demo
//
// Showcases the engine + the public engine/gameplay2d/ layer in a small
// playable starter. WASD/arrows move the local player; F interacts with the
// nearest NPC; Space attacks the nearest hostile in front; Esc toggles the
// editor pause. The scene is built procedurally in onInit so anyone can read
// this file top-to-bottom and see how every public component fits together.
//
// Builds with zero external dependencies via CMake FetchContent.

#include "engine/app.h"
#include "engine/scene/scene.h"
#include "engine/scene/scene_manager.h"
#include "engine/ecs/world.h"
#include "engine/render/camera.h"
#include "engine/render/sprite_batch.h"
#include "engine/core/logger.h"

#include "engine/components/register_engine_components.h"
#include "engine/gameplay2d/gameplay2d.h"

#ifndef FATE_SHIPPING
#include "engine/editor/editor.h"
#endif

#include <SDL.h>
#include <cmath>
#include <string>

using namespace fate;

namespace {

constexpr float TILE_SIZE = 32.0f;

// Faction tags shared between the demo entities. Packed as bit positions so
// Damageable.hostileMask can match Attack.faction with a single bit shift.
constexpr uint32_t FACTION_PLAYER = 0;
constexpr uint32_t FACTION_HOSTILE = 1;
constexpr uint32_t FACTION_FRIENDLY = 2;

// Helper: 1 bit set at the given faction index.
constexpr uint32_t factionBit(uint32_t f) { return 1u << (f & 31u); }

EntityHandle spawnDummyMob(World& world, const std::string& /*key*/, Vec2 pos, uint32_t /*factionOverride*/) {
    auto handle = world.createEntityH("DemoDummyMob");
    auto* e = world.getEntity(handle);
    if (!e) return EntityHandle{};

    e->addComponent<Transform>(pos);
    auto* col = e->addComponent<Collider2D>();
    col->size = {28.0f, 28.0f};
    col->isStatic = false;
    col->isTrigger = false;

    auto* cc = e->addComponent<CharacterController2D>();
    cc->moveSpeed = 64.0f;
    cc->authority = MovementAuthority::AISimulated;

    auto* h = e->addComponent<Health>();
    h->maxHP = 50.0f;
    h->currentHP = 50.0f;
    h->regenPerSec = 2.0f;

    auto* dmg = e->addComponent<Damageable>();
    dmg->faction = FACTION_HOSTILE;
    dmg->hostileMask = factionBit(FACTION_PLAYER);

    auto* atk = e->addComponent<Attack>();
    atk->faction = FACTION_HOSTILE;
    atk->damageOnHit = 4.0f;
    atk->range = 36.0f;
    atk->cooldownSec = 1.0f;

    auto* tgt = e->addComponent<Targetable>();
    tgt->category = TargetCategory::Hostile;
    tgt->radius = 28.0f;

    auto* mob = e->addComponent<Mob2D>();
    mob->aggroRadiusPx = 160.0f;
    mob->leashRadiusPx = 256.0f;
    mob->attackRangePx = 36.0f;
    mob->chaseSpeed = 64.0f;
    mob->returnSpeed = 96.0f;

    auto* np = e->addComponent<Nameplate>();
    np->displayName = "Slime";
    np->level = 1;
    np->size = {48.0f, 8.0f};
    np->worldOffset = {0.0f, 22.0f};
    np->backgroundColor = {0.0f, 0.0f, 0.0f, 0.55f};
    np->healthBarColor = {0.85f, 0.25f, 0.25f, 1.0f};

    return handle;
}

EntityHandle spawnFriendlyVillager(World& world, const std::string& /*key*/, Vec2 pos, uint32_t /*factionOverride*/) {
    auto handle = world.createEntityH("DemoVillager");
    auto* e = world.getEntity(handle);
    if (!e) return EntityHandle{};

    e->addComponent<Transform>(pos);
    auto* col = e->addComponent<Collider2D>();
    col->size = {28.0f, 28.0f};
    col->isStatic = true;

    auto* npc = e->addComponent<NPC2D>();
    npc->displayName = "Villager";
    npc->greeting = "Welcome to the demo zone!";
    setRole(*npc, NPC2DRole::AmbientChat, true);

    auto* it = e->addComponent<Interactable>();
    it->prompt = "Press F to talk to Villager";
    it->actionId = "demo_villager_chat";
    it->interactionRadius = 56.0f;
    it->repeatable = true;

    auto* tgt = e->addComponent<Targetable>();
    tgt->category = TargetCategory::Friendly;
    tgt->radius = 26.0f;

    auto* h = e->addComponent<Health>();
    h->maxHP = 100.0f;
    h->currentHP = 100.0f;
    h->invulnerable = true;

    auto* dmg = e->addComponent<Damageable>();
    dmg->faction = FACTION_FRIENDLY;
    dmg->hostileMask = 0; // can't be hurt

    auto* np = e->addComponent<Nameplate>();
    np->displayName = "Villager";
    np->level = 1;
    np->showHealthBar = false;
    np->size = {56.0f, 6.0f};
    np->worldOffset = {0.0f, 22.0f};

    return handle;
}

} // anonymous

class DemoApp : public App {
public:
    void onInit() override {
        // Component registration: engine + public gameplay2d. Both must run
        // before scene factories so the prefab path can build entities.
        registerEngineComponents();
        registerGameplay2dComponents();

        SceneManager::instance().registerScene("demo", [this](Scene& scene) {
            buildDemoScene(scene);
        });
        SceneManager::instance().switchScene("demo");

#ifndef FATE_SHIPPING
        // Asset browser starting points for the editor — same as the legacy
        // grid-only demo so users can poke around the engine source.
        Editor::instance().setAssetRoot(projectRoot_);
        Editor::instance().setSourceDir(projectRoot_ + "/assets");

        // Default to UNPAUSED so the gameplay layer is interactive on
        // startup. Toggle with Esc inside the editor.
        Editor::instance().setPaused(false);
#endif

        camera().setPosition({0.0f, 0.0f});
        camera().setZoom(1.0f);

        LOG_INFO("Demo", "FateEngine demo ready -- WASD move, F interact, Space attack, Esc toggle pause.");
    }

    void onUpdate(float dt) override {
        (void)dt;

        Input& input = Input::instance();

#ifndef FATE_SHIPPING
        if (input.isKeyPressed(SDL_SCANCODE_ESCAPE)) {
            Editor::instance().setPaused(!Editor::instance().isPaused());
        }
#endif

        // Player attack: Space queues a wantsToAttack_; the HealthDamage
        // system resolves it during world.update() this same tick (player
        // entity's Attack is processed there).
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;
        World& world = scene->world();
        Entity* player = world.getEntity(playerHandle_);
        if (!player) return;

        if (input.isKeyPressed(SDL_SCANCODE_SPACE)) {
            if (auto* atk = player->getComponent<Attack>()) {
                atk->wantsToAttack_ = true;
            }
        }
    }

    void onRender(SpriteBatch& batch, Camera& cam) override {
        Mat4 proj = cam.getViewProjection();

        batch.begin(proj);
        drawGrid(batch, cam);
        drawSceneEntities(batch);
        drawNameplates(batch, cam);
        batch.end();
    }

private:
    static constexpr float kViewRadius = 512.0f * TILE_SIZE;

    EntityHandle playerHandle_;
    PortalZoneSystem*   portalSys_ = nullptr;
    Interaction2DSystem* interactSys_ = nullptr;
    HealthDamageSystem* damageSys_ = nullptr;
    Targeting2DSystem*  targetSys_ = nullptr;
    NameplateRenderSystem nameplateRenderer_;

    std::string projectRoot_;
    friend int main(int, char**);

    // Scene assembly — invoked by the SceneManager factory. Builds entities
    // and wires every system into the freshly-created world.
    void buildDemoScene(Scene& scene) {
        World& world = scene.world();

        wireSystems(world);
        spawnPlayer(world);
        spawnWalls(world);
        spawnZoneBounds(world);
        spawnNPC(world);
        spawnInitialMob(world);
        spawnPortal(world);
        spawnSpawnZone(world);
    }

    void wireSystems(World& world) {
        world.addSystem<CharacterController2DSystem>();
        world.addSystem<SpriteAnimator2DSystem>();
        auto* camSys = world.addSystem<CameraFollow2DSystem>();
        camSys->setCamera(&camera());

        world.addSystem<Trigger2DSystem>();

        interactSys_ = world.addSystem<Interaction2DSystem>();
        interactSys_->setOnInteract([](Entity* npcEnt, Interactable* it) {
            std::string label = it->actionId.empty() ? std::string("interact") : it->actionId;
            std::string greeting;
            if (auto* npc = npcEnt->getComponent<NPC2D>()) {
                greeting = npc->greeting;
            }
            LOG_INFO("Demo", "Interacted with %s -> '%s'",
                     label.c_str(), greeting.c_str());
        });

        damageSys_ = world.addSystem<HealthDamageSystem>();
        damageSys_->setOnHit([](Entity* attacker, Entity* victim, float dmg) {
            LOG_INFO("Demo", "%s hit %s for %.1f",
                     attacker ? attacker->name().c_str() : "?",
                     victim ? victim->name().c_str() : "?",
                     dmg);
        });
        damageSys_->setOnDeath([](Entity* killed, Entity* /*killer*/) {
            LOG_INFO("Demo", "%s died.", killed ? killed->name().c_str() : "?");
        });

        portalSys_ = world.addSystem<PortalZoneSystem>();
        portalSys_->setOnZoneChanged([](Entity*, Entity*, Zone2D* z) {
            if (z) LOG_INFO("Demo", "Entered zone: %s", z->displayName.c_str());
            else   LOG_INFO("Demo", "Left zone.");
        });
        portalSys_->setOnPortalEnter([](Entity* player, Entity* portalEnt, Portal2D* p) {
            LOG_INFO("Demo", "Player crossed portal -> %s",
                     p->targetScene.empty() ? p->targetZoneId.c_str()
                                            : p->targetScene.c_str());
            // Default behavior: same-scene teleport runs in PortalZoneSystem
            // when no callback is set; we install a callback for logging,
            // so re-implement the teleport here.
            if (player && p->targetScene.empty()) {
                if (auto* tx = player->getComponent<Transform>()) {
                    tx->position = p->targetSpawnPos;
                }
            }
            (void)portalEnt;
        });

        auto* spawnSys = world.addSystem<SpawnZone2DSystem>();
        spawnSys->registerFactory("demo_dummy_mob", spawnDummyMob);
        spawnSys->registerFactory("demo_villager_npc", spawnFriendlyVillager);

        targetSys_ = world.addSystem<Targeting2DSystem>();
        targetSys_->setCamera(&camera());

        world.addSystem<Mob2DSystem>();
    }

    void spawnPlayer(World& world) {
        auto handle = world.createEntityH("Player");
        playerHandle_ = handle;
        auto* e = world.getEntity(handle);

        e->addComponent<Transform>(Vec2{0.0f, 0.0f});

        auto* col = e->addComponent<Collider2D>();
        col->size = {26.0f, 26.0f};
        col->isStatic = false;
        col->layer = 1u;

        auto* cc = e->addComponent<CharacterController2D>();
        cc->isLocalPlayer = true;
        cc->moveSpeed = 128.0f;
        cc->authority = MovementAuthority::Local;

        auto* cf = e->addComponent<CameraFollow2D>();
        cf->mode = CameraFollowMode::Smooth;
        cf->lerpRate = 8.0f;
        cf->priority = 100;

        auto* h = e->addComponent<Health>();
        h->maxHP = 100.0f;
        h->currentHP = 100.0f;
        h->regenPerSec = 5.0f;

        auto* atk = e->addComponent<Attack>();
        atk->faction = FACTION_PLAYER;
        atk->damageOnHit = 18.0f;
        atk->range = 56.0f;
        atk->hitArcDegrees = 100.0f;
        atk->cooldownSec = 0.4f;

        auto* dmg = e->addComponent<Damageable>();
        dmg->faction = FACTION_PLAYER;
        dmg->hostileMask = factionBit(FACTION_HOSTILE);

        auto* tgt = e->addComponent<Targetable>();
        tgt->category = TargetCategory::Friendly;
        tgt->radius = 28.0f;
        tgt->canTargetSelf = true;

        auto* np = e->addComponent<Nameplate>();
        np->displayName = "Player";
        np->level = 1;
        np->size = {64.0f, 8.0f};
        np->worldOffset = {0.0f, 22.0f};
        np->healthBarColor = {0.4f, 0.85f, 0.4f, 1.0f};
    }

    void spawnWalls(World& world) {
        auto wall = [&](Vec2 pos, Vec2 size, const char* name) {
            auto handle = world.createEntityH(name);
            auto* e = world.getEntity(handle);
            e->addComponent<Transform>(pos);
            auto* col = e->addComponent<Collider2D>();
            col->size = size;
            col->isStatic = true;
            col->isTrigger = false;
            col->debugColor = {0.5f, 0.5f, 0.55f, 0.85f};
        };

        // Box arena around the play area.
        wall({0.0f,  160.0f}, {320.0f, 16.0f}, "WallTop");
        wall({0.0f, -160.0f}, {320.0f, 16.0f}, "WallBottom");
        wall({-160.0f, 0.0f}, {16.0f, 320.0f}, "WallLeft");
        wall({ 160.0f, 0.0f}, {16.0f, 320.0f}, "WallRight");
        // A pillar in the middle to climb around.
        wall({64.0f, 64.0f}, {32.0f, 32.0f}, "Pillar");
    }

    void spawnZoneBounds(World& world) {
        auto handle = world.createEntityH("DemoZone");
        auto* e = world.getEntity(handle);
        e->addComponent<Transform>(Vec2{0.0f, 0.0f});
        auto* z = e->addComponent<Zone2D>();
        z->zoneId = "demo_arena";
        z->displayName = "Demo Arena";
        z->size = {320.0f, 320.0f};
    }

    void spawnNPC(World& world) {
        spawnFriendlyVillager(world, "demo_villager_npc", Vec2{-96.0f, -64.0f}, 0);
    }

    void spawnInitialMob(World& world) {
        spawnDummyMob(world, "demo_dummy_mob", Vec2{96.0f, -32.0f}, 0);
    }

    void spawnPortal(World& world) {
        auto handle = world.createEntityH("DemoPortal");
        auto* e = world.getEntity(handle);
        e->addComponent<Transform>(Vec2{-128.0f, 96.0f});
        auto* p = e->addComponent<Portal2D>();
        p->triggerSize = {32.0f, 32.0f};
        p->targetSpawnPos = {0.0f, 0.0f};
        p->targetZoneId = "demo_arena";
        p->label = "Return to spawn";
        p->showLabel = true;

        auto* np = e->addComponent<Nameplate>();
        np->displayName = "Spawn Portal";
        np->level = 0;
        np->showHealthBar = false;
        np->showLevel = false;
        np->size = {72.0f, 6.0f};
        np->worldOffset = {0.0f, 24.0f};
        np->backgroundColor = {0.0f, 0.0f, 0.0f, 0.55f};
        np->healthBarColor = {0.4f, 0.7f, 1.0f, 1.0f};
    }

    void spawnSpawnZone(World& world) {
        auto handle = world.createEntityH("MobSpawnZone");
        auto* e = world.getEntity(handle);
        e->addComponent<Transform>(Vec2{96.0f, 96.0f});
        auto* sz = e->addComponent<SpawnZone2D>();
        sz->prefabKey = "demo_dummy_mob";
        sz->targetCount = 2;
        sz->respawnSeconds = 6.0f;
        sz->radius = 40.0f;
    }

    // ----- Render helpers --------------------------------------------------

    void drawGrid(SpriteBatch& batch, Camera& cam) {
        constexpr int RANGE = 32;       // tighter than the legacy demo so
                                        // the play area stays visible.
        float extent = RANGE * TILE_SIZE;
        float zoom = cam.zoom();
        float px = 0.3f / zoom;
        float zoomFade = std::min(zoom, 1.0f);
        Color minorColor{1.0f, 1.0f, 1.0f, 0.06f * zoomFade};
        Color majorColor{1.0f, 1.0f, 1.0f, 0.14f};

        float span = extent * 2.0f;
        for (int i = -RANGE; i <= RANGE; ++i) {
            bool major = (i % 8 == 0);
            const Color& c = major ? majorColor : minorColor;
            float pos = i * TILE_SIZE;
            batch.drawRect({pos, 0.0f}, {px, span}, c);
            batch.drawRect({0.0f, pos}, {span, px}, c);
        }

        batch.drawRect({0.0f, 0.0f}, {span, px}, Color{0.8f, 0.2f, 0.2f, 0.6f});
        batch.drawRect({0.0f, 0.0f}, {px, span}, Color{0.2f, 0.8f, 0.2f, 0.6f});
    }

    void drawSceneEntities(SpriteBatch& batch) {
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;
        World& world = scene->world();

        // Static colliders — gray outlines.
        world.forEach<Transform, Collider2D>(
            [&](Entity*, Transform* tx, Collider2D* col) {
                Rect b = col->getBounds(tx->position);
                if (col->isTrigger) {
                    batch.drawRect({b.x + b.w * 0.5f, b.y + b.h * 0.5f},
                                   {b.w, b.h}, col->debugColor, 1.0f);
                } else if (col->isStatic) {
                    batch.drawRect({b.x + b.w * 0.5f, b.y + b.h * 0.5f},
                                   {b.w, b.h}, col->debugColor, 1.0f);
                }
            });

        // Zones — outlined area.
        world.forEach<Transform, Zone2D>(
            [&](Entity*, Transform* tx, Zone2D* z) {
                Vec2 c = tx->position;
                Vec2 s = z->size;
                Color col{0.2f, 0.4f, 0.9f, 0.18f};
                float t = 1.5f;
                batch.drawRect({c.x, c.y + s.y * 0.5f}, {s.x, t}, col, 2.0f);
                batch.drawRect({c.x, c.y - s.y * 0.5f}, {s.x, t}, col, 2.0f);
                batch.drawRect({c.x - s.x * 0.5f, c.y}, {t, s.y}, col, 2.0f);
                batch.drawRect({c.x + s.x * 0.5f, c.y}, {t, s.y}, col, 2.0f);
            });

        // Spawn zones — yellow circle outline.
        world.forEach<Transform, SpawnZone2D>(
            [&](Entity*, Transform* tx, SpawnZone2D* sz) {
                batch.drawRing(tx->position, sz->radius, 2.0f,
                               Color{0.9f, 0.85f, 0.3f, 0.55f}, 2.0f, 32);
            });

        // Portal triggers — cyan tint.
        world.forEach<Transform, Portal2D>(
            [&](Entity*, Transform* tx, Portal2D* p) {
                Rect b = p->getTriggerBounds(tx->position);
                batch.drawRect({b.x + b.w * 0.5f, b.y + b.h * 0.5f},
                               {b.w, b.h}, Color{0.3f, 0.8f, 0.95f, 0.55f}, 3.0f);
            });

        // Players / mobs / NPCs — fall back to character-class color.
        world.forEach<Transform, CharacterController2D>(
            [&](Entity*, Transform* tx, CharacterController2D* cc) {
                Color c{0.45f, 0.85f, 0.45f, 1.0f}; // local player default
                if (!cc->isLocalPlayer) {
                    c = (cc->authority == MovementAuthority::AISimulated)
                            ? Color{0.95f, 0.4f, 0.4f, 1.0f}
                            : Color{0.6f, 0.7f, 1.0f, 1.0f};
                }
                batch.drawRect(tx->position, {22.0f, 22.0f}, c, 10.0f);
            });

        // Pure NPCs without a controller (shouldn't exist in this demo, but
        // safe to handle).
        world.forEach<Transform, NPC2D>(
            [&](Entity* e, Transform* tx, NPC2D*) {
                if (e->getComponent<CharacterController2D>()) return;
                batch.drawRect(tx->position, {22.0f, 22.0f},
                               Color{0.6f, 0.7f, 1.0f, 1.0f}, 10.0f);
            });
    }

    void drawNameplates(SpriteBatch& batch, Camera& cam) {
        auto* scene = SceneManager::instance().currentScene();
        if (!scene) return;
        nameplateRenderer_.render(scene->world(), batch, cam);
    }
};

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    DemoApp app;
    AppConfig config;
    config.title = "FateEngine Demo";
    config.windowWidth = 1600;
    config.windowHeight = 900;

#ifdef FATE_SOURCE_DIR
    config.assetsDir = std::string(FATE_SOURCE_DIR) + "/assets";
    app.projectRoot_ = std::string(FATE_SOURCE_DIR);
#endif

    if (!app.init(config)) {
        return 1;
    }

    app.run();
    return 0;
}
