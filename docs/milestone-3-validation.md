# Milestone 3 validation and Pixel handoff

Implemented on `godot-handheld-ar-arcore`, stacked on milestone 1. No commits,
Gradle invocation, exports or device operations were performed. Commands ran
directly in the supplied devenv shell.

## Changed code

- `src/platform/android/`: the stub is replaced by the session owner, dynamic C
  API loader, JNI bridge, packet acquisition, stride-safe YUV repack, Q-based
  texture reuse, plane ownership, asynchronous destroy and diagnostics. Extras
  provide live queries, local anchors, optional point clouds, dataset tracks and
  bounded device probes.
- `src/core/`: copied plane/point-cloud/calibration metadata, typed plane trackers,
  polygon raycasts and hit resources. Session generations and native query/anchor
  handles remain unique across interface recreation. `src/register_types.cpp`
  registers the shared types and Android backend; `SConstruct` builds it.
- `android/plugin/`: availability/install/permission, lifecycle and display JNI
  callbacks, SDK 1.56.0 dependency/extraction, device telemetry and validated debug
  harness arguments. The Kotlin singleton is `HandheldARPlugin`, avoiding a name
  collision with the native `HandheldAR` singleton.
- `demo/addons/handheld_ar/`: ARView accepts either backend, clears presentation
  across reset, and raycasts its presented packet. The export plugin supplies
  AR Required/Optional metadata and the Maven dependency. `demo/ar/` contains the
  demo and device harness; the original synthetic harness remains available.
- `tools/`: Android AR launch/monitor support, pinned header updater and native
  verification scripts. `tests/native`, `tests/headless` and `tests/tools` cover
  geometry, repack/lifetime rules, packet ownership, extras, composite sampling,
  dataset assertions and host monitoring. Header provenance is under
  `thirdparty/arcore/`. `AGENTS.md` documents the API and operational rules.

## Offline evidence

Both commands completed successfully with NDK 29.0.14206865:

```bash
scons platform=android target=template_debug arch=arm64 api_version=4.7 ndk_version=29.0.14206865 -j8
scons platform=android target=template_release arch=arm64 api_version=4.7 ndk_version=29.0.14206865 -j8
```

The release ELF is AArch64; all LOAD segments have 0x4000 alignment. The extension
entry point and six Kotlin JNI entry points are exported. There are no unresolved
ARCore C symbols because the backend resolves the packaged shim with `dlopen`.
This inspection does not certify the final APK or Google's shim alignment.

The macOS arm64 debug extension also compiled. `TMPDIR=/tmp tools/test-native.sh`
passed 73,260 projection/ray checks, 40 plane checks, and YUV tests covering NV12,
NV21, generic strides, short final chroma rows and Q=2–4 retirement boundaries.
The round-2 YUV cases include the Pixel's exact 1920×1080 layout, odd dimensions,
truncated planes and identical output across all three layouts. They also pass
with AddressSanitizer and UndefinedBehaviorSanitizer.

Godot headless checks passed for packet lifetime/metadata, ARView backend
selection, mapped composite samples, dataset replay assertions and camera
timestamp/rate acceptance. Camera summaries in the fixture stay within 372 bytes
and retain the runner's step syntax. The plain `-ar-demo` launch routes without
attaching the harness in a headless desktop check. The separate
ARCore extras fixture passed 31 checks using real Godot resources and a mock C API.
It covers stale generations/handles, plane rebasing/subsumption, repeated frames,
query expiry, recording input retries/file boundaries and native reference release.
The dataset assertion fixture deliberately emits a failing sample before its own
`DATASET REPLAY RESULT PASS`.

`python3 -B -m unittest discover -s tests/tools` passed 18 host harness checks,
including escaped argument round trips, app exit with idle or busy logcat, and
draining a final result after the app exits.
The demo/harness GDScript parser checks, shell syntax and `git diff --check` pass.
Headless runs report the known sandbox system-certificate error; native fixture
linking warns about the installed macOS SDK deployment target. These runs cannot
verify rendering. Kotlin and Godot v2 signatures were checked against local
sources, but Kotlin was not compiled and manifest merging was not run.

