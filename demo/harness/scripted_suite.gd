extends RefCounted

const AR_VIEW: PackedScene = preload("res://addons/pocketar/ar_view.tscn")
const FIXTURES: PackedStringArray = ["res://harness/fixtures/table.jsonl", "res://harness/fixtures/table-portrait.jsonl"]
const TABLE_CENTER := Vector3(0.0, -0.7, -2.0)
const TICKS: Array[int] = [0, 500000, 600000, 633333, 666667, 1266667, 1500000, 1533333, 1750000, 1850000, 1866667]
const FRAME_IDS: Array[int] = [1, 16, 19, 20, 21, 39, 46, 47, 53, 56, 57]
const FRAME_TIMES: Array[int] = [0, 500000, 600000, 633333, 666667, 1266667, 1500000, 1533333, 1733333, 1833333, 1866667]

var failures: PackedStringArray = []


class InputObserver extends Node:
	var view: ARView
	var events: Array[Dictionary] = []


	func _input(event: InputEvent) -> void:
		if not event is InputEventScreenTouch and not event is InputEventScreenDrag:
			return
		var kind: String = "move" if event is InputEventScreenDrag else ("down" if event.pressed else "up")
		var point: Vector2 = view.view_point(event.position)
		var packet: ARFramePacket = view.interface.call("get_presented_packet") as ARFramePacket
		var hits: Array[ARRaycastHit] = view.raycast(point)
		events.append({"kind": kind, "finger": event.index, "point": point, "frame": packet.frame_id if packet != null else 0, "hit": hits[0].pose.origin if not hits.is_empty() else Vector3.INF})


func run(host: Node) -> Dictionary:
	failures.clear()
	for fixture: String in FIXTURES:
		var first: Dictionary = await _run_once(host, fixture)
		var second: Dictionary = await _run_once(host, fixture)
		_check(first == second, "fresh scripted sessions produce identical packets, inputs and transitions: " + fixture)
	return {"ok": failures.is_empty(), "detail": "deterministic landscape and portrait scripted sessions" if failures.is_empty() else "; ".join(failures)}


