#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
platform=${1:-macos}
mode=${2:-debug}
case "$mode" in
    debug) target=template_debug ;;
    release) target=template_release ;;
    editor) target=editor ;;
    *) echo "Usage: $0 [macos|ios|android|all] [debug|release|editor]" >&2; exit 2 ;;
esac
case "$platform" in
    all)
        for item in macos ios android; do "$0" "$item" "$mode"; done
        exit ;;
    macos|ios|android) ;;
    *) echo "Unknown platform: $platform" >&2; exit 2 ;;
esac
if [[ "$mode" == editor && "$platform" != macos ]]; then
    echo 'The editor target is macOS only' >&2
    exit 2
fi
arch=${ARCH:-arm64}
if [[ "$platform" != macos && "$arch" != arm64 ]]; then
    echo "Mobile packaging requires ARCH=arm64" >&2
    exit 2
fi
scons platform="$platform" target="$target" arch="$arch" api_version=4.7 \
    -j"${JOBS:-8}"
case "$platform" in
    macos)
        if [[ "$mode" == editor ]]; then
            mkdir -p "harness/addons/pocketar/bin/macos/template_debug/$arch"
            cp "harness/addons/pocketar/bin/macos/editor/$arch/libpocketar.dylib" \
                "harness/addons/pocketar/bin/macos/template_debug/$arch/libpocketar.dylib"
        fi ;;
    ios) tools/xcframework.sh "$target" ;;
    android)
        variant=Debug
        [[ "$mode" != release ]] || variant=Release
        gradle -p android ":plugin:assemble$variant" ;;
esac
