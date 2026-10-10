# Milestone 2 validation and iPhone 12 handoff

Implemented in the `godot-handheld-ar-arkit` worktree without commits. No device
operations, signed app builds, exports, installs or phone tests were performed.
Commands used the supplied shell directly; no devenv invocation was needed.

## Changed files

- `src/platform/ios/arkit_interface.{h,mm}` replaces `interface_stub.{h,cpp}` with
  the ARKit owner, polling, orientation/calibration, plane snapshots, live queries,
  permission/lifecycle/reset handling and diagnostics.
- `src/platform/ios/arkit_texture_pool.{h,mm}`, `import_retirement.h` and
  `arkit_math.h` implement CoreVideo/RD imports, the retain probe, bounded ownership,
  swap-based retirement, canaries and independent calibration checks.
- `SConstruct` compiles Objective-C++ with ARC/modules and an in-worktree module
  cache. `src/register_types.cpp` registers the backend and internal retirement
  interface, and stops sessions before releasing the pool at extension shutdown.
- `src/core/ar_frame_packet.{h,cpp}` adds read-only imported-chroma metadata.
  `src/core/ar_plane_geometry.cpp` handles non-convex simple polygons.
- `demo/addons/handheld_ar/ar_view.gd` selects the iOS backend and forwards the
  packet's chroma layout to `camera.gdshader`; the compositing architecture is
  unchanged. `export_plugin.gd` adds iOS framework flags, the camera description
  fallback and AR Required/Optional capability overrides without duplicate plist keys.
- `demo/ar/ar_demo.gd`, new `ios_ar_harness.gd` and `harness_args.gd`, the entry
  routing in `demo/harness/harness.gd`, and iOS file logging in `demo/project.godot`
  provide the device harness and validated debug environment arguments. Fix round 3
  adds its eighth step, `packet-retention`.
- New `tools/device.sh` and `tools/ios-device.py`, plus the `ios-ar` branch of
  `tools/harness.sh`, provide preflight, install, detached launch, termination and
  individual file pulls. The existing synthetic `ios` transport is untouched.
- Native tests add ARKit orientation/depth/UV and Q=2,3,4 retirement cases and extend
  plane cases. `tools/test-native.sh` runs them. New host-tool and headless argument
  fixtures cover launch encoding, preflight, authoritative logs and input validation.
- `AGENTS.md`, `docs/ios-device.md` and `docs/milestone-2-plan.md` document the
  implementation, operating rules, setup and scoped plan.

## Offline verification

The iOS arm64 debug and release static archives compile with the iOS 26.2 SDK,
minimum deployment target 16.0 and Xcode's compiler:

```bash
TMPDIR=/tmp scons platform=ios target=template_debug arch=arm64 api_version=4.7 \
  ios_min_version=16.0 IOS_TOOLCHAIN_PATH="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain" -j8
TMPDIR=/tmp scons platform=ios target=template_release arch=arm64 api_version=4.7 \
  ios_min_version=16.0 IOS_TOOLCHAIN_PATH="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain" -j8
TMPDIR=/tmp tools/xcframework.sh template_debug
TMPDIR=/tmp tools/xcframework.sh template_release
```

Both device-only static XCFrameworks are generated under
`demo/addons/handheld_ar/bin/ios/`. They include the existing bundled godot-cpp
archive. Both plists contain one arm64 iOS slice and a BinaryPath ending in `.a`;
the native object reports minimum iOS 16.0/SDK 26.2 and the archive defines the
extension entry symbol. Packaging is not a signed app link or a device build.

The macOS arm64 debug extension compiles. `TMPDIR=/tmp tools/test-native.sh` passes
73,260 projection/ray checks, 55 plane checks, ARKit orientation/intrinsics/UV/depth
and aspect-override checks, imported-retirement Q=2,3,4/stall boundaries, and the
existing YUV suite.
The new concave-polygon test failed against the old containment implementation
before the fix. The retirement test covers scheduling arithmetic and the sampling
state transition; it cannot exercise a Metal fence or a CoreVideo pool.

`python3 -B -m unittest discover -s tests/tools` passes 20 tests. The added failure
cases verify that an already absent PID is handled and termination errors do not
prevent final evidence retrieval. Shell syntax and `git diff --check` pass.

Godot headless packet smoke and the iOS argument fixture pass. Explicit parser
checks for the iOS harness, AR demo, entry harness and export plugin exit 0 without
script errors. Headless checks cannot validate rendering or the native iOS classes.

The initial editor import exited 134 with SIGSEGV during shutdown, after scanning
the scripts. Its log also reports sandbox-denied editor-settings writes and an
unavailable adb-daemon connection; those messages do not establish the crash's
cause. Separate runtime fixtures and explicit parser checks pass. Runtime fixtures
report the known sandbox system-certificate error. Native linking warns that the
macOS godot-cpp archive targets a newer SDK than the test executable. XCFramework
creation reports unavailable CoreSimulator services but completes successfully.

## Choices that differ from brief recommendations

1. The task explicitly requires direct Y/CbCr `Texture2DRD` imports. The GPU brief's
   preferred one-frame RGB conversion pass and stable custom Texture2D wrappers
   are not used. Fix round 3 retires textures independently of script-held packet Refs. Only
   current, presented and ARView-applied textures are protected; retained packet
   values stay valid after their textures retire. Safe mode makes Texture2DRD's change notifications stay on the main
   render owner. The separate zero-view retirement interface lets cleanup continue
   after the primary camera interface is removed.
