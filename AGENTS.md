# PocketAR

MIT handheld AR library for Godot 4.7.2. One GDExtension presents ARKit on iOS and
ARCore on Android through a shared Godot API. The synthetic backend supplies the
desktop rendering harness.

## Requirements

Work in this task worktree, not `~/hub/repos/`. Do not commit unless asked.
Use the repository's devenv for Godot, SCons, Python, Java 17 and the Android SDK.
When already inside its shell, run commands directly; nested devenv calls fail in
the coding sandbox. Xcode supplies the Apple compiler. Keep `apple.sdk = null`
and `stdenv = pkgs.stdenvNoCC`; do not add another C toolchain.

The supported targets are macOS arm64 (desktop harness), iOS 16+ arm64 on Metal,
and Android API 29+ arm64 on Mobile/Vulkan. `godot` is PocketAR's engine: Godot
master plus the patches in doublesymmetry/godot's `PATCHES.md`, pinned to a release of
that fork in `nix/godot-pocketar.nix` (editor and export templates). It runs
self-contained from `.devenv/state/godot-pocketar`. Stock Godot 4.7.2 is the oldest
supported engine; `stock <command>` runs a command against it, for example
`stock harness desktop`. godot-cpp is pinned to `b051e69` with `api_version=4.7`.
Android uses compile/target SDK 36, build tools 35.0.0 (Godot 4.8's Gradle template)
and 36.1.0, NDK 29.0.14206865, Kotlin 2.1.21 and Java 17. `SConstruct` derives `IOS_TOOLCHAIN_PATH` from `xcode-select -p`, so Xcode need
not be named `Xcode.app`. iOS signing team: `L7G7TF62MC`. Both demo package IDs:
`org.doublesymmetry.pocketar.demo`.

The certified iOS deployment target stays 16.0 in SConstruct and the preset.
`scons platform=ios ios_min_version=15.0` is a diagnostic override only; it does
not certify iOS 15 or make an A10 device support the Metal renderer.

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
| iOS Xcode project | `env export ios debug` or `env export ios release` |
| Android APK | `env export android debug` or `env export android release` |
| Desktop rendering harness | `harness desktop -harness-out /tmp/pocketar-captures` |
| Selected rendering checks | `harness desktop -harness-steps projection,transparency` |
| Installed iPhone demo | `IOS_DEVICE=<id> harness ios` |
| Pixel demo installation and harness | `ADB=<host-binary> ANDROID_SERIAL=<serial> harness android` |
| Native math, retirement, YUV/GPU and fixture parser tests; Python fixture tests | `TMPDIR=/tmp tools/test-native.sh` |
| Headless packet, facade, scripted, arguments, exporter and thread tests | `tools/test-headless.sh` |
| ARCore native lifecycle/status, GPU completion lifetime and query/recording fixtures | `TMPDIR=/tmp tools/test-arcore.sh` |
| All Python tool tests | `python3 -m unittest discover -s tests/tools` |
| Kotlin launch argument tests | `cd android && ./gradlew :plugin:testDebugUnitTest` |
| Generate landscape fixture | `python3 tools/fixtures.py --output demo/harness/fixtures/table.jsonl --loss-at 1 --reset-at 1.5 --pause-at 1.75 --tap-at 0.6 --drag-to 0.1 -0.7 -2 --plane-examples` |
| Generate portrait fixture | Use the preceding command with `--output demo/harness/fixtures/table-portrait.jsonl --view-size 1080 1920` |

`JOBS` controls build concurrency, default 8. `ARCH` defaults to arm64; macOS can
also build x86_64. `build all` stops on the first failed target. Build both mobile
variants before both exports. Gradle's wrapper downloads its distribution and
dependencies on first use. Godot's matching export templates must be installed;
`tools/export.sh android` installs their Gradle build template into the demo automatically.
Release Android signing comes from the user's Godot export credentials.

The iOS preset exports a loose Xcode project and supporting files under
`build/exports/ios/<mode>/` for subsequent Xcode signing, device builds and release
archives. It produces neither a ZIP nor a signed IPA. The XCFramework
contains a device-only static archive, including godot-cpp objects. The synthetic
backend links no AR SDK. Android loads the ARCore C shim dynamically; the export
plugin supplies `com.google.ar:core:1.56.0`, including its Java code and native shim. Camera declarations live in the AAR
manifest and export presets; the editor plugin adds the chosen debug/release AAR.

On macOS the sandbox may deny the inherited temporary directory. For native
verification there, use `TMPDIR=/tmp tools/test-native.sh`. Headless import can
load the extension and parse scripts, but sandbox errors writing Godot's global
settings/cache or reading system certificates are separate from script errors.

## Layout

- `src/core/`: frame packets, simulated XR providers, shared singleton, projection and ray math.
- `src/platform/android/`: ARCore session owner, JNI bridge, CPU upload, GPU camera submission and live queries.
- `src/platform/ios/`: ARKit session, Metal imports and swap-based retirement.
- `demo/ar/`: runtime-selected AR demo and device diagnostics.
- `thirdparty/arcore/`: C API header and provenance.
- `demo/addons/pocketar/`: ARView, shaders, extension descriptor, plane visualizer and editor export plugin.
- `demo/addons/pocketar/bin/`: generated libraries, XCFrameworks and AARs; ignored by Git.
- `demo/harness/`: rendering assertions and scripted replay fixtures.
- `android/`: Godot v2 Kotlin plugin and AAR Gradle project.
- `tests/native/`: standalone C++ numerical, resource and fixture parser tests.
- `tests/headless/`: GDScript suites and mocked native ARCore fixtures.
- `tests/tools/`: fixture generator, device transport and harness monitor tests.
- `tools/`: build, package, export, harness and test entry points.

C++ follows godot-cpp conventions, using tabs and `p_` parameter names. GDScript
is typed. Comments explain a non-obvious constraint; no comment banners or
narration. Commit messages, if requested, are one lowercase `type: subject` line.

## Settled architecture

One ARView owns one monoscopic `XRInterfaceExtension` and makes it primary.
The synthetic interface is named `PocketARSynthetic`; scripted fixtures use
`PocketARScripted`. Both publish `head` / `default` for
`XRCamera3D`. The transparent XR SubViewport contains `XROrigin3D`, `XRCamera3D`
and `SceneRoot`. `ARView.composition` is a startup-only `Composition` enum:
`IN_SCENE` (default) or `ISOLATED_CAMERA`. Assign it before adding the view to the
tree; changing it after `_ready` reports an error and preserves the selected mode.
In-scene draws the camera inside the XR viewport; isolated draws an SDR camera
canvas beneath the virtual scene. A CanvasLayer holds ordinary HUD Controls above both. Controls receive input first.
ARView ignores input by default. With `handle_presses=true`, unconsumed presses
emit a ray through `ARView.screen_to_ray()` and mark the event handled. The demo
and rendering harness opt in. Touch-emulated mouse presses are ignored.

`ARFramePacket` contains copied calibration, pose, UV mapping, IDs, timestamps,
tracking state and planes. These values remain valid for the packet's lifetime.
On ARKit and the ARCore CPU path, camera textures are valid only while the packet is
current or presented; ARView also protects the last packet applied to its material
until it replaces or clears that material. Script-held packet or texture Refs do
not extend camera-image lifetime. An old ARKit Texture2DRD is cleared and samples
the engine default; an old ARCore CPU texture can show newer slot content. The GPU
path shares one ExternalTexture across packets: submitting the next frame lets
the engine replace its pixels during conversion, before that frame's packet is
presented. Even a current or presented GPU packet does not preserve its image. Keep the
packet for touch/raycast values, not as an image archive. If texture acquisition
is refused, pose, tracking and planes still advance using the previous textures,
and `texture_refusals` increases. Setters affect the next acquisition. Below the
render rate, the entire packet repeats. Resize/calibration changes acquire a new complete
packet immediately on the next XR process. The synthetic clock is monotonic
`Time.get_ticks_usec()`; orientation is clockwise quarter turns relative to the
fixed camera image. Automatic synthetic orientation chooses 0 for landscape and
1 for portrait; explicitly set 0–3 to exercise reverse orientations.

`display_transform` maps top-left-origin viewport UV into top-left-origin camera
image UV, including centred aspect-fill crop and rotation. On the ARCore GPU path,
it maps into `TEXTURE_NORMALIZED`, and `image_size` is the ARCore texture size.
`intrinsics`, including `intrinsics.image_size`, remain in CPU image space on both
ARCore paths. The CPU and GPU streams can have different crops and fields of view. Synthetic projection
uses upright intrinsics rotated into display space, with positive diagonal focal
terms for Godot's LOD and shadow calculations. The quarter turn belongs to the
display-oriented camera pose; `set_pose()` and `physical_camera_transform` remain
in the physical image-camera basis. Camera coordinates are
right-handed, +X right, +Y up, -Z forward. Matrices are column-major with OpenGL
NDC depth [-1,+1]; the engine handles device depth/Y corrections. Rays invert the
full matrix and transform the near point through world-from-camera. They never
use camera half-extents.

The current packet is applied during `frame_pre_draw`, and becomes the presented
packet at `frame_post_draw`. Touch rays use the presented packet. This backend
requires Godot's effective Safe rendering mode: initialization must run on both
the main thread and the render thread (`RenderingServer.is_on_render_thread()`).
Separate is rejected even with a command-line override. Godot 4.7.2 maps the
deprecated Unsafe option to Safe. XROrigin and XR world scale
remain identity/one; camera poses and game geometry share that
world. One game environment and world are exposed through `ARView.environment`
and `ARView.world_3d`. ARView owns a private world so the surrounding game's main
viewport cannot render its AR objects accidentally. `world_3d` returns that active
world via `find_world_3d()`, for physics queries and cameras sharing the AR scene.

The simulated backends reuse one CPU Image pair. Every published packet receives
new ImageTextures; retained packets keep their own image resources, and Godot
handles RID retirement behind fences.

## Engine evidence

Paths below are relative to `/Users/ci/hub/wt/godot-pocketar-4.7.2` (4.7.2).

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
| Mobile sorts opaque priority before shader/material and depth; stencil uses a separate grouping | `servers/rendering/renderer_rd/forward_mobile/render_forward_mobile.h:339`, `:355`, `:484` |
| Transparent background suppresses Mobile's automatic fog sky | `servers/rendering/renderer_rd/forward_mobile/render_forward_mobile.cpp:1088` |
| Mobile decal pairing uses layer masks; decals run even for unshaded materials | `servers/rendering/renderer_scene_cull.cpp:244`; `servers/rendering/renderer_rd/shaders/forward_mobile/scene_forward_mobile.glsl:1474` |
| Mobile material dithering precedes normalized scene storage | `servers/rendering/renderer_rd/shaders/forward_mobile/scene_forward_mobile.glsl:2363` |
| Main-thread pre-draw precedes draw; post-draw follows it | `servers/rendering/rendering_server_default.cpp:446` and `:229` |

`display_transform` is this library's explicit convention, derived in
`src/core/projection.cpp`, not a promise that a future native API has the same
mapping direction. The native adapter must convert its UV and orientation
conventions before constructing a packet. The numerical tests compare against
an independent pixel-rotation/crop oracle.

## Things that bite

Keep both mobile export presets `runnable=false`. A runnable Android preset makes
Godot editor invocations, including `--import`, poll adb and run `adb kill-server`
on exit, disrupting the shared host server. Launch through the harness scripts.
Keep Godot's `.uid` sidecars in version control.

CPU-uploaded `Image.FORMAT_RG8` samples chroma from `.rg`. The later imported RD
path maps RG8 to luminance-alpha and needs `.ra`. There is one CPU path here;
applying the imported swizzle to it destroys chroma. Pure red and blue bars are
explicit harness checks. The synthetic full-range YCbCr contract uses byte 128
as neutral chroma.

`IN_SCENE` is the lower-bandwidth composition for games that process the camera
and virtual objects together. An opaque, unshaded camera quad draws at exact
reverse-Z depth zero, with depth writes and the normal greater-or-equal test.
The XR viewport keeps `transparent_bg=true` to suppress Mobile's automatic fog
sky and uses `use_hdr_2d=false`; its texture is drawn without a canvas material.
The camera fills every sample with alpha one, so ordinary transparency and MSAA
blend in linear scene colour and keep the final alpha at one. Screen-reading
materials can sample the camera. Fog is disabled on the quad, but tonemapping,
exposure, colour adjustments and glow affect its pixels. Mobile's normalized
scene buffer also clips radiance above approximately two, including bright
virtual emission before glow or tonemapping.

The quad's culling AABB is a small box just beyond the calibrated near-plane
centre, updated with each packet; it remains inside asymmetric frusta and short
far planes. Occlusion culling and shadow casting are disabled. Material priority
127 is reserved for the camera: ordinary opaque scene materials must use lower
priorities to draw before it. Mobile compares `sort_key2` first, with priority in
its high bits; depth in `sort_key1` cannot guarantee late drawing. Equal-priority
materials are ordered by shader/material, and stencil opaques form a later group.
These exceptions affect early-depth savings; exact zero still preserves scene depth.

Godot 4.7.2 Mobile has no per-instance receive-decal switch or spatial render mode
that disables decals. It pairs decals by AABB overlap and shared visibility layer
masks, and processes them even in unshaded shaders. Keep decal boxes away from the
camera's near-plane proxy: an overlapping box that contains the camera can paint
the camera image. ARView reserves no additional visibility layer. Other cameras
and reflection probes sharing its private world can also see the quad when their
masks and frusta include it.

Enable `rendering/anti_aliasing/quality/use_debanding` for `IN_SCENE`. This dithers
materials before Mobile's ten-bit scene write. In the measured desktop
Mobile/Metal gradient, 39 of 255 adjacent source levels merged, with visible loss
in dark values below 32; material dithering kept column means within 0.44 of an
eight-bit level, at the cost of a few levels of per-pixel noise. Viewport-stage
debanding alone does not recover this precision. Camera equality within one
level is not the in-scene contract.

Use `ISOLATED_CAMERA` when a game's tonemapping, grading or glow must leave camera
pixels unchanged, or its virtual scene needs the float colour range. The camera
is an SDR canvas below an HDR virtual viewport. That viewport stores
premultiplied linear RGB; the final shader divides by coverage alpha, applies the
sRGB transfer function, then multiplies by alpha for `blend_premul_alpha` in the
SDR canvas. Plain `blend_mix` would multiply alpha twice. Screen-space refraction
sees only the virtual scene, and alpha-zero glow is discarded. The camera is
unaffected by the game's Environment, including fog. Keep the outer viewport SDR
in both modes. Both camera shaders share the same YCbCr/RGB sampling include.

`ARView.environment` is the game's source resource. Background mode and colour
are ignored in both modes: ARView copies the other settings into a private
Environment before each draw and forces a transparent-black clear. Live edits
apply even when the camera packet repeats, and leave the source resource unchanged.
Both modes keep the transparent-viewport depth-of-field restriction. Custom
blending and nonlinear translucent rendering need their own rendering review.

The rendering harness runs both compositions, prefixing captures and step details
with the mode. Projection and `xr-mono-2d` measure geometry against a camera-only
reference, independently of viewport alpha. `transparency` checks linear tonemapping
at exposure one, opaque/half-transparent cubes, MSAA coverage and bright emission;
it also saves glow captures for visual inspection. `color-invariance` checks all
operators, exposure, fog, glow and adjustments for isolated camera; in-scene checks
linear tonemapping, exposure one, fog on/off, no glow or adjustments. `composition`
checks far=23, geometry 1mm inside the far plane, exact zero background depth,
fog, alpha scissor, screen reads, decal overlap and reset/interruption with a live
HUD. `camera-precision` records pointwise and column-mean errors for material and
viewport dithering independently. Headless smoke checks both active materials,
startup locking, textureless packets and clearing on interruption.

Native phone runs select the mode at startup with
`-harness-composition in-scene|isolated-camera` (default `in-scene`), accepted by
both harness transports. Run `camera-image,composite,packet-retention,lifecycle`
and the platform's projection/display-transform checks in each mode. ARKit's
in-scene camera comparison allows up to eight byte levels of pointwise error for
normalized storage and dithering; isolated retains its two-level threshold.

The adb version rule also applies to scripts and manual one-liners. Always use
`"$ADB"`. `harness android` installs only this demo, launches its exported
`GodotAppLauncher` alias and filters logcat by its PID. The underlying GodotApp
activity is not exported in 4.7.2. Debug captures are retrieved through `run-as`
into `build/harness/<platform>/android-userdata.tar`; release builds cannot use
`run-as`. Android runs clear `files/harness` before launch. Host logs live at
`build/harness/<platform>/latest.log`, with separate `android` and `android-ar`
directories and `desktop` used for the `macos` alias.

Headless cannot render. Run the desktop harness through ci4's GUI session, not a
bare SSH command or this sandbox. It reads lossless viewport images after
`frame_post_draw`, saves every check's PNGs, and prints exactly one
`HARNESS RESULT PASS|FAIL`. `user://harness/` is the default capture directory;
`-harness-out` overrides it. `-harness-timeout` defaults to 240 seconds and the
host monitor defaults to 300 (`HARNESS_TIMEOUT`). A script error, timeout, missing
result or duplicate result makes the host monitor fail. A subset PASS certifies
only the selected steps. Phones run all checks automatically on launch.

Readback can lower measured render rate; consistency thresholds and capture details
live in `demo/harness/harness.gd`.

## ARCore backend and Pixel diagnostics

`ARCoreInterface` implements the same packet/presentation methods as the synthetic
interface. `ARView.backend` defaults to synthetic;
`demo/ar/ar_demo.tscn` chooses ARCore on Android and synthetic on desktop. The
native interface name is `PocketARCore`. The Kotlin singleton is
`PocketARPlugin`; `PocketAR` remains the shared native raycast API.

ARCore uses SDK 1.56.0, requires Google Play Services for AR 1.48 or newer, and logs
its installed APK version. Kotlin checks availability and installation on Android's
UI thread and requests CAMERA permission. Native code starts only after the JNI
bridge reports installation and permission. CAMERA permission denial is exposed,
including denial after an app-settings change. The demo exports as AR Optional;
set the Android export option `pocketar/ar_required` to add the AR hardware
requirement and change the ARCore manifest metadata to required.

All session/frame/camera/image/plane/query calls belong to Godot's main
thread (VkThread under Vulkan). Kotlin queues lifecycle commands with
`runOnRenderThread`; it never calls the session itself. Application-pause
notifications provide an idempotent backstop. Effective Safe rendering mode is
required and checked through the render-thread API. JNI stores the JavaVM and
application Context global references, never a JNIEnv. Session destroy
is the exception: a JVM-attached worker owns the bare paused session after every
other native reference has been released. Shutdown never waits for destroy.

The config explicitly uses EXPOSE_HARDWARE_BUFFER, LATEST_CAMERA_IMAGE, horizontal
and vertical planes by default, auto focus and disabled light estimation. Depth, instant
placement, cloud/geospatial anchors, semantics, faces, stabilization and streetscape
geometry keep the SDK's disabled defaults. CPU camera selection uses a
non-null back-camera/30fps/no-depth/no-stereo filter, largest matching-aspect CPU
image and smallest GPU image on ties. `set_camera_config_index(index)` selects a
listed configuration (including VGA); -1 restores automatic selection.
Changing the camera configuration resets an active session.
Do not enable Godot CameraServer feeds while an ARSession runs: both open Camera2.

A new timestamp yields one copied packet. On the CPU path, YUV_420_888 is repacked using each
plane's strides and released in the same tick. Missing images drop the whole
packet. CPU R8/RG8 textures keep the existing shader's `.rg` chroma convention and
full-range BT.601 conversion. The ring contains Q+3 pairs, where Q is
RenderingDevice.get_frame_delay(). Only `_end_frame` counts completed RD swaps.
A pair is reused after Q swaps since its last protected use, once no current,
presented or ARView-applied packet uses it. Script-held Refs do not block reuse;
stale packets can therefore show newer camera pixels. The ring is library-owned CPU-upload storage, with no AHardwareBuffer
import or Metal retain workaround.

`reset()` immediately increments the session generation, clears presentation and
removes every old plane tracker. It pauses/releases the old session and hands
only ArSession_destroy to a worker, then creates/configures/resumes a new session.
CAMERA_NOT_AVAILABLE on resume retries with 100ms–2s backoff. Generations and query
IDs use process-wide unique IDs so interface recreation cannot revive a stale handle.
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

The checked-in header comes from the SDK `v1.56.0` GitHub tag; its provenance is
in `thirdparty/arcore/README.md`. The 1.56.0 AAR has no header, so
`tools/update-arcore-header.sh` uses the exact SDK tag. Godot's Maven export
dependency packages the arm64 shim once. Do not add the shim to both the plugin AAR and the
exported Maven dependency.

After Claude has built/exported a debug APK and checked/unlocked the shared Pixel:

```bash
ADB=<host-binary> ANDROID_SERIAL=18231FDF6004QL tools/harness.sh android-ar -harness-duration 60
ADB=<host-binary> ANDROID_SERIAL=18231FDF6004QL tools/harness.sh android-ar -harness-duration 1800
```

`android-ar` installs the selected APK (`APK` overrides its path) and launches the
AR scene with validated debug-only arguments; step/result checks live in
`tools/run-harness.py`. `-harness-steps` selects a subset and `-harness-out` changes
the capture directory. Real Android home/relaunch cycles remain necessary to
verify OS callback ordering.

To launch the plain AR demo from an installed debug APK, after checking and
unlocking the shared Pixel, use the same host `$ADB` and `ANDROID_SERIAL`:

```bash
"$ADB" shell am force-stop org.doublesymmetry.pocketar.demo
"$ADB" shell am start -W -n org.doublesymmetry.pocketar.demo/com.godot.game.GodotAppLauncher --esa pocketar_harness_args -ar-demo
```

`-ar-demo` selects `ar_demo.tscn` without attaching the device harness. Scene
selection happens in GDScript because official templates disable path overrides.

The ARCore steps are `arcore-session`, `no-gl`, `camera-image`, `gpu-vs-cpu`,
`projection-form`, `display-transform`, `composite`, `packet-retention`, `lifecycle`,
`tracking` and `plane-finding`; thresholds live in `demo/ar/ar_harness.gd` and details in
`diagnostics.json`.

HARNESS and ARCORE lines stay below 1000 bytes. Step lines contain summaries;
full camera configs and native diagnostics are in `diagnostics.json`, and device
telemetry is in `device-<phase>.json`. Plane layouts log one plane per line. The
host monitor polls the app PID and fails promptly if it exits before a result.

Rotate through all four physical orientations, including reverse orientations,
to exercise the DisplayListener; clock offsets remain in `diagnostics.json` because
the ARCore timestamp source is unestablished. Run separate config-index sessions
for VGA/high-resolution thermal and bandwidth comparisons.

Native query helpers are `live_hit_test(view_pixels)` and
`live_hit_test_ray(origin, direction)`; results describe the current native frame
and expire at the next update. Optional `set_point_cloud_enabled(true)` copies
point-cloud data for reprojection diagnostics and releases it in the same tick.

For ARCore datasets, use `PocketARPlugin.getExternalFilesDir()` to construct a
percent-encoded `file://` URI in private external storage, then
`start_recording(uri)`, `record_input(bytes)` and `stop_recording()`.
`start_playback(uri)` creates a fresh generation. `get_playback_inputs()` drains custom input records for the
application to replay. The fixed track UUID is
`61a97725-8be4-4e8f-a196-6af38ca10735`, version 1, with opaque application-owned
payloads, bounded to 65536 bytes. ILLEGAL_STATE writes retry over at most eight
new frames, with a 128-input queue. Recording auto-stops on pause. ARCore 1.56 playback
returns the same track data on later frames; PocketAR delivers each recorded frame's inputs
once and counts the rest as `playback_inputs_repeated`. Playback end
is exposed in extras diagnostics; restarting the same URI creates another fresh
session. Dataset tests must use tolerances, not exact plane counts or poses.
Datasets contain room video and device information; keep them in private storage,
never in the MIT repository.

The demo accepts `-harness-camera-config <index>` before session creation;
`-harness-record <name.mp4>` and `-harness-playback <name.mp4>` are mutually exclusive,
basename-only dataset options whose checks live in `demo/ar/ar_demo.gd`.
Keep ordinary live-camera runs separate from dataset runs.

Device telemetry is saved to `device-<phase>.json`; USB charging affects power
comparisons, and native lifecycle checks do not measure Android callback dispatch.

## ARCore GPU camera path

The zero-copy Vulkan path needs the patched `doublesymmetry/godot` 4.8-dev build
and matching templates until `ExternalTexture.submit_hardware_buffer` ships upstream.
The extension still builds against godot-cpp 4.7: ClassDB detects ExternalTexture
and resolves `SUPPORTS_ANDROID_HARDWARE_BUFFER` by name before selecting a GPU
config. The dynamic call supplies buffer, color model, range, crop and completion.
`OK` means queued; each session generation waits for its first
`COMPLETION_COMPLETED` before enabling GPU presentation.
Stock 4.7.2 and devices without the feature use CPU immediately. A rejected
submission or a failed completion before activation restores CPU config selection
through a session reset. Hardware-buffer errors before activation also fall back;
`NOT_YET_AVAILABLE` gets 30 consecutive attempts. After activation, missing buffers
drop frames and failed completions are counted. `camera_path` and
`camera_path_reason` report the choice. Callbacks from old generations are ignored.
The engine keeps each completion until its GPU frame finishes, which at shutdown is
after the interface has been freed, and godot-cpp's `callable_mp` dereferences its
object to check validity. Completions are therefore static callables bound to the
interface's instance ID; never hand the engine a `callable_mp` it may keep.

GPU selection prefers the largest GPU texture under the existing camera filter,
then the smallest CPU image: a CPU image larger than ARCore's own 640x480 tracking
stream adds a third camera stream nothing reads. An explicit config index still wins. Each new frame submits its borrowed hardware
buffer before the next ARCore update; the engine retains it through GPU completion.
One stable `rgb_texture` replaces Y/CbCr textures, with the display transform
mapping into `TEXTURE_NORMALIZED`. Buffers larger than ARCore's texture dimensions
are cropped to that size at the origin. RGB
hardware buffers use `COLOR_MODEL_RGB`. Ordinary GPU operation never acquires a CPU
image; GPU validation acquires its CPU comparison image explicitly.

The explicit YUV color model stays BT.601 full, matching Camera2's YUV_420_888
(JFIF) definition and the CPU camera shader; `-harness-gpu-validate` runs the
`gpu-vs-cpu` check in `demo/ar/ar_harness.gd` and saves details in `diagnostics.json`.

Both camera paths report RenderingServer frame setup CPU p50/p95; keep validation
runs separate from throughput measurements because synchronous readback adds stalls.

After Claude builds the AAR/APK with patched templates and checks/unlocks the
shared Pixel, use the host `ADB` and `ANDROID_SERIAL` described above:

```bash
tools/harness.sh android-ar -harness-duration 60 -harness-gpu-validate
tools/harness.sh android-ar -harness-duration 60
tools/harness.sh android-ar -harness-duration 60 -harness-camera-path cpu
```

Requested validation fails on a CPU
fallback; ordinary CPU runs report `gpu-vs-cpu INFO`.

## ARKit backend and iPhone diagnostics

`ARKitInterface` (`PocketARKit`) is the iOS Metal backend. `ARView.backend = ARView.Backend.ARKIT`
selects it; the AR demo selects it on iOS. One primary monoscopic camera publishes
`head` / `default`. All ARKit/UIKit access, imports and texture changes happen on
the main thread in Safe rendering mode. Other rendering drivers are unsupported.
The separate internal `PocketARKitRetirement` interface has zero views and is
never primary; it keeps swap callbacks alive after an ARView has been removed.

The session polls `currentFrame` once per `_process` inside an autorelease pool.
Packets copy values and plane boundaries; no ARFrame, ARCamera or ARAnchor enters
a packet. Plane detection defaults to horizontal and vertical; autofocus and gravity
alignment are fixed. Light estimation and HDR video are off.

Interface orientation comes from the Godot view's window scene `effectiveGeometry`.
LandscapeRight is identity; portrait, LandscapeLeft and upside-down rotate the
sensor pose around local Z by +90, +180 and +270 degrees. ARKit supplies upright
projection rows; the backend rebuilds GL row 2 from every requested near/far pair
and adjusts aspect-fill crop for a different requested aspect without changing
the packet's calibration.
The packet maps viewport UV to image UV using the inverse ARKit display transform.
Intrinsics add 0.5 to the principal point as required by the SDK's pixel-centre
convention. Orientation, viewport, pose and calibration are committed together;
a quarter-turn change waits for the window size to change or 0.5 seconds,
whichever comes first. A half-turn commits immediately. ARView may have a
landscape shape in a portrait window; its aspect never blocks orientation.
The inherited identity XROrigin/reference-frame and unit-world-scale limits apply.

Only two-plane full-range BT.601 is accepted. A missing matrix attachment keeps
the established BT.601 matrix (`420f` defaults to it) and increments
`color_tag_missing`; an explicitly different matrix or range is unsupported.
`CVMetalTextureCache` wraps R8 and RG8 planes without CPU image copies, then RD
imports and `Texture2DRD` expose them to the existing camera composite. The packet's
`imported_chroma` flag selects `.ra`; CPU-uploaded synthetic/ARCore frames keep `.rg`.
Unsupported colour encodings fail with a diagnostic instead of using a wrong matrix.

The texture pool owns every CVPixelBuffer and both CVMetalTextureRefs independently
of packet lifetime. A pair retires once no current, presented or ARView-applied
packet uses it, regardless of script-held Refs. On the render thread, clear/free the RS
wrappers first, then free the imported RD RIDs at swap count s. Release CoreVideo
owners at s+Q+1, where Q is `RenderingDevice.get_frame_delay()`: Godot 4.8 drops its
own import retain one swap after Q. Only `_end_frame`
increments the counter, as its first statement, after RD's actual swap. The pool
survives reset, uninitialize and interface recreation. Process shutdown stops
sessions before the pool releases owners; Godot's own import retains remain until
RD finalization. Never synchronously drain the GPU on reset.

Godot 4.7.2's same-format Metal import releases a texture it did not retain
(`drivers/metal/rendering_device_driver_metal.cpp:416`, `:527`).
A one-time same-format R8 import probe records CF retain-count delta, the native
pointer branch and engine version/hash. Only a missing engine retain enables one
compensating CFRetain per successful same-format import. Godot's deferred free
consumes that retain; the library must not balance it. Fixed 4.8 imports must
produce a positive delta and disable the workaround. Version strings alone do not
decide ownership. Device validation of this probe with Metal API Validation is
required. The flush policy is `CVMetalTextureCacheFlush(cache, 0)` after swaps that
release retired owners. At most seven camera buffers are held. Retaining packets
cannot pin those buffers. Retired Texture2DRD objects are cleared, never reused.

In debug builds, the retirement canary records each native texture's retain count
immediately after freeing imports and observes the first later swap where it drops.
Diagnostics report `retain_drop_swaps_min/max`; releasing native owners without an
observed drop increments `premature_release_count`. Retain counts are a heuristic:
in-flight command buffers may also own references. This does not prove GPU
completion. A separate check reads the background material's actual RS texture
parameters and fails on retired RIDs. `camera-image` also requires zero
`texture_refusals` and `buffer_drops`. A swap lasting at least 900ms
extends every pending native retirement by another Q swaps. Godot's Metal fence
wait still times out at 1s and ignores the failure: this mitigation cannot prove
safe GPU work beyond that engine limit. Run the long-stall validation on the phone.

AVFoundation authorization is polled on the main thread; an async permission request
does not capture the interface. Denial, including after Settings changes, pauses
the session and clears presentation. Application pause/resume notifications call
pause/run. ARKit delegate interruption events use the main queue, hide presentation
and resume relocalization only if the app still wants the session running.
Reason codes share the semantics below; ARKit maps its native reasons explicitly.
The delegate always attempts relocalization after interruption.

Reset increments the process-wide generation immediately, removes trackers,
invalidates presentation, and runs ResetTracking|RemoveExistingAnchors. Frames with
timestamps at or before the reset's host-time cutoff are discarded.

Planes keep UUID-derived opaque IDs within a generation. Their pose is the centre
of the extent rectangle, with its rotation about local +Y, and extents are its
width and height. Boundary vertices are transformed into this frame and stored as
local XZ with +Y normal. Positive `rotationOnYAxis` is assumed to follow Godot's
right-handed +Y rotation; draw the extent rectangle over the polygon to verify
that sign on the phone. Limited camera tracking pauses plane trackers and excludes
them from polygon raycasts. Native UUID diagnostics and the lifecycle harness check
that pre-reset anchors do not reappear and log the first post-reset anchor counts. Removed anchors remove trackers. The core raycast handles
simple non-convex polygons in either winding. `live_hit_test(view_pixels)` uses a
native ExistingPlaneGeometry query and returns generation, source timestamp, query
ID and the packet frame ID when that native frame has been published (otherwise 0).
It is a live query; copied packet polygons remain the placement API.

`get_diagnostics()` reports device/session, projection and texture-retirement
details, plus `thermal_state` (`NSProcessInfo.thermalState`, 0 nominal to 3 critical); the ARKit
step thresholds live in `demo/ar/ios_ar_harness.gd`.

Use the iOS 17+ lane from ci4's GUI session after building/signing the exported app:

```bash
IOS_DEVICE=<udid> python3 tools/ios-device.py preflight
IOS_DEVICE=<udid> python3 tools/ios-device.py install /path/to/signed.app
IOS_DEVICE=<udid> tools/harness.sh ios-ar -harness-duration 60
IOS_DEVICE=<udid> tools/harness.sh ios-ar -harness-duration 1800
```

See `docs/ios-device.md` for the iOS transport, run-log, capture and cleanup protocol.

One-time iPhone 12 setup, performed by its owner: finish Setup Assistant without a
passcode, Face ID, Apple ID or Workspace account; connect to ci4 and tap Trust;
set Auto-Lock to Never; disable automatic OS updates; enable Developer Mode and
complete its restart/confirmation; enable Settings > Developer > UI Automation;
set Privacy & Security > Wired Accessories to Always Allow. Silence notifications
with a Focus. If a passcode is required, a human must unlock after every reboot.
Register the UDID with signing team L7G7TF62MC during the first Xcode device build.
After installing, launch the demo once and tap Allow on the camera prompt. The
harness reports not-determined, restricted or denied authorization explicitly.

The iPhone 7 Plus (A10/iOS 15) cannot run this iOS 16+/Metal target; do not lower
the certified preset.

## Tracking reason semantics

| Reason | ARCore | ARKit |
| --- | --- | --- |
| NONE | Normal tracking or initialization with no failure reason | Normal, Initializing, Relocalizing, NotAvailable after run; default after run |
| BAD_STATE | Native bad-state failure | World-tracking failure or unknown native reason |
| INSUFFICIENT_LIGHT | Native insufficient-light failure | Not emitted |
| EXCESSIVE_MOTION | Native excessive motion | Limited(ExcessiveMotion) |
| INSUFFICIENT_FEATURES | Native insufficient features | Limited(InsufficientFeatures) |
| CAMERA_UNAVAILABLE | Camera cannot be used | Session interruption |

Initializing and Relocalizing report `XR_UNKNOWN_TRACKING`; excessive motion
reports `XR_EXCESSIVE_MOTION`.

## Class reference and diagnostics

The game API contract is in `docs/api.md`; onboarding and public-engine setup
are in `README.md`. Native class help lives in `doc_classes/*.xml` and is embedded
by `GodotCPPDocData` only for editor/template_debug targets. Generated C++ stays
under `build/`. ARView and the addon scripts use `##` comments for editor help.
Generate native signatures with `godot --headless --path demo --doctool <scratch-dir>
--gdextension-docs`; this lists only classes registered on the running platform.
Keep mobile-only XML when refreshing docs on macOS.

ARCore and ARKit default to `set_diagnostics_enabled(false)`. Enable it on the
interface for projection, pose and UV oracles, CPU image layout/luma sampling and
plane/point-cloud reprojection checks. Native device harnesses enable it;
ordinary demo runs leave it off. Disabling collection preserves previous samples;
format/lifetime/error guards and path counters remain active. ARCore GPU readback
requires the separate `set_gpu_validation_enabled(true)` opt-in.
Timing percentiles cover the latest 512 observations per series, about 17 seconds
at 30 samples per second. Statistics sampling times are collected only with
diagnostics enabled.

The demo enables material debanding. The desktop harness allows eight byte levels
for dithered in-scene camera landmarks and MSAA edges, retaining the stricter
undithered/isolated bounds. `camera-precision` measures pointwise and column-mean
error with material and viewport dithering independently.

## Camera upload settings

For ARCore CPU uploads, use
`rendering/rendering_device/staging_buffer/block_size_kb=4096` and
`rendering/rendering_device/staging_buffer/texture_upload_region_size_px=1024`.
A region must fit in one staging block. The demo sets both; the library reads and
reports the effective values but never changes a game's settings. It warns once
per session start if the region is below 512 pixels.

Both device suites run `tracking` before `lifecycle`; the shared packet-retention
check and its thresholds live in `demo/ar/packet_retention.gd`. `tracking` also samples
the screen centre 20 times, 0.1 s apart, and reports `native_agreement`: the distance
between `ARView.raycast` and the platform's own `live_hit_test` on the same plane
(median, max, sample count, plane mismatches). It is INFO; the two hit tests can read
frames one apart while the camera moves.

## Scripted sessions and the game facade

`ARView.plane_finding_mode` can be set before startup or while running. Its
`PocketAR.PlaneFindingMode` values are `PLANE_FINDING_DISABLED=0`,
`PLANE_FINDING_HORIZONTAL=1` (upward and downward), and
`PLANE_FINDING_HORIZONTAL_AND_VERTICAL=3` (default). The property reads the
backend immediately; all four backends expose `set_plane_finding_mode()` and
`get_plane_finding_mode()`. The selected mode survives pause/resume and reset.
Reset still clears planes and changes generation, even when detection is disabled.

ARCore changes only plane finding on the existing session config and calls
`ArSession_configure`, without pausing, recreating the session or selecting a
camera config. It reads the config back with `ArSession_getConfig` and
`ArConfig_getPlaneFindingMode`. A rejected change leaves the previous mode active.
The vendored 1.56.0 C header says only that disabled turns off plane detection;
it does not specify whether existing planes stop updating or change tracking state.
PocketAR keeps existing native plane handles and copies their reported state;
native STOPPED planes are still removed. Disabling does not clear the tracker set.

ARKit changes `planeDetection` on its current `ARWorldTrackingConfiguration`
and re-runs with zero options. Tracking and existing anchors continue; disabling
plane finding does not request anchor removal or tracking reset. Plane discovery
and geometry refinement are disabled, while PocketAR continues to copy native
anchors and derive their tracking state from camera tracking. Both native backends
report the selected `plane_finding_mode` and read-back `effective_plane_finding_mode`
in diagnostics. The effective field is absent before a native config exists;
on paused ARKit it describes the last run until resume applies a pending change.

Scripted replay freezes existing plane snapshots while disabled, including their
tracking state, and adds no new planes. Horizontal mode keeps existing vertical
planes frozen and excludes new ones. Re-enabling applies the latest recorded plane
set on the next fixture frame, including when that frame omits `planes`. Reset
discards that remembered set. Fixture version 1 already expresses additions and
geometry changes, so no format extension is needed. Synthetic produces no planes
and remembers the mode across reset and stop/start.

This setting does not change how placed content is held. PocketAR exposes plane
snapshots and raycasts, but has no game-facing anchors for placed content.

Both native harnesses include `plane-finding`. It cycles horizontal, disabled and
both alignments, verifies effective modes, continued frames and unchanged generation
and camera config, then restores the original mode. It reports before/after plane
counts and saves snapshots to `plane-finding.json`. With no observed plane changes
after re-enabling it reports INFO; exercise discovery by moving over textured surfaces.

`ARView.backend` values are Synthetic=0, ARCore=1, ARKit=2, Scripted=3. Supply
a loaded `ScriptedARInterface` through `ARView.interface` before adding a Scripted
view to the tree. `PocketAR.get_backend()`
reports the primary interface: `synthetic`, `scripted`, `arcore`, `arkit`, `none`
when there is no primary, or `unknown` for another provider.

All four backends expose `reset()`, `get_session_state()`,
`get_tracking_reason()` and `get_session_generation()`. Games use `ARView.reset()`
and the read-only `session_state`, `tracking_reason` and `session_generation`
properties. Properties query the backend immediately. ARView forwards `session_state_changed(state)`,
`tracking_reason_changed(reason)`, `session_generation_changed(generation)` and
`planes_changed()`. Backend notifications are deferred in order until the current
native operation finishes, so a signal handler can safely reset the session.
State and reason signals fire on value changes. Generation
IDs remain process-wide unique, so replay comparisons normalize generations
relative to each run rather than expecting the same absolute ID. Plane changes
refer to the current snapshot/tracker set; placement uses the presented packet.

`PocketAR.SessionState` is the shared enum: UNSUPPORTED=0, CHECKING=1,
NEEDS_INSTALL=2, INSTALLING=3, PERMISSION_DENIED=4, READY=5, INITIALIZING=6,
TRACKING=7, PAUSED=8, FAILED=9. `PocketAR.TrackingReason` is NONE=0, BAD_STATE=1,
INSUFFICIENT_LIGHT=2, EXCESSIVE_MOTION=3, INSUFFICIENT_FEATURES=4,
CAMERA_UNAVAILABLE=5.

- Synthetic reports TRACKING/NONE while initialized and PAUSED when stopped.
  Reset immediately changes generation and invalidates presentation.
- Scripted reads state/reason names from the fixture, with reset and resume
  entering INITIALIZING/NONE and pause entering PAUSED.
- ARCore reports availability, install, permission and tracking states. A reset
  while the bridge is pending reports that pending state;
  a ready but suspended bridge reports PAUSED until resume can create a session.
- ARKit reports normal tracking as TRACKING, limited/not-available tracking as
  INITIALIZING, interruption/suspension as PAUSED, authorization denial as
  PERMISSION_DENIED, unsupported hardware as UNSUPPORTED and session failure as
  FAILED. Its reason mapping remains in the table above. Limited ARKit poses may
  be valid while plane trackers are paused; ARCore poses require native TRACKING.

Fixture version 1 is UTF-8 JSON lines with one object per line; string escapes
are rejected. Its first line contains `format: "pocketar-session"`, `version: 1`, `semantics: "arkit"` or
`"arcore"`, `image_size: [width,height]`, and
`intrinsics: {"fx":520,"fy":510,"cx":320,"cy":240}`. Intrinsics are in
physical camera-image pixels. World units are metres; camera axes are +X right,
+Y up, -Z forward. Transforms are rigid world-from-local matrices encoded as 16
column-major numbers. Camera transforms describe the physical image-camera basis,
prior to display rotation. Plane polygons are local XZ pairs with +Y normal;
extents are full lengths. UV coordinates use the viewport's top-left origin.

Integer fields use decimal integer tokens without fractions or exponents.
Subsequent records have nonnegative integer `t_usec`, ordered nondecreasingly;
records at the same time execute in file order. Frame IDs increase across resets.

- Frames contain `frame_id`, `camera_transform`, `tracking_valid`, `session_state`
  and `tracking_reason`. Enum names are lowercase with underscores. Optional
  `planes` replaces the full set; omitting it preserves the previous set.
- Each plane contains `id`, `alignment`, `transform`, `polygon`, `extents`,
  `tracking_state` and `subsumed_by`. Alignment names are
  `horizontal_upward_facing`, `horizontal_downward_facing`, `vertical`; tracking
  names are `tracking`, `paused`, `stopped`. A subsumption target must occur in the
  same plane set; zero means no subsumption.
- Events contain `reset: true` or `lifecycle: "pause"` / `"resume"`.
- Inputs contain `kind: "down"` / `"move"` / `"up"` / `"cancel"`, a nonnegative
  `finger`, normalized `uv: [x,y]` and `target_frame` naming a fixture frame.

`load_fixture(path)` validates the entire file and returns an Error; malformed
records report their line number and leave the existing fixture unchanged.
`advance_to(t_usec)` applies all reached records and publishes the newest reached frame. Time cannot move backwards; reload to rewind.
Call `advance_to(0)` before initialization. Time moves only through `advance_to`.
Geometry changes rebuild projection and display transform from recorded intrinsics and physical
pose even when the frame repeats. Camera pixels use the synthetic bars.

`reset()` keeps the fixture cursor, changes generation and clears packets and
trackers. Planes remain absent until a later frame supplies `planes` again.
Trackers use `pocketar/plane/<generation>/<id>`. `get_pending_inputs()` drains the
reached inputs, with `uv` returned as Vector2 and `session_generation` attached.
The driver waits until `target_frame` is presented, multiplies UV by ARView size,
and transforms that local point into viewport coordinates before constructing
Godot touch/drag events. The library never injects inputs itself. Generated headers
include `view_size: [width,height]` and `quarter_turns`; the driver checks these
against the running view and presented packet before injecting inputs.
`--view-size W H` defaults to 640×480. The generator chooses quarter-turn 1 for
portrait, 0 otherwise. Generated UVs include the target
view's centred aspect-fill crop and rotation; cropped-out targets are rejected.
Generated physical camera poses compose the path's yaw with a local-Z roll of
`-quarter_turns * 90°`. The backend's positive display rotation cancels that roll,
so world up stays screen up in every orientation, including on the orbit path.
The recorded transform remains in the physical image-camera basis.

Generate the desktop suite fixture with:

```sh
python3 tools/fixtures.py --output demo/harness/fixtures/table.jsonl \
  --loss-at 1 --reset-at 1.5 --pause-at 1.75 --tap-at 0.6 \
  --drag-to 0.1 -0.7 -2 --plane-examples
```

`--table-height` sets metres below the camera; `--camera orbit` or `static` chooses
its path. Each `--tap-at` projects the table centre. Fixtures use ARKit semantics,
run for 2 seconds at 30 fps, and use 0.25-second losses and 0.1-second pauses.
The default camera is at the origin and the table centre is `(0,-0.7,-2)`.
The `scripted` desktop harness step runs each landscape and 1080×1920 portrait
fixture twice, with an explicit deferred flush after each time advance. The headless companion
checks the same state, placement and input behavior without claiming GPU coverage.

`ARView.raycast(point, alignments=PocketAR.ALIGNMENT_MASK_ALL)` filters hits by a bit
mask: `ALIGNMENT_MASK_HORIZONTAL_UPWARD=1`,
`ALIGNMENT_MASK_HORIZONTAL_DOWNWARD=2`, `ALIGNMENT_MASK_VERTICAL=4`,
`ALIGNMENT_MASK_ALL=7`. Each `ARRaycastHit`
exposes the plane's `alignment` (the existing ARPlaneTracker enum, values 0–2).
`world_to_screen(world_point)` returns ARView-local pixels from the presented
packet. It returns Vector2.INF when the packet/input cannot be used; world points
behind the camera are invalid.
`view_point(event_position)` converts viewport event positions with the inverse
of `get_global_transform_with_canvas()`, including canvas scaling and rotation.

`ARPlaneVisualizer` is a Node3D with an `ar_view` export; use `visible` to hide it.
Place it in `view.scene_root`; it draws applied-packet polygons and excludes old
session generations, paused/stopped planes and subsumed aliases. The demo uses
this addon node instead of maintaining its own meshes.

## Application arguments

`PocketARArgs.read()` is the addon argument helper. It returns a Dictionary with
`args: PackedStringArray` and `error: String`. Desktop and Android use
`OS.get_cmdline_user_args()`. Debug iOS builds read the `POCKETAR_ARGS` JSON array
when present; malformed JSON/non-string elements return a transport error. Release
iOS ignores that environment variable. Harness-specific validation and capture
cleanup stay in the demo's `ar/harness_args.gd`.

Android accepts two string-array intent extras in debuggable builds only, guarded
by `ApplicationInfo.FLAG_DEBUGGABLE`: `pocketar_harness_args` retains the harness
allowlist, and `pocketar_app_args` carries application-owned flags. Both use the
existing comma/backslash escaping and are appended after one `--`; app-only
launches work too. Every app entry must be nonblank, have no leading/trailing
whitespace, and must not start with `--`; game flags use a single dash. A malformed
app extra is logged and discarded in full while valid harness arguments survive.
Applications validate the remaining flag names and values. Invalid harness extras
still reject the launch arguments. Release builds ignore both extras.

## Debug plane

Debug interfaces expose `set_debug_plane(transform: Transform3D, extents: Vector2) -> int`
and `clear_debug_plane()` for unattended device runs. One upward horizontal rectangle
uses tracking-space metres and full-length extents. Finite rigid transforms are required;
errors in squared column lengths, orthogonality, determinant and the upward normal
must be at most 1e-5.
A successful set or replacement returns a process-unique negative ID; rejection returns
zero and preserves the previous plane. Call on the owner thread.

The marked `debug` snapshot follows backend planes in packets and participates in
polygon raycasts, XRServer trackers and ARPlaneVisualizer. It follows camera tracking;
limited/lost tracking and pause invalidate it for live raycasts. Set and clear reach
the next published packet. Reset, generation changes and uninitialization remove it
synchronously. Retained packets keep their copied geometry. Plane finding mode does
not disable injection. Native queries, handle/UUID maps, diagnostic counts and native
recording/playback remain native-only; datasets do not serialize the debug plane.
Keep virtual-plane runs separate from dataset and native-agreement checks.

Bindings, state, implementation, snapshot marker and publication hooks are excluded
from `template_release` by `DEBUG_ENABLED`. After building both library variants,
run `python3 tools/probe-debug-plane.py macos --output <empty-directory>` for exported
main-scene debug/release controls and packaged-library SHA-256 comparisons. The same
command with `android` or `ios` exports phone probes without launching a device;
`--prepare-only` stages projects without exporting. Android needs both AARs and release
signing credentials. iOS needs both XCFrameworks. Probe package IDs end in `.apiprobe`.

Each export records `native-hashes.json`; the iOS record also checks the Xcode target's
framework link. Build/sign that Xcode project with `LD_GENERATE_MAP_FILE=YES`, retain
the link map and signed app SHA-256, and install that app. Launch both phone variants
as their normal main scene and require one `DEBUG PLANE API` JSON result with `PASS`,
the matching platform and debug flag. The result also appears on screen and in
`user://debug-plane-api.json`. Preserve device logs with the export hashes; a staged
archive hash alone does not certify the app running on the phone.
