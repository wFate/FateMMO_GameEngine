#include "engine/telemetry/telemetry.h"
#include <spdlog/spdlog.h>

void TelemetryCollector::record(const std::string& name, float value) {
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
