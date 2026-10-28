# iPhone diagnostics

`get_diagnostics()` reports device/session, projection and texture-retirement
details, plus `thermal_state` (`NSProcessInfo.thermalState`, 0 nominal to 3 critical); the ARKit
step thresholds live in `harness/ar/ios_ar_harness.gd`.

Follow [docs/ios-device.md](../../../../docs/ios-device.md) for iOS preflight, installation
and harness transport. Run `ios-ar` with `-harness-duration 60`, then
`-harness-duration 1800`.

Before an iOS run, connect a supported iPhone to the Mac and tap Trust; enable
Developer Mode and complete its restart/confirmation. Its owner must unlock it,
including after every reboot, and keep it awake during the run. Enable Settings
> Developer > UI Automation when the run needs automated interaction.
Register the UDID with signing team L7G7TF62MC during the first Xcode device build.
After installing, launch the demo once and tap Allow on the camera prompt. The
harness reports not-determined, restricted or denied authorization explicitly.
