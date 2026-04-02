#include "engine/platform/device_info.h"
#include <SDL.h>

// --- Platform-specific RAM detection ---
#if defined(_WIN32)
#include <windows.h>
uint64_t DeviceInfo::getPhysicalRAM_MB() {
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) return 4096; // fallback
    return status.ullTotalPhys / (1024 * 1024);
}
int DeviceInfo::getThermalState() { return 0; } // desktop: always nominal

#elif defined(__APPLE__)
#include <sys/sysctl.h>
extern "C" int device_info_get_thermal_state(); // defined in device_info_apple.mm
uint64_t DeviceInfo::getPhysicalRAM_MB() {
    int64_t memsize = 0;
    size_t len = sizeof(memsize);
    sysctlbyname("hw.memsize", &memsize, &len, nullptr, 0);
    return memsize > 0 ? static_cast<uint64_t>(memsize) / (1024 * 1024) : 4096;
}
int DeviceInfo::getThermalState() { return device_info_get_thermal_state(); }

#elif defined(__linux__) || defined(__ANDROID__)
#include <fstream>
#include <string>
uint64_t DeviceInfo::getPhysicalRAM_MB() {
    std::ifstream meminfo("/proc/meminfo");
    std::string label;
    uint64_t kb = 0;
    if (meminfo >> label >> kb) {
        return kb / 1024; // MemTotal is in kB
    }
    return 4096; // fallback
}
#if defined(__ANDROID__) && __ANDROID_API__ >= 30
#include <android/thermal.h>
int DeviceInfo::getThermalState() {
    // AThermal status: NONE(0), LIGHT(1), MODERATE(2), SEVERE(3), CRITICAL(4), EMERGENCY(5), SHUTDOWN(6)
    // Map to our 0-3 scale
    AThermalStatus status = AThermal_getCurrentThermalStatus();
    if (status <= ATHERMAL_STATUS_NONE)     return 0; // nominal
    if (status == ATHERMAL_STATUS_LIGHT)    return 1; // fair
    if (status == ATHERMAL_STATUS_MODERATE) return 2; // serious
    return 3;                                          // critical (SEVERE+)
}
#else
int DeviceInfo::getThermalState() { return 0; } // Linux desktop or Android API < 30
#endif

#else
uint64_t DeviceInfo::getPhysicalRAM_MB() { return 4096; }
int DeviceInfo::getThermalState() { return 0; }
#endif

// --- Platform-independent ---
DeviceTier DeviceInfo::getDeviceTier() {
    auto ram = getPhysicalRAM_MB();
    if (ram <= 3072) return DeviceTier::Low;
    if (ram <= 6144) return DeviceTier::Medium;
    return DeviceTier::High;
}

int DeviceInfo::recommendedVRAMBudget() {
    switch (getDeviceTier()) {
        case DeviceTier::Low:    return 200;
        case DeviceTier::Medium: return 350;
        case DeviceTier::High:   return 512;
    }
    return 256;
}

int DeviceInfo::getDisplayRefreshRate() {
    SDL_DisplayMode mode;
    if (SDL_GetDesktopDisplayMode(0, &mode) == 0 && mode.refresh_rate > 0) {
        return mode.refresh_rate;
    }
    return 60; // fallback
}

int DeviceInfo::recommendedFPS() {
    int native = getDisplayRefreshRate();
    switch (getThermalState()) {
        case 0: return native;            // nominal: full refresh rate
        case 1: return native;            // fair: still full
        case 2: return native > 60 ? 60 : native; // serious: cap to 60
        case 3: return native > 60 ? 60 : native; // critical: cap to 60
    }
    return native;
}
