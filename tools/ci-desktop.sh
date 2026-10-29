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
python -m zipfile -e build/ci/godot.zip build/ci/godot
# The Windows console launcher finds the editor by its original name, so neither is renamed.
executable=$PWD/build/ci/godot/Godot_v4.7.2-stable_$build
[[ $platform != windows ]] || executable=${executable%.exe}_console.exe
chmod +x "$executable"
printf '#!/usr/bin/env bash\nexec "%s" "$@"\n' "$executable" > build/ci/bin/godot
chmod +x build/ci/bin/godot
export PATH="$PWD/build/ci/bin:$PATH"
godot --headless --path harness --editor --import --quit-after 180 > build/ci/import.log 2>&1
if rg 'SCRIPT ERROR:|ERROR:' build/ci/import.log; then exit 1; fi
tools/test-headless.sh
