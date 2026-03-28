#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/game_components.h"
#include "game/components/pet_component.h"
#include "game/shared/game_types.h"
#include "game/shared/item_stat_roller.h"
#include "engine/net/game_messages.h"

namespace fate {

void ServerApp::savePlayerToDB(uint16_t clientId, bool forceSaveAll) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* e = getWorldForClient(clientId).getEntity(h);
    if (!e) return;

    auto& dirty = playerDirty_[clientId];

    CharacterRecord rec;
    rec.character_id = client->character_id;
    rec.account_id   = client->account_id;

    auto* charStatsComp = e->getComponent<CharacterStatsComponent>();
    if (charStatsComp) {
        const CharacterStats& s = charStatsComp->stats;
        rec.character_name   = s.characterName;
        rec.class_name       = s.className;
        if (forceSaveAll || dirty.stats) {
            rec.level            = s.level;
            rec.current_xp       = s.currentXP;
            rec.xp_to_next_level = static_cast<int>(s.xpToNextLevel);
            rec.honor            = s.honor;
            rec.pvp_kills        = s.pvpKills;
            rec.pvp_deaths       = s.pvpDeaths;
            rec.pk_status        = static_cast<int>(s.pkStatus);
            rec.faction          = static_cast<int>(s.faction);
            // DISABLED: stat allocation removed — always write 0
            rec.free_stat_points = 0;
            rec.allocated_str    = 0;
            rec.allocated_int    = 0;
            rec.allocated_dex    = 0;
            rec.allocated_con    = 0;
            rec.allocated_wis    = 0;
            rec.recall_scene     = s.recallScene;
        }
        if (forceSaveAll || dirty.vitals) {
            rec.current_hp       = s.currentHP;
            rec.max_hp           = s.maxHP;
            rec.current_mp       = s.currentMP;
            rec.max_mp           = s.maxMP;
            rec.current_fury     = s.currentFury;
            rec.is_dead          = s.isDead;
        }
    }

    if (forceSaveAll || dirty.position) {
        auto* t = e->getComponent<Transform>();
        if (t) {
            // Convert pixel coords to tile coords for DB (matches Unity format)
            Vec2 tilePos = Coords::toTile(t->position);
            rec.position_x = tilePos.x;
            rec.position_y = tilePos.y;
        }

        // Save current scene from the player's own stats (not SceneManager, which
        // is a client-side concept — the server has no loaded scene).
        auto* statsForScene = e->getComponent<CharacterStatsComponent>();
        rec.current_scene = (statsForScene && !statsForScene->stats.currentScene.empty())
            ? statsForScene->stats.currentScene
            : "WhisperingWoods";
    }

    if (forceSaveAll || dirty.inventory) {
        auto* inv = e->getComponent<InventoryComponent>();
        if (inv) {
            rec.gold = inv->inventory.getGold();
        }
        // Save actual inventory items to character_inventory table
        saveInventoryForClient(clientId);
    }

    if (!characterRepo_->saveCharacter(rec)) {
        // Retry once
        if (!characterRepo_->saveCharacter(rec)) {
            LOG_ERROR("Server", "DATA LOSS: failed to save character '%s' (client %d) after retry",
                      rec.character_id.c_str(), clientId);
        }
    }

    // Save skills
    if (forceSaveAll || dirty.skills) {
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
    }

    // Save quest progress
    if (forceSaveAll || dirty.quests) {
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
    }

    // Save bank gold (items saved on-demand when deposited/withdrawn)
    if (forceSaveAll || dirty.bank) {
        auto* bankComp = e->getComponent<BankStorageComponent>();
        if (bankComp) {
            int64_t bankGold = bankComp->storage.getStoredGold();
            if (bankGold > 0) {
                bankRepo_->depositGold(rec.character_id, 0); // ensure row exists
                // Direct set via raw query would be cleaner, but depositGold upserts
            }
        }
    }

    // Save pet state
    if (forceSaveAll || dirty.pet) {
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
    }

    // Update last_online
    socialRepo_->updateLastOnline(rec.character_id);

    if (!forceSaveAll) {
        dirty.clearAll();
    }
}

