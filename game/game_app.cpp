#include "game/game_app.h"
#include "engine/core/logger.h"
#include "engine/scene/scene_manager.h"
#include "engine/editor/editor.h"
#include "game/register_components.h"
#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "game/components/player_controller.h"
#include "game/components/animator.h"
#include "game/components/box_collider.h"
#include "game/components/polygon_collider.h"
#include "game/components/game_components.h"
#include "engine/render/sdf_text.h"
#include "engine/ecs/prefab.h"
#include "game/entity_factory.h"
#include "game/systems/movement_system.h"
#include "game/systems/render_system.h"
#include "game/systems/gameplay_system.h"
#include "game/systems/mob_ai_system.h"
#include "game/systems/combat_action_system.h"
#include "game/systems/zone_system.h"
#include "game/systems/spawn_system.h"
#include "game/systems/npc_interaction_system.h"
#include "game/systems/quest_system.h"
#include "game/ui/inventory_ui.h"
#include "game/ui/skill_bar_ui.h"
#include "game/ui/hud_bars_ui.h"
#include "game/shared/npc_types.h"
#include "imgui.h"
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include "stb_image_write.h"
namespace fs = std::filesystem;  // std::min, std::max (used with parenthesized calls to avoid Windows macro conflict)

namespace fate {

// ============================================================================
// Procedural tile generation — creates village assets if they don't exist
// ============================================================================

static void setPixel(std::vector<unsigned char>& px, int x, int y, int sz,
                     unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
    if (x < 0 || x >= sz || y < 0 || y >= sz) return;
    int i = (y * sz + x) * 4;
    px[i] = r; px[i+1] = g; px[i+2] = b; px[i+3] = a;
}

static void generateTileIfMissing(const std::string& path, int size,
    std::function<void(std::vector<unsigned char>&, int)> generator) {
    if (fs::exists(path)) return;
    std::vector<unsigned char> pixels(size * size * 4, 0);
    generator(pixels, size);
    stbi_write_png(path.c_str(), size, size, 4, pixels.data(), size * 4);
}

static void generateVillageTiles() {
    fs::create_directories("assets/tiles");
    fs::create_directories("assets/sprites");

    // --- Ground tiles ---

    generateTileIfMissing("assets/tiles/dirt.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 7 + y * 13 + x * y) % 20) - 10;
            setPixel(px, x, y, sz, 140+n, 100+n, 60+n);
        }
    });

    generateTileIfMissing("assets/tiles/dirt_path.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 5 + y * 11) % 14) - 7;
            setPixel(px, x, y, sz, 160+n, 130+n, 90+n);
        }
    });

    generateTileIfMissing("assets/tiles/stone_floor.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 3 + y * 7) % 16) - 8;
            // Grid pattern for stone blocks
            bool edge = (x % 16 == 0) || (y % 16 == 0);
            if (edge) setPixel(px, x, y, sz, 80+n, 80+n, 85+n);
            else setPixel(px, x, y, sz, 130+n, 130+n, 135+n);
        }
    });

    generateTileIfMissing("assets/tiles/water.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 3 + y * 5 + x * y / 4) % 24) - 12;
            setPixel(px, x, y, sz, 20+n, 60+n, 160+n, 220);
        }
    });

    generateTileIfMissing("assets/tiles/sand.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 11 + y * 7) % 18) - 9;
            setPixel(px, x, y, sz, 210+n, 190+n, 140+n);
        }
    });

    generateTileIfMissing("assets/tiles/grass_dark.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 7 + y * 13) % 20) - 10;
            setPixel(px, x, y, sz, 20+n, 65+n, 20+n);
        }
    });

    generateTileIfMissing("assets/tiles/flowers.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 7 + y * 13) % 20) - 10;
            setPixel(px, x, y, sz, 30+n, 80+n, 30+n); // grass base
        }
        // Scatter flower dots
        int flowerSeeds[][2] = {{5,5},{12,3},{22,7},{8,14},{18,12},{27,4},
                                 {3,22},{15,20},{25,18},{10,27},{20,25},{28,28}};
        unsigned char colors[][3] = {{255,80,80},{255,200,50},{200,80,255},{255,150,200}};
        for (auto& f : flowerSeeds) {
            auto& c = colors[(f[0]+f[1]) % 4];
            setPixel(px, f[0], f[1], sz, c[0], c[1], c[2]);
            setPixel(px, f[0]+1, f[1], sz, c[0], c[1], c[2]);
            setPixel(px, f[0], f[1]+1, sz, c[0], c[1], c[2]);
        }
    });

    // --- Building tiles ---

    generateTileIfMissing("assets/tiles/wood_wall.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 3 + y * 17) % 16) - 8;
            bool plank = (y % 8 < 7);
            if (plank) setPixel(px, x, y, sz, 140+n, 95+n, 50+n);
            else setPixel(px, x, y, sz, 90, 60, 30); // gap between planks
        }
    });

    generateTileIfMissing("assets/tiles/stone_wall.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 7 + y * 3) % 14) - 7;
            int brickX = x % 16; int brickY = y % 8;
            bool mortar = (brickX == 0) || (brickY == 0);
            // Offset every other row
            if ((y / 8) % 2 == 1 && brickX == 8) mortar = true;
            if (mortar) setPixel(px, x, y, sz, 120+n, 115+n, 110+n);
            else setPixel(px, x, y, sz, 160+n, 155+n, 150+n);
        }
    });

    generateTileIfMissing("assets/tiles/roof_red.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 5 + y * 9) % 16) - 8;
            bool shingle = (y % 6 < 5);
            if (shingle) setPixel(px, x, y, sz, 165+n, 50+n, 35+n);
            else setPixel(px, x, y, sz, 120, 35, 25);
        }
    });

    generateTileIfMissing("assets/tiles/roof_blue.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            int n = ((x * 5 + y * 9) % 16) - 8;
            bool shingle = (y % 6 < 5);
            if (shingle) setPixel(px, x, y, sz, 40+n, 60+n, 140+n);
            else setPixel(px, x, y, sz, 30, 40, 100);
        }
    });

    // --- Objects (sprites) ---

    generateTileIfMissing("assets/tiles/door.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            if (x >= 8 && x <= 23 && y >= 2 && y <= 29) {
                int n = ((x * 3 + y * 11) % 12) - 6;
                setPixel(px, x, y, sz, 110+n, 70+n, 35+n);
                // Door frame
                if (x == 8 || x == 23 || y == 2) setPixel(px, x, y, sz, 70, 45, 20);
                // Handle
                if (x == 20 && y >= 14 && y <= 16) setPixel(px, x, y, sz, 200, 180, 50);
            }
        }
    });

    generateTileIfMissing("assets/tiles/window.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            if (x >= 6 && x <= 25 && y >= 6 && y <= 25) {
                // Glass
                setPixel(px, x, y, sz, 150, 200, 230, 180);
                // Frame
                if (x == 6 || x == 25 || y == 6 || y == 25 || x == 15 || y == 15)
                    setPixel(px, x, y, sz, 100, 70, 40);
            }
        }
    });

    generateTileIfMissing("assets/tiles/fence_h.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            // Horizontal rails
            bool rail = (y >= 10 && y <= 12) || (y >= 20 && y <= 22);
            // Posts
            bool post = (x >= 0 && x <= 3) || (x >= 28 && x <= 31);
            if ((rail || (post && y >= 6 && y <= 26))) {
                int n = ((x+y) % 6) - 3;
                setPixel(px, x, y, sz, 150+n, 110+n, 60+n);
            }
        }
    });

    generateTileIfMissing("assets/tiles/rock.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            float dx = x - 16.0f, dy = y - 18.0f;
            if (dx*dx/(14*14) + dy*dy/(12*12) < 1.0f) {
                int n = ((x * 7 + y * 11) % 20) - 10;
                // Lighter on top, darker on bottom
                int shade = (int)(40.0f * (1.0f - (float)y / sz));
                setPixel(px, x, y, sz, 120+n+shade, 115+n+shade, 110+n+shade);
            }
        }
    });

    generateTileIfMissing("assets/tiles/bush.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            float dx = x - 16.0f, dy = y - 18.0f;
            if (dx*dx/(13*13) + dy*dy/(10*10) < 1.0f) {
                int n = ((x * 5 + y * 9) % 24) - 12;
                setPixel(px, x, y, sz, 25+n, 90+n, 25+n);
            }
        }
    });

    generateTileIfMissing("assets/tiles/well.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            float dx = x - 16.0f, dy = y - 16.0f;
            float dist = dx*dx + dy*dy;
            if (dist < 144 && dist > 81) { // Stone ring
                int n = ((x * 3 + y * 7) % 10) - 5;
                setPixel(px, x, y, sz, 140+n, 135+n, 130+n);
            } else if (dist <= 81) { // Water inside
                int n = ((x * 5 + y * 3) % 16) - 8;
                setPixel(px, x, y, sz, 20+n, 50+n, 130+n);
            }
        }
    });

    generateTileIfMissing("assets/tiles/barrel.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            // Barrel shape (rounded rectangle)
            float dx = x - 16.0f;
            float bulge = 1.0f + 0.15f * (1.0f - ((y-16.0f)*(y-16.0f))/(14*14));
            if (std::abs(dx) < 10 * bulge && y >= 4 && y <= 28) {
                int n = ((x * 3 + y * 11) % 14) - 7;
                setPixel(px, x, y, sz, 130+n, 85+n, 40+n);
                // Metal bands
                if (y == 8 || y == 24) setPixel(px, x, y, sz, 80, 80, 90);
            }
        }
    });

    generateTileIfMissing("assets/tiles/crate.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            if (x >= 4 && x <= 27 && y >= 4 && y <= 27) {
                int n = ((x * 7 + y * 3) % 12) - 6;
                setPixel(px, x, y, sz, 160+n, 120+n, 60+n);
                // Cross braces
                if (x == 4 || x == 27 || y == 4 || y == 27)
                    setPixel(px, x, y, sz, 100+n, 70+n, 30+n);
                if (std::abs(x - y) < 2 || std::abs(x - (31-y)) < 2)
                    setPixel(px, x, y, sz, 120+n, 80+n, 35+n);
            }
        }
    });

    generateTileIfMissing("assets/tiles/sign.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            // Post
            if (x >= 14 && x <= 17 && y >= 16 && y <= 30) {
                setPixel(px, x, y, sz, 100, 70, 35);
            }
            // Sign board
            if (x >= 4 && x <= 27 && y >= 4 && y <= 16) {
                int n = ((x+y) % 6) - 3;
                setPixel(px, x, y, sz, 170+n, 140+n, 80+n);
                if (x == 4 || x == 27 || y == 4 || y == 16)
                    setPixel(px, x, y, sz, 100, 70, 35);
            }
        }
    });

    generateTileIfMissing("assets/tiles/npc.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            float dx = x - 16.0f;
            // Head (circle)
            float headDy = y - 8.0f;
            if (dx*dx + headDy*headDy < 25) {
                setPixel(px, x, y, sz, 230, 190, 150); // skin
                // Eyes
                if ((x == 14 || x == 18) && y == 7) setPixel(px, x, y, sz, 40, 40, 40);
            }
            // Body
            if (x >= 10 && x <= 21 && y >= 13 && y <= 24) {
                setPixel(px, x, y, sz, 60, 100, 160); // blue shirt
            }
            // Legs
            if ((x >= 11 && x <= 14 && y >= 25 && y <= 30) ||
                (x >= 17 && x <= 20 && y >= 25 && y <= 30)) {
                setPixel(px, x, y, sz, 80, 60, 40); // brown pants
            }
        }
    });

    generateTileIfMissing("assets/tiles/torch.png", 32, [](auto& px, int sz) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            // Stick
            if (x >= 14 && x <= 17 && y >= 12 && y <= 28) {
                setPixel(px, x, y, sz, 100, 70, 35);
            }
            // Flame
            float dx = x - 16.0f, dy = y - 8.0f;
            float dist = dx*dx + dy*dy;
            if (dist < 20) setPixel(px, x, y, sz, 255, 220, 50);
            else if (dist < 36) setPixel(px, x, y, sz, 255, 140, 20, 200);
        }
    });

    LOG_INFO("Game", "Village tileset generated (%d tiles)", 20);
}

