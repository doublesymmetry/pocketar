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

func _fixture(semantics: String, version: int, invalid_slot: bool = false) -> String:
	var header: Dictionary = {"format": "pocketar-session", "version": version, "semantics": semantics, "image_size": [64, 48], "intrinsics": {"fx": 52, "fy": 51, "cx": 32, "cy": 24}}
	var plane: Dictionary = {"id": 7, "alignment": "horizontal_upward_facing", "transform": _matrix(Transform3D(Basis(), Vector3(0, -0.7, -2))), "extents": [4, 4], "polygon": [[-2, -2], [2, -2], [2, 2], [-2, 2]], "tracking_state": "tracking", "subsumed_by": 0}
	var frames: Array[Dictionary] = []
	var correction := Transform3D(Basis(Vector3.UP, deg_to_rad(5)), Vector3(0.2, 0, 0))
	for id: int in range(1, 9):
		frames.append(_frame(id, correction if id >= 4 else Transform3D.IDENTITY))
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
	var path: String = "user://anchor-test-%s-%d-%s.jsonl" % [semantics, version, invalid_slot]
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

func _replay(semantics: String, version: int) -> Array:
	var interface := ScriptedARInterface.new()
	interface.set_plane_finding_mode(PocketAR.PLANE_FINDING_HORIZONTAL_AND_VERTICAL)
	_check(interface.load_fixture(_fixture(semantics, version)) == OK, "fixture loads")
	XRServer.add_interface(interface)
	_check(interface.initialize(), "initialize")
	var first := _present(interface, 100000)
	var requested := Transform3D(Basis(), Vector3(0, -0.7, -2))
	var hits := PocketAR.raycast_planes(first, {"origin": Vector3.ZERO, "direction": requested.origin.normalized()})
	_check(hits.size() == 1, "source hit")
	_check(XRServer.get_trackers(XRServer.TRACKER_ANCHOR).size() == 1, "raycast creates no implicit anchor")
	_check(interface.create_anchor(Transform3D(Basis.from_scale(Vector3(2, 1, 1)), Vector3.ZERO)) == null, "scaled pose refused")
	_check(interface.create_anchor(Transform3D(Basis.from_scale(Vector3(-1, 1, 1)), Vector3.ZERO)) == null, "reflection refused")
	_check(interface.create_anchor(Transform3D(Basis(), Vector3(NAN, 0, 0))) == null, "NaN refused")
	var anchor := interface.create_anchor_from_hit(hits[0])
	_check(anchor != null and anchor.anchor_id == 1, "failed calls do not consume slots")
	_check(anchor.source_plane_id == 7, "hit provenance")
	var other := interface.create_anchor(requested.translated(Vector3(0.3, 0, 0)))
	_check(other.anchor_id == 2, "successful slots increase")
	interface.remove_anchor(other)
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
		_check(not (corrected.physical_camera_transform.affine_inverse() * before).is_equal_approx(relative), "fixed numeric pose shifts")
	interface.mark_presented()
	var late := interface.create_anchor(requested)
	_check(late != null, "create after correction")
	_check(interface.create_anchor_from_hit(hits[0]) == null, "stale frame hit refused")
	_check(retained.physical_camera_transform == Transform3D.IDENTITY, "retained packet camera isolated")
	var corrected_hits := PocketAR.raycast_planes(corrected, {"origin": corrected.physical_camera_transform.origin, "direction": (anchor.transform.origin - corrected.physical_camera_transform.origin).normalized()})
	interface.advance_to(500000)
	_check(not corrected_hits.is_empty() and interface.create_anchor_from_hit(corrected_hits[0]) != null, "an unrelated anchor pause does not stale a presented hit")
	_present(interface, 500000)
	_check(late.transform.is_equal_approx(requested), "creation stores inverse of the current map transform")
	_check(anchor.tracking_state == (ARAnchorTracker.PAUSED if version == 2 else ARAnchorTracker.TRACKING), "independent anchor pause")
	_check(third.tracking_state == ARAnchorTracker.TRACKING, "other anchor remains tracking")
	_present(interface, 600000)
	_check(anchor.tracking_state == ARAnchorTracker.TRACKING, "anchor recovery")
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
	interface.mark_applied(retained)
	_check(anchor.tracking_state == ARAnchorTracker.STOPPED, "old packet cannot resurrect")
	interface.uninitialize()
	XRServer.remove_interface(interface)
	return results

func _run() -> void:
	for semantics: String in ["arkit", "arcore"]:
		for version: int in [1, 2]:
			_check(_replay(semantics, version) == _replay(semantics, version), "normalized replay repeats")
	var resumed := ScriptedARInterface.new()
	resumed.load_fixture(_fixture("arkit", 1))
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
	invalid.load_fixture(_fixture("arkit", 2, true))
	XRServer.add_interface(invalid)
	invalid.initialize()
	_present(invalid, 100000)
	invalid.advance_to(500000)
	_check(invalid.get_session_state() == PocketAR.FAILED and invalid.get_current_packet() == null, "unknown slot rejects replay")
	invalid.uninitialize()
	XRServer.remove_interface(invalid)
	var stopped_path := _fixture("arcore", 2)
	var file := FileAccess.open(stopped_path, FileAccess.READ_WRITE)
	file.seek_end()
	file.store_line(JSON.stringify(_frame(9, Transform3D.IDENTITY, [{"slot": 1, "tracking_state": "tracking"}])))
	file.close()
	var stopped := ScriptedARInterface.new()
	stopped.load_fixture(stopped_path)
	XRServer.add_interface(stopped)
	stopped.initialize()
	_present(stopped, 100000)
	var anchor := stopped.create_anchor(Transform3D.IDENTITY)
	_present(stopped, 800000)
	_check(anchor.tracking_state == ARAnchorTracker.STOPPED, "recorded stop is terminal")
	stopped.advance_to(900000)
	_check(stopped.get_session_state() == PocketAR.FAILED, "stopped slot cannot be updated")
	stopped.uninitialize()
	XRServer.remove_interface(stopped)
	await process_frame
	print("ANCHORS REPLAY RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
