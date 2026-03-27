#include "server/server_app.h"
#include "engine/core/logger.h"
#include "engine/ecs/persistent_id.h"
#include "game/components/game_components.h"
#include "game/shared/game_types.h"
#include "engine/net/game_messages.h"

namespace fate {

void ServerApp::processRankingQuery(uint16_t clientId, const CmdRankingQueryMsg& msg) {
    auto* client = server_.connections().findById(clientId);
    if (!client || client->playerEntityId == 0) return;

    auto category = static_cast<RankingCategory>(msg.category);

    // Validate category
    if (msg.category > static_cast<uint8_t>(RankingCategory::CollectionProgress)) {
        LOG_WARN("Server", "Client %d sent invalid ranking category %d", clientId, msg.category);
        return;
    }

    std::string factionClause;
    if (msg.factionFilter >= 1 && msg.factionFilter <= 4) {
        factionClause = " AND faction = " + std::to_string(msg.factionFilter);
    }

    // Refresh cache from DB if stale
    if (!rankingMgr_.isCacheValid(gameTime_)) {
        try {
            if (gameDbConn_.isConnected()) {
                pqxx::work txn(gameDbConn_.connection());

                // --- Player rankings (global, by level/xp) ---
                auto playerResult = txn.exec(
                    "SELECT character_name, class_name, level, current_xp, honor, pvp_kills, pvp_deaths, faction "
                    "FROM characters WHERE 1=1" + factionClause +
                    " ORDER BY level DESC, current_xp DESC LIMIT 500"
                );

                std::vector<PlayerRankingEntry> globalEntries;
                std::vector<PlayerRankingEntry> warriorEntries;
                std::vector<PlayerRankingEntry> mageEntries;
                std::vector<PlayerRankingEntry> archerEntries;

                int globalRank = 1;
                int warriorRank = 1;
                int mageRank = 1;
                int archerRank = 1;

                for (const auto& row : playerResult) {
                    PlayerRankingEntry entry;
                    entry.characterName = row["character_name"].as<std::string>("");
                    entry.classType     = row["class_name"].as<std::string>("");
                    entry.level         = row["level"].as<int>(1);
                    entry.characterId   = globalRank; // use rank as ID placeholder
                    entry.rankPosition  = globalRank++;
                    globalEntries.push_back(entry);

                    // Class-filtered lists
                    if (entry.classType == "Warrior") {
                        PlayerRankingEntry ce = entry;
                        ce.rankPosition = warriorRank++;
                        warriorEntries.push_back(ce);
                    } else if (entry.classType == "Mage") {
                        PlayerRankingEntry ce = entry;
                        ce.rankPosition = mageRank++;
                        mageEntries.push_back(ce);
                    } else if (entry.classType == "Archer") {
                        PlayerRankingEntry ce = entry;
                        ce.rankPosition = archerRank++;
                        archerEntries.push_back(ce);
                    }
                }

                rankingMgr_.setPlayerRankings(RankingCategory::PlayersGlobal, std::move(globalEntries));
                rankingMgr_.setPlayerRankings(RankingCategory::PlayersWarrior, std::move(warriorEntries));
                rankingMgr_.setPlayerRankings(RankingCategory::PlayersMage, std::move(mageEntries));
                rankingMgr_.setPlayerRankings(RankingCategory::PlayersArcher, std::move(archerEntries));

                // --- Honor rankings ---
                auto honorResult = txn.exec(
                    "SELECT character_name, class_name, level, honor, pvp_kills, pvp_deaths, faction "
                    "FROM characters WHERE 1=1" + factionClause +
                    " ORDER BY honor DESC LIMIT 500"
                );

                std::vector<HonorRankingEntry> honorEntries;
                int honorRank = 1;
                for (const auto& row : honorResult) {
                    HonorRankingEntry entry;
                    entry.characterName = row["character_name"].as<std::string>("");
                    entry.classType     = row["class_name"].as<std::string>("");
                    entry.level         = row["level"].as<int>(1);
                    entry.honor         = row["honor"].as<int>(0);
                    entry.pvpKills      = row["pvp_kills"].as<int>(0);
                    entry.pvpDeaths     = row["pvp_deaths"].as<int>(0);
                    entry.characterId   = std::to_string(honorRank);
                    entry.rankPosition  = honorRank++;
                    honorEntries.push_back(entry);
                }
                rankingMgr_.setHonorRankings(std::move(honorEntries));

                // --- Guild rankings ---
                auto guildResult = txn.exec(
                    "SELECT g.id, g.name, g.level, "
                    "(SELECT COUNT(*) FROM guild_members gm WHERE gm.guild_id = g.id) as member_count, "
                    "(SELECT c.character_name FROM characters c "
                    " JOIN guild_members gm2 ON gm2.character_id = c.id "
                    " WHERE gm2.guild_id = g.id AND gm2.rank = 0 LIMIT 1) as owner_name "
                    "FROM guilds g ORDER BY g.level DESC LIMIT 500"
                );

                std::vector<GuildRankingEntry> guildEntries;
                int guildRank = 1;
                for (const auto& row : guildResult) {
                    GuildRankingEntry entry;
                    entry.guildId     = row["id"].as<int>(0);
                    entry.guildName   = row["name"].as<std::string>("");
                    entry.guildLevel  = row["level"].as<int>(1);
                    entry.memberCount = row["member_count"].as<int>(0);
                    entry.ownerName   = row["owner_name"].as<std::string>("");
                    entry.rankPosition = guildRank++;
                    guildEntries.push_back(entry);
                }
                rankingMgr_.setGuildRankings(std::move(guildEntries));

                // --- Mob kill rankings ---
                auto mobKillResult = txn.exec(
                    "SELECT character_name, class_name, level, total_mob_kills, faction "
                    "FROM characters WHERE total_mob_kills > 0" + factionClause +
                    " ORDER BY total_mob_kills DESC LIMIT 500"
                );

                std::vector<MobKillRankingEntry> mobKillEntries;
                int mobKillRank = 1;
                for (const auto& row : mobKillResult) {
                    MobKillRankingEntry entry;
                    entry.characterName = row["character_name"].as<std::string>("");
                    entry.classType     = row["class_name"].as<std::string>("");
                    entry.level         = row["level"].as<int>(1);
                    entry.totalMobKills = row["total_mob_kills"].as<int>(0);
                    entry.faction       = row["faction"].as<int>(0);
                    entry.rankPosition  = mobKillRank++;
                    mobKillEntries.push_back(entry);
                }
                rankingMgr_.setMobKillRankings(std::move(mobKillEntries));

                // --- Collection progress rankings ---
                auto collectionResult = txn.exec(
                    "SELECT c.character_name, c.class_name, c.level, c.faction, "
                    "COALESCE(ic.unique_count, 0) as unique_count "
                    "FROM characters c "
                    "LEFT JOIN ("
                    "  SELECT character_id, COUNT(DISTINCT item_def_id) as unique_count "
                    "  FROM inventory_log GROUP BY character_id"
                    ") ic ON ic.character_id = c.id "
                    "WHERE COALESCE(ic.unique_count, 0) > 0" + factionClause +
                    " ORDER BY unique_count DESC LIMIT 500"
                );

                auto totalItemsResult = txn.exec(
                    "SELECT COUNT(*) as total FROM item_definitions"
                );
                int totalItemDefs = totalItemsResult[0]["total"].as<int>(0);

                std::vector<CollectionRankingEntry> collectionEntries;
                int collectionRank = 1;
                for (const auto& row : collectionResult) {
                    CollectionRankingEntry entry;
                    entry.characterName       = row["character_name"].as<std::string>("");
                    entry.classType           = row["class_name"].as<std::string>("");
                    entry.level               = row["level"].as<int>(1);
                    entry.uniqueItemsCollected = row["unique_count"].as<int>(0);
                    entry.totalItemDefinitions = totalItemDefs;
                    entry.faction             = row["faction"].as<int>(0);
                    entry.rankPosition        = collectionRank++;
                    collectionEntries.push_back(entry);
                }
                rankingMgr_.setCollectionRankings(std::move(collectionEntries));

                txn.commit();
                rankingMgr_.setCacheTime(gameTime_);

                LOG_INFO("Server", "Ranking cache refreshed from DB");
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Server", "Failed to refresh ranking cache: %s", e.what());
            // Serve stale data — better than nothing
        }
    }

    // Build JSON response from cached data
    nlohmann::json jsonEntries = nlohmann::json::array();
    uint16_t totalEntries = 0;

    if (category == RankingCategory::Guilds) {
        auto entries = rankingMgr_.getGuildRankings(msg.page);
        totalEntries = static_cast<uint16_t>(entries.size());
        for (const auto& e : entries) {
            jsonEntries.push_back({
                {"rank", e.rankPosition},
                {"guildId", e.guildId},
                {"guildName", e.guildName},
                {"guildLevel", e.guildLevel},
                {"memberCount", e.memberCount},
                {"ownerName", e.ownerName}
            });
        }
    } else if (category == RankingCategory::Honor) {
        auto entries = rankingMgr_.getHonorRankings(msg.page);
        totalEntries = static_cast<uint16_t>(entries.size());
        for (const auto& e : entries) {
            jsonEntries.push_back({
                {"rank", e.rankPosition},
                {"characterName", e.characterName},
                {"classType", e.classType},
                {"level", e.level},
                {"honor", e.honor},
                {"pvpKills", e.pvpKills},
                {"pvpDeaths", e.pvpDeaths},
                {"kdRatio", e.getKDRatio()}
            });
        }
    } else if (category == RankingCategory::MobsKilled) {
        auto entries = rankingMgr_.getMobKillRankings(msg.page);
        totalEntries = static_cast<uint16_t>(entries.size());
        for (const auto& e : entries) {
            jsonEntries.push_back({
                {"rank", e.rankPosition},
                {"characterName", e.characterName},
                {"classType", e.classType},
                {"level", e.level},
                {"totalMobKills", e.totalMobKills}
            });
        }
    } else if (category == RankingCategory::CollectionProgress) {
        auto entries = rankingMgr_.getCollectionRankings(msg.page);
        totalEntries = static_cast<uint16_t>(entries.size());
        for (const auto& e : entries) {
            jsonEntries.push_back({
                {"rank", e.rankPosition},
                {"characterName", e.characterName},
                {"classType", e.classType},
                {"level", e.level},
                {"uniqueItems", e.uniqueItemsCollected},
                {"totalItems", e.totalItemDefinitions},
                {"percentage", e.getPercentage()}
            });
        }
    } else {
        // PlayersGlobal, PlayersWarrior, PlayersMage, PlayersArcher
        auto entries = rankingMgr_.getPlayerRankings(category, msg.page);
        totalEntries = static_cast<uint16_t>(entries.size());
        for (const auto& e : entries) {
            jsonEntries.push_back({
                {"rank", e.rankPosition},
                {"characterName", e.characterName},
                {"classType", e.classType},
                {"level", e.level}
            });
        }
    }

    // Send response
    SvRankingResultMsg result;
    result.category      = msg.category;
    result.page          = msg.page;
    result.factionFilter = msg.factionFilter;
    result.totalEntries  = totalEntries;
    result.entriesJson   = jsonEntries.dump();

    uint8_t buf[MAX_PAYLOAD_SIZE];
    ByteWriter w(buf, sizeof(buf));
    result.write(w);
    server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::SvRankingResult, buf, w.size());

    LOG_INFO("Server", "Client %d queried rankings cat=%d page=%d (%d entries)",
             clientId, msg.category, msg.page, totalEntries);
}

} // namespace fate
