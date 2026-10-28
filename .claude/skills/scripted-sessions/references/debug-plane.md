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
