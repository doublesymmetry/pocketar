extends SceneTree

var failures: int = 0


func _initialize() -> void:
	_run.call_deferred()


func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("ACTIVITY FAIL " + message)


func _run() -> void:
	var scene: PackedScene = load("res://addons/pocketar/ar_view.tscn")
	for scripted: bool in [false, true]:
		var view: ARView = scene.instantiate()
		var backend: XRInterfaceExtension = ScriptedARInterface.new() if scripted else SyntheticARInterface.new()
		_check(backend.has_method("set_suspended"), "backend exposes explicit suspension")
		if not backend.has_method("set_suspended"):
			view.free()
			continue
		if scripted:
			backend.call("load_fixture", get_script().resource_path.get_base_dir().path_join("plane_finding.jsonl"))
		else:
			backend.call("set_intrinsics", 52, 51, 32, 24, Vector2i(64, 48))
		view.interface = backend
		view.set("suspended", true)
		root.add_child(view)
		_check(view.session_state == PocketAR.PAUSED, "startup suspension is paused")
		_check(backend.call("get_current_packet") == null, "suspended startup publishes no packet")
		_check(view.virtual_scene.render_target_update_mode == SubViewport.UPDATE_DISABLED, "suspension disables viewport updates")
		view.set("suspended", false)
		if scripted:
			backend.call("advance_to", 0)
		await process_frame
		await process_frame
		_check(view.session_state == PocketAR.TRACKING, "resume restarts tracking")
		var packet: ARFramePacket = backend.call("get_current_packet")
		_check(packet != null, "resume publishes a packet")
		var generation: int = view.session_generation
		view.hide()
		if scripted:
			backend.call("advance_to", 1000)
		else:
			backend.call("set_display_geometry", Vector2i(200, 100), 0, 0.05, 100)
		await process_frame
		_check(backend.call("get_current_packet") != packet, "visibility alone does not suspend")
		view.set("suspended", true)
		packet = backend.call("get_current_packet")
		if scripted:
			backend.call("advance_to", 2000)
		else:
			backend.call("set_display_geometry", Vector2i(300, 100), 0, 0.05, 100)
		await process_frame
		_check(backend.call("get_current_packet") == packet and view.session_state == PocketAR.PAUSED, "suspension stops packet updates")
		view.notification(Node.NOTIFICATION_APPLICATION_PAUSED)
		view.notification(Node.NOTIFICATION_APPLICATION_RESUMED)
		_check(view.session_state == PocketAR.PAUSED, "app resume cannot override explicit suspension")
		view.notification(Node.NOTIFICATION_APPLICATION_PAUSED)
		view.set("suspended", false)
		_check(view.session_state == PocketAR.PAUSED, "explicit resume cannot override app pause")
		view.notification(Node.NOTIFICATION_APPLICATION_RESUMED)
		if scripted:
			backend.call("advance_to", 2000)
		await process_frame
		_check(backend.call("get_current_packet") != packet and view.session_state == PocketAR.TRACKING, "resume restarts packet updates")
		_check(view.session_generation == generation, "ordinary resume preserves generation")
		_check(view.virtual_scene.render_target_update_mode == SubViewport.UPDATE_ALWAYS, "resume restores viewport updates")
		view.set("suspended", true)
		view.reset()
		_check(view.session_state == PocketAR.PAUSED and backend.call("get_current_packet") == null, "reset while suspended stays paused")
		if scripted:
			backend.call("load_fixture", get_script().resource_path.get_base_dir().path_join("plane_finding.jsonl"))
			_check(view.session_state == PocketAR.PAUSED and backend.call("get_current_packet") == null, "fixture reload while suspended publishes nothing")
		view.set("suspended", false)
		if scripted:
			backend.call("advance_to", 3000)
		await process_frame
		_check(backend.call("get_current_packet") != null, "resume after reset publishes again")
		view.free()
	await process_frame
	print("ACTIVITY RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
