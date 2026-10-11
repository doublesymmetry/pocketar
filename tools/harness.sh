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
        apk=${APK:-build/exports/android/pocketar-debug.apk}
        "$ADB" -s "$ANDROID_SERIAL" install -r "$apk"
        exec python3 tools/run-harness.py "--$platform" "$ADB" "$ANDROID_SERIAL" -- "$@" ;;
    ios-ar)
        exec python3 tools/ios-device.py harness -- "$@" ;;
    ios)
        : "${IOS_DEVICE:?Set IOS_DEVICE to the connected device identifier}"
        exec python3 tools/run-harness.py --ios "$IOS_DEVICE" -- "$@" ;;
    *) echo "Usage: $0 [desktop|android|android-ar|ios|ios-ar] [-harness-out dir] [-harness-steps names] [-harness-duration seconds] [-harness-camera-config index] [-harness-camera-path auto|cpu] [-harness-gpu-validate] [-harness-record name.mp4|-harness-playback name.mp4]" >&2; exit 2 ;;
esac
