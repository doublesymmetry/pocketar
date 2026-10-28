#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
arch=${ARCH:-arm64}
scons platform=macos target=template_debug arch="$arch" api_version=4.7 -j"${JOBS:-8}"
mkdir -p build/tests
cpp_flags=(-Igodot-cpp/include -Igodot-cpp/gen/include -Igodot-cpp/gdextension)
cpp_archive="godot-cpp/bin/libgodot-cpp.macos.template_debug.$arch.a"
run() {
    local name=$1
    shift
    xcrun clang++ -std=c++17 -arch "$arch" -Isrc "$@" -o "build/tests/$name"
    "build/tests/$name"
}
run test_debug_plane -DDEBUG_ENABLED "${cpp_flags[@]}" tests/native/test_debug_plane.cpp src/core/debug_plane.cpp src/core/ar_plane_tracker.cpp "$cpp_archive"
run test_anchor_values "${cpp_flags[@]}" tests/native/test_anchor_values.cpp src/core/ar_anchor_registry.cpp src/core/ar_anchor_tracker.cpp "$cpp_archive"
run test_projection "${cpp_flags[@]}" tests/native/test_projection.cpp src/core/projection.cpp "$cpp_archive"
run test_plane_raycast "${cpp_flags[@]}" tests/native/test_plane_raycast.cpp src/core/ar_plane_geometry.cpp "$cpp_archive"
run test_arkit_math "${cpp_flags[@]}" tests/native/test_arkit_math.cpp src/core/projection.cpp "$cpp_archive"
run test_arkit_anchors tests/native/test_arkit_anchors.cpp
run test_camera_quality tests/native/test_camera_quality.cpp
run test_import_retirement tests/native/test_import_retirement.cpp
run test_timing_samples tests/native/test_timing_samples.cpp
run test_yuv tests/native/test_yuv.cpp
run test_gpu_camera tests/native/test_gpu_camera.cpp
run test_scripted_session tests/native/test_scripted_session.cpp src/core/scripted_session.cpp
build/tests/test_scripted_session harness/suites/fixtures/table.jsonl
build/tests/test_scripted_session harness/suites/fixtures/table-portrait.jsonl
python3 -m unittest discover -s tests/tools -p test_fixtures.py
