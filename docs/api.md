# API contract

Use `res://addons/pocketar/ar_view.tscn` with `ARFramePacket`, `ARPlaneTracker`,
`ARAnchorTracker`, `ARRaycastHit` and `PocketAR` enums. Member details are in
native editor/debug help and the ARView and ARPlaneVisualizer GDScript documentation.

## Game API

| Surface | Contract |
| --- | --- |
| ARView startup | `backend`: an `ARView.Backend` (`SYNTHETIC`, `ARCORE`, `ARKIT`, `SCRIPTED`). Choose backend and `composition` before adding to the tree. Composition changes after ready are rejected; live backend replacement is unsupported. |
| ARView configuration | `handle_presses`, `environment`, `plane_finding_mode` (disabled by default), `camera_quality`, `camera_frame_rate`; `orientation_quarter_turns` controls simulation (−1 automatic, 0–3 clockwise). Native orientation comes from the device. |
| ARView scene | After ready, put objects under `scene_root`, Controls under `hud`, use `world_3d` for physics/shared cameras, and `camera.near`/`far` for clip distances in metres. The backend owns camera pose/projection. |
| ARView session | Read-only `session_state`, `tracking_reason`, `session_generation`; `reset()` clears planes/presentation, stops anchors and changes generation on an initialized session, preserving game nodes. See [session controls](#session-controls) for `suspended`. |
| ARView placement | `screen_to_ray(point)`, `raycast(point, alignments)`, `world_to_screen(point)`, `view_point(event_position)` follow [coordinates and placement](#coordinates-and-placement). |
| ARView anchors | `create_anchor_from_hit(hit)`, `create_anchor(pose)`, `remove_anchor(anchor)` and `add_anchor_node(anchor)` follow [anchors](#anchors). `anchors_changed` reports membership changes. |
| Packet values | Read-only IDs, timestamps, tracking validity, camera transforms, projection, intrinsics, image/viewport sizes, orientation, display transform and copied plane dictionaries. |
| Plane trackers | Read-only identity, generation, alignment, extents, polygon, subsumption, tracking state and transform. Trackers update in place through XRServer. |
| Anchor trackers | Read-only `anchor_id`, `session_generation`, `source_plane_id`, `transform` and `tracking_state`. Pose updates follow the applied camera packet. |
| Hits | Read-only `pose`, `distance`, `alignment`, `plane_id`, `frame_id`, `session_generation`; copied results remain readable after later frames. |
| PocketAR | Session/reason/plane-finding/alignment-mask enums, `get_version()` and `get_backend()`. Explicit-packet math helpers are advanced. |
| ARPlaneVisualizer | Optional sample/debug visualization with `ar_view` and read-only `plane_count`; catches up when shown after hiding. |

## Coordinates and placement

World units are metres. Camera coordinates are right-handed: +X right, +Y up,
−Z forward. Camera transforms are world-from-camera. `camera_transform`
includes display orientation; `physical_camera_transform` stays in the
physical image-camera basis. Keep XROrigin3D and `scene_root` at identity and XR
world scale at one. One active ARView owns a private world and the primary XR
interface.

Screen arguments/results use pixels local to ARView, with a top-left origin.
`view_point(event.position)` converts viewport/canvas input to those pixels.
`screen_to_ray()` inverts the full presented projection and returns a Dictionary
with `origin` (world near-plane point), `direction` (unit world vector),
`frame_id` and `session_generation`. It returns `{}` if presentation or tracking
is invalid, the generation differs or the view size is not positive.

`raycast()` returns copied hits nearest first, or `[]` without valid presentation
or a hit. It intersects the front of tracking, unsubsumed plane polygons, not
physics bodies or infinite planes. `alignments` uses `PocketAR.AlignmentMask`
bits; `ARPlaneTracker.Alignment` values are ordinals, not bits. A hit's distance
is measured from the ray's near-plane origin, not the camera centre. Its pose
has +Y along the plane normal and +Z toward the incoming ray projected on the
plane, falling back to the plane's +Z for a perpendicular ray.

`world_to_screen()` returns `Vector2.INF` without valid presentation, for
non-finite input or points on/behind the camera plane. Points outside the view
can return finite coordinates outside its bounds; occlusion is not tested.

Plane polygons are local XZ pairs in metres; +Y is the normal and `extents` are
full X/Z lengths. `transform` maps plane-local coordinates to world space.
Plane IDs are opaque within a generation. XRServer names are
`pocketar/plane/<generation>/<id>`; subsumed trackers remain aliases, with
`subsumed_by` naming the replacement, and are excluded from default visualization
and raycasts. Zero means no subsumption. A tracker is live state;
each `packet.planes` read returns editable copies that cannot mutate the packet.
Both plane and game-anchor trackers use `TRACKER_ANCHOR`; filter by
`ARPlaneTracker` or `ARAnchorTracker` when enumerating XRServer trackers.

## Anchors

`create_anchor_from_hit(hit)` and `create_anchor(pose)` return an
`ARAnchorTracker`, or `null` with one warning naming the failure: stale hit,
inactive/untracked session, invalid pose or native refusal. Creation requires an
initialized, running, normally tracking session. Poses are finite, rigid
world-from-anchor transforms in metres, with unit scale and positive determinant.
Free-pose callers supply the current world estimate; a pre-reset coordinate is
not a persistent location. Plane finding can be disabled for free anchors.

Hit creation requires the currently presented frame and generation, the same
uninterrupted tracking interval, and a source plane that is still tracking in
current state. A subsumed plane can resolve to its current tracking replacement.
A hit retained across later presentation, tracking loss or reset remains readable
but cannot create an anchor. Create a preview anchor at the tap and retain it on
Confirm; do not save the hit for later creation.

The native session can be newer than the presented image when creation runs.
PocketAR submits the hit's requested world pose to that current session. It does
not reconstruct a historical map or guarantee an initial placement error bound.
If acquisition or tracking recovery invalidates the hit, let the user tap again.
An individual anchor pause also invalidates older hit samples; wait for a fresh
presented camera packet.

Android hit creation uses `ArTrackable_acquireNewAnchor` on the current native
plane; free creation uses `ArSession_acquireNewAnchor`. iOS creates a dedicated
ordinary `ARAnchor` at the requested pose for either method.

### Ownership and scene nodes

Accepted creation returns a tracker in `PAUSED`, with the requested transform
and an invalid inherited `default` pose. Its first eligible applied packet can
make it `TRACKING`.

`add_anchor_node(anchor)` returns a stock `XRAnchor3D`, bound to that tracker and
parented directly under the view's private `XROrigin3D`. It starts at the anchor's
transform and rejects foreign/stopped anchors. Put game scale, animation, yaw and
offsets below the helper; do not overwrite its transform each frame. For a chosen
world pose, set the content's local transform to `anchor.transform.affine_inverse() * desired_pose`.

The helper leaves `show_when_tracked=false`. Choose visibility explicitly through
`tracking_changed`, as in the [README example](https://github.com/doublesymmetry/pocketar#anchors), and gate
input/physics too. A hidden node still participates in physics.

The backend owns the anchor until removal, terminal native loss or session end.
Freeing the helper or dropping a game reference does not remove it, because the
session and XRServer retain it. `remove_anchor(anchor)` works while suspended,
stops and unregisters the owned anchor, and releases native ownership. It is
idempotent for an already stopped anchor from that owner; foreign objects are
rejected. It does not free game nodes. Retained stopped trackers remain readable
and own no native handle.

Tracker names are `pocketar/anchor/<generation>/<id>`. IDs are opaque and never
reused within a generation. Do not call inherited setters on provider-owned
trackers or remove them through XRServer.

### Tracking, timing and lifecycle

`TRACKING=0` means a usable applied pose; `PAUSED=1` means tracking is unavailable;
`STOPPED=2` is terminal. ARKit ordinary anchors have no independent native
confidence: usability comes from camera/session state and membership in the
sampled frame.

Trackers update from the same packet as the camera, before `packet_applied` and
drawing. Anchor-only estimate changes do not publish extra packets.

`tracking_state_changed(state)` is deferred and coalesced to the latest state,
so a queued tracking notification cannot follow a terminal stop.
`ARView.anchors_changed()` is a deferred membership notification, not a per-frame
pose notification. Inherited tracker pose signals and `XRAnchor3D.tracking_changed`
remain synchronous. Use `packet_applied` to inspect a matching camera/anchor pair.
Lifecycle invalidation is immediate, even without a new camera packet.

| Event | Behavior |
| --- | --- |
| Map refinement | Apply the corrected pose with its camera packet; preserve identity and child-local transforms. |
| Tracking loss, suspension, application pause or interruption | Keep native ownership and identity, report `PAUSED`, invalidate the pose and retain its last transform. Reject creation. |
| Resume or relocalization | Keep identity while the session survives; remain paused until a fresh usable sample. |
| Native terminal loss, explicit removal or terminal session failure | Stop, unregister and release. Recoverable permission denial pauses anchors while the native session survives. |
| Reset, recreation, generation change or uninitialize | Synchronously stop every old anchor and release native owners. Game nodes remain and lose tracking. |
| Source plane subsumption/removal or disabled plane finding | Preserve the anchor's own lifecycle. Android follows native anchor state; iOS's dedicated anchor is independent of plane membership. |

Android camera quality/config/path changes that recreate a session stop its
anchors. Automatic reset after camera/fatal update errors also loses the
generation. iOS configuration changes using run options zero retain anchors.
Recovery is not guaranteed; provide a re-placement path after permanent loss.

Synthetic anchors keep their world pose as the simulated camera moves. Scripted
v1 does the same. Scripted v2 can correct the map and change individual anchor
states; see ScriptedARInterface's class help.

### Cost and limits

Use one anchor per nearby rigid group and remove it when no longer needed.
There is no count cap; native tracking cost is device-dependent. With zero
anchors PocketAR makes no anchor queries or allocations.

Saved world maps and cloud, geospatial or shared anchors are outside this API.

## Frames, signals and lifetime

The backend publishes a current packet, ARView applies it before drawing, and
marks it presented after drawing. Touch placement uses the presented packet.
At camera rates below the render rate a complete packet repeats. Geometry or
calibration changes produce a new packet.

`packet_applied(packet)` fires when ARView applies a different packet at startup,
`SceneTree.process_frame` (before node `_process` callbacks) or `frame_pre_draw`,
but not for repeats or clearing.
`screen_pressed(point, ray)` fires for an unconsumed touch/left mouse press
inside the view when `handle_presses` is true; HUD Controls receive input first.
It marks input handled and ignores touch-emulated mouse presses.
Its ray may be `{}` while tracking is unavailable.

`session_state_changed`, `tracking_reason_changed`,
`session_generation_changed` and `planes_changed` describe current backend
state. Notifications wait until the native operation finishes so handlers can
reset safely; properties query state immediately. State/reason notifications fire
on value changes; startup can publish initial state/generation.
`planes_changed` is not a presentation notification.
Discard old identities and clear or rebuild placements on generation changes.

Packet values remain valid for the packet's lifetime; retaining native textures
does not preserve camera pixels. See the
[ARFramePacket class reference](https://github.com/doublesymmetry/pocketar/blob/v0.0.1/doc_classes/ARFramePacket.xml)
for image lifetime, calibration, UV conventions and timestamps.

## Session controls

`PocketAR.SessionState` distinguishes `UNSUPPORTED`, `CHECKING`, `NEEDS_INSTALL`,
`INSTALLING`, `PERMISSION_DENIED`, `READY`, `INITIALIZING`, `TRACKING`, `PAUSED`
and `FAILED`. `READY` means prerequisites are satisfied, not that a pose exists.
Reasons are `NONE`, `BAD_STATE`, `INSUFFICIENT_LIGHT`, `EXCESSIVE_MOTION`,
`INSUFFICIENT_FEATURES` and `CAMERA_UNAVAILABLE`. `NONE` can also accompany
initialization. ARKit limited poses may be valid while plane tracking is paused;
ARCore valid poses require native tracking. Consult packet `tracking_valid`
when working directly with packets.

Android checks availability without prompting. Initializing ARCore, including
through ARView, requests installation and camera permission; earlier addon loading
or app resumes do not prompt. For setup/retry before or after view creation, call
`Engine.get_singleton("PocketARPlugin").requestSession()` on Android.
State is observable once the view exists. Later resumes continue requested setup,
but do not retry denials. `PERMISSION_DENIED` or `NEEDS_INSTALL` persists until
prerequisites change or the game requests a retry. `reset()` only resets the native
session. Repeated camera denials may require Android app settings. Use session
state for setup errors; tracking reasons describe pose quality.

`plane_finding_mode` accepts disabled (default), horizontal (upward/downward) or
horizontal-and-vertical and persists across reset/pause. Disabling discovery
preserves planes: ARKit stops discovery/refinement, ARCore keeps copying native
states, and Scripted freezes retained snapshots until re-enabled. It neither
freezes game objects nor creates anchors.

`camera_quality` uses `PocketAR.CameraQuality`: `HIGHEST` (default), `BALANCED`
and `LOWEST`. Android filters for a back camera at 30 fps without depth or stereo.
The CPU path requires matching CPU/GPU aspect ratios and selects by CPU pixel
area, breaking ties with the smallest GPU stream. The hardware-buffer path
selects by GPU pixel area, breaking ties with the smallest CPU stream.
`BALANCED` selects the lower middle distinct area; `LOWEST` selects the smallest.
An explicit Android camera config index overrides this preference. Changing the
preference resets an existing Android session unless an explicit index is active.

On iOS, `HIGHEST` selects the first format in Apple's supported order.
`BALANCED` and `LOWEST` use distinct image pixel areas, with Apple's order
breaking ties. `camera_frame_rate` is an iOS preference:
0 (default), 30 or 60. A nonzero value first restricts formats to the nearest
supported rate, choosing the lower rate on a tie. Quality selection then runs
within those formats. iOS reconfigures without requesting a tracking reset.
Simulated backends ignore these preferences; use fixture/intrinsics and camera-rate
controls. Preferences do not change the game's render rate or guarantee power
or thermal savings.

Native diagnostics expose `camera_quality`, `config_list` and the effective
`config` dimensions/rate. ARKit also reports requested `camera_frame_rate` and
`config_index`; ARCore reports CPU and GPU dimensions and the FPS range.

Set `suspended` before startup or while running to pause acquisition and viewport
rendering independently of application lifecycle. The view reports `PAUSED`,
publishes no new packets and uses `SubViewport.UPDATE_DISABLED`; placement returns
no result. Resume requires both application activity and `suspended=false`.
Ordinary resume preserves generation and asks the SDK to retain tracking, which
may need to recover. Android datasets follow ARCore's paused-playback behavior.
Reset while suspended clears packets/planes and changes generation without
starting capture. Visibility and overlays do not affect suspension; virtual node
scripts keep running, so games must pause their own simulation.

## Advanced and internal surfaces

`ARView.interface` is the advanced access path. Calls belong on Godot's main
thread in Safe rendering mode. ARCore exists only on Android and ARKit only
on iOS; use `ClassDB.class_exists()`/`instantiate()` or dynamic calls from shared
scripts. ARView owns initialization and shutdown. Backend-specific diagnostics,
live-query results and test controls are outside the stable game contract.

| Surface | Intended use |
| --- | --- |
| All backends | `set_plane_finding_mode()`/getter, `set_suspended()` and session getters/`reset()` support direct integrations; prefer ARView's facade. |
| ARCore camera controls | `set_camera_config_index(index)` selects a diagnostics-listed config, −1 restores automatic selection; `set_camera_path("auto"\|"cpu")` selects automatic hardware-buffer detection or CPU. Changing either can reset the session and generation. |
| Native diagnostics | `set_diagnostics_enabled(bool)` defaults false; `get_diagnostics()` reports counters/samples. Disabling collection preserves results; schemas may differ by platform. ARCore `set_gpu_validation_enabled()` separately enables readback; `set_point_cloud_enabled()` collects diagnostic points without a public getter. ARKit `get_native_plane_identifiers()` is diagnostic. |
| Native live queries | `live_hit_test(view_pixels)` and ARCore `live_hit_test_ray(origin, direction)` inspect the current native frame. IDs expire on the next update. Results vary by platform and differ from copied `ARRaycastHit` placement results. |
| ARCore datasets | `start_recording(file_uri)`, `stop_recording()`, `record_input(bytes)`, `start_playback(file_uri)`, `get_playback_inputs()`, `get_dataset_diagnostics()` provide Android-only replay; see [ARCoreInterface](https://github.com/doublesymmetry/pocketar/blob/v0.0.1/doc_classes/ARCoreInterface.xml) for storage, limits and lifecycle. |
| Synthetic controls | `set_intrinsics()`, `set_pose()`, `set_camera_rate()` configure desktop simulation. `set_frame_id_pose()` is a harness control. |
| Scripted controls | `load_fixture()`, `advance_to()`, `get_pending_inputs()`, `get_plane_count()`, `get_semantics()` support deterministic tests. Load and advance to zero before supplying the interface to a Scripted ARView. Fixture schema and clock rules are in the source repository's `.claude/skills/scripted-sessions/references/fixtures.md`. |
| Explicit-packet math | `PocketAR.screen_to_ray(packet, viewport_size, point)` and `raycast_planes(packet, ray)` leave packet selection and generation checks to the caller. Prefer ARView for screen input. |
| Internal backend protocol | `set_display_geometry`, `get_current_packet`, `get_presented_packet`, `mark_applied`, `mark_presented`, `application_pause`, `application_resume`, `set_suspended`, `debug_check_camera_material`. Used by ARView/harnesses; do not call these to drive an active view. |
| Debug plane injection | `set_debug_plane`/`clear_debug_plane`, available only in debug/editor libraries; harness controls, not real detection. |
| Internal ARView fields | `virtual_scene`, `world_environment`, `background`, `composite`, `applied_packet`, `waiting_for_packet`, `camera_material`, `camera_quad`. Do not mutate them. |
| Development/editor helpers | PocketARArgs, the editor/export plugin's script members, and Kotlin PocketARPlugin diagnostics/launch plumbing are internal. Export option `pocketar/ar_required` is public; `PocketARPlugin.getExternalFilesDir()` supports advanced dataset storage. |
