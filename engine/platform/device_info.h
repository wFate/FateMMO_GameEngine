/**************************************************************************/
/*  device_info.h                                                         */
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
