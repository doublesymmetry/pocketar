# API contract

Games use the `res://addons/pocketar/ar_view.tscn` scene, `ARFramePacket`,
`ARPlaneTracker`, `ARRaycastHit` and the `PocketAR` singleton's enums. Native
classes have editor help in editor/debug builds; ARView and ARPlaneVisualizer
have GDScript documentation. Existing names are retained.

## Game API

| Surface | Contract |
| --- | --- |
| ARView startup | `backend`: an `ARView.Backend` (`SYNTHETIC`, `ARCORE`, `ARKIT`, `SCRIPTED`). Choose backend and `composition` before adding to the tree. Composition changes after ready are rejected; live backend replacement is unsupported. |
| ARView configuration | `handle_presses`, `environment`, `plane_finding_mode`; `orientation_quarter_turns` controls simulation (−1 automatic, 0–3 clockwise). Native orientation comes from the device. |
| ARView scene | After ready, put objects under `scene_root`, Controls under `hud`, use `world_3d` for physics/shared cameras, and `camera.near`/`far` for clip distances in metres. The backend owns camera pose/projection. |
| ARView session | Read-only `session_state`, `tracking_reason`, `session_generation`; `reset()` clears planes/presentation and changes generation on an initialized session. It does not remove game nodes. |
| ARView placement | `screen_to_ray(point)`, `raycast(point, alignments)`, `world_to_screen(point)` and `view_point(event_position)` use the conventions below. |
| Packet values | Read-only IDs, timestamps, tracking validity, camera transforms, projection, intrinsics, image/viewport sizes, orientation, display transform and copied plane dictionaries. |
| Plane trackers | Read-only identity, generation, alignment, extents, polygon, subsumption, tracking state and transform. Trackers update in place through XRServer. |
| Hits | Read-only `pose`, `distance`, `alignment`, `plane_id`, `frame_id`, `session_generation`; copied results remain readable after later frames. |
| PocketAR | Session/reason/plane-finding/alignment-mask enums, `get_version()` and `get_backend()`. Explicit-packet math helpers are advanced. |
| ARPlaneVisualizer | Optional sample/debug visualization with `ar_view` and read-only `plane_count`. |

## Coordinates and placement

World units are metres. Camera coordinates are right-handed: +X right, +Y up,
−Z forward. Camera transforms are world-from-camera. `camera_transform`
includes display orientation; `physical_camera_transform` stays in the
physical image-camera basis. Keep XROrigin3D and `scene_root` at identity and XR
world scale at one. One active ARView owns a private world and the primary XR
interface.

Screen arguments/results are pixels local to ARView, with a top-left origin.
`view_point(event.position)` converts viewport/canvas input to those pixels.
`screen_to_ray()` inverts the full presented projection and returns a Dictionary
with `origin` (world near-plane point), `direction` (unit world vector),
`frame_id` and `session_generation`. It returns `{}` without a valid presented
packet, valid tracking, matching generation or positive view size.

`raycast()` returns copied hits nearest first, or `[]` without valid presentation
or a hit. It intersects the front of tracking, unsubsumed plane polygons, not
physics bodies or infinite planes. `alignments` uses `PocketAR.AlignmentMask`
bits; `ARPlaneTracker.Alignment` values are ordinals, not bits. A hit's distance
is measured from the ray's near-plane origin, not the camera centre. Its pose
has +Y along the plane normal and +Z toward the incoming ray projected on the
plane, falling back to the plane's +Z for a perpendicular ray.

`world_to_screen()` returns `Vector2.INF` without valid presentation, for
non-finite input or points on/behind the camera plane. Points outside the view
can return finite coordinates outside its bounds. This does not test occlusion.

