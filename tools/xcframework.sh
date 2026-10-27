#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
target=${1:-template_debug}
case "$target" in template_debug|template_release) ;; *) exit 2 ;; esac
output="$PWD/demo/addons/pocketar/bin/ios/$target/pocketar.xcframework"
staging=$(mktemp -d "${TMPDIR:-/tmp}/pocketar-xcframework.XXXXXX")
trap 'rm -rf "$staging"' EXIT
xcodebuild -create-xcframework \
    -library "$PWD/demo/addons/pocketar/bin/ios/$target/arm64/libpocketar.a" \
    -output "$staging/pocketar.xcframework"
rm -rf "$output"
mv "$staging/pocketar.xcframework" "$output"