void GameApp::onInit() {
    LOG_INFO("Game", "FateMMO Game Engine initializing...");

    // Register all component types with the reflection/meta registry
    fate::registerAllComponents();

    // Generate village tileset (only creates files that don't exist yet)
    generateVillageTiles();

    // Set up editor and prefab library
    Editor::instance().setAssetRoot("assets");
#ifdef FATE_SOURCE_DIR
    Editor::instance().setSourceDir(FATE_SOURCE_DIR "/assets/scenes");
    PrefabLibrary::instance().setSourceDirectory(FATE_SOURCE_DIR "/assets/prefabs");
#endif
    PrefabLibrary::instance().setDirectory("assets/prefabs");
    PrefabLibrary::instance().loadAll();

    // Register fallback scene factory (used if no saved scene exists)
    SceneManager::instance().registerScene("TestScene", [this](Scene& scene) {
        createPlayer(scene.world());
        createTestEntities(scene.world());
        spawnTestMobs(scene.world());
        spawnTestNPCs(scene.world());
    });

    SceneManager::instance().switchScene("TestScene");

    // Add systems (these operate on whatever entities are in the scene)
    auto* scene = SceneManager::instance().currentScene();
    if (scene) {
        World& world = scene->world();

        world.addSystem<MovementSystem>();
        world.addSystem<AnimationSystem>();

        auto* cameraFollow = world.addSystem<CameraFollowSystem>();
        cameraFollow->camera = &camera();

        gameplaySystem_ = world.addSystem<GameplaySystem>();
        mobAISystem_ = world.addSystem<MobAISystem>();

        npcInteractionSystem_ = world.addSystem<NPCInteractionSystem>();
        npcInteractionSystem_->camera = &camera();

        combatSystem_ = world.addSystem<CombatActionSystem>();
        combatSystem_->camera = &camera();

        questSystem_ = world.addSystem<QuestSystem>();

        zoneSystem_ = world.addSystem<ZoneSystem>();
        zoneSystem_->camera = &camera();

        world.addSystem<SpawnSystem>();

        renderSystem_ = new SpriteRenderSystem();
        renderSystem_->batch = &spriteBatch();
        renderSystem_->camera = &camera();
        renderSystem_->init(&world);
    }

    // Initialize SDF text rendering
    SDFText::instance().init("assets/fonts/default.png", "assets/fonts/default.json");

    // Try to load a tilemap (if present, replaces procedural ground)
    tilemap_ = std::make_unique<Tilemap>();
    if (tilemap_->loadFromFile("assets/maps/test_map.json")) {
        tilemap_->origin = {
            -(tilemap_->worldWidth() * 0.5f),
            -(tilemap_->worldHeight() * 0.5f)
        };
    } else {
        tilemap_.reset();
    }

    // Auto-load last saved scene (replaces procedural entities, keeps systems)
    if (fs::exists("assets/scenes/scene.json")) {
        auto* s = SceneManager::instance().currentScene();
        if (s) {
            Editor::instance().loadScene(&s->world(), "assets/scenes/scene.json");
            LOG_INFO("Game", "Auto-loaded saved scene");
        }
    }

    LOG_INFO("Game", "Initialized");
}