void ServerApp::enqueuePersist(uint16_t clientId, PersistPriority priority, PersistType type) {
    uint64_t key = (static_cast<uint64_t>(clientId) << 8) | static_cast<uint64_t>(type);
    auto it = pendingPersist_.find(key);
    if (it != pendingPersist_.end()) {
        float elapsed = gameTime_ - it->second;
        if (elapsed < 1.0f) return; // dedup window
    }
    pendingPersist_[key] = gameTime_;
    persistQueue_.enqueue(clientId, priority, type, gameTime_);
}

void ServerApp::tickPersistQueue() {
    if (persistQueue_.empty()) return;
    auto batch = persistQueue_.dequeue(10, gameTime_);
    for (auto& req : batch) {
        savePlayerToDBAsync(req.clientId, false); // false = check dirty flags
    }
}

void ServerApp::savePlayerToDBAsync(uint16_t clientId, bool forceSaveAll) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* e = getWorldForClient(clientId).getEntity(h);
    if (!e) return;

    auto& dirty = playerDirty_[clientId];

    // ---- Snapshot all data on game thread ----
    CharacterRecord rec;
    rec.character_id = client->character_id;
    rec.account_id   = client->account_id;

    auto* charStatsComp = e->getComponent<CharacterStatsComponent>();
    if (charStatsComp) {
        const CharacterStats& s = charStatsComp->stats;
        rec.character_name   = s.characterName;
        rec.class_name       = s.className;
        if (forceSaveAll || dirty.stats) {
            rec.level            = s.level;
            rec.current_xp       = s.currentXP;
            rec.xp_to_next_level = static_cast<int>(s.xpToNextLevel);
            rec.honor            = s.honor;
            rec.pvp_kills        = s.pvpKills;
            rec.pvp_deaths       = s.pvpDeaths;
            rec.pk_status        = static_cast<int>(s.pkStatus);
            rec.faction          = static_cast<int>(s.faction);
            // DISABLED: stat allocation removed — always write 0
            rec.free_stat_points = 0;
            rec.allocated_str    = 0;
            rec.allocated_int    = 0;
            rec.allocated_dex    = 0;
            rec.allocated_con    = 0;
            rec.allocated_wis    = 0;
            rec.recall_scene     = s.recallScene;
        }
        if (forceSaveAll || dirty.vitals) {
            rec.current_hp       = s.currentHP;
            rec.max_hp           = s.maxHP;
            rec.current_mp       = s.currentMP;
            rec.max_mp           = s.maxMP;
            rec.current_fury     = s.currentFury;
            rec.is_dead          = s.isDead;
        }
    }

    if (forceSaveAll || dirty.position) {
        auto* t = e->getComponent<Transform>();
        if (t) {
            Vec2 tilePos = Coords::toTile(t->position);
            rec.position_x = tilePos.x;
            rec.position_y = tilePos.y;
        }

        auto* statsForScene2 = e->getComponent<CharacterStatsComponent>();
        rec.current_scene = (statsForScene2 && !statsForScene2->stats.currentScene.empty())
            ? statsForScene2->stats.currentScene
            : "WhisperingWoods";
    }

    if (forceSaveAll || dirty.inventory) {
        auto* inv = e->getComponent<InventoryComponent>();
        if (inv) rec.gold = inv->inventory.getGold();
    }

    // Snapshot skills
    bool saveSkills = forceSaveAll || dirty.skills;
    std::vector<CharacterSkillRecord> skillRecords;
    int skillEarned = 0, skillSpent = 0;
    std::vector<std::string> skillBar(20, "");
    if (saveSkills) {
        auto* skillComp = e->getComponent<SkillManagerComponent>();
        if (skillComp) {
            for (const auto& learned : skillComp->skills.getLearnedSkills()) {
                CharacterSkillRecord sr;
                sr.skillId = learned.skillId;
                sr.unlockedRank = learned.unlockedRank;
                sr.activatedRank = learned.activatedRank;
                skillRecords.push_back(std::move(sr));
            }
            skillEarned = skillComp->skills.earnedPoints();
            skillSpent = skillEarned - skillComp->skills.availablePoints();
            for (int i = 0; i < 20; ++i)
                skillBar[i] = skillComp->skills.getSkillInSlot(i);
        }
    }

    if (!forceSaveAll) {
        dirty.clearAll();
    }

    std::string charId = client->character_id;

    // ---- Dispatch to fiber worker (non-blocking) ----
    // Acquire per-player lock inside the fiber to serialize against concurrent
    // game-thread mutations (trade execution, loot, market) that may modify
    // inventory/gold while the async save is writing state.
    auto playerMtx = playerLocks_.get(charId);
    dbDispatcher_.dispatchVoid([this, rec, charId, skillRecords, skillEarned, skillSpent, skillBar, saveSkills, playerMtx]
                               (pqxx::connection& conn) {
        std::lock_guard<std::mutex> lock(*playerMtx);
        try {
            pqxx::work txn(conn);
            txn.exec_params(
                "UPDATE characters SET "
                "level = $2, current_xp = $3, xp_to_next_level = $4, "
                "current_scene = $5, position_x = $6, position_y = $7, "
                "current_hp = $8, max_hp = $9, current_mp = $10, max_mp = $11, current_fury = $12, "
                "base_strength = $13, base_vitality = $14, base_intelligence = $15, "
                "base_dexterity = $16, base_wisdom = $17, "
                "gold = $18, honor = $19, pvp_kills = $20, pvp_deaths = $21, "
                "is_dead = $22, death_timestamp = $23, total_playtime_seconds = $24, "
                "pk_status = $25, faction = $26, "
                "free_stat_points = $27, allocated_str = $28, allocated_int = $29, "
                "allocated_dex = $30, allocated_con = $31, allocated_wis = $32, "
                "recall_scene = $33, "
                "last_saved_at = NOW(), last_online = NOW() "
                "WHERE character_id = $1",
                rec.character_id,
                rec.level, rec.current_xp, rec.xp_to_next_level,
                rec.current_scene, rec.position_x, rec.position_y,
                rec.current_hp, rec.max_hp, rec.current_mp, rec.max_mp, rec.current_fury,
                rec.base_strength, rec.base_vitality, rec.base_intelligence,
                rec.base_dexterity, rec.base_wisdom,
                rec.gold, rec.honor, rec.pvp_kills, rec.pvp_deaths,
                rec.is_dead, rec.death_timestamp, rec.total_playtime_seconds,
                rec.pk_status, rec.faction,
                rec.free_stat_points, rec.allocated_str, rec.allocated_int,
                rec.allocated_dex, rec.allocated_con, rec.allocated_wis,
                rec.recall_scene);
            if (saveSkills) {
                for (const auto& s : skillRecords)
                    txn.exec_params(
                        "INSERT INTO character_skills (character_id, skill_id, unlocked_rank, activated_rank, learned_at) "
                        "VALUES ($1, $2, $3, $4, NOW()) "
                        "ON CONFLICT (character_id, skill_id) DO UPDATE SET unlocked_rank = $3, activated_rank = $4",
                        charId, s.skillId, s.unlockedRank, s.activatedRank);
                txn.exec_params("DELETE FROM character_skill_bar WHERE character_id = $1", charId);
                for (int i = 0; i < static_cast<int>(skillBar.size()) && i < 20; ++i) {
                    if (skillBar[i].empty()) continue;
                    txn.exec_params("INSERT INTO character_skill_bar (character_id, slot_index, skill_id) VALUES ($1, $2, $3)",
                        charId, i, skillBar[i]);
                }
                txn.exec_params(
                    "INSERT INTO character_skill_points (character_id, total_earned, total_spent, updated_at) "
                    "VALUES ($1, $2, $3, NOW()) "
                    "ON CONFLICT (character_id) DO UPDATE SET total_earned = $2, total_spent = $3, updated_at = NOW()",
                    charId, skillEarned, skillSpent);
            }
            txn.commit();
        } catch (const std::exception& e) {
            LOG_ERROR("Server", "Atomic save failed for %s: %s", charId.c_str(), e.what());
            throw;
        }
    }, [this]() {
        // Success — one fewer in-flight auto-save
        if (autoSavesInFlight_ > 0) --autoSavesInFlight_;
    }, [this, clientId](std::string err) {
        // Error — still count as completed for WAL purposes
        if (autoSavesInFlight_ > 0) --autoSavesInFlight_;
        // Re-dirty so the next periodic save retries
        auto it = playerDirty_.find(clientId);
        if (it != playerDirty_.end()) {
            it->second.setAll();
        }
    });
}

