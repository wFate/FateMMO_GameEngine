#import <Foundation/Foundation.h>

// Bridge function called from device_info.cpp on Apple platforms.
// NSProcessInfoThermalState maps directly to our 0-3 scale:
//   Nominal=0, Fair=1, Serious=2, Critical=3
extern "C" int device_info_get_thermal_state() {
    return static_cast<int>([[NSProcessInfo processInfo] thermalState]);
}
