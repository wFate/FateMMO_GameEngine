#include <doctest/doctest.h>
#include "engine/platform/device_info.h"

TEST_SUITE("Device Info") {

TEST_CASE("physical RAM is detected") {
    auto ram = DeviceInfo::getPhysicalRAM_MB();
    CHECK(ram > 0);
}

TEST_CASE("device tier is valid") {
    auto tier = DeviceInfo::getDeviceTier();
    CHECK((tier == DeviceTier::Low || tier == DeviceTier::Medium || tier == DeviceTier::High));
}

TEST_CASE("VRAM budget is positive and reasonable") {
    auto budget = DeviceInfo::recommendedVRAMBudget();
    CHECK(budget >= 200);
    CHECK(budget <= 512);
}

TEST_CASE("thermal state is in range") {
    auto state = DeviceInfo::getThermalState();
    CHECK(state >= 0);
    CHECK(state <= 3);
}

TEST_CASE("recommended FPS is 30 or 60") {
    auto fps = DeviceInfo::recommendedFPS();
    CHECK((fps == 30 || fps == 60));
}

} // TEST_SUITE
