extends SceneTree

var failures: int = 0

func _initialize() -> void:
	_run.call_deferred()

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("ANCHORS REPLAY FAIL " + message)

func _matrix(pose: Transform3D) -> Array:
	return [pose.basis.x.x, pose.basis.x.y, pose.basis.x.z, 0, pose.basis.y.x, pose.basis.y.y, pose.basis.y.z, 0, pose.basis.z.x, pose.basis.z.y, pose.basis.z.z, 0, pose.origin.x, pose.origin.y, pose.origin.z, 1]

func _frame(id: int, map_pose: Transform3D, updates: Array = []) -> Dictionary:
	return {"t_usec": id * 100000, "frame_id": id, "camera_transform": _matrix(Transform3D.IDENTITY), "map_transform": _matrix(map_pose), "tracking_valid": true, "session_state": "tracking", "tracking_reason": "none", "anchor_updates": updates}

func _fixture(version: int, invalid_slot: bool = false) -> String:
	var header: Dictionary = {"format": "pocketar-session", "version": version, "semantics": "arkit", "image_size": [64, 48], "intrinsics": {"fx": 52, "fy": 51, "cx": 32, "cy": 24}}
	var plane: Dictionary = {"id": 7, "alignment": "horizontal_upward_facing", "transform": _matrix(Transform3D(Basis(), Vector3(0, -0.7, -2))), "extents": [4, 4], "polygon": [[-2, -2], [2, -2], [2, 2], [-2, 2]], "tracking_state": "tracking", "subsumed_by": 0}
	var frames: Array[Dictionary] = []
	var correction := Transform3D(Basis(Vector3.UP, deg_to_rad(5)), Vector3(0.2, 0, 0))
	var recovery := Transform3D(Basis(Vector3.UP, deg_to_rad(-5)), Vector3(-0.2, 0, 0))
	for id: int in range(1, 9):
		frames.append(_frame(id, recovery if id >= 5 else correction if id >= 4 else Transform3D.IDENTITY))
	frames[0]["planes"] = [plane]
	plane = plane.duplicate(true)
	plane["transform"] = _matrix(Transform3D(Basis(), Vector3(0.4, -0.7, -2)))
	plane["extents"] = [5, 4]
	plane["polygon"] = [[-2.5, -2], [2.5, -2], [2.5, 2], [-2.5, 2]]
	frames[2]["planes"] = [plane]
	frames[4]["anchor_updates"] = [{"slot": 99 if invalid_slot else 1, "tracking_state": "paused"}]
	frames[5]["anchor_updates"] = [{"slot": 1, "tracking_state": "tracking"}]
	frames[6]["planes"] = []
	frames[7]["anchor_updates"] = [{"slot": 1, "tracking_state": "stopped"}]
	var path: String = "user://anchor-test-%d-%s.jsonl" % [version, invalid_slot]
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_line(JSON.stringify(header))
	for frame: Dictionary in frames:
		if version == 1:
			frame.erase("map_transform")
			frame.erase("anchor_updates")
		file.store_line(JSON.stringify(frame))
	file.close()
	return path

func _present(interface: ScriptedARInterface, time: int) -> ARFramePacket:
	interface.advance_to(time)
	var packet := interface.get_current_packet()
	interface.mark_applied(packet)
	interface.mark_presented()
	return packet

