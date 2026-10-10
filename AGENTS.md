# godot-handheld-ar

MIT handheld AR library for Godot 4.7.2. One GDExtension presents ARKit on iOS and
ARCore on Android through a shared Godot API. The synthetic backend supplies the
desktop rendering harness. Milestone 3 adds the ARCore CPU-image backend and Pixel
diagnostics; the iOS provider remains a compile-only unsupported stub.

## Requirements

Work in this task worktree, not `~/hub/repos/`. Do not commit unless asked.
Use the repository's devenv for Godot, SCons, Python, Java 17 and the Android SDK.
When already inside its shell, run commands directly; nested devenv calls fail in
the coding sandbox. Xcode supplies the Apple compiler. Keep `apple.sdk = null`
and `stdenv = pkgs.stdenvNoCC`; do not add another C toolchain.

The supported targets are macOS arm64 (desktop harness), iOS 16+ arm64 on Metal,
and Android API 29+ arm64 on Mobile/Vulkan. Godot and export templates are pinned
to 4.7.2; godot-cpp is pinned to `b051e69` with `api_version=4.7`. Android uses
compile/target SDK 36, build tools 36.1.0, NDK 29.0.14206865, Kotlin 2.1.21 and Java
17. `SConstruct` derives `IOS_TOOLCHAIN_PATH` from `xcode-select -p`, so Xcode need
not be named `Xcode.app`. iOS signing team: `L7G7TF62MC`. Both demo package IDs:
`org.doublesymmetry.handheldar.demo`.

Use `$ADB`, never a bare `adb` command. The devenv client version differs from the
host's running server. Set `ADB` to the matching host binary and set
`ANDROID_SERIAL` after checking that nobody is using the shared Pixel. The phone
must be unlocked before a harness run. Do not reset the adb server or change the
phone lock. The hub instructions describe unlocking without printing the PIN.

## Commands

Run these from the repo root inside the environment. `build`, `export` and
`harness` are devenv script names; the `tools/` forms also work directly. Invoke the `export` script through `env` to
avoid the shell builtin with the same name.

