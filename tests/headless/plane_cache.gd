extends SceneTree

var failures: int = 0


func _initialize() -> void:
	_run.call_deferred()


func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("PLANE CACHE FAIL " + message)


func _run() -> void:
	var backend := ScriptedARInterface.new()
	_check(backend.has_method("get_plane_geometry_rebuild_count"), "plane geometry rebuilds are observable")
	if not backend.has_method("get_plane_geometry_rebuild_count"):
		quit(1)
		return
	backend.set_plane_finding_mode(PocketAR.PLANE_FINDING_HORIZONTAL_AND_VERTICAL)
	_check(backend.load_fixture(get_script().resource_path.get_base_dir().path_join("plane_cache.jsonl")) == OK, "fixture loads")
	XRServer.add_interface(backend)
	_check(backend.initialize(), "scripted starts")
	var packet: ARFramePacket = backend.get_current_packet()
	var original: Array[Dictionary] = packet.planes
	var rebuilt: int = backend.call("get_plane_geometry_rebuild_count")
	_check(rebuilt == 1, "first plane builds once")
	for tick: int in [1000, 2000]:
		backend.advance_to(tick)
		_check(backend.call("get_plane_geometry_rebuild_count") == rebuilt, "omitted and identical planes reuse geometry")
	var values: Array[Dictionary] = packet.planes
	values[0].polygon[0] = Vector2(99, 99)
	values[0].extents = Vector2(99, 99)
	values.clear()
	_check(packet.planes == original, "editing returned containers never changes packet values")
	var tracker: ARPlaneTracker = XRServer.get_tracker("pocketar/plane/%d/1" % backend.get_session_generation())
	var polygon: PackedVector2Array = tracker.polygon
	polygon[0] = Vector2(99, 99)
	_check(tracker.polygon[0] == Vector2(-0.5, -0.5), "editing tracker polygon never changes native geometry")
	backend.advance_to(3000)
	_check(backend.call("get_plane_geometry_rebuild_count") == rebuilt, "tracking and pose changes reuse polygon")
	_check(backend.get_current_packet().planes[0].tracking_state == ARPlaneTracker.PAUSED, "tracking transition is published")
	backend.advance_to(4000)
	_check(backend.call("get_plane_geometry_rebuild_count") == rebuilt + 1, "changed polygon builds once")
	_check(packet.planes == original, "later geometry leaves retained packet unchanged")
	backend.advance_to(5000)
	_check(backend.get_current_packet().planes.is_empty() and XRServer.get_tracker("pocketar/plane/%d/1" % backend.get_session_generation()) == null, "removal unregisters tracker")
	backend.uninitialize()
	XRServer.remove_interface(backend)
	print("PLANE CACHE RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
