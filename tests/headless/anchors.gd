extends SceneTree

var failures: int = 0

func _initialize() -> void:
	_run.call_deferred()

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("ANCHORS FAIL " + message)

func _run() -> void:
	var interface := SyntheticARInterface.new()
	_check(interface.has_method("create_anchor"), "anchor creation API exists")
	if not interface.has_method("create_anchor"):
		quit(1)
		return
	XRServer.add_interface(interface)
	_check(interface.initialize(), "initialization")
	var pose := Transform3D(Basis(), Vector3(0, -0.7, -2))
	var anchor: Object = interface.call("create_anchor", pose)
	_check(anchor != null, "free creation")
	_check(anchor.get("tracking_state") == 1, "starts paused")
	_check(anchor.get("transform") == pose, "initial pose preserved")
	_check(anchor.get_tracker_type() == XRServer.TRACKER_ANCHOR, "tracker category")
	_check(anchor.get_tracker_name() == "pocketar/anchor/%d/1" % interface.get_session_generation(), "tracker name")
	_check(not anchor.get_pose("default").has_tracking_data, "pending pose invalid")
	var previous := interface.get_current_packet()
	interface.set_pose(Transform3D(Basis(), Vector3(1, 0, 0)))
	interface.set_display_geometry(Vector2i(640, 480), 0, 0.05, 100)
	for _frame: int in range(4):
		await process_frame
		if interface.get_current_packet() != previous:
			break
	var packet: ARFramePacket = interface.get_current_packet()
	_check(anchor.get("tracking_state") == 1, "sampling does not apply")
	interface.call("mark_applied", packet)
	_check(anchor.get("tracking_state") == 0, "application tracks")
	_check(anchor.get("transform") == pose, "camera motion does not move world anchor")
	interface.set_suspended(true)
	_check(anchor.get("tracking_state") == 1, "pause invalidates synchronously")
	interface.call("mark_applied", packet)
	_check(anchor.get("tracking_state") == 1, "old packet cannot revive paused anchor")
	interface.set_suspended(false)
	for _frame: int in range(4):
		await process_frame
		if interface.get_current_packet() != null:
			break
	interface.call("mark_applied", interface.get_current_packet())
	_check(anchor.get("tracking_state") == 0, "fresh resume tracks same object")
	interface.call("remove_anchor", anchor)
	interface.call("remove_anchor", anchor)
	_check(anchor.get("tracking_state") == 2, "removal is terminal and idempotent")
	_check(anchor.get("transform") == pose, "stopped reference retains last pose")
	_check(XRServer.get_tracker(anchor.get_tracker_name()) == null, "removal unregisters")
	interface.uninitialize()
	XRServer.remove_interface(interface)
	await process_frame
	print("ANCHORS RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