func _run_once(host: Node, fixture: String) -> Dictionary:
	var header: Dictionary = JSON.parse_string(FileAccess.open(fixture, FileAccess.READ).get_line())
	var target_size := Vector2(header.view_size[0], header.view_size[1])
	var scripted := ScriptedARInterface.new()
	var loaded: Error = scripted.load_fixture(fixture)
	_check(loaded == OK, "table fixture loads")
	if loaded != OK:
		return {}
	scripted.advance_to(0)
	await _flush_notifications(host)
	var view: ARView = AR_VIEW.instantiate() as ARView
	view.backend = 3
	view.interface = scripted
	view.handle_presses = true
	view.set_anchors_and_offsets_preset(Control.PRESET_TOP_LEFT)
	view.size = target_size
	view.orientation_quarter_turns = int(header.quarter_turns)
	view.position = Vector2(24, 18)
	view.scale = Vector2(1.1, 0.9) if target_size.x > target_size.y else Vector2(0.3, 0.3)
	var timeline: Array = []
	view.session_state_changed.connect(func(value: int) -> void: timeline.append(["state", value]))
	view.tracking_reason_changed.connect(func(value: int) -> void: timeline.append(["reason", value]))
	view.session_generation_changed.connect(func(value: int) -> void: timeline.append(["generation", value]))
	view.planes_changed.connect(func() -> void: timeline.append(["planes", scripted.get_plane_count()]))
	var presses: Array[Dictionary] = []
	view.screen_pressed.connect(func(point: Vector2, ray: Dictionary) -> void:
		var hits: Array[ARRaycastHit] = view.raycast(point, PocketAR.ALIGNMENT_MASK_HORIZONTAL_UPWARD)
		presses.append({"point": point, "frame": ray.get("frame_id", 0), "hit": hits[0].pose.origin if not hits.is_empty() else Vector3.INF})
	)
	host.add_child(view)
	_check(scripted.is_initialized(), "scripted interface initializes through ARView")
	if not scripted.is_initialized():
		view.free()
		return {}
	var baseline: int = view.session_generation
	_check(baseline > 0 and view.session_state == PocketAR.INITIALIZING and view.tracking_reason == PocketAR.NONE, "initial facade state matches fixture")
	var visualizer := ARPlaneVisualizer.new()
	visualizer.ar_view = view
	view.scene_root.add_child(visualizer)
	var observer := InputObserver.new()
	observer.view = view
	host.add_child(observer)
	var packets: Array[Dictionary] = []
	var inputs: Array[Dictionary] = []
	var old_names := PackedStringArray()
	for tick: int in TICKS:
		var before: int = timeline.size()
		scripted.advance_to(tick)
		await _flush_notifications(host)
		if tick == 1266667:
			var states_and_reasons: Array = []
			for transition: Array in timeline.slice(before):
				if transition[0] in ["reason", "state"]:
					states_and_reasons.append(transition)
			_check(states_and_reasons == [["reason", PocketAR.INSUFFICIENT_FEATURES], ["state", PocketAR.INITIALIZING], ["reason", PocketAR.NONE], ["state", PocketAR.TRACKING]], "one advance emits every crossed reason and state transition in order")
		if tick == 1500000:
			_check(view.session_generation > baseline, "fixture reset advances facade generation")
			_check(_tracker_names(baseline).is_empty() and scripted.get_plane_count() == 0, "fixture reset removes old plane trackers before presentation")
			_check(visualizer.plane_count == 0, "generation change immediately clears plane meshes")
		if tick == 1750000:
			_check(view.session_state == PocketAR.PAUSED, "fixture pause reaches the session facade")
		if tick == 1850000:
			_check(view.session_state == PocketAR.INITIALIZING, "fixture resume returns to initializing")
		if tick == 1866667:
			_check(view.session_state == PocketAR.TRACKING, "first resumed frame restores tracking")
		await _present(view)
		if tick == 1750000 or tick == 1850000:
			_check(visualizer.plane_count == 0, "paused or initializing session keeps plane meshes hidden")
		var packet: ARFramePacket = scripted.get_presented_packet()
		_check(packet != null and packet == view.applied_packet, "ARView applies the presented packet at tick %d" % tick)
		if packet != null:
			_check(view.size == target_size and packet.viewport_size == target_size and packet.quarter_turns == int(header.quarter_turns), "running view matches fixture input geometry")
			var tick_index: int = TICKS.find(tick)
			_check(packet.frame_id == FRAME_IDS[tick_index] and packet.timestamp_usec == FRAME_TIMES[tick_index], "manual clock selects the expected complete frame at tick %d" % tick)
		packets.append(_snapshot(packet, baseline))
		if tick == 500000:
			_check(view.session_state == PocketAR.TRACKING and view.tracking_reason == PocketAR.NONE, "tracking frame updates the session facade")
			_check_planes(view, visualizer)
			old_names = _tracker_names(view.session_generation)
			_check_helpers(view)
		if tick == 1533333:
			_check(_tracker_names(baseline).is_empty(), "replacement planes do not revive old generation names")
			_check_planes(view, visualizer)
		var pending: Array[Dictionary] = scripted.get_pending_inputs()
		_check(scripted.get_pending_inputs().is_empty(), "pending input drain is destructive")
		for input: Dictionary in pending:
			_check(packet != null and packet.frame_id == input.target_frame and packet.session_generation == input.session_generation, "input waits for its target frame and generation")
			var point: Vector2 = input.uv * view.size
			var event: InputEvent
			if input.kind == "move":
				var drag := InputEventScreenDrag.new()
				drag.index = input.finger
				drag.position = view.get_global_transform_with_canvas() * point
				event = drag
			else:
				var touch := InputEventScreenTouch.new()
				touch.index = input.finger
				touch.position = view.get_global_transform_with_canvas() * point
				touch.pressed = input.kind == "down"
				touch.canceled = input.kind == "cancel"
				event = touch
			var count: int = observer.events.size()
			Input.parse_input_event(event)
			Input.flush_buffered_events()
			_check(observer.events.size() == count + 1, "injected fixture input reaches the scene input path")
			if observer.events.size() == count + 1:
				var received: Dictionary = observer.events.back()
				_check(received.kind == input.kind and received.finger == input.finger and received.frame == input.target_frame, "injected kind, finger and presented frame match fixture")
				_check(Vector2(received.point).distance_to(point) < 0.001, "injected event uses scaled ARView coordinates")
				_check(Vector3(received.hit).distance_to((TABLE_CENTER if input.kind == "down" else TABLE_CENTER + Vector3(0.1, 0, 0))) <= 0.001, "injected input raycast reaches the expected table point within 1mm")
			input["session_generation"] = int(input["session_generation"]) - baseline
			inputs.append(input)
	_check(inputs.size() == 3 and observer.events.size() == 3, "down, move and up are replayed once")
	_check(presses.size() == 1, "only down emits screen_pressed")
	if presses.size() == 1:
		_check(presses[0].frame == 19 and Vector3(presses[0].hit).distance_to(TABLE_CENTER) <= 0.001, "screen_pressed uses target-frame table hit")
	var generation: int = view.session_generation
	old_names.append_array(_tracker_names(generation))
	view.reset()
	_check(view.session_generation > generation and view.session_state == PocketAR.INITIALIZING, "ARView.reset forwards a fresh initializing generation")
	_check(view.applied_packet == null and scripted.get_presented_packet() == null and view.screen_to_ray(Vector2.ZERO).is_empty(), "reset immediately invalidates application and presentation")
	await _flush_notifications(host)
	_check(scripted.get_plane_count() == 0 and visualizer.plane_count == 0, "reset immediately clears trackers and visualizer")
	for name: String in old_names:
		_check(XRServer.get_tracker(name) == null, "reset leaves no stale named plane tracker")
	var normalized: Array = timeline.duplicate(true)
	for transition: Array in normalized:
		if transition[0] == "generation":
			transition[1] = int(transition[1]) - baseline
	var generations: Array = []
	var states: Array = []
	var reasons: Array = []
	for transition: Array in normalized:
		match transition[0]:
			"generation": generations.append(transition[1])
			"state": states.append(transition[1])
			"reason": reasons.append(transition[1])
	_check(generations == [0, 1, 2], "initialization and two resets each emit exactly one generation")
	_check(reasons == [PocketAR.INSUFFICIENT_FEATURES, PocketAR.NONE], "tracking reasons follow fixture order")
	_check(states == [PocketAR.INITIALIZING, PocketAR.TRACKING, PocketAR.INITIALIZING, PocketAR.TRACKING, PocketAR.INITIALIZING, PocketAR.TRACKING, PocketAR.PAUSED, PocketAR.INITIALIZING, PocketAR.TRACKING, PocketAR.INITIALIZING], "facade state transitions preserve initialization, loss, reset and lifecycle order")
	var result: Dictionary = {"packets": packets, "inputs": inputs, "events": observer.events.duplicate(true), "presses": presses, "timeline": normalized}
	observer.free()
	view.free()
	_check(XRServer.get_primary_interface() == null, "scripted view releases primary interface")
	return result


