#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
arch=${ARCH:-arm64}
scons platform=macos target=template_debug arch="$arch" api_version=4.7 -j"${JOBS:-8}"
mkdir -p build/tests
xcrun clang++ -std=c++17 -arch "$arch" -Isrc -Igodot-cpp/include -Igodot-cpp/gen/include \
    -Igodot-cpp/gdextension tests/native/test_projection.cpp src/core/projection.cpp \
    "godot-cpp/bin/libgodot-cpp.macos.template_debug.$arch.a" -o build/tests/test_projection
build/tests/test_projection
xcrun clang++ -std=c++17 -arch "$arch" -Isrc -Igodot-cpp/include -Igodot-cpp/gen/include \
    -Igodot-cpp/gdextension tests/native/test_plane_raycast.cpp src/core/ar_plane_geometry.cpp \
    "godot-cpp/bin/libgodot-cpp.macos.template_debug.$arch.a" -o build/tests/test_plane_raycast
build/tests/test_plane_raycast
xcrun clang++ -std=c++17 -arch "$arch" -Isrc -Igodot-cpp/include -Igodot-cpp/gen/include \
    -Igodot-cpp/gdextension tests/native/test_arkit_math.cpp src/core/projection.cpp \
    "godot-cpp/bin/libgodot-cpp.macos.template_debug.$arch.a" -o build/tests/test_arkit_math
build/tests/test_arkit_math
xcrun clang++ -std=c++17 -arch "$arch" -Isrc tests/native/test_import_retirement.cpp -o build/tests/test_import_retirement
build/tests/test_import_retirement
xcrun clang++ -std=c++17 -arch "$arch" -Isrc tests/native/test_yuv.cpp -o build/tests/test_yuv
exec build/tests/test_yuv
