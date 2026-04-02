#pragma once
#include <cstdint>

enum class DeviceTier : uint8_t {
    Low    = 0,  // <=3GB RAM, 200MB VRAM budget
    Medium = 1,  // 4-6GB RAM, 350MB VRAM budget
    High   = 2   // 8GB+ RAM, 512MB VRAM budget
};

struct DeviceInfo {
    static uint64_t getPhysicalRAM_MB();
    static DeviceTier getDeviceTier();
    static int recommendedVRAMBudget(); // returns MB

    // Thermal: 0=nominal, 1=fair, 2=serious, 3=critical
    static int getThermalState();

    // Returns the display's native refresh rate (e.g. 60, 120). Requires SDL init.
    static int getDisplayRefreshRate();

    // Returns recommended FPS based on display refresh rate and thermal state.
    static int recommendedFPS();
};