func _check_planes(view: ARView, visualizer: ARPlaneVisualizer) -> void:
	_check(_tracker_names(view.session_generation).size() == 4, "XRServer publishes four named plane trackers")
	_check(visualizer.plane_count == 2 and visualizer.get_child_count() == 2, "visualizer draws only tracking, unsubsumed planes")
	var packet: ARFramePacket = view.interface.call("get_presented_packet") as ARFramePacket
	if packet == null:
		return
	var expected_transforms: Array[Transform3D] = []
	for plane: Dictionary in packet.planes:
		var name: String = "pocketar/plane/%d/%d" % [view.session_generation, plane.plane_id]
		var tracker: ARPlaneTracker = XRServer.get_tracker(name) as ARPlaneTracker
		_check(tracker != null, "plane is available under its generation/id tracker name")
		if tracker != null:
			_check(tracker.plane_id == plane.plane_id and tracker.session_generation == view.session_generation and tracker.tracking_state == plane.tracking_state and tracker.subsumed_by == plane.subsumed_by, "named tracker matches its packet snapshot")
		if plane.tracking_state != ARPlaneTracker.TRACKING or plane.subsumed_by != 0:
			continue
		var transform: Transform3D = plane.transform
		expected_transforms.append(transform)
		var point: Vector2 = view.world_to_screen(transform.origin)
		var found: bool = false
		for hit: ARRaycastHit in view.raycast(point):
			if hit.plane_id == plane.plane_id:
				found = hit.pose.origin.distance_to(transform.origin) <= 0.001 and hit.frame_id == packet.frame_id
		_check(found, "projected plane centre raycasts within 1mm using the presented packet")
		if plane.alignment == ARPlaneTracker.VERTICAL:
			for hit: ARRaycastHit in view.raycast(point, PocketAR.ALIGNMENT_MASK_HORIZONTAL_UPWARD):
				_check(hit.alignment == ARPlaneTracker.HORIZONTAL_UPWARD_FACING and hit.plane_id != plane.plane_id, "horizontal alignment filter excludes vertical planes")
	for child: Node in visualizer.get_children():
		var mesh: MeshInstance3D = child as MeshInstance3D
		_check(mesh != null and mesh.global_transform in expected_transforms and mesh.mesh != null and mesh.mesh.get_surface_count() == 1, "visualizer creates polygon meshes only at active plane poses")
	visualizer.enabled = false
	_check(not visualizer.visible, "plane visualizer can be hidden")
	visualizer.enabled = true
	_check(visualizer.visible and visualizer.plane_count == 2, "plane visualizer can be restored")


