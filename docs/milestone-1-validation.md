# Milestone 1 validation

Checked on ci4 on 2026-10-10, inside the supplied environment. No commits were
made. Rendering and device qualification remain pending.

| Check | Result |
| --- | --- |
| `scons platform=macos target=template_debug arch=arm64 api_version=4.7 -j8` | Passed, including the final source changes |
| `scons platform=macos target=editor arch=arm64 api_version=4.7 -j8` | Passed |
| `scons platform=ios target=template_debug arch=arm64 api_version=4.7 -j8` | Passed; static archive contains the extension entry symbol and godot-cpp |
| `TMPDIR=/tmp tools/xcframework.sh template_debug` | Passed; device-only `ios-arm64` XCFramework |
| `scons platform=android target=template_debug arch=arm64 api_version=4.7 -j8` | Passed; all ELF LOAD segments have `0x4000` alignment; only libc, libm and libdl dependencies |
| `TMPDIR=/tmp tools/test-native.sh` | Passed: 73,260 checks, worst normalized error `6.37530945e-07` |
| Headless editor import | Extension loaded and global scripts registered |
| Headless `--check-only` for `demo/harness/harness.gd` | Passed, no script parse errors |
| Headless packet smoke test | `PACKET RESULT PASS` |
| Headless main scene | Rejected with exactly one `HARNESS RESULT FAIL`, as intended |
| Host monitor failure handling | Accepted one PASS; rejected FAIL, missing result, duplicate result, script error and timeout |
| Shell/Python/XML syntax and `git diff --check` | Passed |

Run the packet check from the repository root after building/importing the demo:

```sh
godot --headless --path demo --script "$PWD/tests/headless/packet_smoke.gd" \
    --log-file /tmp/handheld-ar-packet.log
```

It checks singleton access, projection through the XR virtual, packet retention
across CPU-ring reuse, delayed pose acquisition, explicit presentation, snapshot
ray identity and session-generation changes. It does not inspect GPU pixels.

Sandbox limitations appeared as denied writes to Godot's global user-data,
settings and cache directories, denied system certificate access, and unavailable
CoreSimulator services during XCFramework packaging. The XCFramework command
still completed successfully. Native test linking warned that the godot-cpp
archive's macOS deployment version was newer than the test executable's default;
the executable ran successfully on this host. These builds do not establish an
older macOS support floor.

## Pending verification

Run `tools/harness.sh desktop -harness-out /tmp/handheld-ar-captures` in the GUI
session. Start with `-harness-steps loaded,xr-mono-2d,projection,transparency` if
isolating renderer problems. The strict colour and 120-frame consistency checks
must also pass before accepting the architecture. Inspect the saved scene-alpha
captures if the centroid or compositing checks fail.

HDR viewport texture sampling, sRGB conversion after unpremultiplication, MSAA
coverage and exact one-level YCbCr colour accuracy are source-derived decisions,
not measured GPU results. Nonlinear tonemapping of translucent content and
alpha-zero glow have documented limitations in `AGENTS.md`.

Gradle/AAR assembly, debug/release exports, signed Xcode device builds and release
archives, and loading/running on the iPhone and Pixel were not attempted. Release
native targets were not compiled. Check the AAR's manifest, extension asset and
`jni/arm64-v8a/libhandheld_ar.so`, then the exported APK's native alignment and
plugin discovery. Check static entry registration/linkage in the iOS export.
The ELF check above covers this extension only, not all libraries in an APK.

The iOS export preset produces an Xcode project ZIP. Both phone builds launch the
same harness automatically. Device orientation transitions and capture readback
cost can differ from desktop, so retain per-device logs and PNGs.
