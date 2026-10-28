#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
arch=${ARCH:-arm64}
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/pocketar-arcore.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT
mkdir -p "$work_dir/android" "$work_dir/EGL"
cat > "$work_dir/android/log.h" <<'HEADER'
#pragma once
#define ANDROID_LOG_INFO 4
inline int __android_log_print(int, const char *, const char *, ...) { return 0; }
HEADER
cat > "$work_dir/EGL/egl.h" <<'HEADER'
#pragma once
#define EGL_NO_CONTEXT nullptr
inline void *eglGetCurrentContext() { return nullptr; }
HEADER
cat > "$work_dir/android/hardware_buffer.h" <<'HEADER'
#pragma once
#include <cstdint>
struct AHardwareBuffer;
struct AHardwareBuffer_Desc { uint32_t width, height, layers, format; uint64_t usage; uint32_t stride, rfu0; uint64_t rfu1; };
enum { AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM = 1, AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM = 2, AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM = 3,
    AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM = 4, AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT = 0x16, AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420 = 0x23,
    AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM = 0x2b, AHARDWAREBUFFER_FORMAT_R10G10B10A10_UNORM = 0x3b };
inline void AHardwareBuffer_describe(const AHardwareBuffer *p_buffer, AHardwareBuffer_Desc *r_desc) { *r_desc = *reinterpret_cast<const AHardwareBuffer_Desc *>(p_buffer); }
HEADER
fixture() {
    local name=$1 entry=$2 result_prefix=$3
    shift 3
    local fixture_dir="$work_dir/$name"
    mkdir -p "$fixture_dir/.godot"
    xcrun clang++ -std=c++17 -shared -fPIC -arch "$arch" -Isrc -Ithirdparty/arcore/include \
        -Igodot-cpp/include -Igodot-cpp/gen/include -Igodot-cpp/gdextension \
        "tests/headless/arcore_${name}_fixture.cpp" "$@" src/platform/android/arcore_extras.cpp \
        src/platform/android/arcore_api.cpp src/core/ar_frame_packet.cpp src/core/ar_raycast_hit.cpp \
        src/core/ar_plane_geometry.cpp src/core/projection.cpp \
        src/core/ar_anchor_tracker.cpp src/core/ar_anchor_registry.cpp \
        "godot-cpp/bin/libgodot-cpp.macos.template_debug.$arch.a" -o "$fixture_dir/libfixture.dylib"
    cat > "$fixture_dir/project.godot" <<PROJECT
config_version=5
[application]
config/name="ARCore $name tests"
PROJECT
    cat > "$fixture_dir/fixture.gdextension" <<EXTENSION
[configuration]
entry_symbol = "$entry"
compatibility_minimum = "4.7"
[libraries]
macos.debug.$arch = "res://libfixture.dylib"
EXTENSION
    printf '%s\n' 'res://fixture.gdextension' > "$fixture_dir/.godot/extension_list.cfg"
    cp "tests/headless/arcore_${name}_smoke.gd" "$fixture_dir/smoke.gd"
    # Scribbled freed memory makes a use after free crash instead of reading stale values.
    MallocScribble=1 godot --headless --path "$fixture_dir" --script res://smoke.gd --log-file "$fixture_dir/result.log"
    rg -q "^$result_prefix RESULT PASS " "$fixture_dir/result.log"
    if rg -q "SCRIPT ERROR:|$result_prefix FAIL|$result_prefix RESULT FAIL" "$fixture_dir/result.log"; then
        exit 1
    fi
}
fixture session arcore_session_fixture_init SESSION \
    -DDEBUG_ENABLED -fno-access-control -DCLOCK_BOOTTIME=CLOCK_MONOTONIC \
    -I"$work_dir" -I"$JAVA_HOME/include" -I"$JAVA_HOME/include/darwin" \
    src/platform/android/arcore_interface.cpp src/platform/android/arcore_frame.cpp src/platform/android/arcore_gpu.cpp src/platform/android/arcore_anchors.cpp \
    src/core/ar_plane_tracker.cpp src/core/debug_plane.cpp
fixture session arcore_session_fixture_init SESSION \
    -fno-access-control -DCLOCK_BOOTTIME=CLOCK_MONOTONIC \
    -I"$work_dir" -I"$JAVA_HOME/include" -I"$JAVA_HOME/include/darwin" \
    src/platform/android/arcore_interface.cpp src/platform/android/arcore_frame.cpp src/platform/android/arcore_gpu.cpp src/platform/android/arcore_anchors.cpp \
    src/core/ar_plane_tracker.cpp
fixture extras arcore_extras_fixture_init EXTRAS