2. Safe rendering mode and identity XROrigin/reference-frame/unit-scale remain the
   shared library limits. Separate-thread queued activation and arbitrary XR world
   transforms from the broader brief are deferred with the parent architecture.
   Existing synthetic/ARCore threading guards were not changed in this worktree.
3. The cache flushes after swaps that actually release retired owners, instead of
   every swap or never. This bounds idle cache churn while providing periodic
   cleanup. Its cost and pool behavior still need measurement.
4. Application notifications explicitly pause/run, as requested, overriding the
   ARKit brief's advice to rely only on automatic interruption. Interruption
   callbacks themselves do not pause. AVFoundation completion never captures the
   interface; the next main-thread poll reads authorization, rather than dispatching
   an interface callback from the completion block. Relocalization defaults on and
   reports a deferred timeout after 30 seconds; it does not reset automatically.
5. Only the requested full-range BT.601 two-plane input is accepted. Video-range,
   BT.709 and 10-bit inputs are rejected with a reason instead of implementing the
   brief's additional colour matrices. HDR video is explicitly disabled.
6. The retain probe is authoritative on affected and fixed engines. No force-on/off
   override is exposed: forcing a retain despite a positive probe would contradict
   the task's requirement and can leak the capture pool. Engine version/hash and
   probe delta remain available for field diagnosis.
7. Reset uses the strict host-time cutoff to reject old frames. It records the first
   ten post-reset reads, including tracking states and anchor counts, so the phone
   can establish whether an initializing-state criterion is also reliable. Requiring
   that state now could stall forever if it occurs between polls. Live results tag
   their source timestamp; frame ID is 0 when that source has no published packet.
8. Packaging retains milestone 1's combined archive. The brief's optional private
   `ld -r` isolation of godot-cpp, SDL weak stubs and simulator slice are not added;
   there is no verified final-app linker failure or second-extension requirement
   here to justify those changes. Multiple static extensions need a coexistence test.
9. The modern AR transport is a separate `ios-ar` lane because the parent branch
   owns the existing iOS separator/log-pull fix. The pulled file is authoritative;
   no stdout logger, console mirroring or PTY stream is added. No full-screen grab
   dependency is installed. The legacy phone is documented rather than given a
   new iOS 15/Compatibility build target, as requested.

The user-assigned parent changes remain outside this diff: synthetic/ARCore
upright-projection corrections, effective-mode guard changes, transparent-black
enforcement, their camera-transform harness checks, export preset cleanup, and
the original iOS harness transport fix.

## First iPhone 12 checks

After the one-time setup in AGENTS.md, register/sign the debug app with team
L7G7TF62MC, then run the preflight and 60-second lane from ci4's GUI session.
Inspect the exported plist for one camera-description key and one capabilities
array, check Optional/Required modes, and confirm the final app links all four
frameworks and loads the extension once. Those steps have not run here.

1. Check the retain probe first: same native pointer, delta 0 on affected 4.7.2,
   positive delta on a fixed 4.8 build. Under Metal API Validation, verify the
   IOSurface R8/RG8 shared views work and show correct colour through `.ra`. Run a
   120-second import loop, then 30 minutes while monitoring memory and pool drops.
2. Run Q=2,3,4. Correlate free_swap with actual driver texture destruction at s+Q;
   require growing retirement/release counts and zero canary violations. Retain a
   few packets/textures deliberately, remove/recreate ARView, reset during motion,
   and ensure packet values survive while old texture wrappers clear. Inject a greater-than-one-second GPU stall under
   validation. The 900ms extension is a mitigation, not a repair of Godot's ignored
   fence timeout, and the CPU canary cannot prove GPU completion after that timeout.
3. Physically rotate through all four interface orientations. Check upright
   P00/P11, native finite-far depth values, near/far substitution, pose error below
   1e-4 and projection oracle error below 0.25 pixels. The +0.5 principal-point
   convention versus Apple's actual matrix is an unresolved device assumption;
   the harness deliberately fails rather than silently accepting an offset.
4. Confirm devicectl forwards `--log-file` before Godot initializes logging. The
   debug environment fallback recovers harness flags, but cannot redirect an
   already initialized engine logger. Verify CoreDevice's real JSON fields, unique
   file-log flushing, individual PNG pulls and host termination, including a crash.
5. Test denied/regranted camera permission, real background/foreground interruptions,
   two manual pause/resume cycles, two resets and relocalization. Confirm the host
   clock offset and post-reset samples establish generation isolation. The automatic
   in-app lifecycle step does not prove UIKit/OS callback ordering.
6. Inspect non-convex boundaries and compare copied-polygon placement hits against
   native live hits on a tracked textured surface. Tracking/centre hits on a desk
   remain INFO. The existing front-facing-plane policy is unchanged.

The composite step saves lossless screen and camera-layer PNGs from the same
presented frame, compares 432 camera-only samples, and records diagnostics. It
tests composition consistency, not an independently calibrated physical colour
target. Merge the parent fixes before treating combined harness results as a
milestone completion gate.