func _check_helpers(view: ARView) -> void:
	var point: Vector2 = view.world_to_screen(TABLE_CENTER)
	var expected := Vector2(320.0, 418.5) if view.size.x > view.size.y else Vector2(540.0, 1506.0)
	_check(point.distance_to(expected) < 0.001, "world_to_screen projects calibrated table centre")
	_check(point.y > view.size.y * 0.5, "table centre projects below the view centre")
	var down: Vector2 = view.world_to_screen(TABLE_CENTER + Vector3.DOWN * 0.1) - point
	_check(absf(down.x) < 0.001 and down.y > 0.0, "world gravity points down the screen")
	_check(view.screen_to_point(point, 2.0).distance_to(TABLE_CENTER) < 0.001, "screen_to_point roundtrips at camera depth")
	var viewport_point: Vector2 = view.get_global_transform_with_canvas() * point
	_check(view.view_point(viewport_point).distance_to(point) < 0.0001, "view_point reverses the scaled canvas transform")


func _present(view: ARView) -> void:
	if DisplayServer.get_name() == "headless":
		view.call("_apply_packet")
		view.interface.call("mark_presented")
	else:
		await RenderingServer.frame_post_draw


func _flush_notifications(host: Node) -> void:
	await host.get_tree().process_frame
	await host.get_tree().process_frame


func _snapshot(packet: ARFramePacket, baseline: int) -> Dictionary:
	if packet == null:
		return {}
	var planes: Array[Dictionary] = packet.planes
	for plane: Dictionary in planes:
		plane["session_generation"] = int(plane["session_generation"]) - baseline
	return {
		"generation": packet.session_generation - baseline,
		"frame": packet.frame_id,
		"timestamp": packet.timestamp_usec,
		"pose": packet.camera_transform,
		"projection": packet.projection,
		"image_size": packet.image_size,
		"viewport_size": packet.viewport_size,
		"intrinsics": packet.intrinsics,
		"display_transform": packet.display_transform,
		"tracking": packet.tracking_valid,
		"planes": planes,
	}


func _tracker_names(generation: int) -> PackedStringArray:
	var names := PackedStringArray()
	for name: StringName in XRServer.get_trackers(XRServer.TRACKER_ANCHOR):
		if String(name).begins_with("pocketar/plane/%d/" % generation):
			names.append(String(name))
	names.sort()
	return names


func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
		printerr("SCRIPTED FAIL " + message)
