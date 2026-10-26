# PocketAR

MIT handheld AR for Godot 4.7.2, with ARKit on iOS and ARCore on Android behind
one Godot API. The demo selects the native backend on each phone.

From the repository's devenv shell:

```sh
tools/build.sh macos debug
tools/test-native.sh
tools/harness.sh desktop -harness-out /tmp/pocketar-captures
```

The harness needs a GPU-backed GUI session. Native compilation and headless
script checks alone do not prove rendering correctness, exports or device loads.
See [AGENTS.md](AGENTS.md) for build/export commands, device requirements, source
evidence and the rendering limitations being tested.

ARCore and ARKit default to `set_diagnostics_enabled(false)`. Enable it on the
interface for projection, pose and UV oracles, CPU image layout/luma sampling and
plane/point-cloud reprojection checks. The native device harnesses enable it;
ordinary demo runs leave it off. `get_diagnostics()` reports `diagnostics_enabled`
and accumulated samples; disabling collection preserves previous results. Format,
lifetime and error guards and path counters remain active. GPU readback still
requires the separate `set_gpu_validation_enabled(true)` opt-in on ARCore.

Timing percentiles cover the latest 512 observations in each series, about
17 seconds at 30 samples per second, rather than the whole session. Statistics
sampling times are collected only while diagnostics are enabled.
