#include "server/db/bounty_repository.h"
#include "engine/core/logger.h"

namespace fate {

std::vector<BountyInfo> BountyRepository::getBountyBoard() {
    std::vector<BountyInfo> board;
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec(
            "SELECT bounty_id, target_character_id, target_character_name, total_amount, "
            "EXTRACT(EPOCH FROM (expires_at - NOW()))::BIGINT AS seconds_remaining "
            "FROM bounties WHERE is_active = TRUE AND expires_at > NOW() "
            "ORDER BY expires_at ASC LIMIT 10");
        txn.commit();
        board.reserve(result.size());
        for (const auto& row : result) {
            BountyInfo b;
            b.bountyId            = row["bounty_id"].as<int>();
            b.targetCharacterId   = row["target_character_id"].as<std::string>();
            b.targetCharacterName = row["target_character_name"].as<std::string>();
            b.totalAmount         = row["total_amount"].as<int64_t>();
            b.secondsRemaining    = row["seconds_remaining"].as<int64_t>();
            board.push_back(std::move(b));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("BountyRepo", "getBountyBoard failed: %s", e.what());
    }
    return board;
}

int BountyRepository::getActiveBountyCount() {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec(
            "SELECT COUNT(*) FROM bounties WHERE is_active = TRUE AND expires_at > NOW()");
        txn.commit();
        return result[0][0].as<int>();
    } catch (const std::exception& e) {
        LOG_ERROR("BountyRepo", "getActiveBountyCount failed: %s", e.what());
    }
    return 0;
}

bool BountyRepository::hasActiveBounty(const std::string& targetCharId) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT EXISTS(SELECT 1 FROM bounties "
            "WHERE target_character_id = $1 AND is_active = TRUE AND expires_at > NOW())",
            targetCharId);
        txn.commit();
        return result[0][0].as<bool>();
    } catch (const std::exception& e) {
        LOG_ERROR("BountyRepo", "hasActiveBounty failed: %s", e.what());
    }
    return false;
}

std::optional<BountyInfo> BountyRepository::getBountyForTarget(const std::string& targetCharId) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT bounty_id, target_character_id, target_character_name, total_amount, "
            "EXTRACT(EPOCH FROM (expires_at - NOW()))::BIGINT AS seconds_remaining "
            "FROM bounties WHERE target_character_id = $1 AND is_active = TRUE AND expires_at > NOW()",
            targetCharId);
        txn.commit();
        if (result.empty()) return std::nullopt;

        BountyInfo b;
        b.bountyId            = result[0]["bounty_id"].as<int>();
        b.targetCharacterId   = result[0]["target_character_id"].as<std::string>();
        b.targetCharacterName = result[0]["target_character_name"].as<std::string>();
        b.totalAmount         = result[0]["total_amount"].as<int64_t>();
        b.secondsRemaining    = result[0]["seconds_remaining"].as<int64_t>();
        return b;
    } catch (const std::exception& e) {
        LOG_ERROR("BountyRepo", "getBountyForTarget failed: %s", e.what());
    }
    return std::nullopt;
}

int BountyRepository::placeBounty(const std::string& targetCharId, const std::string& targetCharName,
                                   const std::string& contributorCharId, const std::string& contributorCharName,
                                   int64_t amount, BountyResult& result) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());

        // Check if bounty already exists for this target
        auto existing = txn.exec_params(
            "SELECT bounty_id, total_amount FROM bounties "
            "WHERE target_character_id = $1 AND is_active = TRUE AND expires_at > NOW() "
            "FOR UPDATE", targetCharId);

        int bountyId;
        if (!existing.empty()) {
            bountyId = existing[0]["bounty_id"].as<int>();
            int64_t currentTotal = existing[0]["total_amount"].as<int64_t>();
            if (currentTotal + amount > BountyConstants::MAX_BOUNTY) {
                result = BountyResult::ExceedsMaxBounty;
                return -1;
            }
            txn.exec_params(
                "UPDATE bounties SET total_amount = total_amount + $2 WHERE bounty_id = $1",
                bountyId, amount);
        } else {
            // Check board capacity
            auto countResult = txn.exec(
                "SELECT COUNT(*) FROM bounties WHERE is_active = TRUE AND expires_at > NOW()");
            if (countResult[0][0].as<int>() >= BountyConstants::MAX_ACTIVE_BOUNTIES) {
                result = BountyResult::BoardFull;
                return -1;
            }
            auto ins = txn.exec_params(
                "INSERT INTO bounties (target_character_id, target_character_name, total_amount, "
                "created_at, expires_at, is_active) "
                "VALUES ($1, $2, $3, NOW(), NOW() + INTERVAL '2 days', TRUE) RETURNING bounty_id",
                targetCharId, targetCharName, amount);
            bountyId = ins[0][0].as<int>();
        }

        // Record contribution
        txn.exec_params(
            "INSERT INTO bounty_contributions (bounty_id, contributor_character_id, "
            "contributor_character_name, amount, contributed_at, is_cancelled) "
            "VALUES ($1, $2, $3, $4, NOW(), FALSE)",
            bountyId, contributorCharId, contributorCharName, amount);

        recordHistory(txn, "placed", targetCharId, targetCharName,
                      contributorCharId, contributorCharName, amount, 0);

        txn.commit();
        result = BountyResult::Success;
        return bountyId;
    } catch (const std::exception& e) {
        LOG_ERROR("BountyRepo", "placeBounty failed: %s", e.what());
        result = BountyResult::ServerError;
    }
    return -1;
}

