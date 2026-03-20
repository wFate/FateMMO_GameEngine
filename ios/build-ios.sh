#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "=== FateMMO iOS Build ==="
echo "Project: $PROJECT_DIR"

# Generate Xcode project
cmake -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_CXX_STANDARD=23 \
    -DFATEMMO_BUILD_MOBILE=ON \
    -S "$PROJECT_DIR" -B "$PROJECT_DIR/build-ios"

echo ""
echo "=== Xcode project generated at build-ios/ ==="
echo ""
echo "Next steps:"
echo "  1. Open build-ios/FateEngine.xcodeproj in Xcode"
echo "  2. Select your Apple ID in Signing & Capabilities"
echo "  3. Select your iPhone as the run destination"
echo "  4. Press Cmd+R to build and run"
echo ""
echo "Or build from command line:"
echo "  cmake --build build-ios --config Debug -- -allowProvisioningUpdates"
