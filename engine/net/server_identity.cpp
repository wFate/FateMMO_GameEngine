#include "engine/net/server_identity.h"
#include "engine/core/logger.h"
#include <fstream>
#include <cstring>
#include <filesystem>
#include <vector>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#if FATE_HAS_SODIUM
#include <sodium.h>
#endif

namespace {
    // Lock down the static identity key to owner-only read/write so it can't
    // leak to other local users on shared hosts. On POSIX this is 0600; on
    // Windows we DACL the file to the current user + SYSTEM + Administrators.
    // Best-effort: a failure here is logged but not fatal (dev setups often
    // run in sandboxes where chmod/SetSecurityInfo would be redundant).
    bool restrictKeyFilePermissions(const std::string& path) {
#ifdef _WIN32
        // Build a DACL granting FILE_ALL_ACCESS only to:
        //   - BUILTIN\Administrators (S-1-5-32-544)
        //   - NT AUTHORITY\SYSTEM     (S-1-5-18)
        //   - the current user
        PSID pAdminSid = nullptr, pSystemSid = nullptr, pUserSid = nullptr;
        PACL pNewDacl = nullptr;
        bool ok = false;

        SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
        if (!AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                      DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdminSid)) goto cleanup;
        if (!AllocateAndInitializeSid(&ntAuth, 1, SECURITY_LOCAL_SYSTEM_RID,
                                      0, 0, 0, 0, 0, 0, 0, &pSystemSid)) goto cleanup;

        {
            HANDLE hToken = nullptr;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) goto cleanup;
            DWORD needed = 0;
            GetTokenInformation(hToken, TokenUser, nullptr, 0, &needed);
            std::vector<uint8_t> userBuf(needed);
            if (!GetTokenInformation(hToken, TokenUser, userBuf.data(), needed, &needed)) {
                CloseHandle(hToken);
                goto cleanup;
            }
            PSID src = reinterpret_cast<TOKEN_USER*>(userBuf.data())->User.Sid;
            DWORD sidLen = GetLengthSid(src);
            pUserSid = static_cast<PSID>(LocalAlloc(LPTR, sidLen));
            if (!pUserSid || !CopySid(sidLen, pUserSid, src)) {
                CloseHandle(hToken);
                goto cleanup;
            }
            CloseHandle(hToken);
        }

        {
            EXPLICIT_ACCESSW ea[3] = {};
            for (int i = 0; i < 3; ++i) {
                ea[i].grfAccessPermissions = FILE_ALL_ACCESS;
                ea[i].grfAccessMode = SET_ACCESS;
                ea[i].grfInheritance = NO_INHERITANCE;
                ea[i].Trustee.TrusteeForm = TRUSTEE_IS_SID;
                ea[i].Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
            }
            ea[0].Trustee.ptstrName = static_cast<LPWSTR>(pAdminSid);
            ea[1].Trustee.ptstrName = static_cast<LPWSTR>(pSystemSid);
            ea[2].Trustee.ptstrName = static_cast<LPWSTR>(pUserSid);

            if (SetEntriesInAclW(3, ea, nullptr, &pNewDacl) != ERROR_SUCCESS) goto cleanup;

            // PROTECTED_DACL_SECURITY_INFORMATION removes inherited ACEs that
            // might allow broader access.
            std::wstring wpath(path.begin(), path.end());
            DWORD rc = SetNamedSecurityInfoW(
                const_cast<LPWSTR>(wpath.c_str()),
                SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                nullptr, nullptr, pNewDacl, nullptr);
            ok = (rc == ERROR_SUCCESS);
        }

    cleanup:
        if (pNewDacl) LocalFree(pNewDacl);
        if (pUserSid) LocalFree(pUserSid);
        if (pSystemSid) FreeSid(pSystemSid);
        if (pAdminSid) FreeSid(pAdminSid);
        return ok;
#else
        // POSIX: 0600 — owner read/write, no one else.
        return ::chmod(path.c_str(), S_IRUSR | S_IWUSR) == 0;
#endif
    }
}