void GameApp::createPlayer(World& world) {
    // TODO: faction selection UI — hardcoded to Xyros for now
    Faction playerFaction = Faction::Xyros;
    Entity* player = EntityFactory::createPlayer(world, "Player", ClassType::Warrior, true, playerFaction);

    // Spawn at faction's home village position
    auto* transform = player->getComponent<Transform>();
    if (transform) {
        const auto* factionDef = FactionRegistry::get(playerFaction);
        // Each faction gets a distinct spawn offset (will be replaced by zone system)
        float spawnX = 16.0f;
        float spawnY = 16.0f;
        if (factionDef) {
            // Spread factions across the map — 8 tile spacing per faction index
            spawnX = 16.0f + static_cast<float>(static_cast<uint8_t>(playerFaction) - 1) * 8.0f * Coords::TILE_SIZE;
        }
        transform->position = {spawnX, spawnY};
    }

    // Create placeholder sprite if no texture was loaded by the factory
    auto* sprite = player->getComponent<SpriteComponent>();
    if (sprite && !sprite->texture) {
        std::string playerPath = "assets/sprites/player.png";
        if (!fs::exists(playerPath)) {
            // Generate and save placeholder (blue character shape)
            const int SZ = 16;
            std::vector<unsigned char> pixels(SZ * SZ * 4, 0);
            for (int y = 0; y < SZ; y++) {
                for (int x = 0; x < SZ; x++) {
                    int i = (y * SZ + x) * 4;
                    float dx = x - 7.5f, dy = y - 7.5f;
                    bool body = (x >= 4 && x <= 11 && y >= 2 && y <= 13);
                    bool head = (dx * dx + (dy + 3) * (dy + 3)) < 12.0f;
                    if (body || head) {
                        pixels[i + 0] = 80;  pixels[i + 1] = 120;
                        pixels[i + 2] = 230; pixels[i + 3] = 255;
                    }
                }
            }
            fs::create_directories("assets/sprites");
            stbi_write_png(playerPath.c_str(), SZ, SZ, 4, pixels.data(), SZ * 4);
        }
        sprite->texture = TextureCache::instance().load(playerPath);
        sprite->texturePath = playerPath;
    }

    LOG_INFO("Game", "Player entity created at (%d, %d)", Coords::tileX(16.0f), Coords::tileY(16.0f));
}