Plane polygons are local XZ pairs in metres; +Y is the normal and `extents` are
full X/Z lengths. `transform` maps plane-local coordinates to world space.
Plane IDs are opaque within a generation. XRServer names are
`pocketar/plane/<generation>/<id>`; subsumed trackers remain aliases, with
`subsumed_by` naming the replacement, and are excluded from default visualization
and raycasts. Zero means no subsumption. A tracker is live state; use
`packet.planes` for a copied snapshot. An XR tracker of type `TRACKER_ANCHOR`
here describes a detected plane, not a game-created anchor.

## Frames, signals and lifetime

The backend publishes a current packet, ARView applies it before drawing, and
marks it presented after drawing. Touch placement uses the presented packet.
At camera rates below the render rate a complete packet repeats. Geometry or
calibration changes produce a new packet.

`packet_applied(packet)` fires when ARView applies a different packet, before
draw (also during startup). It does not fire for repeats or a cleared packet.
`screen_pressed(point, ray)` fires for an unconsumed touch/left mouse press
inside the view when `handle_presses` is true; it marks that input handled.
Touch-emulated mouse presses are ignored. Its ray may be `{}` while tracking
is unavailable.

`session_state_changed`, `tracking_reason_changed`,
`session_generation_changed` and `planes_changed` describe current backend
state. Notifications are deferred until the native operation finishes, so
handlers can reset safely; properties query state immediately. State/reason
notifications fire on value changes, and initial startup can publish the
initial state/generation. `planes_changed` is not a presentation notification.
Discard old identities and clear or rebuild placements on generation changes.

Packet pose, calibration, timestamps, IDs and plane values are copied and remain
valid for the packet's lifetime. Native textures have a separate lifetime:

- ARKit and ARCore CPU images are protected while current, presented, or still
  applied to ARView's material. Keeping a script Ref does not extend that protection.
  Retired ARKit Texture2DRD references are cleared; reused ARCore CPU slots can
  display newer pixels.
- ARCore's hardware-buffer path shares one RGB texture. The engine can replace
  its pixels during the next conversion, before the next packet is presented.
  Even a current or presented packet does not freeze that image.
- Simulated packets own their ImageTextures, but games must not rely on that
  behavior for native camera images.

Texture acquisition can be refused while pose, tracking and planes advance
using previous textures. Texture fields and `imported_chroma` are advanced
rendering details, not an image archival API.

`display_transform` maps top-left viewport UV to top-left camera-image UV with
aspect-fill crop and rotation. ARCore GPU packets use `TEXTURE_NORMALIZED` UV
and the GPU texture size in `image_size`; `intrinsics` always describes the CPU
image on ARCore. Its `fx`, `fy`, `cx`, `cy` are pixels in
`intrinsics.image_size`, which can differ from `image_size`. The two streams can
have different crops and fields of view. Projection uses OpenGL NDC depth
[−1,+1]; Godot applies device depth/Y corrections.

Timestamps are backend clock values in microseconds/nanoseconds, not wall time.
Do not compare them across backends or to an assumed host clock. Synthetic time
uses `Time.get_ticks_usec()`; scripted time advances explicitly.

## Session controls

`PocketAR.SessionState` distinguishes `UNSUPPORTED`, `CHECKING`, `NEEDS_INSTALL`,
`INSTALLING`, `PERMISSION_DENIED`, `READY`, `INITIALIZING`, `TRACKING`, `PAUSED`
and `FAILED`. `READY` means prerequisites are satisfied, not that a pose exists.
Reasons are `NONE`, `BAD_STATE`, `INSUFFICIENT_LIGHT`, `EXCESSIVE_MOTION`,
`INSUFFICIENT_FEATURES` and `CAMERA_UNAVAILABLE`. `NONE` can also accompany
initialization. ARKit limited poses may be valid while plane tracking is paused;
ARCore valid poses require native tracking. Consult packet `tracking_valid`
when working directly with packets.

`plane_finding_mode` accepts disabled, horizontal (both upward/downward) or
horizontal-and-vertical. It persists across reset/pause. Disabling discovery
preserves existing planes: ARKit stops discovery/refinement, ARCore continues
copying the native states, and Scripted freezes retained snapshots until
re-enabled. It neither freezes game objects nor creates anchors.