int64_t BountyRepository::cancelContribution(const std::string& targetCharId,
                                              const std::string& contributorCharId,
                                              int64_t& taxAmount, BountyResult& result) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());

        // Get bounty
        auto bounty = txn.exec_params(
            "SELECT bounty_id, total_amount FROM bounties "
            "WHERE target_character_id = $1 AND is_active = TRUE FOR UPDATE", targetCharId);
        if (bounty.empty()) { result = BountyResult::BountyNotFound; return 0; }

        int bountyId = bounty[0]["bounty_id"].as<int>();

        // Sum contributions
        auto contrib = txn.exec_params(
            "SELECT COALESCE(SUM(amount), 0) FROM bounty_contributions "
            "WHERE bounty_id = $1 AND contributor_character_id = $2 AND is_cancelled = FALSE",
            bountyId, contributorCharId);
        int64_t contributed = contrib[0][0].as<int64_t>();
        if (contributed <= 0) { result = BountyResult::NoContribution; return 0; }

        // Mark cancelled
        txn.exec_params(
            "UPDATE bounty_contributions SET is_cancelled = TRUE, cancelled_at = NOW() "
            "WHERE bounty_id = $1 AND contributor_character_id = $2 AND is_cancelled = FALSE",
            bountyId, contributorCharId);

        // Update bounty total
        txn.exec_params(
            "UPDATE bounties SET total_amount = total_amount - $2 WHERE bounty_id = $1",
            bountyId, contributed);

        // Deactivate if below minimum
        auto remaining = txn.exec_params(
            "SELECT total_amount FROM bounties WHERE bounty_id = $1", bountyId);
        if (!remaining.empty() && remaining[0][0].as<int64_t>() < BountyConstants::MIN_BOUNTY) {
            txn.exec_params("UPDATE bounties SET is_active = FALSE WHERE bounty_id = $1", bountyId);
        }

        taxAmount = static_cast<int64_t>(contributed * BountyConstants::TAX_PERCENT);
        int64_t refund = contributed - taxAmount;

        recordHistory(txn, "cancelled", targetCharId, "",
                      contributorCharId, "", contributed, taxAmount);

        txn.commit();
        result = BountyResult::Success;
        return refund;
    } catch (const std::exception& e) {
        LOG_ERROR("BountyRepo", "cancelContribution failed: %s", e.what());
        result = BountyResult::ServerError;
    }
    return 0;
}

int64_t BountyRepository::claimBounty(const std::string& targetCharId,
                                       const std::string& claimerCharId, const std::string& claimerName,
                                       int partySize, int64_t& taxAmount, int64_t& amountPerMember,
                                       BountyResult& result) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());

        auto bounty = txn.exec_params(
            "SELECT bounty_id, total_amount, target_character_name FROM bounties "
            "WHERE target_character_id = $1 AND is_active = TRUE FOR UPDATE", targetCharId);
        if (bounty.empty() || bounty[0]["total_amount"].as<int64_t>() <= 0) {
            result = BountyResult::BountyNotFound;
            return 0;
        }

        int bountyId = bounty[0]["bounty_id"].as<int>();
        int64_t totalAmount = bounty[0]["total_amount"].as<int64_t>();
        std::string targetName = bounty[0]["target_character_name"].as<std::string>();

        // Deactivate
        txn.exec_params("UPDATE bounties SET is_active = FALSE WHERE bounty_id = $1", bountyId);

        // Calculate payout
        taxAmount = static_cast<int64_t>(totalAmount * BountyConstants::TAX_PERCENT);
        int64_t netPayout = totalAmount - taxAmount;
        int effectiveParty = partySize > 0 ? partySize : 1;
        amountPerMember = netPayout / effectiveParty;

        // Log claim
        txn.exec_params(
            "INSERT INTO bounty_claim_log (claimer_character_id, target_character_id, "
            "amount_claimed, claimed_at) VALUES ($1, $2, $3, NOW())",
            claimerCharId, targetCharId, netPayout);

        recordHistory(txn, "claimed", targetCharId, targetName,
                      claimerCharId, claimerName, totalAmount, taxAmount,
                      effectiveParty, amountPerMember);

        txn.commit();
        result = BountyResult::Success;
        return netPayout;
    } catch (const std::exception& e) {
        LOG_ERROR("BountyRepo", "claimBounty failed: %s", e.what());
        result = BountyResult::ServerError;
    }
    return 0;
}

