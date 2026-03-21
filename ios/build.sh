#!/bin/bash
# ios/build.sh — FateMMO iOS build pipeline
# Usage: ./ios/build.sh [debug|release] [build|device|testflight]
set -e

CONFIG=${1:-Debug}
ACTION=${2:-build}
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-ios"

echo "=== FateMMO iOS $CONFIG build ==="

# Step 1: Generate Xcode project via CMake
cmake -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_CXX_STANDARD=23 \
  -DFATEMMO_BUILD_MOBILE=ON \
  -S "$PROJECT_ROOT" -B "$BUILD_DIR"

# Step 2: Build
cmake --build "$BUILD_DIR" --config "$CONFIG" -- -quiet

case "$ACTION" in
  device)
    echo "=== Deploying to connected device ==="
    if ! command -v ios-deploy &> /dev/null; then
      echo "Error: ios-deploy not found. Install via: brew install ios-deploy"
      exit 1
    fi
    ios-deploy --bundle "$BUILD_DIR/$CONFIG-iphoneos/FateEngine.app"
    ;;
  testflight)
    echo "=== Archiving for TestFlight ==="
    xcodebuild archive \
      -project "$BUILD_DIR/FateEngine.xcodeproj" \
      -scheme FateEngine \
      -configuration Release \
      -archivePath "$BUILD_DIR/FateMMO.xcarchive"

    xcodebuild -exportArchive \
      -archivePath "$BUILD_DIR/FateMMO.xcarchive" \
      -exportOptionsPlist "$PROJECT_ROOT/ios/ExportOptions.plist" \
      -exportPath "$BUILD_DIR/export"

    echo "=== Uploading to TestFlight ==="
    xcrun altool --upload-app \
      -f "$BUILD_DIR/export/FateEngine.ipa" \
      -t ios \
      --apiKey "${APP_STORE_API_KEY}" \
      --apiIssuer "${APP_STORE_ISSUER}"
    ;;
  build)
    echo "=== Build complete ==="
    ;;
  *)
    echo "Unknown action: $ACTION (use: build, device, testflight)"
    exit 1
    ;;
esac

echo "=== Done ==="