## Advanced and internal surfaces

`ARView.interface` is the advanced access path. Calls belong on Godot's main
thread in Safe rendering mode. ARCore exists only on Android and ARKit only
on iOS; use `ClassDB.class_exists()`/`instantiate()` or dynamic calls from shared
scripts. ARView owns initialization and shutdown. Backend-specific diagnostics,
live-query results and test controls are outside the stable game contract.

| Surface | Intended use |
| --- | --- |
| All backends | `set_plane_finding_mode()`/getter and session getters/`reset()` support direct integrations; prefer ARView's facade. |
| ARCore camera controls | `set_camera_config_index(index)` selects a diagnostics-listed config, −1 restores automatic selection; `set_camera_path("auto"\|"cpu")` selects automatic hardware-buffer detection or CPU. Changing either can reset the session and generation. |
| Native diagnostics | `set_diagnostics_enabled(bool)` defaults false; `get_diagnostics()` reports counters and collected samples. Disabling collection preserves prior results. No uniform cross-platform schema is promised. ARCore `set_gpu_validation_enabled()` is a separate readback opt-in; `set_point_cloud_enabled()` collects diagnostic point data, with no public point-cloud getter. ARKit `get_native_plane_identifiers()` is diagnostic. |
| Native live queries | `live_hit_test(view_pixels)` and ARCore `live_hit_test_ray(origin, direction)` inspect the current native frame. Query IDs expire on the next update. Results differ by platform and are distinct from copied ARRaycastHit placement results. |
| ARCore datasets | `start_recording(file_uri)`, `stop_recording()`, `record_input(bytes)`, `start_playback(file_uri)`, `get_playback_inputs()`, `get_dataset_diagnostics()`. Android-only advanced replay, described below. |
| Synthetic controls | `set_intrinsics()`, `set_pose()`, `set_camera_rate()` configure desktop simulation. `set_frame_id_pose()` is a harness control. |
| Scripted controls | `load_fixture()`, `advance_to()`, `get_pending_inputs()`, `get_plane_count()`, `get_semantics()` support deterministic tests. Load and advance to zero before supplying the interface to a Scripted ARView. Fixture schema and clock rules are in AGENTS.md. |
| Explicit-packet math | `PocketAR.screen_to_ray(packet, viewport_size, point)` and `raycast_planes(packet, ray)` leave packet selection and generation checks to the caller. Prefer ARView for screen input. |
| Internal backend protocol | `set_display_geometry`, `get_current_packet`, `get_presented_packet`, `mark_applied`, `mark_presented`, `application_pause`, `application_resume`, `debug_check_camera_material`. Used by ARView/harnesses; do not call these to drive an active view. |
| Debug plane injection | `set_debug_plane`/`clear_debug_plane`, available only in debug/editor libraries; harness controls, not real detection. |
| Internal ARView fields | `virtual_scene`, `world_environment`, `background`, `composite`, `applied_packet`, `waiting_for_packet`, `camera_material`, `camera_quad`. Do not mutate them. |
| Development/editor helpers | PocketARArgs, the editor/export plugin's script members, and Kotlin PocketARPlugin diagnostics/launch plumbing are internal. Export option `pocketar/ar_required` is public; `PocketARPlugin.getExternalFilesDir()` supports advanced dataset storage. |

For Android datasets, `PocketARPlugin.getExternalFilesDir()` supplies private
external storage. Build a percent-encoded `file://` URI beneath it. Recording
returns status dictionaries, stops on pause and accepts application-owned input
payloads up to 65536 bytes (128 queued inputs, at most eight new-frame retries).
Playback creates a fresh generation; `start_playback()` returns void, so inspect
session/dataset status for failures and completion. `get_playback_inputs()`
drains each recorded frame's inputs once. Datasets contain room video and device
information; keep them in private storage. They are not persistent AR anchors.
