#pragma once
#include <string>
#include <functional>

namespace fate {

// ============================================================================
// AdService — rewarded video ad integration (AdMob on Android/iOS)
// ============================================================================
//
// Platform support:
//   - Android: AdMob via Java JNI bridge (ad_service_android.cpp)
//   - iOS:     AdMob via Objective-C++ bridge (ad_service_ios.mm)
//   - Desktop: stub (ad_service_stub.cpp) — always returns false.  Desktop is
//              dev-only so desktop builds simply don't show the ad button.
//
// Reward flow:
//   The client NEVER grants the XP buff directly.  When a rewarded ad finishes
//   playing, AdMob's servers fire a Server-Side Verification (SSV) HTTP POST
//   to https://ads.your-domain.com/admob-ssv.  That endpoint writes a row to
//   ad_verifications.  FateServer polls the table and applies the buff via
//   the normal SvAdRewardResult push.  A modded client cannot forge a grant.
//
// Usage from UI:
//   if (AdService::isAvailable() && AdService::isRewardedAdReady()) {
//       showButton("Watch Ad (+20% XP)");
//       if (clicked) AdService::showRewardedAd(clientCharacterId);
//   }
//
// The client passes `userId` (character_id) to the SDK; AdMob forwards it in
// the SSV callback as the `user_id` field.  That's how we know which player
// to grant the reward to even though the grant comes from AdMob's servers,
// not the client's network session.
// ============================================================================

class AdService {
public:
    // Initialize the ad SDK.  Call once at app startup, AFTER the consent
    // flow (UMP / ATT) has resolved.  Safe to call multiple times.
    static void initialize();

    // True on mobile platforms where the AdMob SDK is linked.  False on
    // desktop — UI should hide the "Watch Ad" button.
    static bool isAvailable();

    // Preload a rewarded video.  Call at app start and again after each show()
    // so a video is ready when the player wants one.  No-op on desktop.
    static void loadRewardedAd();

    // True if a preloaded rewarded video is ready to play right now.
    static bool isRewardedAdReady();

    // Show the preloaded rewarded video.  `userId` is typically the player's
    // character_id; it is forwarded verbatim to AdMob's SSV callback so the
    // server knows which account to reward.
    //
    // The function returns immediately (non-blocking).  When the ad finishes,
    // AdMob's servers hit the SSV endpoint and the server applies the buff;
    // the client sees it via the normal SvBuffSync / SvPlayerState path.
    static void showRewardedAd(const std::string& userId);

    // Optional: opt-in consent state. On EU/UK users the ad SDK must NOT be
    // initialized before this is either "granted" or "declined".  Wire this
    // up to your UMP (Android) / ATT (iOS) consent result.
    enum class ConsentStatus : int {
        Unknown = 0,
        Required = 1,
        NotRequired = 2,
        Obtained = 3,
    };
    static void setConsentStatus(ConsentStatus status);
    static ConsentStatus consentStatus();
};

} // namespace fate