void GameApp::createTestEntities(World& world) {
    // Create a grid of ground tiles to show the world
    std::string grassPath = "assets/sprites/grass_tile.png";
    auto groundTex = TextureCache::instance().load(grassPath);

    if (!groundTex) {
        // Generate procedural grass tile and save to disk
        const int SIZE = 32;
        std::vector<unsigned char> pixels(SIZE * SIZE * 4);
        for (int y = 0; y < SIZE; y++) {
            for (int x = 0; x < SIZE; x++) {
                int i = (y * SIZE + x) * 4;
                int noise = ((x * 7 + y * 13) % 20) - 10;
                pixels[i + 0] = (unsigned char)(std::max)(0, (std::min)(255, 34 + noise));
                pixels[i + 1] = (unsigned char)(std::max)(0, (std::min)(255, 85 + noise));
                pixels[i + 2] = (unsigned char)(std::max)(0, (std::min)(255, 34 + noise));
                pixels[i + 3] = 255;
            }
        }
        fs::create_directories("assets/sprites");
        stbi_write_png(grassPath.c_str(), SIZE, SIZE, 4, pixels.data(), SIZE * 4);
        groundTex = TextureCache::instance().load(grassPath);
    }

    // Tiles placed so origin (0,0) is at tile corner, not tile center
    // Centers at 16, 48, 80... so edges align with 0, 32, 64...
    int tilesX = 32;
    int tilesY = 20;
    float tileSize = 32.0f;
    float half = tileSize * 0.5f;
    int halfX = tilesX / 2;
    int halfY = tilesY / 2;

    for (int y = 0; y < tilesY; y++) {
        for (int x = 0; x < tilesX; x++) {
            Entity* tile = world.createEntity("Tile");
            tile->setTag("ground");

            auto* transform = tile->addComponent<Transform>(
                (float)(x - halfX) * tileSize + half,
                (float)(y - halfY) * tileSize + half
            );
            transform->depth = 0.0f;

            auto* sprite = tile->addComponent<SpriteComponent>();
            sprite->texture = groundTex;
            sprite->texturePath = grassPath;
            sprite->size = {tileSize, tileSize};
        }
    }

    // Create some test objects (trees, rocks) scattered around
    std::string treePath = "assets/sprites/tree.png";
    auto objTex = TextureCache::instance().load(treePath);
    if (!objTex) {
        // Generate procedural tree and save to disk
        const int SIZE = 32;
        std::vector<unsigned char> pixels(SIZE * SIZE * 4, 0);
        for (int y = 0; y < SIZE; y++) {
            for (int x = 0; x < SIZE; x++) {
                int i = (y * SIZE + x) * 4;
                // Trunk at bottom (high y = bottom of image in PNG top-left coords)
                if (x >= 13 && x <= 18 && y >= 20) {
                    pixels[i + 0] = 101; pixels[i + 1] = 67;
                    pixels[i + 2] = 33;  pixels[i + 3] = 255;
                } else {
                    // Canopy at top
                    float dx = x - 16.0f;
                    float dy = y - 10.0f;
                    if (dx * dx + dy * dy < 100.0f && y < 22) {
                        int noise = ((x * 3 + y * 7) % 30) - 15;
                        pixels[i + 0] = (unsigned char)(std::max)(0, (std::min)(255, 20 + noise));
                        pixels[i + 1] = (unsigned char)(std::max)(0, (std::min)(255, 100 + noise));
                        pixels[i + 2] = (unsigned char)(std::max)(0, (std::min)(255, 20 + noise));
                        pixels[i + 3] = 255;
                    }
                }
            }
        }
        stbi_write_png(treePath.c_str(), SIZE, SIZE, 4, pixels.data(), SIZE * 4);
        objTex = TextureCache::instance().load(treePath);
    }

    // Place some trees
    Vec2 treePositions[] = {
        {-128, 64}, {96, -96}, {-64, 128}, {192, 48},
        {-200, -64}, {64, 200}, {256, -128}, {-160, 180}
    };

    for (auto& pos : treePositions) {
        Entity* tree = world.createEntity("Tree");
        tree->setTag("obstacle");

        auto* transform = tree->addComponent<Transform>(pos);
        transform->depth = 5.0f;

        auto* sprite = tree->addComponent<SpriteComponent>();
        sprite->texture = objTex;
        sprite->texturePath = treePath;
        sprite->size = {32.0f, 48.0f}; // trees are taller

        // Tree collision — covers the trunk and lower canopy
        // Full-sprite collision (editor will allow custom polygon shapes)
        auto* collider = tree->addComponent<BoxCollider>();
        collider->size = {28.0f, 44.0f};
        collider->offset = {0.0f, 0.0f};
        collider->isStatic = true;
    }

    LOG_INFO("Game", "Test scene created: %zu entities total", world.entityCount());
}

