extends SceneTree

var failures: int = 0


func _initialize() -> void:
	_run.call_deferred()


func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("PLANE FINDING FAIL " + message)


func _run() -> void:
	var scene: PackedScene = load("res://addons/pocketar/ar_view.tscn") as PackedScene
	var view: ARView = scene.instantiate() as ARView
	var interface := ScriptedARInterface.new()
	var fixture: String = get_script().resource_path.get_base_dir().path_join("plane_finding.jsonl")
	_check(interface.load_fixture(fixture) == OK, "plane fixture loads")
	view.backend = ARView.Backend.SCRIPTED
	view.interface = interface
	root.add_child(view)
	var first: ARFramePacket = interface.get_current_packet()
	var frozen: Array[Dictionary] = first.planes
	_check(frozen.size() == 3, "default finds upward, downward and vertical planes")
	interface.mark_presented()
	await process_frame
	var changes: Array[int] = []
	view.planes_changed.connect(func() -> void: changes.append(interface.get_plane_count()))
	view.plane_finding_mode = PocketAR.PLANE_FINDING_DISABLED
	interface.advance_to(1000)
	_check(interface.get_current_packet().planes == frozen, "disabled blocks growth and new planes")
	_check(interface.get_presented_packet() == first and first.planes == frozen, "mode changes leave retained and presented packets intact")
	interface.advance_to(3000)
	_check(interface.get_current_packet().planes == frozen, "disabled preserves planes omitted from a later set")
	view.notification(Node.NOTIFICATION_APPLICATION_PAUSED)
	_check(view.session_state == PocketAR.PAUSED and view.plane_finding_mode == PocketAR.PLANE_FINDING_DISABLED, "pause keeps disabled mode")
	view.notification(Node.NOTIFICATION_APPLICATION_RESUMED)
	_check(view.plane_finding_mode == PocketAR.PLANE_FINDING_DISABLED, "resume keeps disabled mode")
	interface.advance_to(4000)
	await process_frame
	_check(changes.is_empty(), "disabled plane changes emit no notifications")
	view.plane_finding_mode = PocketAR.PLANE_FINDING_HORIZONTAL
	interface.advance_to(5000)
	var horizontal: Array[Dictionary] = interface.get_current_packet().planes
	_check(horizontal.size() == 4, "horizontal resumes both horizontal alignments and blocks new vertical planes")
	_check(horizontal[0] == frozen[1], "existing vertical plane keeps its state in horizontal mode")
	_check(horizontal[1]["extents"] == Vector2(3, 3), "re-enable consumes the latest geometry when planes are omitted")
	view.plane_finding_mode = PocketAR.PLANE_FINDING_HORIZONTAL_AND_VERTICAL
	interface.advance_to(6000)
	_check(interface.get_current_packet().planes.size() == 5, "both alignments resume vertical detection")
	_check(interface.get_current_packet().planes[1]["extents"] == Vector2(4, 4), "existing vertical geometry resumes")
	view.plane_finding_mode = PocketAR.PLANE_FINDING_DISABLED
	var generation: int = view.session_generation
	view.reset()
	_check(view.session_generation != generation and view.plane_finding_mode == PocketAR.PLANE_FINDING_DISABLED, "reset preserves disabled mode and changes generation")
	interface.advance_to(7000)
	_check(interface.get_current_packet().planes.is_empty(), "reset discards the remembered fixture plane set")
	interface.advance_to(8000)
	_check(interface.get_current_packet().planes.is_empty(), "disabled reset cannot discover planes")
	view.plane_finding_mode = PocketAR.PLANE_FINDING_HORIZONTAL_AND_VERTICAL
	interface.advance_to(9000)
	_check(interface.get_current_packet().planes.size() == 5, "re-enable after reset discovers the latest set")
	for plane: Dictionary in interface.get_current_packet().planes:
		_check(int(plane["session_generation"]) == view.session_generation, "resumed planes use the new generation")
	_check(first.planes == frozen, "later detections never mutate old packet planes")
	view.free()
	view = scene.instantiate() as ARView
	interface = ScriptedARInterface.new()
	_check(interface.load_fixture(fixture) == OK, "fixture reloads")
	view.backend = ARView.Backend.SCRIPTED
	view.interface = interface
	view.plane_finding_mode = PocketAR.PLANE_FINDING_DISABLED
	root.add_child(view)
	_check(interface.get_current_packet().planes.is_empty(), "disabled startup has no planes")
	view.plane_finding_mode = PocketAR.PLANE_FINDING_HORIZONTAL
	interface.advance_to(1000)
	_check(interface.get_current_packet().planes.size() == 3, "horizontal startup admits upward and downward planes only")
	view.free()
	await process_frame
	print("PLANE FINDING RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
