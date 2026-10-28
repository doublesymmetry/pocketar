# Harness checks

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

Headless cannot render. Run the desktop harness through a logged-in GUI session
on an Apple Silicon Mac. It reads lossless viewport images after
`frame_post_draw`, saves every check's PNGs, and prints exactly one
`HARNESS RESULT PASS|FAIL`. `user://harness/` is the default capture directory;
`-harness-out` overrides it. `-harness-timeout` defaults to 240 seconds and the
host monitor defaults to 300 (`HARNESS_TIMEOUT`). A script error, timeout, missing
result or duplicate result makes the host monitor fail. A subset PASS certifies
only the selected steps. Phones run all checks automatically on launch.

Readback can lower measured render rate; consistency thresholds and capture details
live in `harness/suites/harness.gd`.

The harness project enables material debanding. The desktop harness allows eight byte levels
for dithered in-scene camera landmarks and MSAA edges, retaining the stricter
undithered/isolated bounds. `camera-precision` measures pointwise and column-mean
error with material and viewport dithering independently.

Both device suites run `tracking` before `lifecycle`; the shared packet-retention
check and its thresholds live in `harness/ar/packet_retention.gd`. `tracking` also samples
the screen centre 20 times, 0.1 s apart, and reports `native_agreement`: the distance
between `ARView.raycast` and the platform's own `live_hit_test` on the same plane
(median, max, sample count, plane mismatches). It is INFO; the two hit tests can read
frames one apart while the camera moves.

Both native harnesses include `plane-finding`. It cycles horizontal, disabled and
both alignments, verifies effective modes, continued frames and unchanged generation
and camera config, then restores the original mode. It reports before/after plane
counts and saves snapshots to `plane-finding.json`. With no observed plane changes
after re-enabling it reports INFO; exercise discovery by moving over textured surfaces.