The header now matches the SDK `v1.56.0` GitHub tag (SHA-256
`aa21558d0ffe73e99d06cbf67a399841e3d9b5fdfec286fc91dcb948c3e3cd3d`).
Android debug and release native builds pass against it. Pixel verification of
the Kotlin launch fixes and the revised upload timings remains with Claude;
the 4 ms p95 budget has not been verified for this revision.

## Differences from the brief

1. Safe rendering thread mode remains mandatory. Separate-mode packet delivery
   from the brief is deferred with milestone 1's render-thread activation work.
2. Resume is synchronous on the owner and timed. The conditional worker path for
   resume calls exceeding roughly 100ms is deferred pending Pixel measurements;
   it needs session locking and lifecycle/reset coordination. Destroy already
   runs on a detached JVM-attached worker, with no shutdown join.
3. Restarting dataset playback creates a fresh session/generation instead of
   pausing and reusing the session with the same URI. This keeps stale plane,
   query, anchor and packet rejection consistent with reset.

The permitted `dlopen` option avoids a SCons shim dependency. Gradle extracts the
shim for inspection and the exported Maven dependency packages it once. CPU
R8/RG8 uploads and a Q+3 ring follow the task's explicit override of the imported
texture/conversion path. Capacity, repeated-frame and old-frame probes are opt-in
because they disturb normal throughput measurements.

## Claude's first device checks

First update the header, build both AAR variants and export a debug APK. Verify one
arm64 `libarcore_sdk_c.so`, the `HandheldARPlugin` singleton, AR Optional metadata,
CAMERA permission, and SDK/service version logs. Confirm AR Required in a separate
export if that option will be used. The installed Google Play Services for AR must
be at least 1.48.

After checking that the shared Pixel is free and unlocking it:

```bash
ADB=<host-binary> ANDROID_SERIAL=18231FDF6004QL tools/harness.sh android-ar -harness-duration 60
ADB=<host-binary> ANDROID_SERIAL=18231FDF6004QL tools/harness.sh android-ar -harness-duration 1800
```

The monitor requires all eight named steps and exactly one result. Check these
risks first:

1. No EGL context on the actual Vulkan owner thread, no MISSING_GL_CONTEXT or
   TEXTURE_NOT_SET, and CPU images available at the selected rate with at most 5%
   missing new frames. Compare frame/image timestamps and acquire p50/p99.
2. Visible full-range BT.601 colour and stable camera/virtual alignment during
   motion. Check Q, ring drops, retained-packet pixels, and no stale texture after
   reset. A dark desk gives composite INFO; use a lit textured scene for inspection.
3. Real home/relaunch twice, permission denial/regrant, then resets while watching
   pause queue latency, resume latency, memory overlap and generation cleanup.
   The automatic lifecycle step exercises owner callbacks; it cannot prove Android
   dispatch ordering. A resume over 100ms triggers the deferred worker requirement.
4. All four rotations: projection form, third near/far oracle within 1e-5, affine
   UVs, CPU/GPU intrinsics and world-point reprojection within 0.5px. Inspect plane
   meshes and centre/tap raycasts on a textured surface; desk tracking is INFO.

Use `-harness-camera-config <index>` for separate VGA/high-resolution 30-minute
comparisons. JSON telemetry includes thermal status, PSS and available battery
counters, with charging state. This is diagnostic sampling, not calibrated power
measurement. Reset snapshots are taken immediately after replacement is started.

Use `-harness-probes` on a separate run for held-image capacity, repeated-frame
behaviour, point-cloud reprojection, plane identity observations and old-frame
hitTest/hitTestRay. The no-held-reference identity probe matches centre/type on
the same frame and reports ambiguity; ARCore exposes no independent logical-plane
identifier that could prove identity. Production always holds one plane reference.

Use `-harness-record pixel-room.mp4`, then `-harness-playback pixel-room.mp4` for
the custom-track test. Recording under hardware-buffer mode, playback config/FOV,
hardware-buffer CPU readability, playback completion/restart and second-model
compatibility all remain unverified. Datasets contain room video; keep them in
private device storage. The harness compares recorded positive hit distances with
max(0.15m, 25%) tolerance and reports actual playback duration.
