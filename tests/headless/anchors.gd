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
	XRServer.add_interface(interface)
	_check(interface.initialize(), "initialization")
	var pose := Transform3D(Basis(), Vector3(0, -0.7, -2))
	var anchor := interface.create_anchor(pose)
	_check(anchor != null, "free creation")
	_check(anchor.tracking_state == ARAnchorTracker.PAUSED, "starts paused")
	_check(anchor.transform == pose, "initial pose preserved")
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
	_check(anchor.tracking_state == ARAnchorTracker.PAUSED, "sampling does not apply")
	interface.mark_applied(packet)
	_check(anchor.tracking_state == ARAnchorTracker.TRACKING, "application tracks")
	_check(anchor.transform == pose, "camera motion does not move world anchor")
	interface.set_suspended(true)
	_check(anchor.tracking_state == ARAnchorTracker.PAUSED, "pause invalidates synchronously")
	_check(not anchor.get_pose("default").has_tracking_data, "pause invalidates the pose")
	interface.mark_applied(packet)
	_check(anchor.tracking_state == ARAnchorTracker.PAUSED, "old packet cannot revive paused anchor")
	interface.set_suspended(false)
	for _frame: int in range(4):
		await process_frame
		if interface.get_current_packet() != null:
			break
	interface.mark_applied(interface.get_current_packet())
	_check(anchor.tracking_state == ARAnchorTracker.TRACKING, "fresh resume tracks same object")
	interface.remove_anchor(anchor)
	interface.remove_anchor(anchor)
	_check(anchor.tracking_state == ARAnchorTracker.STOPPED, "removal is terminal and idempotent")
	_check(anchor.transform == pose, "stopped reference retains last pose")
	_check(XRServer.get_tracker(anchor.get_tracker_name()) == null, "removal unregisters")
	interface.uninitialize()
	XRServer.remove_interface(interface)
	await process_frame
	print("ANCHORS RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