func _replay(version: int) -> Array:
	var interface := ScriptedARInterface.new()
	interface.set_plane_finding_mode(PocketAR.PLANE_FINDING_HORIZONTAL_AND_VERTICAL)
	_check(interface.load_fixture(_fixture(version)) == OK, "fixture loads")
	XRServer.add_interface(interface)
	_check(interface.initialize(), "initialize")
	var first := _present(interface, 100000)
	var requested := Transform3D(Basis(), Vector3(0, -0.7, -2))
	var hits := PocketAR.raycast_planes(first, {"origin": Vector3.ZERO, "direction": requested.origin.normalized()})
	_check(hits.size() == 1, "source hit")
	_check(interface.create_anchor(Transform3D(Basis.from_scale(Vector3(2, 1, 1)), Vector3.ZERO)) == null, "scaled pose refused")
	var anchor := interface.create_anchor_from_hit(hits[0])
	_check(anchor != null and anchor.anchor_id == 1, "failed calls do not consume slots")
	_check(anchor.source_plane_id == 7, "hit provenance")
	var other := interface.create_anchor(requested.translated(Vector3(0.3, 0, 0)))
	_check(other.anchor_id == 2, "successful slots increase")
	interface.remove_anchor(other)
	var third := interface.create_anchor(requested)
	_check(third.anchor_id == 3, "removed slot not recycled")
	var retained := _present(interface, 200000)
	_check(anchor.tracking_state == ARAnchorTracker.TRACKING, "pending to tracking")
	var relative: Transform3D = retained.physical_camera_transform.affine_inverse() * anchor.transform
	var before: Transform3D = anchor.transform
	_present(interface, 300000)
	_check(anchor.transform.is_equal_approx(before), "plane extent growth does not move anchor")
	interface.set_plane_finding_mode(PocketAR.PLANE_FINDING_DISABLED)
	interface.advance_to(400000)
	_check(anchor.transform.is_equal_approx(before), "acquisition does not advance tracker")
	_check(interface.get_presented_packet().frame_id == 3, "presentation stays old")
	interface.mark_applied(interface.get_current_packet())
	var corrected := interface.get_current_packet()
	_check((corrected.physical_camera_transform.affine_inverse() * anchor.transform).is_equal_approx(relative), "camera/anchor map correction invariant")
	if version == 2:
		_check(not anchor.transform.is_equal_approx(before), "map correction moves world anchor")
	interface.mark_presented()
	var late := interface.create_anchor(requested)
	_check(late != null, "create after correction")
	_check(interface.create_anchor_from_hit(hits[0]) == null, "stale frame hit refused")
	_check(retained.physical_camera_transform == Transform3D.IDENTITY, "retained packet camera isolated")
	var corrected_hits := PocketAR.raycast_planes(corrected, {"origin": corrected.physical_camera_transform.origin, "direction": (anchor.transform.origin - corrected.physical_camera_transform.origin).normalized()})
	var before_pause: Transform3D = anchor.transform
	interface.advance_to(500000)
	if version == 2:
		_check(not corrected_hits.is_empty() and interface.create_anchor_from_hit(corrected_hits[0]) == null, "anchor pause invalidates pre-pause hit samples")
	_present(interface, 500000)
	var recovery := Transform3D(Basis(Vector3.UP, deg_to_rad(-5)), Vector3(-0.2, 0, 0)) if version == 2 else Transform3D.IDENTITY
	_check(late.transform.is_equal_approx(recovery * corrected.physical_camera_transform.affine_inverse() * requested), "creation stores inverse of the current map transform")
	if version == 2:
		_check(anchor.transform.is_equal_approx(before_pause), "pause freezes the pose across a map correction")
		_check(not anchor.get_pose("default").has_tracking_data, "pause invalidates the pose")
	_check(anchor.tracking_state == (ARAnchorTracker.PAUSED if version == 2 else ARAnchorTracker.TRACKING), "independent anchor pause")
	_check(third.tracking_state == ARAnchorTracker.TRACKING, "other anchor remains tracking")
	_present(interface, 600000)
	_check(anchor.tracking_state == ARAnchorTracker.TRACKING, "anchor recovery")
	_check(anchor.transform.is_equal_approx(recovery * before), "recovery applies the fresh mapped pose")
	interface.set_plane_finding_mode(PocketAR.PLANE_FINDING_HORIZONTAL_AND_VERTICAL)
	_present(interface, 700000)
	_check(anchor.tracking_state == ARAnchorTracker.TRACKING, "plane removal retains anchor")
	var results: Array = [anchor.anchor_id, anchor.source_plane_id, _matrix(anchor.transform), anchor.tracking_state]
	_present(interface, 800000)
	if version == 2:
		_check(anchor.tracking_state == ARAnchorTracker.STOPPED, "terminal slot loss")
	interface.set_suspended(true)
	interface.reset()
	_check(anchor.tracking_state == ARAnchorTracker.STOPPED and third.tracking_state == ARAnchorTracker.STOPPED, "reset while paused stops all")
	interface.uninitialize()
	XRServer.remove_interface(interface)
	return results

