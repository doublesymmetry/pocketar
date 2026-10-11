#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
arch=${ARCH:-arm64}
fixture_dir=$(mktemp -d "${TMPDIR:-/tmp}/pocketar-session.XXXXXX")
trap 'rm -rf "$fixture_dir"' EXIT
mkdir -p "$fixture_dir/android" "$fixture_dir/EGL"
cat > "$fixture_dir/android/log.h" <<'EOF'
#pragma once
#define ANDROID_LOG_INFO 4
inline int __android_log_print(int, const char *, const char *, ...) { return 0; }
EOF
cat > "$fixture_dir/EGL/egl.h" <<'EOF'
#pragma once
#define EGL_NO_CONTEXT nullptr
inline void *eglGetCurrentContext() { return nullptr; }
EOF
cat > "$fixture_dir/android/hardware_buffer.h" <<'EOF'
#pragma once
#include <cstdint>
struct AHardwareBuffer;
struct AHardwareBuffer_Desc { uint32_t width, height, layers, format; uint64_t usage; uint32_t stride, rfu0; uint64_t rfu1; };
struct AHardwareBuffer_Planes { uint32_t planeCount; };
constexpr uint64_t AHARDWAREBUFFER_USAGE_CPU_READ_MASK = 15;
constexpr uint64_t AHARDWAREBUFFER_USAGE_CPU_READ_RARELY = 2;
inline void AHardwareBuffer_describe(AHardwareBuffer *, AHardwareBuffer_Desc *) {}
inline int AHardwareBuffer_lockPlanes(AHardwareBuffer *, uint64_t, int32_t, const void *, AHardwareBuffer_Planes *) { return -1; }
inline int AHardwareBuffer_unlock(AHardwareBuffer *, int32_t *) { return 0; }
EOF
xcrun clang++ -std=c++17 -shared -fPIC -fno-access-control -DCLOCK_BOOTTIME=CLOCK_MONOTONIC -arch "$arch" -Isrc -Ithirdparty/arcore/include \
    -I"$fixture_dir" -I"$JAVA_HOME/include" -I"$JAVA_HOME/include/darwin" \
    -Igodot-cpp/include -Igodot-cpp/gen/include -Igodot-cpp/gdextension \
    tests/headless/arcore_session_fixture.cpp src/platform/android/arcore_interface.cpp src/platform/android/arcore_frame.cpp \
    src/platform/android/arcore_extras.cpp src/platform/android/arcore_api.cpp \
    src/core/ar_frame_packet.cpp src/core/ar_plane_tracker.cpp src/core/ar_raycast_hit.cpp \
    src/core/ar_plane_geometry.cpp src/core/projection.cpp \
    "godot-cpp/bin/libgodot-cpp.macos.template_debug.$arch.a" -o "$fixture_dir/libsession_fixture.dylib"
cat > "$fixture_dir/project.godot" <<'EOF'
config_version=5
[application]
config/name="ARCore session tests"
EOF
cat > "$fixture_dir/session.gdextension" <<EOF
[configuration]
entry_symbol = "arcore_session_fixture_init"
compatibility_minimum = "4.7"
[libraries]
macos.debug.$arch = "res://libsession_fixture.dylib"
EOF
mkdir -p "$fixture_dir/.godot"
printf '%s\n' 'res://session.gdextension' > "$fixture_dir/.godot/extension_list.cfg"
cp tests/headless/arcore_session_smoke.gd "$fixture_dir/smoke.gd"
godot --headless --path "$fixture_dir" --script res://smoke.gd --log-file "$fixture_dir/result.log"
rg -q '^SESSION RESULT PASS ' "$fixture_dir/result.log"
if rg -q 'SCRIPT ERROR:|SESSION FAIL|SESSION RESULT FAIL' "$fixture_dir/result.log"; then
    exit 1
fi
