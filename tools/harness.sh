#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
platform=${1:-desktop}
[[ $# == 0 ]] || shift
case "$platform" in
    desktop|macos)
        exec python3 tools/run-harness.py -- godot --path demo --rendering-method mobile \
            --windowed --resolution 960x720 -- --harness "$@" ;;
    android|android-ar)
        : "${ADB:?Set ADB to the adb binary matching the machine server; do not use devenv adb}"
        : "${ANDROID_SERIAL:?Set ANDROID_SERIAL after checking that the shared phone is free}"
        apk=${APK:-build/exports/android/handheld_ar-debug.apk}
        "$ADB" -s "$ANDROID_SERIAL" install -r "$apk"
        exec python3 tools/run-harness.py "--$platform" "$ADB" "$ANDROID_SERIAL" -- "$@" ;;
    ios)
        : "${IOS_DEVICE:?Set IOS_DEVICE to the connected device identifier}"
        exec python3 tools/run-harness.py -- xcrun devicectl device process launch \
            --device "$IOS_DEVICE" --console org.doublesymmetry.handheldar.demo \
            -harness "$@" ;;
    *) echo "Usage: $0 [desktop|android|android-ar|ios] [-harness-out dir] [-harness-steps names] [-harness-duration seconds] [-harness-probes] [-harness-camera-config index] [-harness-record name.mp4|-harness-playback name.mp4]" >&2; exit 2 ;;
esac
