/**************************************************************************/
/*  telemetry.cpp                                                         */
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
#include "engine/telemetry/telemetry.h"
#include <spdlog/spdlog.h>

void TelemetryCollector::record(const std::string& name, float value) {
    if (pending_.size() >= MAX_PENDING) return; // drop new entries at capacity
    auto now = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    pending_.push_back({name, value, now});
}

nlohmann::json TelemetryCollector::flushToJson() {
    nlohmann::json j;
    j["session_id"] = sessionId_;
    j["metrics"] = nlohmann::json::array();
    for (const auto& m : pending_) {
        j["metrics"].push_back({
            {"name", m.name},
            {"value", m.value},
            {"timestamp", m.timestamp}
        });
    }
    pending_.clear();
    return j;
}

bool TelemetryCollector::trySend() {
    if (endpoint_.empty() || pending_.empty()) return false;
    auto payload = flushToJson().dump();
    spdlog::debug("[Telemetry] Would send {} bytes to {}", payload.size(), endpoint_);
    return true;
}