| Task | Command |
| --- | --- |
| Desktop debug library | `build macos debug` or `scons platform=macos target=template_debug arch=arm64 api_version=4.7 -j8` |
| Desktop editor library | `build macos editor` (also stages it at the descriptor's editor/debug path) |
| Release library | `build macos release` |
| iOS archive and device XCFramework | `build ios debug` or `build ios release` |
| Android library and AAR | `build android debug` or `build android release` |
| All three platforms | `build all debug` |
| iOS Xcode project ZIP | `env export ios debug` or `env export ios release` |
| Android APK | `env export android debug` or `env export android release` |
| Desktop rendering harness | `harness desktop -harness-out /tmp/handheld-ar-captures` |
| Selected rendering checks | `harness desktop -harness-steps projection,transparency` |
| Installed iPhone demo | `IOS_DEVICE=<id> harness ios` |
| Pixel demo installation and harness | `ADB=<host-binary> ANDROID_SERIAL=<serial> harness android` |
| Native projection/ray tests | `tools/test-native.sh` |

`JOBS` controls build concurrency, default 8. `ARCH` defaults to arm64; macOS can
also build x86_64. `build all` stops on the first failed target. Build both mobile
variants before both exports. Gradle's wrapper downloads its distribution and
dependencies on first use. Godot's matching export templates must be installed;
`tools/export.sh android` installs their Gradle build template into the demo automatically.
Release Android signing comes from the user's Godot export credentials.

The iOS preset exports a project ZIP for subsequent Xcode signing, device builds
and release archives. It does not claim to produce a signed IPA. The XCFramework
contains a device-only static archive, including godot-cpp objects. The synthetic
backend links no AR SDK. Android loads the ARCore C shim dynamically; the export
plugin supplies `com.google.ar:core:1.56.0`, including its Java code and native shim. Camera declarations live in the AAR
manifest and export presets; the editor plugin adds the chosen debug/release AAR.

On macOS the sandbox may deny the inherited temporary directory. For native
verification there, use `TMPDIR=/tmp tools/test-native.sh`. Headless import can
load the extension and parse scripts, but sandbox errors writing Godot's global
settings/cache or reading system certificates are separate from script errors.

## Layout

- `src/core/`: read-only frame packets, synthetic XR provider, singleton, projection and ray math.
- `src/platform/android/`: ARCore session owner, JNI bridge, CPU upload and live queries.
- `src/platform/ios/`: unsupported provider stub, compiled only on iOS.
- `demo/ar/`: runtime-selected AR demo and device diagnostics.
- `thirdparty/arcore/`: C API header and its provenance.
- `demo/addons/handheld_ar/`: ARView, shaders, extension descriptor and editor export plugin.
- `demo/addons/handheld_ar/bin/`: generated libraries, XCFrameworks and AARs; ignored by Git.
- `demo/harness/`: the runnable demo and all rendered-image assertions.
- `android/`: Godot v2 Kotlin plugin and AAR Gradle project.
- `tests/native/`: standalone C++ numerical tests; no Godot process or GPU needed.
- `tests/headless/`: packet lifetime, presentation and XR-binding smoke test.
- `docs/milestone-1-validation.md`: completed checks and remaining device/GPU work.
- `tools/`: build, package, export and harness entry points.

C++ follows godot-cpp conventions, using tabs and `p_` parameter names. GDScript
is typed. Comments explain a non-obvious constraint; no comment banners or
narration. Commit messages, if requested, are one lowercase `type: subject` line.

## Settled architecture

One ARView owns one monoscopic `XRInterfaceExtension`, named
`HandheldARSynthetic`, and makes it primary. It publishes `head` / `default` for
`XRCamera3D`. The transparent XR SubViewport contains `XROrigin3D`, `XRCamera3D`
and `SceneRoot`. The camera is a separate SDR canvas underneath its texture; a
CanvasLayer holds ordinary HUD Controls above both. Controls receive input first.
Unconsumed presses emit a ray through `ARView.screen_to_ray()`.

Packets contain copied calibration, pose, UV mapping, IDs, timestamps, validity
and owned textures. Setters affect the next acquisition. Below the render rate,
the entire packet repeats. Resize/calibration changes acquire a new complete
packet immediately on the next XR process. The synthetic clock is monotonic
`Time.get_ticks_usec()`; orientation is clockwise quarter turns relative to the
fixed camera image. Automatic synthetic orientation chooses 0 for landscape and
1 for portrait; explicitly set 0–3 to exercise reverse orientations. This is not
a substitute for native display-rotation sampling in milestone 2.

`display_transform` maps top-left-origin viewport UV into top-left-origin camera
image UV, including centred aspect-fill crop and rotation. Projection uses the
inverse of that mapping and off-centre image intrinsics. Camera coordinates are
right-handed, +X right, +Y up, -Z forward. Matrices are column-major with OpenGL
NDC depth [-1,+1]; the engine handles device depth/Y corrections. Rays invert the
full matrix and transform the near point through world-from-camera. They never
use camera half-extents.

The current packet is applied during `frame_pre_draw`, and becomes the presented
packet at `frame_post_draw`. Touch rays use the presented packet. This backend
requires Godot's Safe rendering thread mode (1); Unsafe and Separate are rejected
until ordered render-thread packet activation exists. XROrigin and XR world scale
remain identity/one in this milestone; camera poses and game geometry share that
world. One game environment and world are exposed through `ARView.environment`
and `ARView.world_3d`.

Three CPU Image slots are reused. Every published packet receives new
ImageTextures. No fixed ring of GPU textures is overwritten: retained packets
keep their own image resources, and Godot handles RID retirement behind fences.
This deliberately favours correctness over upload/allocation throughput. It is
not the native image-import lifetime solution for milestone 2.

## Engine evidence

Paths below are relative to `/Users/ci/hub/scratch/godot-ar/src/godot` (4.7.2).

| Decision | Source |
| --- | --- |
| One XR view keeps canvas rendering | `servers/rendering/renderer_viewport.cpp:349` |
| XR renders with the interface transform and full projection | `servers/rendering/renderer_scene_cull.cpp:2749` |
| XR matrix array is copied column-major | `servers/xr/xr_interface_extension.cpp:223` |
| Projection has -Z forward and near/far NDC -1/+1 | `core/math/projection.cpp:364` (also check `godot-cpp/src/variant/projection.cpp:366`) |
| CPU RG8 uses R,G,0,1; imported RD RG8 uses R,R,R,G | `servers/rendering/renderer_rd/storage_rd/texture_storage.cpp:2296` and `:2871` |
| Shader mat3 uniforms consume a Basis | `servers/rendering/renderer_rd/storage_rd/material_storage.cpp:303` |
| Transparent viewport supplies transparent black | `servers/rendering/renderer_viewport.cpp:394` |
| Mobile writes scene colour and alpha, then blends RGB by source alpha | `servers/rendering/renderer_rd/shaders/forward_mobile/scene_forward_mobile.glsl:2344`; `servers/rendering/renderer_rd/storage_rd/material_storage.cpp:655` |
| HDR 2D forces float scene buffers, avoiding Mobile's two-bit alpha | `servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.cpp:159`; `servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h:195` |
| HDR viewport skips the final sRGB conversion | `servers/rendering/renderer_rd/renderer_scene_render_rd.cpp:752` and `:961` |
| Tonemapping changes RGB and preserves alpha | `servers/rendering/renderer_rd/shaders/effects/tonemap.glsl:860`, `:893`, `:954` |
| Canvas uses the same blend factors, including premultiplied alpha | `servers/rendering/renderer_rd/renderer_canvas_render_rd.cpp:1517`; `servers/rendering/renderer_rd/storage_rd/material_storage.cpp:700` |
| Main-thread pre-draw precedes draw; post-draw follows it | `servers/rendering/rendering_server_default.cpp:446` and `:229` |

`display_transform` is this library's explicit convention, derived in
`src/core/projection.cpp`, not a promise that a future native API has the same
mapping direction. The native adapter must convert its UV and orientation
conventions before constructing a packet. The numerical tests compare against
an independent pixel-rotation/crop oracle.

## Things that bite

CPU-uploaded `Image.FORMAT_RG8` samples chroma from `.rg`. The later imported RD
path maps RG8 to luminance-alpha and needs `.ra`. There is one CPU path here;
applying the imported swizzle to it destroys chroma. Pure red and blue bars are
explicit harness checks. The synthetic full-range YCbCr contract uses byte 128
as neutral chroma. Reserved colour encodings display magenta until implemented.

The camera is composited separately because 3D background draws pass through the
tonemapper, fog and glow. Changing a game's Environment must not recolour camera
pixels. Keep the outer viewport in SDR. The HDR virtual viewport stores
premultiplied linear RGB; the final shader divides by coverage alpha, applies the
sRGB transfer function, then multiplies by alpha for `blend_premul_alpha` in the
SDR canvas. Plain `blend_mix` would multiply alpha twice. Ordinary screen-space
refraction sees the virtual scene, not the camera. Alpha-zero glow is discarded.

The transparency assertions use linear tonemapping and exposure 1. Nonlinear
tonemapping happens to accumulated premultiplied colour before composition, so
translucent material appearance under filmic/ACES/AgX needs separate artistic and
rendering review. The colour-invariance check covers all these operators and
exposures, fog and glow in camera-only regions. Passing it alone does not prove
nonlinear translucent rendering or glow halos.

The Metal import over-release is recorded for milestone 2: the same-format
native-texture branch does not retain the imported MTLTexture, while destruction
releases it (`drivers/metal/rendering_device_driver_metal.cpp:416`, `:527`).
Compensation must target the affected engine and branch; retaining a
CVMetalTexture wrapper is a different operation. Milestone 1 does not import
Metal textures and must not add a retain workaround.

The adb version rule also applies to scripts and manual one-liners. Always use
`"$ADB"`. `harness android` installs only this demo, launches its exported
`GodotAppLauncher` alias and filters logcat by its PID. The underlying GodotApp
activity is not exported in 4.7.2. Debug captures are retrieved through `run-as`
into `build/harness/android-userdata.tar`; release builds cannot use `run-as`.

Headless cannot render. Run the desktop harness through ci4's GUI session, not a
bare SSH command or this sandbox. It reads lossless viewport images after
`frame_post_draw`, saves every check's PNGs, and prints exactly one
`HARNESS RESULT PASS|FAIL`. `user://harness/` is the default capture directory;
`-harness-out` overrides it. `-harness-timeout` defaults to 240 seconds and the
host monitor defaults to 300 (`HARNESS_TIMEOUT`). A script error, timeout, missing
result or duplicate result makes the host monitor fail. A subset PASS certifies
only the selected steps. Phones run all checks automatically on launch.

Readback can lower measured render rate. The consistency check requests 60 fps,
sets the camera to 30 fps, captures 120 consecutive rendered frames and reports
observed rate, repeats and advances. It fails if it never exercises a repeated
packet or advances fewer than 30 times. It does not claim a sustained device
performance measurement. Frame images are saved after acquisition to reduce I/O
interference.

First GUI checks: inspect HDR/alpha handling on Metal, strict one-level camera
colour accuracy, the off-centre marker centroids and repeated-packet motion.
Then build both mobile variants, inspect AAR assets/native alignment, export,
sign and launch on both devices. iOS static entry-symbol retention and Android
AAR discovery are unverified until those exported apps actually load.


## ARCore backend and Pixel diagnostics

`ARCoreInterface` implements the same packet/presentation methods as the synthetic
interface. `ARView.backend` defaults to synthetic to preserve the original harness;
`demo/ar/ar_demo.tscn` chooses ARCore on Android and synthetic on desktop. The
native interface name is `HandheldARCore`. The Kotlin singleton is
`HandheldARPlugin`; `HandheldAR` remains the shared native raycast API.

ARCore uses SDK 1.56.0, requires Google Play Services for AR 1.48 or newer, and logs
its installed APK version. Kotlin checks availability and installation on Android's
UI thread and requests CAMERA permission. Native code starts only after the JNI
bridge reports installation and permission. CAMERA permission denial is exposed,
including denial after an app-settings change. The demo exports as AR Optional;
set the Android export option `handheld_ar/ar_required` to add the AR hardware
requirement and change the ARCore manifest metadata to required.

All session/frame/camera/image/plane/query/anchor calls belong to Godot's main
thread (VkThread under Vulkan). Kotlin queues lifecycle commands with
`runOnRenderThread`; it never calls the session itself. Application-pause
notifications provide an idempotent backstop. Safe rendering thread mode (1) is
required; Separate and Unsafe remain unsupported. JNI stores the JavaVM and
Activity/application Context global references, never a JNIEnv. Session destroy
is the exception: a JVM-attached worker owns the bare paused session after every
other native reference has been released. Shutdown never waits for destroy.

The config explicitly uses EXPOSE_HARDWARE_BUFFER, LATEST_CAMERA_IMAGE, horizontal
and vertical planes, auto focus and disabled light/depth/instant placement/cloud/
geospatial/semantic/face/stabilization/streetscape modes. Camera selection uses a
non-null back-camera/30fps/no-depth/no-stereo filter, largest matching-aspect CPU
image and smallest GPU image on ties. `set_camera_config_index(index)` selects a
listed configuration (including VGA); -1 restores automatic selection.
`set_focus_auto(false)` selects fixed focus. Both settings reset an active session.
Do not enable Godot CameraServer feeds while an ARSession runs: both open Camera2.

A new timestamp yields one copied packet. CPU YUV_420_888 is repacked using each
plane's strides and released in the same tick. Missing images drop the whole
packet. CPU R8/RG8 textures keep the existing shader's `.rg` chroma convention and
full-range BT.601 conversion. The ring contains Q+3 pairs, where Q is
RenderingDevice.get_frame_delay(). Only `_end_frame` counts completed RD swaps.
A pair is reused after Q swaps since its last submitted use and only after all
external packet and texture references are gone. Retaining every ring packet
therefore drops new packets until readers release one; it never rewrites their
pixels. The ring is library-owned CPU-upload storage, with no AHardwareBuffer
import or Metal retain workaround.

`reset()` immediately increments the session generation, clears presentation and
removes every old plane tracker. It pauses/releases the old session and hands
only ArSession_destroy to a worker, then creates/configures/resumes a new session.
CAMERA_NOT_AVAILABLE on resume retries with 100ms–2s backoff. Old query/anchor IDs
are invalidated; anchors must be recreated. Generations and query/anchor handles
use process-wide unique IDs so interface recreation cannot revive a stale handle.
Plane IDs are opaque within a generation. Subsumed trackers retain their identity
as aliases but are excluded from default visualisation and raycasts. Polygon
coordinates are plane-local XZ, with +Y normal. `ARView.raycast(point)` intersects the presented packet and returns
sorted `ARRaycastHit` resources, including pose, distance, plane ID, frame ID and
generation. Native live queries are separate and expire at the next update.

Build native code inside the existing shell, without running Gradle:

```bash
scons platform=android target=template_debug arch=arm64 api_version=4.7 ndk_version=29.0.14206865 -j8
scons platform=android target=template_release arch=arm64 api_version=4.7 ndk_version=29.0.14206865 -j8
```

The checked-in header is the supplied local 1.44-era snapshot; its provenance is
in `thirdparty/arcore/README.md`. `tools/update-arcore-header.sh` downloads the
pinned 1.56.0 AAR and extracts its header if present, otherwise the exact SDK tag's
header. Gradle extracts the AAR's arm64 shim for inspection; Godot's Maven export
dependency packages it once. Do not add the shim to both the plugin AAR and the
exported Maven dependency.

After Claude has built/exported a debug APK and checked/unlocked the shared Pixel:

```bash
ADB=<host-binary> ANDROID_SERIAL=18231FDF6004QL tools/harness.sh android-ar -harness-duration 60
ADB=<host-binary> ANDROID_SERIAL=18231FDF6004QL tools/harness.sh android-ar -harness-duration 1800
```

`android-ar` installs the selected APK (`APK` overrides its path), passes a
validated debug-only intent argument to the Kotlin plugin, and launches the AR
scene. Its monitor requires the named AR steps and exactly one result; an ignored
intent cannot pass by running the synthetic suite. The host timeout scales with
the requested duration. `-harness-steps` selects a comma-separated subset;
`-harness-out` changes the capture directory. `-harness-lifecycle` is accepted for
compatibility; the lifecycle step itself always exercises two pauses/resumes
through the owner callbacks and two resets. Real Android home/relaunch cycles
remain necessary to verify OS callback ordering.

The steps print `HARNESS STEP <name> OK|FAIL|INFO <detail>` under logcat's `godot`
tag: arcore-session, no-gl, camera-image, projection-form, display-transform,
composite, lifecycle and tracking. The no-GL step asserts EGL_NO_CONTEXT on the
update owner and counts every status code. Camera checks include image rate,
missing-image fraction (at most 5%), timestamp equality, strides/layout,
acquire p50/p99 and repack/upload p50/p95. A dark camera image is INFO only after
presentation/upload/capture checks pass. Tracking and centre hits are INFO on a
desk. Screen/camera PNGs, diagnostics and device telemetry are retrieved with the
existing run-as capture archive. Native timing samples retain up to 131072 values,
enough for 30 minutes at 60fps; longer runs report a rolling window.

Rotate through all four physical orientations to exercise the DisplayListener,
including reverse orientations. Diagnostics include CPU/GPU intrinsics, UV affine
and corner checks, projection near/far substitution, plane-vertex reprojection
error, AHardwareBuffer format/usage (and lockPlanes timing when CPU-readable),
clock-domain offsets, session timings, reset-to-tracking, config lists and status
counts. The packet timestamp remains labelled `arcore_monotonic_unspecified` until
device clock comparisons establish its source. Run separate config-index sessions
for VGA/high-resolution thermal and bandwidth comparisons.

`-harness-probes` enables repeated-frame observations, copied point clouds and
reprojection diagnostics during the run, then runs bounded image-capacity and
old-frame hitTestRay probes after the measured steps. With an empty plane table,
it also compares two same-frame plane enumerations after releasing all application
references; centre/type matching reports ambiguity rather than inventing identity.
The capacity probe intentionally retains CPU images until RESOURCE_EXHAUSTED or
its cap, publishes no packets while probing, and releases them on completion,
pause or shutdown. Keep it off for ordinary throughput measurements. Old-frame
hits require a tracked textured surface; lack of tracking is reported as INFO.

Native query helpers are `live_hit_test(view_pixels)`, `live_hit_test_ray(origin,
direction)` and `create_live_anchor(query_id)`. They are valid only until the next
update. `create_anchor(packet, hit, allow_fallback=false)` rebases an older polygon
hit through the latest plane centre, rejecting stale generations or changed
subsumption. Anchors are queried with `get_anchors()` and removed with
`remove_anchor(id)`. Optional `set_point_cloud_enabled(true)` copies point-cloud
data into packets and releases the native point cloud in the same tick.

For ARCore datasets, use `HandheldARPlugin.getExternalFilesDir()` to construct a
percent-encoded `file://` URI in private external storage, then
`start_recording(uri)`, `record_input(bytes)` and `stop_recording()`.
`start_playback(uri)` creates a fresh generation; `stop_playback()` returns to a
fresh live session. `get_playback_inputs()` drains custom input records for the
application to replay. The fixed track UUID is
`61a97725-8be4-4e8f-a196-6af38ca10735`, version 1, with opaque application-owned
payloads, bounded to 65536 bytes. ILLEGAL_STATE writes retry over at most eight
new frames, with a 128-input queue. Recording auto-stops on pause. Playback end
is exposed in extras diagnostics; restarting the same URI creates another fresh
session. Dataset tests must use tolerances, not exact plane counts or poses.
Datasets contain room video and device information; keep them in private storage,
never in the MIT repository. This is separate from the eventual deterministic
library replay format.

The demo accepts `-harness-camera-config <index>` before session creation.
`-harness-record <name.mp4>` and `-harness-playback <name.mp4>` use private external
storage, are mutually exclusive, and reject paths. Recording includes normalized
tap positions and expected hit distance. Playback checks positive expected hits
within max(0.15 metres, 25%); an absent expected hit fails. It ends observation at
PLAYBACK_FINISHED and reports the actual duration. Restarting playback creates a
fresh session instead of reusing the paused session, preserving generation
isolation. Keep the ordinary live-camera suite separate from dataset runs.

Device telemetry records PSS, available memory, thermal status and available
battery current/energy/voltage/temperature counters at start, every minute,
immediately after each reset, and at finish. USB charging is recorded because it
affects power comparisons. Real onMainPause logs queue-to-pause-return latency;
the in-app native lifecycle check does not measure Android callback dispatch.

Resume currently runs synchronously on the owner and reports its duration.
The brief's conditional worker-resume path for calls exceeding about 100ms is
deferred until Pixel timings establish the need. If triggered, it requires a
session lock and update suppression, including coordination with reset/shutdown.
See `docs/milestone-3-validation.md` for offline evidence and the device handoff.