func _plane_mapping() -> void:
	var path: String = _fixture(2)
	var file := FileAccess.open(path, FileAccess.READ)
	var header: String = file.get_line()
	var first: Dictionary = JSON.parse_string(file.get_line())
	file.close()
	first["t_usec"] = 100000
	first["frame_id"] = 1
	first["planes"][0]["id"] = 7
	first["planes"][0]["subsumed_by"] = 0
	var correction := Transform3D(Basis(Vector3.UP, deg_to_rad(5)), Vector3(0.2, 0, 0))
	first["map_transform"] = _matrix(correction)
	file = FileAccess.open(path, FileAccess.WRITE)
	file.store_line(header)
	file.store_line(JSON.stringify(first))
	file.store_line(JSON.stringify(_frame(2, Transform3D.IDENTITY)))
	file.store_line(JSON.stringify(_frame(3, correction)))
	file.close()
	var interface := ScriptedARInterface.new()
	interface.set_plane_finding_mode(PocketAR.PLANE_FINDING_HORIZONTAL_AND_VERTICAL)
	interface.load_fixture(path)
	XRServer.add_interface(interface)
	interface.initialize()
	var added: Array[Transform3D] = []
	var on_added: Callable = func(name: StringName, _type: int) -> void:
		var tracker: XRTracker = XRServer.get_tracker(name)
		if tracker is ARPlaneTracker:
			added.append((tracker as ARPlaneTracker).transform)
	XRServer.tracker_added.connect(on_added)
	_present(interface, 100000)
	XRServer.tracker_added.disconnect(on_added)
	var original := Transform3D(Basis.IDENTITY, Vector3(0, -0.7, -2))
	_check(added.size() == 1 and added[0].is_equal_approx(correction * original), "new plane tracker publishes only a mapped pose")
	var plane: ARPlaneTracker = XRServer.get_tracker("pocketar/plane/%d/7" % interface.get_session_generation()) as ARPlaneTracker
	var poses: Array[Transform3D] = []
	plane.pose_changed.connect(func(pose: XRPose) -> void: poses.append(pose.transform))
	var changes: Array[int] = [0]
	interface.planes_changed.connect(func() -> void: changes[0] += 1)
	await process_frame
	changes[0] = 0
	_present(interface, 200000)
	_check(poses.size() == 1 and poses[0].is_equal_approx(original), "map-only frame applies one mapped plane pose")
	await process_frame
	_check(changes[0] == 1, "map-only frame emits planes_changed")
	plane.pose_changed.connect(func(_pose: XRPose) -> void: interface.set_suspended(true), CONNECT_ONE_SHOT)
	interface.advance_to(300000)
	_check(interface.get_session_state() == PocketAR.PAUSED and interface.get_current_packet() == null, "plane callback suspension prevents packet publication")
	interface.uninitialize()
	XRServer.remove_interface(interface)

func _run() -> void:
	await _plane_mapping()
	for version: int in [1, 2]:
		_check(_replay(version) == _replay(version), "normalized replay repeats")
	var resumed := ScriptedARInterface.new()
	resumed.load_fixture(_fixture(1))
	XRServer.add_interface(resumed)
	resumed.initialize()
	_present(resumed, 100000)
	var resumed_anchor := resumed.create_anchor(Transform3D.IDENTITY)
	_present(resumed, 200000)
	resumed.set_suspended(true)
	resumed.set_suspended(false)
	resumed.set_display_geometry(Vector2i(100, 80), 0, 0.05, 100)
	for _frame: int in range(3):
		await process_frame
	resumed.mark_applied(resumed.get_current_packet())
	_check(resumed_anchor.tracking_state == ARAnchorTracker.PAUSED, "cached pre-pause fixture frame cannot recover an anchor")
	_present(resumed, 300000)
	_check(resumed_anchor.tracking_state == ARAnchorTracker.TRACKING, "fresh fixture frame recovers the same anchor")
	resumed.uninitialize()
	XRServer.remove_interface(resumed)
	var invalid := ScriptedARInterface.new()
	invalid.load_fixture(_fixture(2, true))
	XRServer.add_interface(invalid)
	invalid.initialize()
	_present(invalid, 100000)
	var invalid_anchor := invalid.create_anchor(Transform3D.IDENTITY)
	invalid.advance_to(500000)
	_check(invalid.get_session_state() == PocketAR.FAILED and invalid.get_current_packet() == null, "unknown slot rejects replay")
	_check(invalid_anchor.tracking_state == ARAnchorTracker.STOPPED, "failed replay stops existing anchors")
	invalid.uninitialize()
	XRServer.remove_interface(invalid)
	var stopped_path := _fixture(2)
	var file := FileAccess.open(stopped_path, FileAccess.READ_WRITE)
	file.seek_end()
	file.store_line(JSON.stringify(_frame(9, Transform3D.IDENTITY, [{"slot": 1, "tracking_state": "tracking"}])))
	file.close()
	var stopped := ScriptedARInterface.new()
	stopped.load_fixture(stopped_path)
	XRServer.add_interface(stopped)
	stopped.initialize()
	_present(stopped, 100000)
	stopped.create_anchor(Transform3D.IDENTITY)
	stopped.advance_to(900000)
	_check(stopped.get_session_state() == PocketAR.FAILED, "stopped slot cannot be updated")
	stopped.uninitialize()
	XRServer.remove_interface(stopped)
	await process_frame
	print("ANCHORS REPLAY RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
