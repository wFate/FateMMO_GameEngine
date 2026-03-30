#include "server/server_app.h"
#include "server/target_validator.h"
#include "server/handlers/aoe_helpers.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/game_components.h"
#include "game/components/pet_component.h"
#include "game/shared/game_types.h"
#include "engine/net/game_messages.h"

namespace fate {

void ServerApp::processUseSkill(uint16_t clientId, const CmdUseSkillMsg& msg) {
    // Find caster's player entity
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    World& world = getWorldForClient(clientId);
    ReplicationManager& repl = getReplicationForClient(clientId);

    if (msg.targetId != 0) {
        if (!TargetValidator::isInAOI(client->aoi, msg.targetId, repl)) {
            LOG_WARN("Net", "Client %u targeted entity %llu not in AOI", clientId, msg.targetId);
            return;
        }
    }

    // Per-tick skill command cap: silently drop excess skill commands
    skillCommandsThisTick_[clientId]++;
    if (skillCommandsThisTick_[clientId] > 1) {
        return; // Only 1 skill command per client per tick
    }

    PersistentId casterPid(client->playerEntityId);
    EntityHandle casterHandle = repl.getEntityHandle(casterPid);
    Entity* caster = world.getEntity(casterHandle);
    if (!caster) return;

    auto* skillComp = caster->getComponent<SkillManagerComponent>();
    if (!skillComp) {
        LOG_WARN("Server", "Client %d has no SkillManagerComponent", clientId);
        return;
    }

    auto* casterStatsComp = caster->getComponent<CharacterStatsComponent>();
    if (!casterStatsComp) return;

    // Check if caster is dead or dying
    if (!casterStatsComp->stats.isAlive()) {
        LOG_WARN("Server", "Client %d tried to use skill while dead", clientId);
        return;
    }

    auto* casterTransform = caster->getComponent<Transform>();
    if (!casterTransform) return;

    // Caster status effect and crowd control components
    auto* casterSEComp = caster->getComponent<StatusEffectComponent>();
    auto* casterCCComp = caster->getComponent<CrowdControlComponent>();

    // Build execution context
    SkillExecutionContext ctx;
    ctx.casterEntityId = casterHandle.value;
    ctx.casterStats = &casterStatsComp->stats;
    ctx.casterSEM = casterSEComp ? &casterSEComp->effects : nullptr;
    ctx.casterCC = casterCCComp ? &casterCCComp->cc : nullptr;

    // Find target if specified
    Entity* target = nullptr;
    PersistentId targetPid(msg.targetId);
    bool targetIsPlayer = false;
    bool targetIsBoss = false;

    if (msg.targetId != 0) {
        EntityHandle targetHandle = repl.getEntityHandle(targetPid);
        target = world.getEntity(targetHandle);
    }

    // Reject skill if target was specified but no longer exists (died/disconnected)
    if (msg.targetId != 0 && !target) {
        LOG_WARN("Server", "Client %d used skill on non-existent target %llu", clientId, msg.targetId);
        return;
    }

    if (target) {
        ctx.targetEntityId = target->handle().value;

        auto* targetTransform = target->getComponent<Transform>();
        if (targetTransform && casterTransform) {
            ctx.distanceToTarget = casterTransform->position.distance(targetTransform->position);
        }

        // Target status effects and CC
        auto* targetSEComp = target->getComponent<StatusEffectComponent>();
        auto* targetCCComp = target->getComponent<CrowdControlComponent>();
        ctx.targetSEM = targetSEComp ? &targetSEComp->effects : nullptr;
        ctx.targetCC = targetCCComp ? &targetCCComp->cc : nullptr;

        // Determine target type: mob or player
        auto* targetEnemyStats = target->getComponent<EnemyStatsComponent>();
        auto* targetCharStats = target->getComponent<CharacterStatsComponent>();

        if (targetEnemyStats) {
            // Target is a mob
            ctx.targetMobStats = &targetEnemyStats->stats;
            ctx.targetIsPlayer = false;
            ctx.targetLevel = targetEnemyStats->stats.level;
            ctx.targetArmor = targetEnemyStats->stats.armor;
            ctx.targetMagicResist = targetEnemyStats->stats.magicResist;
            ctx.targetCurrentHP = targetEnemyStats->stats.currentHP;
            ctx.targetMaxHP = targetEnemyStats->stats.maxHP;
            ctx.targetAlive = targetEnemyStats->stats.isAlive;

            // Check if target is a boss
            auto* mobNameplate = target->getComponent<MobNameplateComponent>();
            if (mobNameplate && mobNameplate->isBoss) {
                ctx.targetIsBoss = true;
                targetIsBoss = true;
            }
        } else if (targetCharStats) {
            // Target is a player — validate PvP rules
            bool inSameParty = false;
            auto* casterPartyComp = caster->getComponent<PartyComponent>();
            auto* targetPartyComp = target->getComponent<PartyComponent>();
            if (casterPartyComp && targetPartyComp
                && casterPartyComp->party.isInParty() && targetPartyComp->party.isInParty()
                && casterPartyComp->party.partyId == targetPartyComp->party.partyId) {
                inSameParty = true;
            }

            bool inSafeZone = !casterStatsComp->stats.isInPvPZone;

            if (!TargetValidator::canAttackPlayer(casterStatsComp->stats, targetCharStats->stats,
                                                  inSameParty, inSafeZone)) {
                return;
            }

            ctx.targetPlayerStats = &targetCharStats->stats;
            ctx.targetIsPlayer = true;
            targetIsPlayer = true;
            ctx.targetLevel = targetCharStats->stats.level;
            ctx.targetArmor = targetCharStats->stats.getArmor();
            ctx.targetMagicResist = targetCharStats->stats.getMagicResist();
            for (int i = 0; i < 8; i++) {
                ctx.targetElementalResists[i] = targetCharStats->stats.getElementalResist(static_cast<DamageType>(i));
            }
            ctx.targetCurrentHP = targetCharStats->stats.currentHP;
            ctx.targetMaxHP = targetCharStats->stats.maxHP;
            ctx.targetAlive = targetCharStats->stats.isAlive();
        }
    }

    // Validate skill cooldown
    auto& clientCooldowns = skillCooldowns_[clientId];
    auto cooldownIt = clientCooldowns.find(msg.skillId);
    if (cooldownIt != clientCooldowns.end()) {
        const CachedSkillRank* rank = skillDefCache_.getRank(msg.skillId, msg.rank);
        float cooldown = rank ? rank->cooldownSeconds : 1.0f;
        if (gameTime_ - cooldownIt->second < cooldown - TICK_INTERVAL) {
            LOG_DEBUG("Server", "Client %d skill '%s' rejected: cooldown (%.1f < %.1f)",
                      clientId, msg.skillId.c_str(), gameTime_ - cooldownIt->second, cooldown);
            return; // reject — too fast
        }
    }
    clientCooldowns[msg.skillId] = gameTime_;

    // Check cast time — if skill has a cast time, enter casting state instead of instant execution
    if (!castCompleting_) {
        const CachedSkillDef* skillDef = skillDefCache_.getSkill(msg.skillId);
        if (skillDef && skillDef->castTime > 0.0f && !casterStatsComp->stats.isCasting()) {
            uint32_t castTargetId = static_cast<uint32_t>(msg.targetId);
            casterStatsComp->stats.beginCast(msg.skillId, skillDef->castTime, castTargetId, msg.rank);
            LOG_INFO("Server", "Client %d began casting '%s' (%.1fs)", clientId, msg.skillId.c_str(), skillDef->castTime);
            return; // don't execute yet — wait for cast to complete
        }
    }

    // ---- Life Tap: HP → MP conversion (self-cast, no target needed) ----
    if (msg.skillId == "mage_life_tap") {
        if (casterStatsComp->stats.classDef.classType != ClassType::Mage) {
            return;
        }

        const LearnedSkill* ls = skillComp->skills.getLearnedSkill("mage_life_tap");
        if (!ls || ls->activatedRank <= 0) {
            return;
        }

        if (casterCCComp && !casterCCComp->cc.canAct()) return;

        int rank = ls->activatedRank;

        // HP sacrifice: 20% of current HP (constant across ranks)
        int hpSacrifice = static_cast<int>(casterStatsComp->stats.currentHP * 0.20f);
        if (casterStatsComp->stats.currentHP - hpSacrifice < 1) {
            hpSacrifice = casterStatsComp->stats.currentHP - 1;
        }
        if (hpSacrifice <= 0) return;

        // MP restore: scales by rank (20% / 28% / 36% of max MP)
        float mpRestorePercent = 0.0f;
        switch (rank) {
            case 1: mpRestorePercent = 0.20f; break;
            case 2: mpRestorePercent = 0.28f; break;
            case 3: default: mpRestorePercent = 0.36f; break;
        }
        int mpRestore = static_cast<int>(casterStatsComp->stats.maxMP * mpRestorePercent);

        casterStatsComp->stats.currentHP -= hpSacrifice;
        casterStatsComp->stats.currentMP = (std::min)(
            casterStatsComp->stats.currentMP + mpRestore,
            casterStatsComp->stats.maxMP);

        clientCooldowns[msg.skillId] = gameTime_;

        SvSkillResultMsg result;
        result.casterId = casterPid.value();
        result.targetId = casterPid.value();
        result.skillId  = msg.skillId;
        result.damage   = hpSacrifice;
        result.hitFlags = HitFlags::HIT;
        result.casterNewMP = static_cast<uint16_t>(casterStatsComp->stats.currentMP);

        const CachedSkillRank* rankInfo2 = skillDefCache_.getRank(msg.skillId, rank);
        result.cooldownMs = rankInfo2 ? static_cast<uint16_t>(rankInfo2->cooldownSeconds * 1000.0f) : 0;

        uint8_t buf[256];
        ByteWriter w(buf, sizeof(buf));
        result.write(w);
        server_.broadcast(Channel::ReliableOrdered, PacketType::SvSkillResult, buf, w.size());

        playerDirty_[clientId].vitals = true;
        sendPlayerState(clientId);

        LOG_INFO("Server", "Client %d used Life Tap rank %d: -%d HP, +%d MP",
                 clientId, rank, hpSacrifice, mpRestore);
        return;
    }

    // God mode check for skill targets (player targets only)
    if (targetIsPlayer && godModeEntities_.count(msg.targetId)) return;

    // Detect AOE skill
    const SkillDefinition* skillDef = skillComp->skills.getSkillDefinition(msg.skillId);
    bool isAOE = false;
    if (skillDef) {
        isAOE = skillDef->aoeRadius > 0.0f ||
                skillDef->targetType == SkillTargetType::AreaAtTarget ||
                skillDef->targetType == SkillTargetType::AreaAroundSelf ||
                skillDef->targetType == SkillTargetType::Cone ||
                skillDef->targetType == SkillTargetType::Line;
    }

    if (isAOE && skillDef) {
        // === AOE EXECUTION PATH ===

        // Hook callback (scoped tightly)
        bool wasMiss = false, wasResist = false, wasValidationError = false;
        std::string failReason;
        auto prevOnFailed = skillComp->skills.onSkillFailed;
        skillComp->skills.onSkillFailed = [&](const std::string& id, std::string reason) {
            if (reason == "Spell resisted") wasResist = true;
            else if (reason == "Attack missed") wasMiss = true;
            else wasValidationError = true;
            failReason = std::move(reason);
        };

        // Determine gather center
        auto* casterT = caster->getComponent<Transform>();
        Vec2 center = casterT ? casterT->position : Vec2{0,0};
        if ((skillDef->targetType == SkillTargetType::AreaAtTarget) && target) {
            auto* targetT = target->getComponent<Transform>();
            if (targetT) center = targetT->position;
        }

        float radiusPixels = skillDef->aoeRadius * 32.0f + 16.0f;
        std::string casterScene = casterStatsComp->stats.currentScene;

        // Gather targets based on geometry
        std::vector<AOETarget> aoeTargets;
        if (skillDef->targetType == SkillTargetType::Cone) {
            Vec2 targetPos = target ? target->getComponent<Transform>()->position : center;
            float lengthPx = skillDef->range * 32.0f + 16.0f;
            aoeTargets = gatherConeTargets(world, repl, center, targetPos,
                                           lengthPx, 0.5236f, casterHandle.value, casterScene);
        } else if (skillDef->targetType == SkillTargetType::Line) {
            Vec2 targetPos = target ? target->getComponent<Transform>()->position : center;
            float lengthPx = skillDef->range * 32.0f + 16.0f;
            aoeTargets = gatherLineTargets(world, repl, center, targetPos,
                                           lengthPx, 32.0f, casterHandle.value, casterScene);
        } else {
            aoeTargets = gatherCircleTargets(world, repl, center, radiusPixels,
                                             casterHandle.value, casterScene);
        }

        // Build per-target execution contexts
        SkillExecutionContext primaryCtx = ctx; // copy the existing context for primary
        std::vector<SkillExecutionContext> targetContexts;

        for (auto& at : aoeTargets) {
            Entity* tgtEntity = world.getEntity(at.handle);
            if (!tgtEntity) continue;

            SkillExecutionContext tctx;
            tctx.casterEntityId = ctx.casterEntityId;
            tctx.casterStats = ctx.casterStats;
            tctx.casterSEM = ctx.casterSEM;
            tctx.casterCC = ctx.casterCC;
            tctx.targetEntityId = at.handle.value;

            auto* tgtT = tgtEntity->getComponent<Transform>();
            if (tgtT && casterT) {
                tctx.distanceToTarget = casterT->position.distance(tgtT->position);
            }

            auto* tgtES = tgtEntity->getComponent<EnemyStatsComponent>();
            auto* tgtCS = tgtEntity->getComponent<CharacterStatsComponent>();
            if (tgtES) {
                tctx.targetMobStats = &tgtES->stats;
                tctx.targetLevel = tgtES->stats.level;
                tctx.targetArmor = tgtES->stats.armor;
                tctx.targetMagicResist = tgtES->stats.magicResist;
                tctx.targetCurrentHP = tgtES->stats.currentHP;
                tctx.targetMaxHP = tgtES->stats.maxHP;
                tctx.targetAlive = tgtES->stats.isAlive;
            } else if (tgtCS) {
                tctx.targetPlayerStats = &tgtCS->stats;
                tctx.targetIsPlayer = true;
                tctx.targetLevel = tgtCS->stats.level;
                tctx.targetArmor = tgtCS->stats.getArmor();
                tctx.targetMagicResist = tgtCS->stats.getMagicResist();
                tctx.targetCurrentHP = tgtCS->stats.currentHP;
                tctx.targetMaxHP = tgtCS->stats.maxHP;
                tctx.targetAlive = tgtCS->stats.isAlive();
            }

            auto* tgtSE = tgtEntity->getComponent<StatusEffectComponent>();
            auto* tgtCC = tgtEntity->getComponent<CrowdControlComponent>();
            tctx.targetSEM = tgtSE ? &tgtSE->effects : nullptr;
            tctx.targetCC = tgtCC ? &tgtCC->cc : nullptr;

            targetContexts.push_back(tctx);
        }

        // Execute AOE skill
        int totalDamage = skillComp->skills.executeSkillAOE(msg.skillId, msg.rank, primaryCtx, targetContexts);

        // Restore callback
        skillComp->skills.onSkillFailed = prevOnFailed;

        // Validation error -- don't broadcast
        if (wasValidationError && totalDamage == 0) {
            LOG_INFO("Server", "Client %d AOE skill '%s' rank %d rejected: %s",
                     clientId, msg.skillId.c_str(), msg.rank, failReason.c_str());
            return;
        }

        // Broadcast per-target results
        const CachedSkillRank* aoeRankInfo = skillDefCache_.getRank(msg.skillId, msg.rank);
        uint16_t aoeCooldownMs = aoeRankInfo ? static_cast<uint16_t>(aoeRankInfo->cooldownSeconds * 1000.0f) : 0;

        for (size_t i = 0; i < targetContexts.size(); ++i) {
            auto& tctx = targetContexts[i];
            Entity* tgtEntity = world.getEntity(EntityHandle(tctx.targetEntityId));
            if (!tgtEntity) continue;

            int32_t tgtNewHP = 0;
            bool isKill = false;
            auto* tgtES = tgtEntity->getComponent<EnemyStatsComponent>();
            auto* tgtCS = tgtEntity->getComponent<CharacterStatsComponent>();
            if (tgtES) {
                isKill = !tgtES->stats.isAlive;
                tgtNewHP = tgtES->stats.currentHP;
            } else if (tgtCS) {
                isKill = !tgtCS->stats.isAlive();
                tgtNewHP = tgtCS->stats.currentHP;
            }

            uint64_t tgtPid = repl.getPersistentId(EntityHandle(tctx.targetEntityId)).value();

            // Calculate damage dealt to this target
            int dmgDealt = 0;
            if (tctx.targetCurrentHP > tgtNewHP) {
                dmgDealt = tctx.targetCurrentHP - tgtNewHP;
            }
            if (isKill && dmgDealt == 0) {
                dmgDealt = tctx.targetCurrentHP; // was killed
            }

            SvSkillResultMsg result;
            result.casterId = casterPid.value();
            result.targetId = tgtPid;
            result.skillId = msg.skillId;
            result.damage = dmgDealt;
            result.targetNewHP = tgtNewHP;
            result.hitFlags = (dmgDealt > 0) ? HitFlags::HIT : 0;
            if (isKill) result.hitFlags |= HitFlags::KILLED;
            result.cooldownMs = (i == 0) ? aoeCooldownMs : 0; // only first target sends cooldown
            result.casterNewMP = static_cast<uint16_t>(casterStatsComp->stats.currentMP);

            uint8_t buf[256];
            ByteWriter w(buf, sizeof(buf));
            result.write(w);
            server_.broadcast(Channel::ReliableOrdered, PacketType::SvSkillResult, buf, w.size());

            // Handle mob death
            if (isKill) {
                if (tgtES) {
                    tgtES->stats.lastDamageTime = gameTime_;
                    auto* tgtTransform = tgtEntity->getComponent<Transform>();
                    Vec2 deathPos = tgtTransform ? tgtTransform->position : Vec2{0, 0};
                    processMobDeath(clientId, casterStatsComp, tgtES, deathPos, world, repl);
                }
            } else if (dmgDealt > 0 && tgtES) {
                tgtES->stats.lastDamageTime = gameTime_;
            }
        }

        playerDirty_[clientId].vitals = true;
        sendPlayerState(clientId);

        LOG_INFO("Server", "Client %d used AOE skill '%s' rank %d -> %zu targets, total dmg=%d",
                 clientId, msg.skillId.c_str(), msg.rank, targetContexts.size(), totalDamage);
        return;
    }

    // === SINGLE TARGET PATH (existing code continues) ===

    // Hook onSkillFailed to capture combat outcome (installed only around executeSkill)
    bool wasMiss = false;
    bool wasResist = false;
    bool wasValidationError = false;
    std::string failReason;
    auto prevOnFailed = skillComp->skills.onSkillFailed;
    skillComp->skills.onSkillFailed = [&](const std::string& id, std::string reason) {
        if (reason == "Spell resisted") {
            wasResist = true;
        } else if (reason == "Attack missed") {
            wasMiss = true;
        } else {
            wasValidationError = true;
        }
        failReason = std::move(reason);
    };

    // Execute the skill
    int damage = skillComp->skills.executeSkill(msg.skillId, msg.rank, ctx);

    // Restore callback immediately
    skillComp->skills.onSkillFailed = prevOnFailed;

    // Stamp leash timer on mob targets that took damage
    if (damage > 0 && ctx.targetMobStats) {
        ctx.targetMobStats->lastDamageTime = gameTime_;
    }

    // Validation errors (out of range, dead target, no resources) — don't broadcast
    if (wasValidationError && damage == 0) {
        LOG_INFO("Server", "Client %d skill '%s' rank %d rejected: %s",
                 clientId, msg.skillId.c_str(), msg.rank, failReason.c_str());
        return;
    }

    // Determine kill state and build hitFlags
    bool isKill = false;
    int32_t targetNewHP = 0;
    int32_t overkill = 0;
    if (target) {
        auto* tgtEnemyStats = target->getComponent<EnemyStatsComponent>();
        auto* tgtCharStats = target->getComponent<CharacterStatsComponent>();
        if (tgtEnemyStats) {
            isKill = !tgtEnemyStats->stats.isAlive;
            targetNewHP = tgtEnemyStats->stats.currentHP;
            if (isKill && damage > 0)
                overkill = damage - (ctx.targetCurrentHP > 0 ? ctx.targetCurrentHP : 0);
        } else if (tgtCharStats) {
            isKill = !tgtCharStats->stats.isAlive();
            targetNewHP = tgtCharStats->stats.currentHP;
            if (isKill && damage > 0)
                overkill = damage - (ctx.targetCurrentHP > 0 ? ctx.targetCurrentHP : 0);
        }
    }

    // Build hitFlags bitmask
    uint8_t hitFlags = 0;
    if (wasResist && damage == 0) {
        hitFlags |= HitFlags::RESIST;
    } else if (wasMiss && damage == 0) {
        hitFlags |= HitFlags::MISS;
    } else if (damage > 0) {
        hitFlags |= HitFlags::HIT;
    }
    if (isKill) hitFlags |= HitFlags::KILLED;
    // TODO: expose isCrit from executeSkill return value; for now crit flag not set

    // Resolve authoritative cooldown duration
    const CachedSkillRank* rankInfo = skillDefCache_.getRank(msg.skillId, msg.rank);
    uint16_t cooldownMs = 0;
    if (rankInfo) cooldownMs = static_cast<uint16_t>(rankInfo->cooldownSeconds * 1000.0f);

    // Build and broadcast SvSkillResultMsg
    SvSkillResultMsg result;
    result.casterId     = casterPid.value();
    result.targetId     = msg.targetId;
    result.skillId      = msg.skillId;
    result.damage       = damage;
    result.overkill     = (std::max)(0, static_cast<int>(overkill));
    result.targetNewHP  = targetNewHP;
    result.hitFlags     = hitFlags;
    result.resourceCost = 0; // resource already deducted inside executeSkill
    result.cooldownMs   = cooldownMs;
    result.casterNewMP  = static_cast<uint16_t>(casterStatsComp->stats.currentMP);

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    result.write(w);
    server_.broadcast(Channel::ReliableOrdered, PacketType::SvSkillResult, buf, w.size());

    // PK status transitions
    if (targetIsPlayer && damage > 0) {
        uint16_t skillTargetClientId = 0;
        server_.connections().forEach([&](const ClientConnection& conn) {
            if (conn.playerEntityId == msg.targetId) skillTargetClientId = conn.clientId;
        });
        if (skillTargetClientId != 0) {
            processPKAttack(clientId, skillTargetClientId, damage);
            if (isKill) {
                processPKKill(clientId, skillTargetClientId);
            }
        }
    }

    // Handle mob death: XP, loot, honor, pet, gauntlet
    if (isKill && target) {
        auto* targetEnemyStats = target->getComponent<EnemyStatsComponent>();
        if (targetEnemyStats) {
            auto* targetTransform = target->getComponent<Transform>();
            Vec2 deathPos = targetTransform ? targetTransform->position : Vec2{0, 0};
            processMobDeath(clientId, casterStatsComp, targetEnemyStats, deathPos, world, repl);
        }
    }

    // Caster vitals dirty (MP/Fury may have changed from skill use)
    playerDirty_[clientId].vitals = true;

    // Send updated player state (HP/MP/Fury may have changed)
    sendPlayerState(clientId);

    if (!failReason.empty()) {
        LOG_INFO("Server", "Client %d used skill '%s' rank %d -> dmg=%d kill=%d fail='%s'",
                 clientId, msg.skillId.c_str(), msg.rank, damage, isKill ? 1 : 0,
                 failReason.c_str());
    } else {
        LOG_INFO("Server", "Client %d used skill '%s' rank %d -> dmg=%d kill=%d",
                 clientId, msg.skillId.c_str(), msg.rank, damage, isKill ? 1 : 0);
    }
}

void ServerApp::processAction(uint16_t clientId, const CmdAction& action) {
    // Find attacker's player entity
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    World& world = getWorldForClient(clientId);
    ReplicationManager& repl = getReplicationForClient(clientId);

    if (action.targetId != 0) {
        if (!TargetValidator::isInAOI(client->aoi, action.targetId, repl)) {
            LOG_WARN("Net", "Client %u targeted entity %llu not in AOI", clientId, action.targetId);
            return;
        }
    }

    PersistentId attackerPid(client->playerEntityId);
    EntityHandle attackerHandle = repl.getEntityHandle(attackerPid);
    Entity* attacker = world.getEntity(attackerHandle);
    if (!attacker) return;

    // Find target entity
    PersistentId targetPid(action.targetId);
    EntityHandle targetHandle = repl.getEntityHandle(targetPid);
    Entity* target = world.getEntity(targetHandle);
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

        // Validate player is alive
        if (charStats && !charStats->stats.isAlive()) return;

        float maxRange = attackRange * 32.0f + 16.0f;
        float dist = attackerTransform->position.distance(targetTransform->position);
        if (dist > maxRange) {
            LOG_WARN("Server", "Client %d attack out of range (%.1f > %.1f)", clientId, dist, maxRange);
            return;
        }

        // Check target is a living enemy
        auto* enemyStats = target->getComponent<EnemyStatsComponent>();

        // Validate auto-attack cooldown (shared for both PvE and PvP)
        float weaponSpeed = charStats ? charStats->stats.weaponAttackSpeed : 1.0f;
        float cooldown = (weaponSpeed > 0.0f) ? (1.0f / weaponSpeed) : 1.5f;
        auto lastIt = lastAutoAttackTime_.find(clientId);
        if (lastIt != lastAutoAttackTime_.end() && gameTime_ - lastIt->second < cooldown - TICK_INTERVAL) return;
        lastAutoAttackTime_[clientId] = gameTime_;

        if (enemyStats) {
        if (!enemyStats->stats.isAlive) return;

        // Same-scene check: player and mob must be in the same scene
        if (charStats && !enemyStats->stats.sceneId.empty() &&
            charStats->stats.currentScene != enemyStats->stats.sceneId) return;

        // Calculate damage
        int damage = 10; // default
        bool isCrit = false;
        if (charStats) {
            damage = charStats->stats.calculateDamage(false, isCrit);
        }

        // Apply damage
        enemyStats->stats.takeDamageFrom(attackerHandle.value, damage);
        enemyStats->stats.lastDamageTime = gameTime_;
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

        // Fury generation on auto-attack hit
        if (charStats && charStats->stats.classDef.primaryResource == ResourceType::Fury && damage > 0) {
            float furyGain = isCrit ? charStats->stats.classDef.furyPerCriticalHit
                                    : charStats->stats.classDef.furyPerBasicAttack;
            charStats->stats.addFury(furyGain);
            playerDirty_[clientId].vitals = true;
            sendPlayerState(clientId);
        }

        // Handle mob death: XP, loot, honor, pet, gauntlet
        if (killed) {
            auto* targetEnemyStats = target->getComponent<EnemyStatsComponent>();
            if (targetEnemyStats) {
                Vec2 deathPos = targetTransform ? targetTransform->position : Vec2{0, 0};
                processMobDeath(clientId, charStats, targetEnemyStats, deathPos, world, repl);
            }
        }

        // Send updated player state (XP, HP may have changed)
        sendPlayerState(clientId);
        } else {
        // PvP auto-attack: target is another player
        auto* targetCharStats = target->getComponent<CharacterStatsComponent>();
        if (targetCharStats) {
            // Determine party membership for PvP validation
            bool inSameParty = false;
            auto* attackerPartyComp = attacker->getComponent<PartyComponent>();
            auto* targetPartyComp = target->getComponent<PartyComponent>();
            if (attackerPartyComp && targetPartyComp
                && attackerPartyComp->party.isInParty() && targetPartyComp->party.isInParty()
                && attackerPartyComp->party.partyId == targetPartyComp->party.partyId) {
                inSameParty = true;
            }

            // Determine safe zone status
            bool inSafeZone = !charStats->stats.isInPvPZone;

            // Full PvP target validation (faction, party, safe zone, PK status, alive)
            if (!TargetValidator::canAttackPlayer(charStats->stats, targetCharStats->stats,
                                                  inSameParty, inSafeZone)) return;

            // Same-scene check
            if (charStats && !charStats->stats.currentScene.empty() &&
                charStats->stats.currentScene != targetCharStats->stats.currentScene) return;

            // Calculate PvP damage using shared formulas
            int damage = 10;
            bool isCrit = false;
            if (charStats) {
                damage = charStats->stats.calculateDamage(false, isCrit);
            }

            // Apply PvP damage multiplier (0.05x)
            damage = static_cast<int>(std::round(damage * CombatSystem::getPvPDamageMultiplier()));
            if (damage < 1) damage = 1;

            // Apply armor reduction on target
            damage = CombatSystem::applyArmorReduction(damage, targetCharStats->stats.getArmor());

            // God mode check: skip damage if target is invulnerable
            if (godModeEntities_.count(targetPid.value())) return;

            // Apply damage
            targetCharStats->stats.takeDamage(damage);
            bool killed = !targetCharStats->stats.isAlive();

            // Find target's clientId for dirty flags
            uint16_t pvpTargetClientId = 0;
            server_.connections().forEach([&](const ClientConnection& conn) {
                if (conn.playerEntityId == targetPid.value()) pvpTargetClientId = conn.clientId;
            });
            if (pvpTargetClientId != 0) {
                playerDirty_[pvpTargetClientId].vitals = true;
            }

            // PK status transitions
            if (charStats && damage > 0 && pvpTargetClientId != 0) {
                processPKAttack(clientId, pvpTargetClientId, damage);
            }

            // Broadcast SvCombatEvent
            SvCombatEventMsg pvpEvt;
            pvpEvt.attackerId = attackerPid.value();
            pvpEvt.targetId   = targetPid.value();
            pvpEvt.damage     = damage;
            pvpEvt.skillId    = 0;
            pvpEvt.isCrit     = isCrit ? 1 : 0;
            pvpEvt.isKill     = killed ? 1 : 0;
            uint8_t pvpBuf[64];
            ByteWriter pvpW(pvpBuf, sizeof(pvpBuf));
            pvpEvt.write(pvpW);
            server_.broadcast(Channel::ReliableOrdered, PacketType::SvCombatEvent, pvpBuf, pvpW.size());

            // Fury generation on auto-attack hit
            if (charStats && charStats->stats.classDef.primaryResource == ResourceType::Fury && damage > 0) {
                float furyGain = isCrit ? charStats->stats.classDef.furyPerCriticalHit
                                        : charStats->stats.classDef.furyPerBasicAttack;
                charStats->stats.addFury(furyGain);
                playerDirty_[clientId].vitals = true;
                sendPlayerState(clientId);
            }

            if (killed && pvpTargetClientId != 0) {
                processPKKill(clientId, pvpTargetClientId);
            }

            sendPlayerState(clientId);
        }
        } // end else (PvP branch)
    } else if (action.actionType == 3) {
        // Pickup
        PersistentId itemPid(action.targetId);
        EntityHandle itemHandle = repl.getEntityHandle(itemPid);
        Entity* itemEntity = world.getEntity(itemHandle);
        if (!itemEntity) return;

        auto* dropComp = itemEntity->getComponent<DroppedItemComponent>();
        if (!dropComp) return;

        // Validate proximity
        auto* playerT = attacker->getComponent<Transform>();
        auto* itemT = itemEntity->getComponent<Transform>();
        if (!playerT || !itemT) return;
        float dist = playerT->position.distance(itemT->position);
        if (dist > 48.0f) return;

        // Validate scene match
        auto* attackerStats = attacker->getComponent<CharacterStatsComponent>();
        if (attackerStats && !dropComp->sceneId.empty()
            && dropComp->sceneId != attackerStats->stats.currentScene) return;

        // Validate loot rights — allow owner always; allow party members only in FreeForAll mode
        if (dropComp->ownerEntityId != 0 && dropComp->ownerEntityId != attackerHandle.value) {
            bool sameParty = false;
            auto* attackerParty = attacker->getComponent<PartyComponent>();
            if (attackerParty && attackerParty->party.isInParty()) {
                EntityHandle ownerHandle(dropComp->ownerEntityId);
                auto* ownerEntity = world.getEntity(ownerHandle);
                if (ownerEntity) {
                    auto* ownerParty = ownerEntity->getComponent<PartyComponent>();
                    if (ownerParty && ownerParty->party.isInParty()
                        && ownerParty->party.partyId == attackerParty->party.partyId
                        && attackerParty->party.lootMode == PartyLootMode::FreeForAll) {
                        sameParty = true;
                    }
                }
            }
            if (!sameParty) return;
        }

        // Claim the drop atomically (single-threaded: simple bool flag prevents
        // two pickups of the same item in the same tick)
        if (!dropComp->tryClaim(attackerHandle.value)) {
            return; // Already claimed by another player this tick
        }

        // Process pickup
        auto* inv = attacker->getComponent<InventoryComponent>();
        if (!inv) return;

        SvLootPickupMsg pickupMsg;

        if (dropComp->isGold) {
            // WAL: record gold pickup before mutating
            {
                auto* lootClient = server_.connections().findById(clientId);
                if (lootClient) wal_.appendGoldChange(lootClient->character_id, static_cast<int64_t>(dropComp->goldAmount));
            }
            inv->inventory.addGold(dropComp->goldAmount);
            playerDirty_[clientId].inventory = true;
            enqueuePersist(clientId, PersistPriority::IMMEDIATE, PersistType::Inventory);
            pickupMsg.isGold = 1;
            pickupMsg.goldAmount = dropComp->goldAmount;
            pickupMsg.displayName = "Gold";
        } else {
            const auto* def = itemDefCache_.getDefinition(dropComp->itemId);

            ItemInstance item;
            item.instanceId = generateItemInstanceId();
            item.itemId = dropComp->itemId;
            item.quantity = dropComp->quantity;
            item.enchantLevel = dropComp->enchantLevel;
            item.rolledStats = ItemStatRoller::parseRolledStats(dropComp->rolledStatsJson);
            item.rarity = parseItemRarity(dropComp->rarity);
            item.displayName = def ? def->displayName : dropComp->itemId;
            if (!inv->inventory.addItem(item)) {
                dropComp->releaseClaim();
                // Tell client their inventory is full
                SvChatMessageMsg fullMsg;
                fullMsg.channel = 6; // system
                fullMsg.senderName = "[System]";
                fullMsg.message = "Inventory full!";
                fullMsg.faction = 0;
                uint8_t chatBuf[256];
                ByteWriter chatW(chatBuf, sizeof(chatBuf));
                fullMsg.write(chatW);
                server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvChatMessage, chatBuf, chatW.size());
                return;
            }
            // WAL: record item pickup after successful addItem
            {
                auto* lootClient = server_.connections().findById(clientId);
                if (lootClient) wal_.appendItemAdd(lootClient->character_id, -1, item.instanceId);
            }
            playerDirty_[clientId].inventory = true;

            pickupMsg.itemId = dropComp->itemId;
            pickupMsg.quantity = dropComp->quantity;
            pickupMsg.rarity = dropComp->rarity;
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
        repl.unregisterEntity(itemHandle);
        world.destroyEntity(itemHandle);
    } else {
        LOG_INFO("Server", "Unhandled action type %d from client %d", action.actionType, clientId);
    }
}

} // namespace fate
