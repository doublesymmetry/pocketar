#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
target=${1:-template_debug}
case "$target" in template_debug|template_release) ;; *) exit 2 ;; esac
output="$PWD/demo/addons/handheld_ar/bin/ios/$target/handheld_ar.xcframework"
staging=$(mktemp -d "${TMPDIR:-/tmp}/handheld-ar-xcframework.XXXXXX")
trap 'rm -rf "$staging"' EXIT
xcodebuild -create-xcframework \
    -library "$PWD/demo/addons/handheld_ar/bin/ios/$target/arm64/libhandheld_ar.a" \
    -output "$staging/handheld_ar.xcframework"
rm -rf "$output"
mv "$staging/handheld_ar.xcframework" "$output"
