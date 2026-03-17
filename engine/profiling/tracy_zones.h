#pragma once

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#define FATE_ZONE(name, color) ZoneScopedNC(name, color)
#define FATE_ZONE_NAME(name) ZoneScopedN(name)
#define FATE_ALLOC(ptr, size) TracyAlloc(ptr, size)
#define FATE_FREE(ptr) TracyFree(ptr)
#define FATE_FRAME_MARK FrameMark
#else
#define FATE_ZONE(name, color)
#define FATE_ZONE_NAME(name)
#define FATE_ALLOC(ptr, size)
#define FATE_FREE(ptr)
#define FATE_FRAME_MARK
#endif
