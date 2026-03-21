#!/bin/bash
# android/build.sh — Build and install FateMMO on Android
set -e

ACTION=${1:-installDebug}
cd "$(dirname "$0")"

if [ ! -f "gradlew" ]; then
    echo "Error: Gradle wrapper not found. Run 'gradle wrapper' first."
    echo "Or download from: https://services.gradle.org/distributions/gradle-8.11-bin.zip"
    exit 1
fi

echo "=== FateMMO Android build: $ACTION ==="
./gradlew "$ACTION"
echo "=== Done ==="
