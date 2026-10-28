# PocketAR

MIT handheld AR GDExtension for Godot 4.7.2+, with ARKit, ARCore and desktop simulation.

## Requirements

Work in the task's worktree. Do not commit unless asked.
Use the repository's devenv for Godot, SCons, Python, Java 17 and the Android SDK.
When already inside its shell, run commands directly; nested devenv calls fail in
the coding sandbox. Xcode supplies the Apple compiler. Keep `apple.sdk = null`
and `stdenv = pkgs.stdenvNoCC`; do not add another C toolchain.

The supported targets are macOS 13+ arm64 (desktop harness), iOS 16+ arm64 on Metal,
and Android API 29+ arm64 on Mobile/Vulkan. `godot` is PocketAR's engine: Godot
master plus the patches in doublesymmetry/godot's `PATCHES.md`, pinned to a release of
that fork in `nix/godot-pocketar.nix` (editor and export templates). It runs
self-contained from `.devenv/state/godot-pocketar`. Stock Godot 4.7.2 is the oldest
supported engine; `stock <command>` runs a command against it, for example
`stock harness desktop`. Its editor and matching templates run self-contained
from `.devenv/state/godot-stock`. godot-cpp is pinned to `b051e69` with `api_version=4.7`.
Android uses compile/target SDK 36, build tools 35.0.0 (Godot 4.8's Gradle template)
and 36.1.0, NDK 29.0.14206865, Kotlin 2.1.21 and Java 17. `SConstruct` derives `IOS_TOOLCHAIN_PATH` from `xcode-select -p`, so Xcode need
not be named `Xcode.app`. iOS signing team: `L7G7TF62MC`. Both harness package IDs:
`org.doublesymmetry.pocketar.demo`.

The certified iOS deployment target stays 16.0 in SConstruct and the preset.
`scons platform=ios ios_min_version=15.0` is a diagnostic override only; it does
not certify iOS 15 or make an A10 device support the Metal renderer.

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
| Installed iPhone harness | `IOS_DEVICE=<id> harness ios` |
| Android harness installation and run | `ADB=<host-binary> ANDROID_SERIAL=<serial> harness android` |
| Native math, retirement, YUV/GPU and fixture parser tests; Python fixture tests | `TMPDIR=/tmp tools/test-native.sh` |
| Headless packet, facade, scripted, arguments, exporter and thread tests | `tools/test-headless.sh` |
| ARCore native lifecycle/status, GPU completion lifetime and query/recording fixtures | `TMPDIR=/tmp tools/test-arcore.sh` |
| All Python tool tests | `python3 -m unittest discover -s tests/tools` |
| Kotlin launch argument and AR request tests | `gradle -p android :plugin:testDebugUnitTest` |

`JOBS` controls build concurrency, default 8. `ARCH` defaults to arm64; macOS can
also build x86_64. `build all` stops on the first failed target. Build both mobile
variants before both exports. Gradle's wrapper downloads its distribution and
dependencies on first use. Godot's matching export templates must be installed;
`tools/export.sh android` installs their Gradle build template into the harness project automatically.
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

`harness/` is the test project; `samples/tap_to_place/` is the example game.
Source checkouts contain no compiled libraries. Build before opening either project.

- `src/core/`: frame packets, simulated XR providers, shared singleton, projection and ray math.
- `src/platform/android/`: ARCore session owner, JNI bridge, CPU upload, GPU camera submission and live queries.
- `src/platform/ios/`: ARKit session, Metal imports and swap-based retirement.
- `harness/ar/`: runtime-selected AR demo and device diagnostics.
- `thirdparty/arcore/`: C API header and provenance.
- `harness/addons/pocketar/`: ARView, shaders, extension descriptor, plane visualizer and editor export plugin.
- `harness/addons/pocketar/bin/`: generated libraries, XCFrameworks and AARs; ignored by Git.
- `harness/suites/`: rendering assertions and scripted replay fixtures.
- `samples/tap_to_place/`: standalone game example, with a relative symlink to the addon and its own desktop table fixture.
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
and `SceneRoot`. A CanvasLayer holds ordinary HUD Controls above both.
The demo and rendering harness opt in to `handle_presses`.
See the [game API](docs/api.md#game-api),
[input and frame contracts](docs/api.md#frames-signals-and-lifetime), and
[composition](README.md#composition).

Packet lifetimes and clocks follow [the frame contract](docs/api.md#frames-signals-and-lifetime).
`texture_refusals` counts refused acquisitions. Setters affect the next acquisition;
geometry/calibration changes acquire on the next XR process. Synthetic orientation
uses clockwise quarter turns: automatic selects 0 for landscape and 1 for portrait;
explicit 0–3 exercises reverse orientations.

Synthetic projection
uses upright intrinsics rotated into display space, with positive diagonal focal
terms for Godot's LOD and shadow calculations. The quarter turn belongs to the
display-oriented camera pose; `set_pose()` and `physical_camera_transform` remain
in the physical image-camera basis. Rays invert the full projection, never camera half-extents.
Calibration, UVs and coordinate conventions follow [ARFramePacket](doc_classes/ARFramePacket.xml).

The current packet is applied on `SceneTree.process_frame` before node `_process`
callbacks, with another check at `frame_pre_draw` for packets replaced later in
the frame. It becomes the presented packet at `frame_post_draw`. Touch rays use
the presented packet. This backend requires Godot's effective Safe rendering mode: initialization must run on both
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

Keep the outer viewport SDR. `IN_SCENE` uses an opaque camera quad at exact
reverse-Z zero, reserves material priority 127 and needs material debanding.
Keep decals away from its near-plane proxy. `ISOLATED_CAMERA` needs
`blend_premul_alpha`; `blend_mix` would multiply coverage twice.
For shader, projection or composition changes, load the
[rendering reference](.claude/skills/scripted-sessions/references/rendering.md),
including the Godot source evidence, and run the rendering harness in both modes.
Headless cannot render; use a logged-in GUI session on an Apple Silicon Mac.

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
Generate native signatures with `godot --headless --path harness --doctool <scratch-dir>
--gdextension-docs`; this lists only classes registered on the running platform.
Keep mobile-only XML when refreshing docs on macOS.

Native harnesses enable diagnostics; ordinary demo runs leave them off.
Collection contracts are in [ARCoreInterface](doc_classes/ARCoreInterface.xml) and
[ARKitInterface](doc_classes/ARKitInterface.xml); GPU readback requires ARCore's
separate opt-in. Statistics sampling times are collected only while diagnostics
are enabled.

## Repo skills

Load the relevant skill before changing or validating these areas:

- [arcore](.claude/skills/arcore/SKILL.md): Android session/JNI work, CPU/GPU camera paths, exports and device diagnostics.
- [arkit](.claude/skills/arkit/SKILL.md): iOS sessions, orientation, Metal ownership, exports and iPhone diagnostics.
- [scripted-sessions](.claude/skills/scripted-sessions/SKILL.md): simulated sessions, fixtures, the game facade, rendering/harness checks, launch arguments and debug planes.

Both agents use these files: `.agents/skills` is a relative symlink to `.claude/skills`.
