#include "server/db/account_repository.h"
#include "engine/core/logger.h"

namespace fate {

int AccountRepository::createAccount(const std::string& username, const std::string& passwordHash,
                                     const std::string& email) {
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "INSERT INTO accounts (username, password_hash, email) "
            "VALUES ($1, $2, $3) RETURNING account_id",
            username, passwordHash, email);
        txn.commit();
        if (!result.empty()) return result[0][0].as<int>();
    } catch (const pqxx::unique_violation&) {
        LOG_WARN("AccountRepo", "Username or email already exists: %s", username.c_str());
    } catch (const std::exception& e) {
        LOG_ERROR("AccountRepo", "createAccount failed: %s", e.what());
    }
    return -1;
}

std::optional<AccountRecord> AccountRepository::findByUsername(const std::string& username) {
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT account_id, username, password_hash, email, is_active, is_banned, ban_reason "
            "FROM accounts WHERE username = $1",
            username);
        txn.commit();
        if (result.empty()) return std::nullopt;
        AccountRecord rec;
        rec.account_id    = result[0]["account_id"].as<int>();
        rec.username      = result[0]["username"].as<std::string>();
        rec.password_hash = result[0]["password_hash"].as<std::string>();
        rec.email         = result[0]["email"].as<std::string>();
        rec.is_active     = result[0]["is_active"].is_null() ? true : result[0]["is_active"].as<bool>();
        rec.is_banned     = result[0]["is_banned"].is_null() ? false : result[0]["is_banned"].as<bool>();
        rec.ban_reason    = result[0]["ban_reason"].is_null() ? "" : result[0]["ban_reason"].as<std::string>();
        return rec;
    } catch (const std::exception& e) {
        LOG_ERROR("AccountRepo", "findByUsername failed: %s", e.what());
    }
    return std::nullopt;
}

void AccountRepository::updateLastLogin(int accountId) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params("UPDATE accounts SET last_login = NOW() WHERE account_id = $1", accountId);
        txn.commit();
    } catch (const std::exception& e) {
        LOG_ERROR("AccountRepo", "updateLastLogin failed: %s", e.what());
    }
}

} // namespace fate
