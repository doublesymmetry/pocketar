extends SceneTree

var failures: int = 0


func _initialize() -> void:
	_run.call_deferred()


func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("VISUALIZER FAIL " + message)


func _run() -> void:
	var backend := ScriptedARInterface.new()
	backend.load_fixture(get_script().resource_path.get_base_dir().path_join("plane_cache.jsonl"))
	var view: ARView = load("res://addons/pocketar/ar_view.tscn").instantiate()
	view.backend = ARView.Backend.SCRIPTED
	view.interface = backend
	view.plane_finding_mode = PocketAR.PLANE_FINDING_HORIZONTAL
	root.add_child(view)
	var visualizer := ARPlaneVisualizer.new()
	visualizer.ar_view = view
	view.scene_root.add_child(visualizer)
	_check(visualizer.plane_count == 1, "visible plane has a mesh")
	var mesh: Mesh = visualizer._meshes[1].mesh
	visualizer.hide()
	backend.advance_to(4000)
	view.call("_apply_packet")
	_check(visualizer._meshes[1].mesh == mesh, "hidden visualizer does not rebuild changed geometry")
	visualizer.show()
	_check(visualizer._meshes[1].mesh != mesh, "shown visualizer catches up without another packet")
	visualizer.hide()
	backend.advance_to(5000)
	view.call("_apply_packet")
	_check(visualizer.plane_count == 1, "hidden visualizer skips removals")
	visualizer.show()
	_check(visualizer.plane_count == 0, "shown visualizer catches up with removals")
	view.free()
	await process_frame
	print("VISUALIZER RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