void GameApp::spawnTestMobs(World& world) {
    // Create a spawn zone entity
    Entity* zone = world.createEntity("WhisperingWoods_SpawnZone");
    zone->setTag("spawnzone");
    auto* zoneTransform = zone->addComponent<Transform>(0.0f, 0.0f);
    zoneTransform->depth = -5.0f; // Behind everything (invisible zone marker)
    auto* szComp = zone->addComponent<SpawnZoneComponent>();
    szComp->config.zoneName = "Whispering Woods";
    szComp->config.size = {400.0f, 300.0f};

    // Add spawn rules
    MobSpawnRule slimeRule;
    slimeRule.enemyId = "Slime";
    slimeRule.targetCount = 3;
    slimeRule.minLevel = 1; slimeRule.maxLevel = 2;
    slimeRule.baseHP = 30; slimeRule.baseDamage = 5;
    slimeRule.respawnSeconds = 10.0f;
    szComp->config.rules.push_back(slimeRule);

    MobSpawnRule goblinRule;
    goblinRule.enemyId = "Goblin";
    goblinRule.targetCount = 2;
    goblinRule.minLevel = 2; goblinRule.maxLevel = 3;
    goblinRule.baseHP = 50; goblinRule.baseDamage = 8;
    goblinRule.respawnSeconds = 15.0f;
    szComp->config.rules.push_back(goblinRule);

    MobSpawnRule wolfRule;
    wolfRule.enemyId = "Wolf";
    wolfRule.targetCount = 2;
    wolfRule.minLevel = 3; wolfRule.maxLevel = 4;
    wolfRule.baseHP = 70; wolfRule.baseDamage = 12;
    wolfRule.respawnSeconds = 20.0f;
    szComp->config.rules.push_back(wolfRule);

    MobSpawnRule mushroomRule;
    mushroomRule.enemyId = "Mushroom";
    mushroomRule.targetCount = 2;
    mushroomRule.minLevel = 1; mushroomRule.maxLevel = 1;
    mushroomRule.baseHP = 20; mushroomRule.baseDamage = 3;
    mushroomRule.isAggressive = false;
    mushroomRule.respawnSeconds = 10.0f;
    szComp->config.rules.push_back(mushroomRule);

    MobSpawnRule golemRule;
    golemRule.enemyId = "Forest_Golem";
    golemRule.targetCount = 1;
    golemRule.minLevel = 5; golemRule.maxLevel = 5;
    golemRule.baseHP = 200; golemRule.baseDamage = 25;
    golemRule.isBoss = true;
    golemRule.isAggressive = true;
    golemRule.respawnSeconds = 60.0f;
    szComp->config.rules.push_back(golemRule);

    LOG_INFO("Game", "Created spawn zone '%s' with %zu rules",
             szComp->config.zoneName.c_str(), szComp->config.rules.size());
}

