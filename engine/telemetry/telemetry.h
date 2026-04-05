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