std::vector<BountyRepository::ExpiredRefund> BountyRepository::processExpiredBounties() {
    std::vector<ExpiredRefund> refunds;
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());

        // Find expired bounties
        auto expired = txn.exec(
            "SELECT bounty_id, target_character_id, target_character_name "
            "FROM bounties WHERE is_active = TRUE AND expires_at <= NOW()");

        for (const auto& bRow : expired) {
            int bountyId = bRow["bounty_id"].as<int>();
            std::string targetId = bRow["target_character_id"].as<std::string>();
            std::string targetName = bRow["target_character_name"].as<std::string>();

            // Deactivate
            txn.exec_params("UPDATE bounties SET is_active = FALSE WHERE bounty_id = $1", bountyId);

            // Get contributions for refund
            auto contribs = txn.exec_params(
                "SELECT contributor_character_id, contributor_character_name, "
                "SUM(amount) AS total_contributed "
                "FROM bounty_contributions "
                "WHERE bounty_id = $1 AND is_cancelled = FALSE "
                "GROUP BY contributor_character_id, contributor_character_name",
                bountyId);

            for (const auto& cRow : contribs) {
                ExpiredRefund r;
                r.contributorCharId   = cRow["contributor_character_id"].as<std::string>();
                r.contributorCharName = cRow["contributor_character_name"].as<std::string>();
                r.contributed         = cRow["total_contributed"].as<int64_t>();
                r.tax    = static_cast<int64_t>(r.contributed * BountyConstants::TAX_PERCENT);
                r.refund = r.contributed - r.tax;
                refunds.push_back(std::move(r));

                recordHistory(txn, "expired", targetId, targetName,
                              r.contributorCharId, r.contributorCharName,
                              r.contributed, r.tax);
            }
        }

        txn.commit();
        if (!refunds.empty()) {
            LOG_INFO("BountyRepo", "Processed %d expired bounty refunds", static_cast<int>(refunds.size()));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("BountyRepo", "processExpiredBounties failed: %s", e.what());
    }
    return refunds;
}

bool BountyRepository::recentlyLeftGuild(const std::string& characterId) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT guild_left_at FROM characters WHERE character_id = $1", characterId);
        txn.commit();
        if (result.empty() || result[0]["guild_left_at"].is_null()) return false;
        // Check if left within 12 hours — use DB-side comparison
        auto guard2 = acquireConn();
        pqxx::work txn2(guard2.connection());
        auto check = txn2.exec_params(
            "SELECT guild_left_at > NOW() - INTERVAL '12 hours' AS recent "
            "FROM characters WHERE character_id = $1 AND guild_left_at IS NOT NULL",
            characterId);
        txn2.commit();
        return !check.empty() && check[0][0].as<bool>();
    } catch (const std::exception& e) {
        LOG_ERROR("BountyRepo", "recentlyLeftGuild failed: %s", e.what());
    }
    return false;
}

void BountyRepository::recordHistory(pqxx::work& txn, const std::string& eventType,
                                      const std::string& targetCharId, const std::string& targetCharName,
                                      const std::string& actorCharId, const std::string& actorCharName,
                                      int64_t amount, int64_t taxAmount, int partySize,
                                      int64_t amountPerMember) {
    txn.exec_params(
        "INSERT INTO bounty_history (event_type, target_character_id, target_character_name, "
        "actor_character_id, actor_character_name, amount, tax_amount, "
        "party_size, amount_per_member, occurred_at) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, NOW())",
        eventType, targetCharId, targetCharName,
        actorCharId.empty() ? std::optional<std::string>(std::nullopt) : std::optional(actorCharId),
        actorCharName.empty() ? std::optional<std::string>(std::nullopt) : std::optional(actorCharName),
        amount, taxAmount, partySize, amountPerMember);
}

} // namespace fate