namespace fate {

bool ServerIdentity::loadOrGenerate(const std::string& keyFilePath,
                                    PacketCrypto::Keypair& outKeypair) {
#if FATE_HAS_SODIUM
    // Try loading existing keypair
    {
        std::ifstream f(keyFilePath, std::ios::binary);
        if (f.good()) {
            uint8_t buf[64];
            f.read(reinterpret_cast<char*>(buf), 64);
            if (f.gcount() == 64) {
                std::memcpy(outKeypair.pk.data(), buf, 32);
                std::memcpy(outKeypair.sk.data(), buf + 32, 32);
                PacketCrypto::secureWipe(buf, 64);
                LOG_INFO("ServerIdentity", "Loaded static identity keypair from %s", keyFilePath.c_str());
                return true;
            }
            LOG_WARN("ServerIdentity", "Key file %s exists but has wrong size (%lld bytes), regenerating",
                     keyFilePath.c_str(), static_cast<long long>(f.gcount()));
        }
    }

    // Generate new keypair (crypto_box keypair = X25519)
    crypto_box_keypair(outKeypair.pk.data(), outKeypair.sk.data());

    // Ensure parent directory exists
    {
        auto parent = std::filesystem::path(keyFilePath).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
    }

    // Save to file
    {
        std::ofstream f(keyFilePath, std::ios::binary | std::ios::trunc);
        if (!f.good()) {
            LOG_ERROR("ServerIdentity", "Failed to write key file %s", keyFilePath.c_str());
            return false;
        }
        f.write(reinterpret_cast<const char*>(outKeypair.pk.data()), 32);
        f.write(reinterpret_cast<const char*>(outKeypair.sk.data()), 32);
    }

    // Restrict the secret key file to owner-only — this key authenticates
    // every future connection, so a world-readable file is a real risk on
    // multi-user hosts. Failure is non-fatal but logged.
    if (!restrictKeyFilePermissions(keyFilePath)) {
        LOG_WARN("ServerIdentity",
                 "Failed to restrict permissions on %s — secret key may be readable by other users. "
                 "Lock down the file manually (chmod 600 / ACL) before going to production.",
                 keyFilePath.c_str());
    }

    // Also save the .pub file alongside
    std::string pubPath = keyFilePath + ".pub";
    savePublicKey(pubPath, outKeypair.pk);

    LOG_INFO("ServerIdentity", "Generated new static identity keypair -> %s", keyFilePath.c_str());
    LOG_INFO("ServerIdentity", "Public key saved to %s (distribute to client builds)", pubPath.c_str());

    return true;
#else
    LOG_WARN("ServerIdentity", "libsodium not available, cannot manage identity keys");
    outKeypair.pk.fill(0);
    outKeypair.sk.fill(0);
    return true;
#endif
}

bool ServerIdentity::loadPublicKey(const std::string& pubFilePath,
                                   PacketCrypto::PublicKey& outPk) {
    std::ifstream f(pubFilePath, std::ios::binary);
    if (!f.good()) {
        LOG_WARN("ServerIdentity", "Could not open public key file %s", pubFilePath.c_str());
        return false;
    }
    f.read(reinterpret_cast<char*>(outPk.data()), 32);
    if (f.gcount() != 32) {
        LOG_ERROR("ServerIdentity", "Public key file %s has wrong size (%lld bytes)",
                  pubFilePath.c_str(), static_cast<long long>(f.gcount()));
        return false;
    }
    LOG_INFO("ServerIdentity", "Loaded server public key from %s", pubFilePath.c_str());
    return true;
}

bool ServerIdentity::savePublicKey(const std::string& pubFilePath,
                                   const PacketCrypto::PublicKey& pk) {
    std::ofstream f(pubFilePath, std::ios::binary | std::ios::trunc);
    if (!f.good()) {
        LOG_ERROR("ServerIdentity", "Failed to write public key file %s", pubFilePath.c_str());
        return false;
    }
    f.write(reinterpret_cast<const char*>(pk.data()), 32);
    return true;
}

} // namespace fate
