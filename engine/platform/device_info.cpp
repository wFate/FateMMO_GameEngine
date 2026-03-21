#include "engine/platform/device_info.h"

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
uint64_t DeviceInfo::getPhysicalRAM_MB() {
    int64_t memsize = 0;
    size_t len = sizeof(memsize);
    sysctlbyname("hw.memsize", &memsize, &len, nullptr, 0);
    return memsize > 0 ? static_cast<uint64_t>(memsize) / (1024 * 1024) : 4096;
}
int DeviceInfo::getThermalState() { return 0; } // TODO: iOS ObjC++ bridge for NSProcessInfo.thermalState

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
int DeviceInfo::getThermalState() { return 0; } // TODO: Android AThermal API

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

int DeviceInfo::recommendedFPS() {
    switch (getThermalState()) {
        case 0: return 60;
        case 1: return 60;
        case 2: return 30;
        case 3: return 30;
    }
    return 60;
}
