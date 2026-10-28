# Android builds and diagnostics

Use `$ADB`, never a bare `adb` command. The devenv client version may differ from the
host's running server. Set `ADB` to the matching host binary and set
`ANDROID_SERIAL` to the test phone's serial. Unlock it before a harness run,
retrieving any PIN from your password manager without printing it. Disable
Godot's `export/android/shutdown_adb_on_exit` to keep exports from stopping the
adb server.

The adb version rule also applies to scripts and manual one-liners. Always use
`"$ADB"`. `harness android` installs the harness project, launches its exported
`GodotAppLauncher` alias and filters logcat by its PID. The underlying GodotApp
activity is not exported in 4.7.2. Debug captures are retrieved through `run-as`
into `build/harness/<platform>/android-userdata.tar`; release builds cannot use
`run-as`. Android runs clear `files/harness` before launch. Host logs live at
`build/harness/<platform>/latest.log`, with separate `android` and `android-ar`
directories and `desktop` used for the `macos` alias.

Build native code inside the existing shell, without running Gradle:

```bash
scons platform=android target=template_debug arch=arm64 api_version=4.7 ndk_version=29.0.14206865 -j8
scons platform=android target=template_release arch=arm64 api_version=4.7 ndk_version=29.0.14206865 -j8
```

ARCore header provenance, updates and single-copy shim packaging are documented
in [thirdparty/arcore/README.md](../../../../thirdparty/arcore/README.md).

After building/exporting a debug APK and checking/unlocking the test phone:

```bash
ADB=<host-binary> ANDROID_SERIAL=<serial> tools/harness.sh android-ar -harness-duration 60
ADB=<host-binary> ANDROID_SERIAL=<serial> tools/harness.sh android-ar -harness-duration 1800
```

`android-ar` installs the selected APK (`APK` overrides its path) and launches the
AR scene with validated debug-only arguments; step/result checks live in
`tools/run-harness.py`. `-harness-steps` selects a subset and `-harness-out` changes
the capture directory. Real Android home/relaunch cycles remain necessary to
verify OS callback ordering.

To launch the plain AR demo from an installed debug APK, after checking and
unlocking the test phone, use the same host `$ADB` and `ANDROID_SERIAL`:

```bash
"$ADB" shell am force-stop org.doublesymmetry.pocketar.demo
"$ADB" shell am start -W -n org.doublesymmetry.pocketar.demo/com.godot.game.GodotAppLauncher --esa pocketar_harness_args -ar-demo
```

`-ar-demo` selects `ar_demo.tscn` without attaching the device harness. Scene
selection happens in GDScript because official templates disable path overrides.

The ARCore steps are `arcore-session`, `no-gl`, `camera-image`, `gpu-vs-cpu`,
`projection-form`, `display-transform`, `composite`, `packet-retention`, `lifecycle`,
`tracking` and `plane-finding`; thresholds live in `harness/ar/ar_harness.gd` and details in
`diagnostics.json`.

HARNESS and ARCORE lines stay below 1000 bytes. Step lines contain summaries;
full camera configs and native diagnostics are in `diagnostics.json`, and device
telemetry is in `device-<phase>.json`. Plane layouts log one plane per line. The
host monitor polls the app PID and fails promptly if it exits before a result.

Rotate through all four physical orientations, including reverse orientations,
to exercise the DisplayListener; clock offsets remain in `diagnostics.json` because
the ARCore timestamp source is unestablished. Run separate config-index sessions
for VGA/high-resolution thermal and bandwidth comparisons.

Native queries, point clouds and datasets follow
[advanced and internal surfaces](../../../../docs/api.md#advanced-and-internal-surfaces) and
[ARCoreInterface](../../../../doc_classes/ARCoreInterface.xml).
Keep input track UUID `61a97725-8be4-4e8f-a196-6af38ca10735`, version 1, stable.
Repeated playback inputs increment `playback_inputs_repeated`. Dataset tests
require tolerances for poses and plane counts. Keep room recordings private and
out of the repository.

The demo accepts `-harness-camera-config <index>` before session creation;
`-harness-record <name.mp4>` and `-harness-playback <name.mp4>` are mutually exclusive,
basename-only dataset options whose checks live in `harness/ar/ar_demo.gd`.
Keep ordinary live-camera runs separate from dataset runs.

Device telemetry is saved to `device-<phase>.json`; USB charging affects power
comparisons, and native lifecycle checks do not measure Android callback dispatch.