void ServerApp::saveInventoryForClient(uint16_t clientId) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    PersistentId pid(client->playerEntityId);
    EntityHandle h = getReplicationForClient(clientId).getEntityHandle(pid);
    Entity* e = getWorldForClient(clientId).getEntity(h);
    if (!e) return;

    auto* inv = e->getComponent<InventoryComponent>();
    if (!inv) return;

    std::vector<InventorySlotRecord> slots;

    // Helper to build a record from an ItemInstance
    auto buildRecord = [&](const ItemInstance& item, int slotIdx, int bagSlotIdx, int bagItemSlot) {
        InventorySlotRecord s;
        s.instance_id   = item.instanceId;
        s.character_id  = client->character_id;
        s.item_id       = item.itemId;
        s.slot_index    = slotIdx;
        s.bag_slot_index = bagSlotIdx;
        s.bag_item_slot  = bagItemSlot;
        s.rolled_stats  = ItemStatRoller::rolledStatsToJson(item.rolledStats);
        s.enchant_level = item.enchantLevel;
        s.is_protected  = item.isProtected;
        s.is_soulbound  = item.isSoulbound;
        s.is_broken     = item.isBroken;
        s.quantity      = item.quantity;
        // Socket data
        if (item.hasSocket()) {
            switch (item.socket.statType) {
                case StatType::Strength:     s.socket_stat = "STR"; break;
                case StatType::Dexterity:    s.socket_stat = "DEX"; break;
                case StatType::Intelligence: s.socket_stat = "INT"; break;
                default: break;
            }
            s.socket_value = item.socket.value;
        }
        return s;
    };

    // Save main inventory slots (0-14)
    const auto& items = inv->inventory.getSlots();
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (!items[i].isValid()) continue;
        slots.push_back(buildRecord(items[i], i, -1, -1));

        // Save bag contents for this slot (if any)
        const auto& bagItems = inv->inventory.getBagContents(i);
        for (int j = 0; j < static_cast<int>(bagItems.size()); ++j) {
            if (!bagItems[j].isValid()) continue;
            slots.push_back(buildRecord(bagItems[j], -1, i, j));
        }
    }

    // Save equipped items
    for (const auto& [eqSlot, item] : inv->inventory.getEquipmentMap()) {
        if (!item.isValid()) continue;
        InventorySlotRecord s = buildRecord(item, -1, -1, -1);
        s.is_equipped = true;
        switch (eqSlot) {
            case EquipmentSlot::Weapon:    s.equipped_slot = "Weapon"; break;
            case EquipmentSlot::SubWeapon: s.equipped_slot = "SubWeapon"; break;
            case EquipmentSlot::Hat:       s.equipped_slot = "Hat"; break;
            case EquipmentSlot::Armor:     s.equipped_slot = "Armor"; break;
            case EquipmentSlot::Gloves:    s.equipped_slot = "Gloves"; break;
            case EquipmentSlot::Shoes:     s.equipped_slot = "Shoes"; break;
            case EquipmentSlot::Belt:      s.equipped_slot = "Belt"; break;
            case EquipmentSlot::Cloak:     s.equipped_slot = "Cloak"; break;
            case EquipmentSlot::Ring:      s.equipped_slot = "Ring"; break;
            case EquipmentSlot::Necklace:  s.equipped_slot = "Necklace"; break;
            default: break;
        }
        slots.push_back(std::move(s));
    }

    inventoryRepo_->saveInventory(client->character_id, slots);
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
        ++autoSavesInFlight_;
        savePlayerToDBAsync(clientId);
    });

    // Truncate WAL only after all in-flight auto-saves have committed —
    // truncating early would discard the only crash-recovery data.
    if (autoSavesInFlight_ <= 0) {
        autoSavesInFlight_ = 0;
        wal_.truncate();
    }
}

} // namespace fate
