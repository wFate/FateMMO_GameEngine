/**************************************************************************/
/*  ad_service_stub.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                          FateMMO Game Engine                           */
/*                       https://www.FateMMO.com                          */
/**************************************************************************/
/* Copyright (c) 2026-present FateMMO Game Engine contributors.           */
/* Copyright (c) 2026-present Caleb Kious.                                */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/
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
