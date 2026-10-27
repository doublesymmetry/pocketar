extends SceneTree

var failures: int = 0


func _initialize() -> void:
	_run.call_deferred()


func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("SCRIPTED CLOCK FAIL " + message)


func _run() -> void:
	var interface := ScriptedARInterface.new()
	interface.set_plane_finding_mode(PocketAR.PLANE_FINDING_HORIZONTAL_AND_VERTICAL)
	_check(interface.load_fixture("res://suites/fixtures/table.jsonl") == OK, "fixture loads")
	interface.advance_to(500000)
	interface.set_display_geometry(Vector2i(640, 480), 0, 0.05, 100.0)
	XRServer.add_interface(interface)
	_check(interface.initialize(), "manual clock initializes")
	var first: ARFramePacket = interface.get_current_packet()
	interface.mark_presented()
	await create_timer(0.12).timeout
	_check(interface.get_current_packet() == first and first.frame_id == 16, "external clock does not follow elapsed wall time")
	interface.set_display_geometry(Vector2i(480, 640), 1, 0.05, 100.0)
	var pending: Projection = interface.get_projection_for_view(0, 640.0 / 480.0, 0.05, 100.0)
	for column: int in range(4):
		_check(pending[column].is_equal_approx(first.projection[column]), "pending geometry cannot alter current XR projection")
	interface.advance_to(500000)
	var rotated: ARFramePacket = interface.get_current_packet()
	_check(rotated != first and rotated.frame_id == first.frame_id and rotated.timestamp_usec == first.timestamp_usec, "geometry rebuild preserves the fixture frame identity")
	_check(rotated.quarter_turns == 1 and rotated.viewport_size == Vector2(480, 640), "geometry rebuild uses the requested view")
	_check(interface.get_presented_packet() == first, "reprojection does not advance presentation")
	interface.uninitialize()
	await process_frame
	await process_frame
	var generations: Array[int] = []
	interface.session_generation_changed.connect(func(value: int) -> void: generations.append(value))
	interface.advance_to(0)
	_check(interface.initialize(), "same interface reinitializes")
	await process_frame
	await process_frame
	_check(generations == [interface.get_session_generation()], "reinitialization publishes its generation to a new listener")
	interface.uninitialize()
	XRServer.remove_interface(interface)
	interface = null
	await process_frame
	print("SCRIPTED CLOCK RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
