extends SceneTree

var failures: int = 0


func _initialize() -> void:
	_run.call_deferred()


func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("REENTRANCY FAIL " + message)


func _run() -> void:
	var interface := ScriptedARInterface.new()
	interface.set_plane_finding_mode(PocketAR.PLANE_FINDING_HORIZONTAL_AND_VERTICAL)
	_check(interface.load_fixture("res://harness/fixtures/table.jsonl") == OK, "fixture loads")
	interface.advance_to(0)
	XRServer.add_interface(interface)
	_check(interface.initialize(), "scripted initialization")
	await process_frame
	var resets: Array[int] = []
	var on_reason: Callable = func(reason: int) -> void:
		if reason == PocketAR.INSUFFICIENT_FEATURES:
			interface.reset()
			resets.append(interface.get_session_generation())
	interface.tracking_reason_changed.connect(on_reason)
	interface.advance_to(1000000)
	_check(resets.is_empty(), "notifications do not interrupt the native operation")
	_check(interface.get_current_packet() != null, "advance publishes before notifying consumers")
	await process_frame
	_check(resets.size() == 1, "consumer reset receives the tracking-loss notification")
	_check(interface.get_current_packet() == null, "callback reset clears the completed frame")
	_check(XRServer.get_trackers(XRServer.TRACKER_ANCHOR).is_empty(), "outer advance cannot restore pre-reset planes")
	interface.tracking_reason_changed.disconnect(on_reason)
	on_reason = Callable()
	interface.uninitialize()
	XRServer.remove_interface(interface)
	interface = null
	await process_frame
	print("REENTRANCY RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
