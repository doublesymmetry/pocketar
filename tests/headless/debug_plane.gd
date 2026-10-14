extends SceneTree

var failures: int = 0
var changes: int = 0
const POSE := Transform3D(Basis.IDENTITY, Vector3(0, -1, -2))
const EXTENTS := Vector2(2, 4)


func _initialize() -> void:
	_run.call_deferred()


func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("DEBUG PLANE FAIL " + message)


func _tracker(generation: int, id: int) -> ARPlaneTracker:
	return XRServer.get_tracker("pocketar/plane/%d/%d" % [generation, id]) as ARPlaneTracker


func _hits(packet: ARFramePacket, point: Vector3, direction: Vector3 = Vector3.DOWN) -> Array[ARRaycastHit]:
	return PocketAR.raycast_planes(packet, {"origin": point, "direction": direction})


func _run() -> void:
	for name: String in ["SyntheticARInterface", "ScriptedARInterface"]:
		_check(ClassDB.class_has_method(name, "set_debug_plane") and ClassDB.class_has_method(name, "clear_debug_plane"), name + " debug API")
	if failures:
		quit(1)
		return
	var scene: PackedScene = load("res://addons/pocketar/ar_view.tscn") as PackedScene
	var view: ARView = scene.instantiate() as ARView
	var interface := ScriptedARInterface.new()
	var fixture: String = get_script().resource_path.get_base_dir().path_join("debug_plane.jsonl")
	_check(interface.load_fixture(fixture) == OK, "plane-free fixture loads")
	view.backend = 3
	view.interface = interface
	root.add_child(view)
	var visualizer := ARPlaneVisualizer.new()
	visualizer.ar_view = view
	view.scene_root.add_child(visualizer)
	interface.mark_presented()
	var empty: ARFramePacket = interface.get_presented_packet()
	_check(empty.planes.is_empty(), "normal plane-free run")
	await process_frame
	interface.planes_changed.connect(func() -> void: changes += 1)
	var id: int = interface.call("set_debug_plane", POSE, EXTENTS)
	var generation: int = interface.get_session_generation()
	_check(id < 0, "negative identity")
	_check(interface.get_current_packet() == empty and _tracker(generation, id) == null, "set waits for publication")
	interface.advance_to(0)
	_check(interface.get_current_packet() == empty, "repeated frame does not publish pending plane")
	interface.set_plane_finding_mode(PocketAR.PLANE_FINDING_DISABLED)
	interface.advance_to(1000)
	var packet: ARFramePacket = interface.get_current_packet()
	_check(packet.planes.size() == 1, "scripted merge survives disabled detection")
	if packet.planes.size() != 1:
		view.free()
		quit(1)
		return
	var plane: Dictionary = packet.planes[0].duplicate(true)
	_check(plane.plane_id == id and plane.session_generation == generation and plane.debug, "snapshot identity and marker")
	_check(plane.transform == POSE and plane.extents == EXTENTS and plane.alignment == ARPlaneTracker.HORIZONTAL_UPWARD_FACING and plane.subsumed_by == 0, "snapshot geometry")
	_check(plane.polygon == PackedVector2Array([Vector2(-1, -2), Vector2(1, -2), Vector2(1, 2), Vector2(-1, 2)]), "full-length rectangle corners")
	var tracker: ARPlaneTracker = _tracker(generation, id)
	_check(tracker != null and tracker.transform == POSE and tracker.extents == EXTENTS and tracker.polygon == plane.polygon and tracker.tracking_state == ARPlaneTracker.TRACKING, "snapshot tracker correspondence")
	if tracker == null:
		view.free()
		quit(1)
		return
	_check(interface.get_plane_count() == 0, "scripted count excludes debug plane")
	var copied: Dictionary = packet.planes[0]
	copied.debug = false
	copied.plane_id = 900
	copied.polygon[0] = Vector2(9, 9)
	_check(packet.planes[0] == plane, "dictionary and polygon copies are immutable")
	_check(_hits(packet, Vector3(0, 1, -2)).size() == 1, "inside ray")
	_check(_hits(packet, Vector3(1, 1, -4)).size() == 1, "boundary ray")
	_check(_hits(packet, Vector3(1.01, 1, -2)).is_empty(), "outside ray")
	_check(_hits(packet, Vector3(0, -2, -2), Vector3.UP).is_empty(), "back-face ray")
	_check(interface.get_presented_packet() == empty and empty.planes.is_empty(), "addition preserves presented and retained packets")
	interface.mark_presented()
	var point: Vector2 = view.world_to_screen(POSE.origin)
	_check(view.raycast(point, PocketAR.ALIGNMENT_MASK_HORIZONTAL_UPWARD).size() == 1 and view.raycast(point, PocketAR.ALIGNMENT_MASK_VERTICAL).is_empty(), "ARView alignment filter")
	view.packet_applied.emit(packet)
	_check(visualizer.plane_count == 1 and visualizer.get_child(0) is MeshInstance3D and (visualizer.get_child(0) as MeshInstance3D).mesh.get_surface_count() == 1, "visualizer creates rectangle mesh")
	await process_frame
	_check(changes == 1, "one deferred notification for addition")
	interface.advance_to(2000)
	await process_frame
	_check(changes == 1, "unchanged merged set is silent")
	for bad: Transform3D in [Transform3D(Basis.from_scale(Vector3(2, 1, 1)), Vector3.ZERO), Transform3D(Basis.from_scale(Vector3(2, 1, 0.5)), Vector3.ZERO), Transform3D(Basis.from_scale(Vector3(1.001, 1, 1.0 / 1.001)), Vector3.ZERO), Transform3D(Basis(Vector3.RIGHT, 0.1), Vector3.ZERO), Transform3D(Basis.from_scale(Vector3(-1, 1, 1)), Vector3.ZERO), Transform3D(Basis.IDENTITY, Vector3(INF, 0, 0))]:
		_check(int(interface.call("set_debug_plane", bad, EXTENTS)) == 0, "invalid transform preserves plane")
	for bad: Vector2 in [Vector2.ZERO, Vector2(-1, 1), Vector2(INF, 1), Vector2(NAN, 1)]:
		_check(int(interface.call("set_debug_plane", POSE, bad)) == 0, "invalid extents preserve plane")
	var worker := Thread.new()
	worker.start(func() -> int:
		var rejected: int = interface.call("set_debug_plane", POSE, EXTENTS)
		interface.call("clear_debug_plane")
		return rejected)
	_check(int(worker.wait_to_finish()) == 0, "foreign-thread set is rejected")
	interface.advance_to(3000)
	_check(interface.get_current_packet().planes.size() == 1, "foreign-thread clear preserves plane")
	if interface.get_current_packet().planes.size() != 1:
		view.free()
		quit(1)
		return
	_check(interface.get_current_packet().planes[0] == plane, "rejected geometry leaves installed plane")
	interface.advance_to(4000)
	_check(interface.get_presented_packet() == null and view.raycast(point).is_empty(), "limited tracking drops presentation before post-draw")
	var limited: ARFramePacket = interface.get_current_packet()
	_check(limited.tracking_valid and limited.planes[0].tracking_state == ARPlaneTracker.PAUSED and _hits(limited, Vector3(0, 1, -2)).is_empty(), "limited camera pose pauses debug plane")
	_check(tracker.tracking_state == ARPlaneTracker.PAUSED, "limited camera pauses tracker")
	interface.mark_presented()
	_check(interface.get_presented_packet() == limited and limited.planes[0].tracking_state == ARPlaneTracker.PAUSED, "post-draw presents the limited packet with its plane paused")
	interface.advance_to(5000)
	_check(not interface.get_current_packet().tracking_valid and interface.get_current_packet().planes[0].tracking_state == ARPlaneTracker.PAUSED, "lost camera pauses plane")
	interface.advance_to(6000)
	interface.mark_presented()
	_check(interface.get_current_packet().planes[0].plane_id == id and tracker.tracking_state == ARPlaneTracker.TRACKING, "tracking recovery preserves identity")
	interface.application_pause()
	_check(interface.get_presented_packet() == null and view.raycast(point).is_empty(), "pause drops presentation before post-draw")
	interface.mark_presented()
	_check(tracker.tracking_state == ARPlaneTracker.PAUSED and view.raycast(point).is_empty(), "pause invalidates live raycasts and tracker")
	_check(packet.planes[0] == plane, "pause leaves retained snapshot immutable")
	interface.application_resume()
	_check(view.raycast(point).is_empty(), "resume waits for tracked publication")
	interface.advance_to(7000)
	interface.mark_presented()
	_check(view.raycast(point).size() == 1 and tracker.tracking_state == ARPlaneTracker.TRACKING, "resume restores plane after tracked frame")
	var replacement: int = interface.call("set_debug_plane", Transform3D(Basis(Vector3.UP, 0.4), POSE.origin), Vector2(3, 3))
	_check(replacement < 0 and replacement != id, "replacement never reuses identity")
	interface.advance_to(8000)
	_check(_tracker(generation, id) == null and _tracker(generation, replacement) != null and interface.get_current_packet().planes.size() == 1, "replacement removes stale tracker")
	var retained: ARFramePacket = interface.get_current_packet()
	interface.mark_presented()
	interface.call("clear_debug_plane")
	_check(interface.get_presented_packet() == retained, "clear waits for publication")
	interface.advance_to(9000)
	_check(interface.get_current_packet().planes.is_empty() and _tracker(generation, replacement) == null and retained.planes.size() == 1, "clear removes tracker without editing retained packet")
	view.packet_applied.emit(interface.get_current_packet())
	_check(visualizer.plane_count == 0, "visualizer removes cleared mesh")
	var reset_id: int = interface.call("set_debug_plane", POSE, EXTENTS)
	interface.advance_to(10000)
	interface.reset()
	_check(interface.get_session_generation() != generation and _tracker(generation, reset_id) == null and interface.get_current_packet() == null, "reset removes immediately before another packet")
	interface.advance_to(11000)
	_check(interface.get_current_packet().planes.is_empty(), "new generation cannot revive debug plane")
	var reload_id: int = interface.call("set_debug_plane", POSE, EXTENTS)
	interface.advance_to(12000)
	generation = interface.get_session_generation()
	_check(interface.load_fixture(fixture) == OK and _tracker(generation, reload_id) == null and interface.get_current_packet().planes.is_empty(), "fixture reload removes immediately")
	await process_frame
	var reset_callback: Callable = func() -> void:
		if interface.get_current_packet() != null and not interface.get_current_packet().planes.is_empty():
			interface.reset()
	interface.planes_changed.connect(reset_callback)
	var callback_id: int = interface.call("set_debug_plane", POSE, EXTENTS)
	interface.advance_to(1000)
	_check(interface.get_current_packet() != null, "plane signal is deferred")
	await process_frame
	_check(interface.get_current_packet() == null and XRServer.get_trackers(XRServer.TRACKER_ANCHOR).is_empty(), "deferred plane callback reset cannot restore old plane")
	interface.planes_changed.disconnect(reset_callback)
	interface.advance_to(2000)
	var teardown_id: int = interface.call("set_debug_plane", POSE, EXTENTS)
	interface.advance_to(3000)
	generation = interface.get_session_generation()
	view.free()
	_check(_tracker(generation, teardown_id) == null, "uninitialize removes tracker synchronously")
	var mixed := ScriptedARInterface.new()
	_check(mixed.load_fixture(fixture.get_base_dir().path_join("plane_finding.jsonl")) == OK, "mixed fixture loads")
	XRServer.add_interface(mixed)
	_check(mixed.initialize(), "mixed fixture initializes")
	var mixed_id: int = mixed.call("set_debug_plane", POSE, EXTENTS)
	mixed.advance_to(1000)
	var merged: Array[Dictionary] = mixed.get_current_packet().planes
	_check(merged.size() == 5 and merged[0].plane_id == 1 and merged[3].plane_id == 4 and merged[4].plane_id == mixed_id, "scripted appends after all fixture planes")
	_check(mixed.get_plane_count() == 4 and XRServer.get_trackers(XRServer.TRACKER_ANCHOR).size() == 5, "scripted native count and merged trackers stay distinct")
	mixed.uninitialize()
	XRServer.remove_interface(mixed)
	var synthetic := SyntheticARInterface.new()
	XRServer.add_interface(synthetic)
	_check(synthetic.initialize(), "synthetic initializes")
	var synthetic_id: int = synthetic.call("set_debug_plane", POSE, EXTENTS)
	synthetic.set_display_geometry(Vector2i(640, 480), 0, 0.05, 100)
	await process_frame
	await process_frame
	_check(synthetic.get_current_packet().planes.size() == 1, "synthetic packet merges plane")
	generation = synthetic.get_session_generation()
	synthetic.reset()
	_check(synthetic.get_current_packet().planes.is_empty() and _tracker(generation, synthetic_id) == null, "synthetic reset removes immediately")
	var synthetic_teardown_id: int = synthetic.call("set_debug_plane", POSE, EXTENTS)
	var issued_ids: Array[int] = []
	for issued_id: int in [id, replacement, reset_id, reload_id, callback_id, teardown_id, mixed_id, synthetic_id, synthetic_teardown_id]:
		_check(issued_id < 0 and not issued_ids.has(issued_id), "every accepted id is unique across interface recreation")
		issued_ids.append(issued_id)
	synthetic.set_display_geometry(Vector2i(641, 480), 0, 0.05, 100)
	await process_frame
	await process_frame
	synthetic.uninitialize()
	_check(XRServer.get_trackers(XRServer.TRACKER_ANCHOR).is_empty(), "synthetic uninitialize removes tracker")
	XRServer.remove_interface(synthetic)
	await process_frame
	print("DEBUG PLANE RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
