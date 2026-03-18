#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/scene/scene_manager.h"
#include "engine/net/protocol.h"
#include "engine/ecs/persistent_id.h"
#include "game/entity_factory.h"
#include "game/shared/game_types.h"
#include "game/components/game_components.h"
#include "game/components/faction_component.h"
#include "game/components/dropped_item_component.h"
#include "game/shared/item_stat_roller.h"
#include "game/components/pet_component.h"
#include "engine/net/game_messages.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

#include <chrono>
#include <thread>
#include <cstdlib>
#include <random>

namespace fate {

bool ServerApp::init(uint16_t port) {
    if (!NetSocket::initPlatform()) {
        LOG_ERROR("Server", "Failed to init network platform");
        return false;
    }

    if (!server_.start(port)) {
        LOG_ERROR("Server", "Failed to start on port %d", port);
        return false;
    }

    // Set callbacks
    server_.onClientConnected = [this](uint16_t id) { onClientConnected(id); };
    server_.onClientDisconnected = [this](uint16_t id) { onClientDisconnected(id); };
    server_.onPacketReceived = [this](uint16_t id, uint8_t type, ByteReader& r) {
        onPacketReceived(id, type, r);
    };

    // Register gameplay systems (no render systems)
    // For now, empty world — systems will be added when we have entity replication

    // Register existing mobs for replication
    world_.forEach<Transform, EnemyStatsComponent>([&](Entity* entity, Transform*, EnemyStatsComponent*) {
        PersistentId pid = PersistentId::generate(1);
        replication_.registerEntity(entity->handle(), pid);
    });

    // DB connection
    const char* dbUrl = std::getenv("DATABASE_URL");
    if (dbUrl) {
        dbConnectionString_ = dbUrl;
    }

    if (dbConnectionString_.empty()) {
        LOG_ERROR("Server", "DATABASE_URL not set; cannot start server without DB");
        return false;
    }

    if (!gameDbConn_.connect(dbConnectionString_)) {
        LOG_ERROR("Server", "Failed to connect to game DB");
        return false;
    }

    // Initialize connection pool
    DbPool::Config poolCfg;
    poolCfg.connectionString = dbConnectionString_;
    poolCfg.minConnections = 5;
    poolCfg.maxConnections = 50;
    if (!dbPool_.initialize(poolCfg)) {
        LOG_ERROR("Server", "Failed to initialize DB connection pool");
        return false;
    }
    dbDispatcher_.init(&dbPool_);

    // Create repositories (all use gameDbConn_ for synchronous operations)
    characterRepo_ = std::make_unique<CharacterRepository>(gameDbConn_.connection());
    inventoryRepo_ = std::make_unique<InventoryRepository>(gameDbConn_.connection());
    skillRepo_     = std::make_unique<SkillRepository>(gameDbConn_.connection());
    guildRepo_     = std::make_unique<GuildRepository>(gameDbConn_.connection());
    socialRepo_    = std::make_unique<SocialRepository>(gameDbConn_.connection());
    marketRepo_    = std::make_unique<MarketRepository>(gameDbConn_.connection());
    tradeRepo_     = std::make_unique<TradeRepository>(gameDbConn_.connection());
    bountyRepo_    = std::make_unique<BountyRepository>(gameDbConn_.connection());
    questRepo_     = std::make_unique<QuestRepository>(gameDbConn_.connection());
    bankRepo_      = std::make_unique<BankRepository>(gameDbConn_.connection());
    petRepo_       = std::make_unique<PetRepository>(gameDbConn_.connection());
    mobStateRepo_  = std::make_unique<ZoneMobStateRepository>(gameDbConn_.connection());

    // Initialize definition caches
    itemDefCache_.initialize(gameDbConn_.connection());
    lootTableCache_.initialize(gameDbConn_.connection(), itemDefCache_);
    mobDefCache_.initialize(gameDbConn_.connection());
    skillDefCache_.initialize(gameDbConn_.connection());
    sceneCache_.initialize(gameDbConn_.connection());
    LOG_INFO("Server", "Caches loaded: %zu items, %zu loot tables, %d mobs, %d skills (%d ranks), %d scenes",
             itemDefCache_.size(), lootTableCache_.tableCount(),
             mobDefCache_.count(), skillDefCache_.skillCount(), skillDefCache_.rankCount(),
             sceneCache_.count());

    // Auth server startup (warning only — game server can run without auth in dev)
    if (!authServer_.start(authPort_, tlsCertPath_, tlsKeyPath_, dbConnectionString_)) {
        LOG_WARN("Server", "Auth server failed to start on port %d; continuing without auth", authPort_);
    }

    LOG_INFO("Server", "Started on port %d at %.0f ticks/sec", port, TICK_RATE);
    return true;
}

void ServerApp::run() {
    running_ = true;
    auto lastTick = std::chrono::high_resolution_clock::now();

#ifdef _WIN32
    timeBeginPeriod(1);
#endif

    while (running_) {
        auto now = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastTick).count();

        if (elapsed >= TICK_INTERVAL) {
            lastTick = now;
            gameTime_ += TICK_INTERVAL;
            tick(TICK_INTERVAL);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

void ServerApp::shutdown() {
    // Save all connected players before stopping
    server_.connections().forEach([this](ClientConnection& c) {
        savePlayerToDB(c.clientId);
    });

    authServer_.stop();
    dbPool_.shutdown();
    gameDbConn_.disconnect();

    server_.stop();
    NetSocket::shutdownPlatform();
    LOG_INFO("Server", "Shutdown complete");
}

void ServerApp::tick(float dt) {
    // Reset per-tick move counters
    for (auto& [id, count] : moveCountThisTick_) count = 0;

    // 1. Drain incoming packets
    server_.poll(gameTime_);

    // 2. Drain auth results
    consumePendingSessions();

    // 3. World update (systems)
    world_.update(dt);

    // Despawn expired ground items
    {
        std::vector<EntityHandle> toDestroy;
        world_.forEach<DroppedItemComponent>([&](Entity* e, DroppedItemComponent* drop) {
            if (gameTime_ - drop->spawnTime > drop->despawnAfter) {
                toDestroy.push_back(e->handle());
            }
        });
        for (auto handle : toDestroy) {
            replication_.unregisterEntity(handle);
            world_.destroyEntity(handle);
        }
    }

    // Boss spawn tick (0.25s interval)
    bossTickTimer_ += dt;
    if (bossTickTimer_ >= 0.25f) {
        bossTickTimer_ = 0.0f;

        std::vector<EntityHandle> bossZoneHandles;
        world_.forEach<Transform, BossSpawnPointComponent>(
            [&](Entity* e, Transform*, BossSpawnPointComponent*) {
                bossZoneHandles.push_back(e->handle());
            }
        );

        for (auto handle : bossZoneHandles) {
            Entity* zoneEntity = world_.getEntity(handle);
            if (!zoneEntity) continue;
            auto* bossComp = zoneEntity->getComponent<BossSpawnPointComponent>();
            auto* zoneT = zoneEntity->getComponent<Transform>();
            if (!bossComp || !zoneT) continue;

            // Initialize: load persisted death state
            if (!bossComp->initialized) {
                bossComp->initialized = true;
                auto* sc = SceneManager::instance().currentScene();
                std::string sceneName = sc ? sc->name() : "unknown";
                auto deaths = mobStateRepo_->loadZoneDeaths(sceneName, bossComp->bossDefId);
                for (const auto& death : deaths) {
                    if (!death.hasRespawned()) {
                        bossComp->respawnAt = gameTime_ + death.getRemainingRespawnTime();
                        bossComp->bossAlive = false;
                    }
                }
                // Spawn boss if no pending respawn
                if (bossComp->respawnAt <= 0.0f && !bossComp->spawnCoordinates.empty()) {
                    thread_local std::mt19937 bossRng{std::random_device{}()};
                    std::uniform_int_distribution<int> coordDist(
                        0, static_cast<int>(bossComp->spawnCoordinates.size()) - 1);
                    int idx = coordDist(bossRng);
                    bossComp->lastSpawnIndex = idx;
                    Vec2 spawnPos = bossComp->spawnCoordinates[idx];

                    Entity* boss = EntityFactory::createMob(
                        world_, bossComp->bossDefId, bossComp->levelOverride > 0 ? bossComp->levelOverride : 1,
                        500, 50, spawnPos, true, true);
                    bossComp->bossEntityHandle = boss->handle();
                    bossComp->bossAlive = true;

                    PersistentId pid = PersistentId::generate(1);
                    replication_.registerEntity(boss->handle(), pid);
                }
                continue;
            }

            // Detect death
            if (bossComp->bossAlive && bossComp->bossEntityHandle) {
                Entity* boss = world_.getEntity(bossComp->bossEntityHandle);
                if (boss) {
                    auto* es = boss->getComponent<EnemyStatsComponent>();
                    if (es && !es->stats.isAlive) {
                        bossComp->bossAlive = false;
                        bossComp->respawnAt = gameTime_ + 300.0f;

                        auto* sc = SceneManager::instance().currentScene();
                        std::string sceneName = sc ? sc->name() : "unknown";
                        DeadMobRecord rec;
                        rec.enemyId = bossComp->bossDefId;
                        rec.mobIndex = 0;
                        rec.diedAtUnix = static_cast<int64_t>(std::time(nullptr));
                        rec.respawnSeconds = 300;
                        mobStateRepo_->saveZoneDeaths(sceneName, bossComp->bossDefId, {rec});
                    }
                }
            }

            // Process respawn
            if (!bossComp->bossAlive && bossComp->respawnAt > 0.0f && gameTime_ >= bossComp->respawnAt) {
                bossComp->respawnAt = 0.0f;

                if (!bossComp->spawnCoordinates.empty()) {
                    thread_local std::mt19937 bossRng{std::random_device{}()};
                    int idx = 0;
                    if (bossComp->spawnCoordinates.size() > 1) {
                        do {
                            std::uniform_int_distribution<int> coordDist(
                                0, static_cast<int>(bossComp->spawnCoordinates.size()) - 1);
                            idx = coordDist(bossRng);
                        } while (idx == bossComp->lastSpawnIndex);
                    }
                    bossComp->lastSpawnIndex = idx;
                    Vec2 spawnPos = bossComp->spawnCoordinates[idx];

                    Entity* boss = EntityFactory::createMob(
                        world_, bossComp->bossDefId, bossComp->levelOverride > 0 ? bossComp->levelOverride : 1,
                        500, 50, spawnPos, true, true);
                    bossComp->bossEntityHandle = boss->handle();
                    bossComp->bossAlive = true;

                    PersistentId pid = PersistentId::generate(1);
                    replication_.registerEntity(boss->handle(), pid);

                    auto* sc = SceneManager::instance().currentScene();
                    std::string sceneName = sc ? sc->name() : "unknown";
                    mobStateRepo_->clearZoneDeaths(sceneName, bossComp->bossDefId);
                }
            }
        }
    }

    // 4. Replicate entity state to connected clients
    replication_.update(world_, server_);

    // 5. Retransmit unacked reliable packets
    server_.processRetransmits(gameTime_);

    // 5b. Drain async DB completions
    dbDispatcher_.drainCompletions();

    // 5c. Auto-save and periodic maintenance
    tickAutoSave(dt);
    tickMaintenance(dt);

    // 6. Check timeouts
    auto timedOut = server_.checkTimeouts(gameTime_);
    for (uint16_t id : timedOut) {
        LOG_INFO("Server", "Client %d timed out", id);
        if (server_.onClientDisconnected) server_.onClientDisconnected(id);
        server_.connections().removeClient(id);
    }

    // 7. Clean expired pending sessions
    for (auto it = pendingSessions_.begin(); it != pendingSessions_.end(); ) {
        if (static_cast<double>(gameTime_) > it->second.expires_at) {
            it = pendingSessions_.erase(it);
        } else {
            ++it;
        }
    }
}

void ServerApp::consumePendingSessions() {
    AuthResult result;
    while (authServer_.popAuthResult(result)) {
        // Duplicate login: kick existing session for this account
        auto existing = activeAccountSessions_.find(result.session.account_id);
        if (existing != activeAccountSessions_.end()) {
            uint16_t oldClientId = existing->second;
            LOG_INFO("Server", "Duplicate login for account %d; kicking client %d",
                     result.session.account_id, oldClientId);
            savePlayerToDB(oldClientId);

            auto* oldClient = server_.connections().findById(oldClientId);
            if (oldClient) {
                // Disconnect them
                if (server_.onClientDisconnected) server_.onClientDisconnected(oldClientId);
                server_.connections().removeClient(oldClientId);
            }
            activeAccountSessions_.erase(existing);
        }

        // Store pending session with 30s expiry
        result.session.expires_at = static_cast<double>(gameTime_) + 30.0;
        pendingSessions_[result.token] = result.session;
    }
}

void ServerApp::onClientConnected(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client) return;

    // Drain any pending auth results that arrived between ticks
    consumePendingSessions();

    // Look up auth token in pending sessions
    auto it = pendingSessions_.find(client->authToken);
    if (it == pendingSessions_.end()) {
        LOG_WARN("Server", "Client %d has invalid or expired auth token; rejecting", clientId);
        server_.sendConnectReject(client->address, "Invalid or expired auth token");
        server_.connections().removeClient(clientId);
        return;
    }

    // Consume the pending session
    PendingSession session = it->second;
    pendingSessions_.erase(it);

    client->account_id = session.account_id;
    client->character_id = session.character_id;

    // Load character from DB
    auto recOpt = characterRepo_->loadCharacter(session.character_id);
    if (!recOpt) {
        LOG_ERROR("Server", "Client %d: failed to load character '%s'; rejecting",
                  clientId, session.character_id.c_str());
        server_.sendConnectReject(client->address, "Character not found");
        server_.connections().removeClient(clientId);
        return;
    }
    const CharacterRecord& rec = *recOpt;

    // Determine ClassType from class_name string
    ClassType classType = ClassType::Warrior;
    if (rec.class_name == "Mage") {
        classType = ClassType::Mage;
    } else if (rec.class_name == "Archer") {
        classType = ClassType::Archer;
    }

    // Create player entity
    Entity* player = EntityFactory::createPlayer(world_, rec.character_name, classType, false, Faction::None);

    // Override stats with DB values
    auto* charStatsComp = player->getComponent<CharacterStatsComponent>();
    if (charStatsComp) {
        CharacterStats& s = charStatsComp->stats;
        s.level        = rec.level;
        s.currentXP    = rec.current_xp;
        s.xpToNextLevel = static_cast<int64_t>(rec.xp_to_next_level);
        s.honor        = rec.honor;
        s.pvpKills     = rec.pvp_kills;
        s.pvpDeaths    = rec.pvp_deaths;
        s.isDead       = rec.is_dead;

        // Recalculate derived stats (resets currentHP/MP to max)
        s.recalculateStats();

        // Re-apply saved HP/MP after recalc
        s.currentHP    = rec.current_hp;
        s.currentMP    = rec.current_mp;
        s.currentFury  = rec.current_fury;
        s.maxHP        = rec.max_hp;
        s.maxMP        = rec.max_mp;
    }

    // Set position (DB stores tile coords, engine uses pixel coords)
    auto* t = player->getComponent<Transform>();
    if (t) {
        t->position = Coords::toPixel({rec.position_x, rec.position_y});
    }

    // Set gold on inventory
    auto* inv = player->getComponent<InventoryComponent>();
    if (inv) {
        inv->inventory.addGold(rec.gold);
    }

    // Assign persistent ID based on hash of character_id
    uint64_t pidVal = std::hash<std::string>{}(rec.character_id);
    if (pidVal == 0) pidVal = 1;
    PersistentId pid(pidVal);

    // Register with replication
    replication_.registerEntity(player->handle(), pid);

    // Link client to their player entity
    client->playerEntityId = pid.value();

    // Track active account session
    activeAccountSessions_[session.account_id] = clientId;

    // Load skills from DB
    auto* skillComp = player->getComponent<SkillManagerComponent>();
    if (skillComp) {
        auto skills = skillRepo_->loadCharacterSkills(rec.character_id);
        for (const auto& s : skills) {
            skillComp->skills.learnSkill(s.skillId, s.unlockedRank);
            // activateSkillRank activates the NEXT rank each call
            for (int r = 0; r < s.activatedRank; ++r)
                skillComp->skills.activateSkillRank(s.skillId);
        }
        auto skillBar = skillRepo_->loadSkillBar(rec.character_id);
        for (int i = 0; i < static_cast<int>(skillBar.size()) && i < 20; ++i) {
            if (!skillBar[i].empty())
                skillComp->skills.assignSkillToSlot(skillBar[i], i);
        }
        auto sp = skillRepo_->loadSkillPoints(rec.character_id);
        // Restore earned skill points; activateSkillRank above already consumed spent ones
        for (int i = 0; i < sp.totalEarned; ++i)
            skillComp->skills.grantSkillPoint();
    }

    // Load guild membership
    auto* guildComp = player->getComponent<GuildComponent>();
    if (guildComp) {
        guildComp->guild.serverInitialize(rec.character_id);
        int guildId = guildRepo_->getPlayerGuildId(rec.character_id);
        if (guildId > 0) {
            auto guildInfo = guildRepo_->getGuildInfo(guildId);
            int rank = guildRepo_->getPlayerRank(rec.character_id);
            if (guildInfo) {
                guildComp->guild.setGuildData(guildInfo->guildId, guildInfo->guildName,
                                               {}, static_cast<GuildRank>(rank),
                                               guildInfo->guildLevel);
            }
        }
    }

    // Load friends and blocks
    auto* friendsComp = player->getComponent<FriendsComponent>();
    if (friendsComp) {
        auto friends = socialRepo_->getFriends(rec.character_id);
        auto blocks = socialRepo_->getBlockedPlayers(rec.character_id);
        // FriendsManager is initialized from these records via the networking layer
        // For now, the data is loaded and ready for when sync messages are sent
        friendsComp->friends.initialize(rec.character_id);
    }

    // Load quest progress
    auto* questComp = player->getComponent<QuestComponent>();
    if (questComp) {
        auto activeQuests = questRepo_->loadQuestProgress(rec.character_id);
        auto completedIds = questRepo_->loadCompletedQuests(rec.character_id);

        // Convert DB records to QuestManager format
        std::vector<uint32_t> completedU32;
        completedU32.reserve(completedIds.size());
        for (const auto& id : completedIds) {
            try { completedU32.push_back(static_cast<uint32_t>(std::stoul(id))); }
            catch (...) { /* skip non-numeric quest IDs */ }
        }

        std::vector<ActiveQuest> activeQuestObjs;
        for (const auto& q : activeQuests) {
            ActiveQuest aq;
            try { aq.questId = static_cast<uint32_t>(std::stoul(q.questId)); }
            catch (...) { continue; }
            // Restore progress — single-objective quests store count in index 0
            if (q.currentCount > 0) {
                aq.objectiveProgress.push_back(static_cast<uint16_t>(q.currentCount));
            }
            activeQuestObjs.push_back(std::move(aq));
        }

        questComp->quests.setSerializedState(std::move(completedU32), std::move(activeQuestObjs));
    }

    // Load bank storage
    auto* bankComp = player->getComponent<BankStorageComponent>();
    if (bankComp) {
        int64_t bankGold = bankRepo_->loadBankGold(rec.character_id);
        bankComp->storage.depositGold(bankGold, 0.0f); // no fee on load
        // Bank items loaded on-demand when player opens banker NPC
    }

    // Load equipped pet
    auto* petComp = player->getComponent<PetComponent>();
    if (petComp) {
        auto equippedPet = petRepo_->loadEquippedPet(rec.character_id);
        if (equippedPet) {
            petComp->equippedPet.petDefinitionId = equippedPet->petDefId;
            petComp->equippedPet.petName = equippedPet->petName;
            petComp->equippedPet.level = equippedPet->level;
            petComp->equippedPet.currentXP = equippedPet->currentXP;
            petComp->equippedPet.autoLootEnabled = equippedPet->autoLootEnabled;
            petComp->equippedPet.isSoulbound = equippedPet->isSoulbound;
            petComp->dbPetId = equippedPet->id;
        }
    }

    // Update last_online timestamp
    socialRepo_->updateLastOnline(rec.character_id);

    // Initialize movement tracking
    lastValidPositions_[clientId] = t ? t->position : Vec2{0.0f, 0.0f};
    lastMoveTime_[clientId] = gameTime_;
    moveCountThisTick_[clientId] = 0;

    // Stagger auto-save (offset by clientId to spread DB load)
    float saveOffset = static_cast<float>(clientId % 60);
    nextAutoSaveTime_[clientId] = gameTime_ + saveOffset + AUTO_SAVE_INTERVAL;

    // Send initial player state
    sendPlayerState(clientId);

    LOG_INFO("Server", "Client %d connected: account=%d char='%s' level=%d guild=%d",
             clientId, session.account_id, rec.character_name.c_str(), rec.level,
             guildComp ? guildComp->guild.guildId : 0);
}

void ServerApp::savePlayerToDB(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = replication_.getEntityHandle(pid);
    Entity* e = world_.getEntity(h);
    if (!e) return;

    CharacterRecord rec;
    rec.character_id = client->character_id;
    rec.account_id   = client->account_id;

    auto* charStatsComp = e->getComponent<CharacterStatsComponent>();
    if (charStatsComp) {
        const CharacterStats& s = charStatsComp->stats;
        rec.character_name   = s.characterName;
        rec.class_name       = s.className;
        rec.level            = s.level;
        rec.current_xp       = s.currentXP;
        rec.xp_to_next_level = static_cast<int>(s.xpToNextLevel);
        rec.current_hp       = s.currentHP;
        rec.max_hp           = s.maxHP;
        rec.current_mp       = s.currentMP;
        rec.max_mp           = s.maxMP;
        rec.current_fury     = s.currentFury;
        rec.honor            = s.honor;
        rec.pvp_kills        = s.pvpKills;
        rec.pvp_deaths       = s.pvpDeaths;
        rec.is_dead          = s.isDead;
    }

    auto* t = e->getComponent<Transform>();
    if (t) {
        // Convert pixel coords to tile coords for DB (matches Unity format)
        Vec2 tilePos = Coords::toTile(t->position);
        rec.position_x = tilePos.x;
        rec.position_y = tilePos.y;
    }

    // Save current scene name
    auto* sc = SceneManager::instance().currentScene();
    rec.current_scene = sc ? sc->name() : "Scene2";

    auto* inv = e->getComponent<InventoryComponent>();
    if (inv) {
        rec.gold = inv->inventory.getGold();
    }

    if (!characterRepo_->saveCharacter(rec)) {
        // Retry once
        if (!characterRepo_->saveCharacter(rec)) {
            LOG_ERROR("Server", "DATA LOSS: failed to save character '%s' (client %d) after retry",
                      rec.character_id.c_str(), clientId);
        }
    }

    // Save skills
    auto* skillComp = e->getComponent<SkillManagerComponent>();
    if (skillComp) {
        // Collect learned skills from vector
        std::vector<CharacterSkillRecord> skillRecords;
        for (const auto& learned : skillComp->skills.getLearnedSkills()) {
            CharacterSkillRecord sr;
            sr.skillId = learned.skillId;
            sr.unlockedRank = learned.unlockedRank;
            sr.activatedRank = learned.activatedRank;
            skillRecords.push_back(std::move(sr));
        }
        skillRepo_->saveAllCharacterSkills(rec.character_id, skillRecords);

        // Save skill bar
        std::vector<std::string> bar;
        bar.reserve(20);
        for (int i = 0; i < 20; ++i) {
            bar.push_back(skillComp->skills.getSkillInSlot(i));
        }
        skillRepo_->saveSkillBar(rec.character_id, bar);

        // Save skill points
        int earned = skillComp->skills.earnedPoints();
        int spent = earned - skillComp->skills.availablePoints();
        skillRepo_->saveSkillPoints(rec.character_id, earned, spent);
    }

    // Save quest progress
    auto* questComp = e->getComponent<QuestComponent>();
    if (questComp) {
        std::vector<QuestProgressRecord> questRecords;
        for (const auto& aq : questComp->quests.getActiveQuests()) {
            QuestProgressRecord qr;
            qr.questId = std::to_string(aq.questId);
            qr.status = "active";
            qr.currentCount = aq.objectiveProgress.empty() ? 0 : aq.objectiveProgress[0];
            qr.targetCount = 1; // Will be updated by quest definition lookup if needed
            questRecords.push_back(std::move(qr));
        }
        questRepo_->saveAllQuestProgress(rec.character_id, questRecords);
    }

    // Save bank gold (items saved on-demand when deposited/withdrawn)
    auto* bankComp = e->getComponent<BankStorageComponent>();
    if (bankComp) {
        int64_t bankGold = bankComp->storage.getStoredGold();
        if (bankGold > 0) {
            bankRepo_->depositGold(rec.character_id, 0); // ensure row exists
            // Direct set via raw query would be cleaner, but depositGold upserts
        }
    }

    // Save pet state
    auto* petComp = e->getComponent<PetComponent>();
    if (petComp && petComp->hasPet()) {
        PetRecord petRec;
        petRec.id = petComp->dbPetId;
        petRec.characterId = rec.character_id;
        petRec.petDefId = petComp->equippedPet.petDefinitionId;
        petRec.petName = petComp->equippedPet.petName;
        petRec.level = petComp->equippedPet.level;
        petRec.currentXP = petComp->equippedPet.currentXP;
        petRec.isEquipped = true;
        petRec.isSoulbound = petComp->equippedPet.isSoulbound;
        petRec.autoLootEnabled = petComp->equippedPet.autoLootEnabled;
        petRepo_->savePet(petRec);
    }

    // Update last_online
    socialRepo_->updateLastOnline(rec.character_id);
}

void ServerApp::onClientDisconnected(uint16_t clientId) {
    LOG_INFO("Server", "Client %d disconnected", clientId);

    // Save player data first
    savePlayerToDB(clientId);

    // Remove from active account sessions
    auto* client = server_.connections().findById(clientId);
    if (client && client->account_id != 0) {
        activeAccountSessions_.erase(client->account_id);
    }

    if (client && client->playerEntityId != 0) {
        PersistentId pid(client->playerEntityId);
        EntityHandle h = replication_.getEntityHandle(pid);
        if (h) {
            world_.destroyEntity(h);
            world_.processDestroyQueue();
        }
        replication_.unregisterEntity(h);
    }

    // Clean up tracking maps
    lastValidPositions_.erase(clientId);
    lastMoveTime_.erase(clientId);
    moveCountThisTick_.erase(clientId);
    nextAutoSaveTime_.erase(clientId);
}

void ServerApp::onPacketReceived(uint16_t clientId, uint8_t type, ByteReader& payload) {
    // Handle game packets — stub for now, will be implemented in Phase 6C/6D
    switch (type) {
        case PacketType::CmdMove: {
            auto move = CmdMove::read(payload);

            // Rate limit check
            int maxPerTick = static_cast<int>(MAX_MOVES_PER_SEC / TICK_RATE);
            if (maxPerTick < 1) maxPerTick = 1;
            moveCountThisTick_[clientId]++;
            if (moveCountThisTick_[clientId] > maxPerTick) {
                // Silently drop excess moves (was spamming logs)
                break;
            }

            auto* client = server_.connections().findById(clientId);
            if (client && client->playerEntityId != 0) {
                PersistentId pid(client->playerEntityId);
                EntityHandle h = replication_.getEntityHandle(pid);
                Entity* e = world_.getEntity(h);
                if (!e) break;

                // Compute time delta since last move
                float now = gameTime_;
                float timeDelta = now - lastMoveTime_[clientId];
                if (timeDelta < 0.001f) timeDelta = 0.001f;

                // Check distance against max allowed
                Vec2 lastPos = lastValidPositions_[clientId];
                float dist = lastPos.distance(move.position);
                float maxDist = MAX_MOVE_SPEED * timeDelta;

                if (dist > maxDist + RUBBER_BAND_THRESHOLD) {
                    // Rubber-band: reject move and send correction
                    LOG_WARN("Server", "Client %d moved too far (%.1f > %.1f), rubber-banding",
                             clientId, dist, maxDist);
                    SvMovementCorrectionMsg correction;
                    correction.correctedPosition = lastPos;
                    correction.rubberBand = 1;
                    uint8_t buf[32];
                    ByteWriter w(buf, sizeof(buf));
                    correction.write(w);
                    server_.sendTo(clientId, Channel::Unreliable,
                                   PacketType::SvMovementCorrection, buf, w.size());
                } else {
                    // Accept position
                    auto* t = e->getComponent<Transform>();
                    if (t) t->position = move.position;
                    lastValidPositions_[clientId] = move.position;
                    lastMoveTime_[clientId] = now;
                }
            }
            break;
        }
        case PacketType::CmdAction: {
            auto action = CmdAction::read(payload);
            processAction(clientId, action);
            break;
        }
        case PacketType::CmdChat: {
            auto chat = CmdChat::read(payload);
            LOG_INFO("Server", "Chat from client %d (ch=%d): %s",
                     clientId, chat.channel, chat.message.c_str());

            // Get sender info
            std::string senderName = "Unknown";
            uint8_t senderFaction = 0;
            auto* client = server_.connections().findById(clientId);
            if (client && client->playerEntityId != 0) {
                PersistentId pid(client->playerEntityId);
                EntityHandle h = replication_.getEntityHandle(pid);
                Entity* e = world_.getEntity(h);
                if (e) {
                    auto* nameplate = e->getComponent<NameplateComponent>();
                    if (nameplate) senderName = nameplate->displayName;
                    auto* factionComp = e->getComponent<FactionComponent>();
                    if (factionComp) senderFaction = static_cast<uint8_t>(factionComp->faction);
                }
            }

            // Build and broadcast chat message
            SvChatMessageMsg msg;
            msg.channel    = chat.channel;
            msg.senderName = senderName;
            msg.message    = chat.message;
            msg.faction    = senderFaction;

            uint8_t buf[512];
            ByteWriter w(buf, sizeof(buf));
            msg.write(w);
            server_.broadcast(Channel::ReliableOrdered, PacketType::SvChatMessage, buf, w.size());
            break;
        }
        default:
            LOG_WARN("Server", "Unknown packet type 0x%02X from client %d", type, clientId);
            break;
    }
}

void ServerApp::processAction(uint16_t clientId, const CmdAction& action) {
    // Find attacker's player entity
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId attackerPid(client->playerEntityId);
    EntityHandle attackerHandle = replication_.getEntityHandle(attackerPid);
    Entity* attacker = world_.getEntity(attackerHandle);
    if (!attacker) return;

    // Find target entity
    PersistentId targetPid(action.targetId);
    EntityHandle targetHandle = replication_.getEntityHandle(targetPid);
    Entity* target = world_.getEntity(targetHandle);
    if (!target) {
        LOG_WARN("Server", "Client %d action on invalid target %llu", clientId, action.targetId);
        return;
    }

    if (action.actionType == 0) {
        // Basic attack
        auto* attackerTransform = attacker->getComponent<Transform>();
        auto* targetTransform = target->getComponent<Transform>();
        if (!attackerTransform || !targetTransform) return;

        // Validate range
        float attackRange = 1.0f; // default
        auto* charStats = attacker->getComponent<CharacterStatsComponent>();
        if (charStats) {
            attackRange = charStats->stats.classDef.attackRange;
        }
        float maxRange = attackRange * 32.0f + 16.0f;
        float dist = attackerTransform->position.distance(targetTransform->position);
        if (dist > maxRange) {
            LOG_WARN("Server", "Client %d attack out of range (%.1f > %.1f)", clientId, dist, maxRange);
            return;
        }

        // Check target is a living enemy
        auto* enemyStats = target->getComponent<EnemyStatsComponent>();
        if (!enemyStats || !enemyStats->stats.isAlive) return;

        // Calculate damage
        int damage = 10; // default
        bool isCrit = false;
        if (charStats) {
            damage = charStats->stats.calculateDamage(false, isCrit);
        }

        // Apply damage
        enemyStats->stats.takeDamageFrom(attackerHandle.value, damage);
        bool killed = !enemyStats->stats.isAlive;

        // Build and broadcast combat event
        SvCombatEventMsg evt;
        evt.attackerId = attackerPid.value();
        evt.targetId   = targetPid.value();
        evt.damage     = damage;
        evt.skillId    = action.skillId;
        evt.isCrit     = isCrit ? 1 : 0;
        evt.isKill     = killed ? 1 : 0;

        uint8_t buf[64];
        ByteWriter w(buf, sizeof(buf));
        evt.write(w);
        server_.broadcast(Channel::ReliableOrdered, PacketType::SvCombatEvent, buf, w.size());

        if (killed) {
            auto* targetEnemyStats = target->getComponent<EnemyStatsComponent>();
            EnemyStats& es = targetEnemyStats->stats;
            Vec2 deathPos = targetTransform ? targetTransform->position : Vec2{0, 0};

            // Determine top damager for loot ownership
            uint32_t topDamagerId = attackerHandle.value;
            int topDamage = 0;
            for (const auto& [attackerId, totalDmg] : es.damageByAttacker) {
                if (totalDmg > topDamage) {
                    topDamage = totalDmg;
                    topDamagerId = attackerId;
                }
            }

            // Roll loot table
            if (!es.lootTableId.empty()) {
                auto drops = lootTableCache_.rollLoot(es.lootTableId);

                constexpr float kItemSpacing = 10.0f;
                constexpr int kMaxPerRow = 4;
                thread_local std::mt19937 dropRng{std::random_device{}()};
                std::uniform_real_distribution<float> jitter(-3.0f, 3.0f);

                int totalDrops = static_cast<int>(drops.size());
                int cols = std::min(totalDrops, kMaxPerRow);
                float gridWidth = (cols - 1) * kItemSpacing;

                for (size_t i = 0; i < drops.size(); ++i) {
                    int col = static_cast<int>(i) % kMaxPerRow;
                    int row = static_cast<int>(i) / kMaxPerRow;
                    Vec2 offset = {
                        (col * kItemSpacing) - (gridWidth * 0.5f) + jitter(dropRng),
                        row * kItemSpacing + jitter(dropRng)
                    };
                    Vec2 dropPos = {deathPos.x + offset.x, deathPos.y + offset.y};

                    Entity* dropEntity = EntityFactory::createDroppedItem(world_, dropPos, false);
                    auto* dropComp = dropEntity->getComponent<DroppedItemComponent>();
                    if (dropComp) {
                        dropComp->itemId = drops[i].item.itemId;
                        dropComp->quantity = drops[i].item.quantity;
                        dropComp->enchantLevel = drops[i].item.enchantLevel;
                        dropComp->rolledStatsJson = ItemStatRoller::rolledStatsToJson(drops[i].item.rolledStats);
                        dropComp->ownerEntityId = topDamagerId;
                        dropComp->spawnTime = gameTime_;

                        const auto* def = itemDefCache_.getDefinition(drops[i].item.itemId);
                        if (def) dropComp->rarity = def->rarity;
                    }

                    PersistentId dropPid = PersistentId::generate(1);
                    replication_.registerEntity(dropEntity->handle(), dropPid);
                }
            }

            // Roll gold drop
            if (es.goldDropChance > 0.0f) {
                thread_local std::mt19937 goldRng{std::random_device{}()};
                std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
                if (chanceDist(goldRng) <= es.goldDropChance && es.maxGoldDrop > 0) {
                    std::uniform_int_distribution<int> goldDist(es.minGoldDrop, es.maxGoldDrop);
                    int goldAmount = goldDist(goldRng);

                    Entity* goldEntity = EntityFactory::createDroppedItem(world_, deathPos, true);
                    auto* goldComp = goldEntity->getComponent<DroppedItemComponent>();
                    if (goldComp) {
                        goldComp->isGold = true;
                        goldComp->goldAmount = goldAmount;
                        goldComp->ownerEntityId = topDamagerId;
                        goldComp->spawnTime = gameTime_;
                    }

                    PersistentId goldPid = PersistentId::generate(1);
                    replication_.registerEntity(goldEntity->handle(), goldPid);
                }
            }

            // Hide mob sprite (SpawnSystem handles respawn)
            auto* mobSprite = target->getComponent<SpriteComponent>();
            if (mobSprite) mobSprite->enabled = false;

            LOG_INFO("Server", "Client %d killed mob '%s'", clientId, es.enemyName.c_str());
        }
    } else if (action.actionType == 3) {
        // Pickup
        PersistentId itemPid(action.targetId);
        EntityHandle itemHandle = replication_.getEntityHandle(itemPid);
        Entity* itemEntity = world_.getEntity(itemHandle);
        if (!itemEntity) return;

        auto* dropComp = itemEntity->getComponent<DroppedItemComponent>();
        if (!dropComp) return;

        // Validate proximity
        auto* playerT = attacker->getComponent<Transform>();
        auto* itemT = itemEntity->getComponent<Transform>();
        if (!playerT || !itemT) return;
        float dist = playerT->position.distance(itemT->position);
        if (dist > 48.0f) return;

        // Validate loot rights
        if (dropComp->ownerEntityId != 0 && dropComp->ownerEntityId != attackerHandle.value) {
            return;
        }

        // Process pickup
        auto* inv = attacker->getComponent<InventoryComponent>();
        if (!inv) return;

        SvLootPickupMsg pickupMsg;

        if (dropComp->isGold) {
            inv->inventory.addGold(dropComp->goldAmount);
            pickupMsg.isGold = 1;
            pickupMsg.goldAmount = dropComp->goldAmount;
            pickupMsg.displayName = "Gold";
        } else {
            ItemInstance item;
            item.instanceId = std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
            item.itemId = dropComp->itemId;
            item.quantity = dropComp->quantity;
            item.enchantLevel = dropComp->enchantLevel;
            item.rolledStats = ItemStatRoller::parseRolledStats(dropComp->rolledStatsJson);
            inv->inventory.addItem(item);

            pickupMsg.itemId = dropComp->itemId;
            pickupMsg.quantity = dropComp->quantity;
            pickupMsg.rarity = dropComp->rarity;

            const auto* def = itemDefCache_.getDefinition(dropComp->itemId);
            pickupMsg.displayName = def ? def->displayName : dropComp->itemId;
            if (dropComp->enchantLevel > 0) {
                pickupMsg.displayName += " +" + std::to_string(dropComp->enchantLevel);
            }
        }

        // Send pickup notification
        uint8_t buf[256];
        ByteWriter w(buf, sizeof(buf));
        pickupMsg.write(w);
        server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvLootPickup, buf, w.size());

        sendPlayerState(clientId);

        // Destroy the dropped item
        replication_.unregisterEntity(itemHandle);
        world_.destroyEntity(itemHandle);
    } else {
        LOG_INFO("Server", "Unhandled action type %d from client %d", action.actionType, clientId);
    }
}

void ServerApp::sendPlayerState(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = replication_.getEntityHandle(pid);
    Entity* e = world_.getEntity(h);
    if (!e) return;

    auto* charStats = e->getComponent<CharacterStatsComponent>();
    if (!charStats) return;

    const auto& s = charStats->stats;

    // Get gold from inventory if available
    int64_t gold = 0;
    auto* inv = e->getComponent<InventoryComponent>();
    if (inv) gold = inv->inventory.getGold();

    SvPlayerStateMsg msg;
    msg.currentHP   = s.currentHP;
    msg.maxHP       = s.maxHP;
    msg.currentMP   = s.currentMP;
    msg.maxMP       = s.maxMP;
    msg.currentFury = s.currentFury;
    msg.currentXP   = s.currentXP;
    msg.gold        = gold;
    msg.level       = s.level;

    uint8_t buf[64];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvPlayerState, buf, w.size());
}

