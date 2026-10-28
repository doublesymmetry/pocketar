# Fixture format and replay

Fixtures are UTF-8 JSON lines with one object per line; string escapes are
rejected. The first line contains `format: "pocketar-session"`, `version: 1` or
`version: 2`, `semantics: "arkit"` or
`"arcore"`, `image_size: [width,height]`, and
`intrinsics: {"fx":520,"fy":510,"cx":320,"cy":240}`. Intrinsics are in
physical camera-image pixels. World units are metres; camera axes are +X right,
+Y up, -Z forward. Transforms are finite rigid matrices encoded as 16 column-major
numbers, with unit scale and positive determinant. In version 1 they map local
coordinates to world space; in version 2 camera/plane transforms map local
coordinates to fixture-map space. Camera transforms describe the physical
image-camera basis, prior to display rotation. Plane polygons are local XZ pairs with +Y normal;
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

Geometry changes rebuild projection and display transform from recorded intrinsics and physical
pose even when the frame repeats. Camera pixels use the synthetic bars.

## Version 2 anchor controls

Version 1 anchors keep their requested world pose, subject to ordinary session
loss/reset. Version 2 adds two optional frame fields:

| Field | Contract |
| --- | --- |
| `map_transform` | Rigid 16-number column-major world-from-fixture-map transform. Omission retains the previous value; reset restores identity. |
| `anchor_updates` | Array of `{"slot": 1, "tracking_state": "paused"}` objects. `slot` is a positive integer; states are `tracking`, `paused` or `stopped`. Omission or an empty array changes no state. |

Slots number successful game creation calls starting at 1 in each generation.
Failed creation consumes no slot; removal never recycles one. Updates apply in
file order and persist until another update or reset. Unknown or stopped slots
are rejected at replay time, since their validity depends on the game's creation
calls. Controls never create anchors. Reset clears slots and remembered state.

For world pose `A` created while the map transform is `M`, the backend retains
anchor-local pose `L = M.affine_inverse() * A`. Each later frame uses its map
transform to publish `M * L`. That same map transform is applied to the frame's
physical camera and plane values before display orientation. Plane extent/centre
changes alone must not drag anchors. Disabling plane finding freezes recorded
plane geometry/state but does not suppress map correction of retained planes or
anchors. `ARFramePacket` exposes no public anchors collection; inspect applied
`ARAnchorTracker` values in `packet_applied`.

The `anchors` desktop step uses the checked-in `anchors-*.jsonl` fixtures for both
ARKit and ARCore semantics, landscape and portrait, in both compositions. Camera
records are 100 ms apart and each is held across multiple rendered frames. It
checks extent growth, map correction, pause/recovery, source-plane removal,
explicit anchor removal and reset, saving lossless PNGs and numeric reprojections.
Run `harness desktop -harness-steps anchors` on both stock and fork engine lanes.

## Input replay and generated fixtures

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

`--table-height` sets metres below the camera; `--camera orbit` or `static` chooses
its path. Each `--tap-at` projects the table centre. Generated fixtures use version 1 and ARKit semantics,
run for 2 seconds at 30 fps, and use 0.25-second losses and 0.1-second pauses.
The default camera is at the origin and the table centre is `(0,-0.7,-2)`.
The `scripted` desktop harness step runs each landscape and 1080×1920 portrait
fixture twice, with an explicit deferred flush after each time advance. The headless companion
checks the same state, placement and input behavior without claiming GPU coverage.

## Generate fixtures

| Task | Command |
| --- | --- |
| Generate landscape fixture | `python3 tools/fixtures.py --output harness/suites/fixtures/table.jsonl --loss-at 1 --reset-at 1.5 --pause-at 1.75 --tap-at 0.6 --drag-to 0.1 -0.7 -2 --plane-examples` |
| Generate portrait fixture | Use the preceding command with `--output harness/suites/fixtures/table-portrait.jsonl --view-size 1080 1920` |
