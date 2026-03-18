#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <pqxx/pqxx>
#include "game/shared/bounty_system.h"

namespace fate {

class BountyRepository {
public:
    explicit BountyRepository(pqxx::connection& conn) : conn_(conn) {}

    // Board queries
    std::vector<BountyInfo> getBountyBoard();
    int getActiveBountyCount();
    bool hasActiveBounty(const std::string& targetCharId);
    std::optional<BountyInfo> getBountyForTarget(const std::string& targetCharId);

    // Placement (returns bountyId or -1)
    int placeBounty(const std::string& targetCharId, const std::string& targetCharName,
                    const std::string& contributorCharId, const std::string& contributorCharName,
                    int64_t amount, BountyResult& result);

    // Cancellation (returns refund before tax, sets taxAmount)
    int64_t cancelContribution(const std::string& targetCharId,
                               const std::string& contributorCharId,
                               int64_t& taxAmount, BountyResult& result);

    // Claiming (returns net payout after tax)
    int64_t claimBounty(const std::string& targetCharId,
                        const std::string& claimerCharId, const std::string& claimerName,
                        int partySize, int64_t& taxAmount, int64_t& amountPerMember,
                        BountyResult& result);

    // Expiration
    struct ExpiredRefund {
        std::string contributorCharId;
        std::string contributorCharName;
        int64_t contributed = 0;
        int64_t tax = 0;
        int64_t refund = 0;
    };
    std::vector<ExpiredRefund> processExpiredBounties();

    // Guild cooldown check
    bool recentlyLeftGuild(const std::string& characterId);

private:
    pqxx::connection& conn_;

    void recordHistory(pqxx::work& txn, const std::string& eventType,
                       const std::string& targetCharId, const std::string& targetCharName,
                       const std::string& actorCharId, const std::string& actorCharName,
                       int64_t amount, int64_t taxAmount, int partySize = 1,
                       int64_t amountPerMember = 0);
};

} // namespace fate
