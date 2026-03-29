#pragma once

namespace fate {

struct SafeAreaInsets {
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float left = 0.0f;
};

// Returns safe area insets for the current platform/device.
// In editor: returns simulated values from the selected DeviceProfile.
// On real devices: queries the OS (SDL/iOS/Android).
// On desktop: returns zeros.
SafeAreaInsets getPlatformSafeArea();

// Editor sets simulated insets when a DeviceProfile is selected
void setSimulatedSafeArea(const SafeAreaInsets& insets);

} // namespace fate
