---
name: scripted-sessions
description: Work on PocketAR synthetic or scripted sessions, JSON-lines fixtures, replay/input drivers, the ARView game facade, rendering and shared harness checks, application arguments or debug-plane injection. Load for desktop rendering validation and cross-backend harness changes.
---

# Scripted sessions and the game facade

The public contracts are [game API](../../../docs/api.md#game-api),
[session controls](../../../docs/api.md#session-controls) and
[frames and lifetime](../../../docs/api.md#frames-signals-and-lifetime).
Keep backend implementation details in [arcore](../arcore/SKILL.md) or [arkit](../arkit/SKILL.md).

- Read [fixtures](references/fixtures.md) when generating, parsing or replaying sessions and inputs.
- Read [rendering](references/rendering.md) for shader, projection or composition work; it includes the Godot source evidence.
- Read [harness checks](references/harness.md) for desktop runs and shared native validation.
- Read [application arguments](references/arguments.md) for launch transport and validation on any platform.
- Read [debug planes](references/debug-plane.md) for injection, release exclusion and exported API probes.

Scripted replay freezes existing plane geometry and tracking state while disabled,
and adds no new planes. Horizontal mode keeps existing vertical planes frozen and
excludes new ones. Re-enabling applies the latest recorded plane set on the next
fixture frame, including when that frame omits `planes`. Reset discards that
remembered set. Synthetic produces no planes and remembers the mode across reset
and stop/start.

Version 2 adds `map_transform` and `anchor_updates`; read the fixture reference.

Backend selection and replay methods are documented in
[ARView's editor help](../../../harness/addons/pocketar/ar_view.gd),
[PocketAR](../../../doc_classes/PocketAR.xml) and
[ScriptedARInterface](../../../doc_classes/ScriptedARInterface.xml).

Session state and notifications follow [the frame contract](../../../docs/api.md#frames-signals-and-lifetime)
and [PocketAR](../../../doc_classes/PocketAR.xml). Replay comparisons must normalize
process-wide unique generation IDs relative to each run, and compare anchor IDs
relative to successful creation order.

- Synthetic reports TRACKING/NONE while initialized and PAUSED when stopped.
  Reset immediately changes generation and invalidates presentation.
- Scripted reads state/reason names from the fixture, with reset and resume
  entering INITIALIZING/NONE and pause entering PAUSED.

Placement follows [coordinates and placement](../../../docs/api.md#coordinates-and-placement)
and `PocketAR.AlignmentMask` in [PocketAR.xml](../../../doc_classes/PocketAR.xml).
`view_point(event_position)` converts viewport event positions with the inverse
of `get_global_transform_with_canvas()`, including canvas scaling and rotation.

Anchors follow [the anchor contract](../../../docs/api.md#anchors). Use
`ARAnchorTracker` and stock `XRAnchor3D`; inspect applied `ARAnchorTracker` values
in `packet_applied`.
The desktop `anchors` step checks map corrections, rigid child offsets and
lifecycle in both compositions and orientations; it requires rendered PNG
evidence, not only headless state checks.

`ARPlaneVisualizer` is a Node3D with an `ar_view` export; use `visible` to hide it.
Place it in `view.scene_root`; it draws applied-packet polygons and excludes old
session generations, paused/stopped planes and subsumed aliases. The demo uses
this addon node instead of maintaining its own meshes.
