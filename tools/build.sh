#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
desktop=macos
[[ $(uname -s) != Linux ]] || desktop=linux
platform=${1:-$desktop}
mode=${2:-debug}
case "$mode" in
    debug) target=template_debug ;;
    release) target=template_release ;;
    editor) target=editor ;;
    *) echo "Usage: $0 [macos|linux|ios|android|all] [debug|release|editor]" >&2; exit 2 ;;
esac
case "$platform" in
    all)
        platforms=("$desktop" android)
        [[ $desktop != macos ]] || platforms=(macos ios android)
        for item in "${platforms[@]}"; do "$0" "$item" "$mode"; done
        exit ;;
    macos|linux|ios|android) ;;
    *) echo "Unknown platform: $platform" >&2; exit 2 ;;
esac
if [[ $platform == ios && $desktop != macos ]]; then
    echo 'iOS builds require macOS and Xcode' >&2
    exit 2
fi
if [[ "$mode" == editor && "$platform" != macos && "$platform" != linux ]]; then
    echo 'The editor target is desktop only' >&2
    exit 2
fi
arch=arm64
[[ $platform != linux ]] || arch=x86_64
arch=${ARCH:-$arch}
if [[ ( $platform == ios || $platform == android ) && "$arch" != arm64 ]]; then
    echo "Mobile packaging requires ARCH=arm64" >&2
    exit 2
fi
scons platform="$platform" target="$target" arch="$arch" api_version=4.7 \
    -j"${JOBS:-8}"
case "$platform" in
    macos|linux)
        extension=dylib
        [[ $platform != linux ]] || extension=so
        if [[ "$mode" == editor ]]; then
            mkdir -p "harness/addons/pocketar/bin/$platform/template_debug/$arch"
            cp "harness/addons/pocketar/bin/$platform/editor/$arch/libpocketar.$extension" \
                "harness/addons/pocketar/bin/$platform/template_debug/$arch/libpocketar.$extension"
        fi ;;
    ios) tools/xcframework.sh "$target" ;;
    android)
        variant=Debug
        [[ "$mode" != release ]] || variant=Release
        gradle -p android ":plugin:assemble$variant" ;;
esac