void ServerApp::tickAutoSave(float /*dt*/) {
    server_.connections().forEach([this](ClientConnection& c) {
        auto it = nextAutoSaveTime_.find(c.clientId);
        if (it == nextAutoSaveTime_.end()) return;
        if (gameTime_ < it->second) return;

        // Stagger next save
        it->second = gameTime_ + AUTO_SAVE_INTERVAL;

        // Dispatch save to worker fiber (non-blocking)
        uint16_t clientId = c.clientId;
        savePlayerToDB(clientId);
    });
}

void ServerApp::tickMaintenance(float dt) {
    // Market listing expiry
    marketExpiryTimer_ += dt;
    if (marketExpiryTimer_ >= MARKET_EXPIRY_INTERVAL) {
        marketExpiryTimer_ = 0.0f;
        int expired = marketRepo_->deactivateExpiredListings();
        if (expired > 0) {
            LOG_INFO("Server", "Deactivated %d expired market listings", expired);
        }
    }

    // Bounty expiry
    bountyExpiryTimer_ += dt;
    if (bountyExpiryTimer_ >= BOUNTY_EXPIRY_INTERVAL) {
        bountyExpiryTimer_ = 0.0f;
        auto refunds = bountyRepo_->processExpiredBounties();
        for (const auto& r : refunds) {
            // Try to refund online players directly
            // (Offline refunds would need a separate offline_gold table — deferred)
            LOG_INFO("Server", "Bounty expired: refunding %lld gold to %s",
                     r.refund, r.contributorCharId.c_str());
        }
    }

    // Stale trade session cleanup
    tradeCleanupTimer_ += dt;
    if (tradeCleanupTimer_ >= TRADE_CLEANUP_INTERVAL) {
        tradeCleanupTimer_ = 0.0f;
        int cleaned = tradeRepo_->cleanStaleSessions(30);
        if (cleaned > 0) {
            LOG_INFO("Server", "Cleaned %d stale trade sessions", cleaned);
        }
    }
}

} // namespace fate