void GameApp::spawnTestNPCs(World& world) {
    NPCTemplate testNPC;
    testNPC.name = "Village Elder";
    testNPC.npcId = 1;
    testNPC.position = {256.0f, 256.0f};
    testNPC.isQuestGiver = true;
    testNPC.questIds = {1, 2, 3};
    testNPC.dialogueGreeting = "Welcome, adventurer! I have tasks for you.";
    EntityFactory::createNPC(world, testNPC);

    NPCTemplate merchantNPC;
    merchantNPC.name = "Potion Merchant";
    merchantNPC.npcId = 2;
    merchantNPC.position = {320.0f, 256.0f};
    merchantNPC.isMerchant = true;
    merchantNPC.shopName = "Potion Shop";
    merchantNPC.shopItems = {
        {"potion_hp_small", "Small HP Potion", 50, 12, 0},
        {"potion_mp_small", "Small MP Potion", 50, 12, 0},
        {"potion_hp_medium", "Medium HP Potion", 200, 50, 0}
    };
    merchantNPC.dialogueGreeting = "Welcome to my shop! Take a look around.";
    EntityFactory::createNPC(world, merchantNPC);

    LOG_INFO("Game", "Test NPCs created (Village Elder, Potion Merchant)");
}

void GameApp::onUpdate(float deltaTime) {
    // F1 HUD toggle removed — HUD is always on
    // F2 collision debug removed — now controlled via editor toolbar toggle
    auto& input = Input::instance();

    // UI toggles — action map suppresses these in Chat context automatically
    if (input.isActionPressed(ActionId::ToggleInventory) && !Editor::instance().wantsKeyboard()) {
        InventoryUI::instance().toggle();
    }
    if (input.isActionPressed(ActionId::ToggleSkillBar) && !Editor::instance().wantsKeyboard()) {
        SkillBarUI::instance().toggle();
    }
    if (input.isActionPressed(ActionId::ToggleQuestLog) && !Editor::instance().wantsKeyboard()) {
        questLogUI_.toggle();
    }
    // Skill bar page switching
    if (input.isActionPressed(ActionId::SkillPagePrev) && !Editor::instance().wantsKeyboard()) {
        SkillBarUI::instance().prevPage();
    }
    if (input.isActionPressed(ActionId::SkillPageNext) && !Editor::instance().wantsKeyboard()) {
        SkillBarUI::instance().nextPage();
    }
}

