#pragma once
#include <string>
#include <expected>
#include <cstdint>

namespace fate {

enum class ErrorCategory : uint8_t {
    Transient   = 0, // retry likely succeeds (timeout, busy)
    Recoverable = 1, // can queue and degrade (DB down)
    Degraded    = 2, // subsystem offline
    Fatal       = 3  // unrecoverable
};

struct EngineError {
    ErrorCategory category = ErrorCategory::Transient;
    uint16_t code = 0;
    std::string message;
};

template<typename T>
using Result = std::expected<T, EngineError>;

inline EngineError transientError(uint16_t code, std::string msg) {
    return {ErrorCategory::Transient, code, std::move(msg)};
}
inline EngineError recoverableError(uint16_t code, std::string msg) {
    return {ErrorCategory::Recoverable, code, std::move(msg)};
}
inline EngineError fatalError(uint16_t code, std::string msg) {
    return {ErrorCategory::Fatal, code, std::move(msg)};
}

} // namespace fate
