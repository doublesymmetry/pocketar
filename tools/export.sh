#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
platform=${1:-android}
mode=${2:-debug}
case "$platform" in
    ios) preset=iOS; suffix=zip ;;
    android) preset=Android; suffix=apk ;;
    *) echo "Usage: $0 [ios|android] [debug|release]" >&2; exit 2 ;;
esac
case "$mode" in debug|release) ;; *) exit 2 ;; esac
mkdir -p "build/exports/$platform"
extra=()
if [[ "$platform" == android ]]; then
    extra+=(--install-android-build-template)
fi
godot --headless --path demo --editor --import
exec godot --headless --path demo "${extra[@]}" "--export-$mode" "$preset" \
    "../build/exports/$platform/handheld_ar-$mode.$suffix"
