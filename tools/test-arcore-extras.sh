#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
arch=${ARCH:-arm64}
fixture_dir=$(mktemp -d "${TMPDIR:-/tmp}/handheld-ar-extras.XXXXXX")
trap 'rm -rf "$fixture_dir"' EXIT
xcrun clang++ -std=c++17 -shared -fPIC -arch "$arch" -Isrc -Ithirdparty/arcore/include \
    -Igodot-cpp/include -Igodot-cpp/gen/include -Igodot-cpp/gdextension \
    tests/headless/arcore_extras_fixture.cpp src/platform/android/arcore_extras.cpp \
    src/platform/android/arcore_api.cpp src/core/ar_frame_packet.cpp src/core/ar_raycast_hit.cpp \
    src/core/ar_plane_geometry.cpp src/core/projection.cpp \
    "godot-cpp/bin/libgodot-cpp.macos.template_debug.$arch.a" -o "$fixture_dir/libextras_fixture.dylib"
cat > "$fixture_dir/project.godot" <<'EOF'
config_version=5
[application]
config/name="ARCore extras tests"
EOF
cat > "$fixture_dir/extras.gdextension" <<EOF
[configuration]
entry_symbol = "arcore_extras_fixture_init"
compatibility_minimum = "4.7"
[libraries]
macos.debug.$arch = "res://libextras_fixture.dylib"
EOF
mkdir -p "$fixture_dir/.godot"
printf '%s\n' 'res://extras.gdextension' > "$fixture_dir/.godot/extension_list.cfg"
cp tests/headless/arcore_extras_smoke.gd "$fixture_dir/smoke.gd"
godot --headless --path "$fixture_dir" --script res://smoke.gd --log-file "$fixture_dir/result.log"
rg -q '^EXTRAS RESULT PASS ' "$fixture_dir/result.log"
if rg -q 'SCRIPT ERROR:|EXTRAS FAIL|EXTRAS RESULT FAIL' "$fixture_dir/result.log"; then
    exit 1
fi
