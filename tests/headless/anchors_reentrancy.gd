extends SceneTree

var failures: int = 0

func _initialize() -> void:
	_run.call_deferred()

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("ANCHOR REENTRANCY FAIL " + message)

func _tick(interface: SyntheticARInterface, size: int) -> void:
	var previous := interface.get_current_packet()
	interface.set_display_geometry(Vector2i(size, 48), 0, 0.05, 100)
	for _frame: int in range(4):
		await process_frame
		if interface.get_current_packet() != previous:
			return
	_check(false, "camera packet advances")

func _run() -> void:
	var interface := SyntheticARInterface.new()
	XRServer.add_interface(interface)
	interface.initialize()
	var pose := Transform3D(Basis(), Vector3(0, -0.7, -2))
	var pending := interface.create_anchor(pose)
	interface.remove_anchor(pending)
	await _tick(interface, 64)
	interface.mark_applied(interface.get_current_packet())
	_check(pending.tracking_state == ARAnchorTracker.STOPPED, "removed pending anchor stays stopped")
	var first := interface.create_anchor(pose)
	var second := interface.create_anchor(pose)
	var applied: Array[int] = []
	var reset_on_pose: Callable = func(_pose: XRPose) -> void:
		applied.append(1)
		interface.reset()
	first.pose_changed.connect(reset_on_pose)
	await _tick(interface, 65)
	interface.mark_applied(interface.get_current_packet())
	_check(applied.size() == 1, "engine pose callback resets once")
	_check(first.tracking_state == ARAnchorTracker.STOPPED and second.tracking_state == ARAnchorTracker.STOPPED, "reset during apply cannot revive remaining refs")
	first.pose_changed.disconnect(reset_on_pose)
	var notifications: Array[int] = []
	var current := interface.create_anchor(pose)
	current.tracking_state_changed.connect(func(state: int) -> void: notifications.append(state))
	await _tick(interface, 66)
	interface.mark_applied(interface.get_current_packet())
	interface.remove_anchor(current)
	await process_frame
	_check(notifications == [ARAnchorTracker.STOPPED], "deferred state notifications coalesce to terminal state")
	var remove_on_add: Callable = func(name: StringName, _type: int) -> void:
		if String(name).begins_with("pocketar/anchor/"):
			interface.reset()
	XRServer.tracker_added.connect(remove_on_add)
	var reset_during_add := interface.create_anchor(pose)
	_check(reset_during_add == null, "reentrant reset during registration refuses returned stopped anchor")
	XRServer.tracker_added.disconnect(remove_on_add)
	var next := interface.create_anchor(pose)
	var remove_on_signal: Callable = func() -> void:
		if interface.owns_anchor(next):
			interface.remove_anchor(next)
	interface.anchors_changed.connect(remove_on_signal)
	await process_frame
	_check(next.tracking_state == ARAnchorTracker.STOPPED, "library membership callback can remove")
	interface.anchors_changed.disconnect(remove_on_signal)
	var sibling_a := interface.create_anchor(pose)
	var sibling_b := interface.create_anchor(pose)
	await _tick(interface, 67)
	interface.mark_applied(interface.get_current_packet())
	sibling_a.pose_lost_tracking.connect(func(_pose: XRPose) -> void: interface.remove_anchor(sibling_b), CONNECT_ONE_SHOT)
	interface.reset()
	_check(sibling_a.tracking_state == ARAnchorTracker.STOPPED and sibling_b.tracking_state == ARAnchorTracker.STOPPED, "removing a sibling during clear is idempotent")
	var deferred_reset := interface.create_anchor(pose)
	deferred_reset.tracking_state_changed.connect(func(state: int) -> void:
		if state == ARAnchorTracker.TRACKING:
			interface.reset())
	await _tick(interface, 68)
	interface.mark_applied(interface.get_current_packet())
	await process_frame
	_check(deferred_reset.tracking_state == ARAnchorTracker.STOPPED, "library tracking callback can reset")
	interface.uninitialize()
	XRServer.remove_interface(interface)
	var other := SyntheticARInterface.new()
	XRServer.add_interface(other)
	other.initialize()
	_check(not other.owns_anchor(next), "interface recreation rejects old owner")
	other.remove_anchor(next)
	_check(not other.owns_anchor(ARAnchorTracker.new()), "constructed tracker rejected")
	other.uninitialize()
	XRServer.remove_interface(other)
	await process_frame
	print("ANCHOR REENTRANCY RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
