#pragma once
#include <string>
#include <optional>
#include <pqxx/pqxx>

namespace fate {

struct AccountRecord {
    int account_id = 0;
    std::string username;
    std::string password_hash;
    std::string email;
    bool is_active = true;
    bool is_banned = false;
    std::string ban_reason;
};

class AccountRepository {
public:
    explicit AccountRepository(pqxx::connection& conn) : conn_(conn) {}

    // Returns account_id on success, -1 on failure (duplicate username/email)
    int createAccount(const std::string& username, const std::string& passwordHash,
                      const std::string& email);

    // Returns nullopt if not found
    std::optional<AccountRecord> findByUsername(const std::string& username);

    void updateLastLogin(int accountId);

private:
    pqxx::connection& conn_;
};

} // namespace fate
