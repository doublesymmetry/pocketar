#!/usr/bin/env bash
# CI's Linux and Windows jobs: build the desktop library and run the headless suites on stock Godot.
set -euo pipefail
cd "$(dirname "$0")/.."
platform=$1 arch=$2 build=$3
for target in template_debug template_release; do
    scons platform="$platform" arch="$arch" target="$target" api_version=4.7 -j4
done
mkdir -p build/ci/godot build/ci/bin
curl -fsSL -o build/ci/godot.zip \
    "https://github.com/godotengine/godot-builds/releases/download/4.7.2-stable/Godot_v4.7.2-stable_$build.zip"
unzip -q -o build/ci/godot.zip -d build/ci/godot
if [[ $platform == windows ]]; then
    cp build/ci/godot/*_console.exe build/ci/bin/godot.exe
else
    cp build/ci/godot/Godot_v4.7.2-stable_"$build" build/ci/bin/godot
    chmod +x build/ci/bin/godot
fi
export PATH="$PWD/build/ci/bin:$PATH"
godot --headless --path harness --editor --import --quit-after 180 > build/ci/import.log 2>&1
if rg 'SCRIPT ERROR:|ERROR:' build/ci/import.log; then exit 1; fi
tools/test-headless.sh
