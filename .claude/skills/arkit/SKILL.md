---
name: arkit
description: Implement or validate PocketAR on iOS, including ARKit sessions, orientation, projection, plane queries, CoreVideo/Metal imports and retirement, Xcode export and iPhone diagnostics. Load for changes under src/platform/ios and for iOS harness runs.
---

# ARKit

Follow the [core requirements](../../../AGENTS.md) and shared tracking-reason table.
Read [Metal ownership](references/metal.md) before changing camera textures,
retirement, reset or shutdown; it includes the import-retain workaround and stall limits.
For phone preflight and validation, read [iPhone diagnostics](references/iphone.md)
and the [shared harness guidance](../scripted-sessions/references/harness.md).
For `POCKETAR_ARGS`, read [application arguments](../scripted-sessions/references/arguments.md).

`ARKitInterface` (`PocketARKit`) is the iOS Metal backend. `ARView.backend = ARView.Backend.ARKIT`
selects it; the AR demo selects it on iOS. One primary monoscopic camera publishes
`head` / `default`. All ARKit/UIKit access, imports and texture changes happen on
the main thread in Safe rendering mode. Other rendering drivers are unsupported.
The separate internal `PocketARKitRetirement` interface has zero views and is
never primary; it keeps swap callbacks alive after an ARView has been removed.

The session polls `currentFrame` once per `_process` inside an autorelease pool.
Packets copy values and plane boundaries; no ARFrame, ARCamera or ARAnchor enters
a packet. Plane detection defaults to disabled; autofocus and gravity
alignment are fixed. Light estimation and HDR video are off.

Interface orientation comes from the Godot view's window scene `effectiveGeometry`.
LandscapeRight is identity; portrait, LandscapeLeft and upside-down rotate the
sensor pose around local Z by +90, +180 and +270 degrees. ARKit supplies upright
projection rows; the backend rebuilds GL row 2 from every requested near/far pair
and adjusts aspect-fill crop for a different requested aspect without changing
the packet's calibration.
The packet maps viewport UV to image UV using the inverse ARKit display transform.
Intrinsics add 0.5 to the principal point as required by the SDK's pixel-centre
convention. Orientation, viewport, pose and calibration are committed together;
a quarter-turn change waits for the window size to change or 0.5 seconds,
whichever comes first. A half-turn commits immediately. ARView may have a
landscape shape in a portrait window; its aspect never blocks orientation.
The inherited identity XROrigin/reference-frame and unit-world-scale limits apply.

AVFoundation authorization is polled on the main thread; an async permission request
does not capture the interface. Denial, including after Settings changes, pauses
the session and clears presentation. Application pause/resume notifications call
pause/run. ARKit delegate interruption events use the main queue, hide presentation
and resume relocalization only if the app still wants the session running.
Reason codes follow the shared table in `AGENTS.md`; ARKit maps its native reasons explicitly.
The delegate always attempts relocalization after interruption.

Reset increments the process-wide generation immediately, removes trackers,
invalidates presentation, and runs ResetTracking|RemoveExistingAnchors. Frames with
timestamps at or before the reset's host-time cutoff are discarded.

Planes keep UUID-derived opaque IDs within a generation. Their pose is the centre
of the extent rectangle, with its rotation about local +Y, and extents are its
width and height. Boundary vertices are transformed into this frame and stored as
local XZ with +Y normal. Positive `rotationOnYAxis` is assumed to follow Godot's
right-handed +Y rotation; draw the extent rectangle over the polygon to verify
that sign on the phone. Limited camera tracking pauses plane trackers and excludes
them from polygon raycasts. Native UUID diagnostics and the lifecycle harness check
that pre-reset anchors do not reappear and log the first post-reset anchor counts. Removed anchors remove trackers. The core raycast handles
simple non-convex polygons in either winding. `live_hit_test(view_pixels)` uses a
native ExistingPlaneGeometry query and returns generation, source timestamp, query
ID and the packet frame ID when that native frame has been published (otherwise 0).
It is a live query; copied packet polygons remain the placement API.

## Plane finding

ARKit changes `planeDetection` on its current `ARWorldTrackingConfiguration`
and re-runs with zero options. Tracking and existing anchors continue; disabling
plane finding does not request anchor removal or tracking reset. Plane discovery
and geometry refinement are disabled, while PocketAR continues to copy native
anchors and derive their tracking state from camera tracking. Both native backends
report the selected `plane_finding_mode` and read-back `effective_plane_finding_mode`
in diagnostics. The effective field is absent before a native config exists;
on paused ARKit it describes the last run until resume applies a pending change.

## Session state

- ARKit reports normal tracking as TRACKING, limited/not-available tracking as
  INITIALIZING, interruption/suspension as PAUSED, authorization denial as
  PERMISSION_DENIED, unsupported hardware as UNSUPPORTED and session failure as
  FAILED. Its reason mapping follows the shared table in `AGENTS.md`. Limited ARKit poses may
  be valid while plane trackers are paused; ARCore poses require native TRACKING.