void GameApp::onRender(SpriteBatch& batch, Camera& camera) {
    // Tilemap (behind everything)
    if (tilemap_) {
        Mat4 vp = camera.getViewProjection();
        batch.begin(vp);
        tilemap_->render(batch, camera, -10.0f);
        batch.end();
    }

    // Entity sprites
    if (renderSystem_) {
        renderSystem_->update(0.0f);
    }

    // Floating damage/XP text (rendered in world space)
    if (combatSystem_) {
        combatSystem_->renderFloatingTexts(batch, camera);
    }
    // Debug overlays — only in editor/pause mode
    if (Editor::instance().isPaused()) {
        if (Editor::instance().showCollisionDebug()) {
            renderCollisionDebug(batch, camera);
        }
        renderAggroRadius(batch, camera);
        if (auto* spawnSys = SceneManager::instance().currentScene()->world().getSystem<SpawnSystem>()) {
            spawnSys->renderDebug(batch, camera);
        }
    }

    // ImGui game UI — suppress when editor is open and paused (no gameplay happening)
    if (!(Editor::instance().isOpen() && Editor::instance().isPaused())) {
        // Position HUD bars relative to the viewport panel
        auto& ed = Editor::instance();
        Vec2 vp = ed.viewportPos();
        Vec2 vs = ed.viewportSize();
        HudBarsUI::instance().setViewportRect(vp.x, vp.y, vs.x, vs.y);

        auto* scene = SceneManager::instance().currentScene();
        if (scene) {
            World* w = &scene->world();
            HudBarsUI::instance().draw(w);
            SkillBarUI::instance().draw(w);
            InventoryUI::instance().draw(w);

            // NPC dialogue UI
            if (npcInteractionSystem_ && npcInteractionSystem_->dialogueOpen
                && npcInteractionSystem_->interactingNPC) {
                npcDialogueUI_.render(
                    npcInteractionSystem_->interactingNPC,
                    npcInteractionSystem_->localPlayer,
                    npcInteractionSystem_,
                    questSystem_);
            }

            // Quest log UI
            if (questLogUI_.isOpen && npcInteractionSystem_ && npcInteractionSystem_->localPlayer) {
                questLogUI_.render(npcInteractionSystem_->localPlayer);
            }
        }
    }

    // HUD bar positions are configured per device preset via normalized coords

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

void GameApp::onShutdown() {
    tilemap_.reset();
    SDFText::instance().shutdown();
    delete renderSystem_;
    renderSystem_ = nullptr;
    LOG_INFO("Game", "Game shutting down...");
}

} // namespace fate
