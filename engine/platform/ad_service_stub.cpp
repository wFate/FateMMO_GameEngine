#include "engine/platform/ad_service.h"
#include "engine/core/logger.h"

// Desktop stub.  Desktop is dev-only — real ads only run on Android/iOS.
// Linked on Windows/Mac/Linux dev builds so the UI can call AdService
// unconditionally; isAvailable() returns false and the chat button is hidden.

namespace fate {

static AdService::ConsentStatus s_consent = AdService::ConsentStatus::NotRequired;

void AdService::initialize() {
    // Desktop is dev-only; real ads only run on Android/iOS.
}
bool AdService::isAvailable()      { return false; }
void AdService::loadRewardedAd()   { /* no-op */ }
bool AdService::isRewardedAdReady(){ return false; }
void AdService::showRewardedAd(const std::string&) {
    LOG_WARN("AdService", "showRewardedAd called on desktop — ignoring");
}
void AdService::setConsentStatus(ConsentStatus s) { s_consent = s; }
AdService::ConsentStatus AdService::consentStatus() { return s_consent; }

} // namespace fate
