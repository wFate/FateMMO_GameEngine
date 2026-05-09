/**************************************************************************/
/*  telemetry.h                                                           */
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
#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <chrono>

struct TelemetryMetric {
    std::string name;
    float value;
    double timestamp; // seconds since epoch
};

class TelemetryCollector {
public:
    void setSessionId(const std::string& id) { sessionId_ = id; }
    void setEndpoint(const std::string& url) { endpoint_ = url; }

    static constexpr size_t MAX_PENDING = 10000;

    void record(const std::string& name, float value);
    size_t pendingCount() const { return pending_.size(); }

    // Serialize pending metrics to JSON and clear buffer
    nlohmann::json flushToJson();

    // Placeholder: log payload and return true (actual HTTPS POST is future work)
    bool trySend();

private:
    std::string sessionId_;
    std::string endpoint_;
    std::vector<TelemetryMetric> pending_;
};
