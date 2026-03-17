#pragma once
#include <cstdint>

namespace fate {

// ============================================================================
// SpatialError -- shared error enum for spatial query results
// ============================================================================
enum class SpatialError : uint8_t {
    NotFound,
    OutOfBounds,
    ChunkNotLoaded
};

} // namespace fate
