extends SceneTree

var failures: int = 0


func _initialize() -> void:
	_run.call_deferred()


func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("SESSION FAIL " + message)


func _run() -> void:
	_check(PocketAR.alignment_mask([]) == 0, "empty alignment list gives an empty mask")
	_check(PocketAR.alignment_mask([ARPlaneTracker.HORIZONTAL_UPWARD_FACING]) == PocketAR.ALIGNMENT_MASK_HORIZONTAL_UPWARD, "upward alignment maps to its mask")
	_check(PocketAR.alignment_mask([ARPlaneTracker.HORIZONTAL_DOWNWARD_FACING]) == PocketAR.ALIGNMENT_MASK_HORIZONTAL_DOWNWARD, "downward alignment maps to its mask")
	_check(PocketAR.alignment_mask([ARPlaneTracker.VERTICAL]) == PocketAR.ALIGNMENT_MASK_VERTICAL, "vertical alignment maps to its distinct mask")
	_check(PocketAR.alignment_mask([ARPlaneTracker.HORIZONTAL_UPWARD_FACING, ARPlaneTracker.HORIZONTAL_DOWNWARD_FACING, ARPlaneTracker.VERTICAL, ARPlaneTracker.VERTICAL]) == PocketAR.ALIGNMENT_MASK_ALL, "alignment masks combine and deduplicate")
	var scene: PackedScene = load("res://addons/pocketar/ar_view.tscn") as PackedScene
	var view: ARView = scene.instantiate() as ARView
	_check(view.has_method("reset"), "ARView has a session reset facade")
	_check(view.has_method("world_to_screen"), "ARView projects from the presented packet")
	_check(view.has_method("screen_to_point"), "ARView unprojects at camera depth")
	_check(view.has_method("view_point"), "ARView converts canvas coordinates")
	_check(view.has_signal("session_state_changed"), "ARView exposes state transitions")
	if failures > 0:
		view.free()
		quit(1)
		return
	root.add_child(view)
	view.set_anchors_and_offsets_preset(Control.PRESET_TOP_LEFT)
	view.size = Vector2(640, 480)
	view.call("_update_geometry")
	await process_frame
	_check(int(view.get("session_state")) == 7, "synthetic is tracking")
	_check(int(view.get("tracking_reason")) == 0, "synthetic has no tracking failure")
	_check(not bool(view.get("handle_presses")), "input is opt-in")
	view.interface.call("mark_presented")
	var point := Vector3(0.2, -0.1, -2.0)
	var pixel: Vector2 = view.call("world_to_screen", point)
	var restored: Vector3 = view.call("screen_to_point", pixel, 2.0)
	_check(restored.distance_to(point) < 0.00001, "projection roundtrip at optical depth")
	view.position = Vector2(20, 30)
	view.scale = Vector2(1.5, 0.75)
	var local := Vector2(70, 90)
	var viewport_point: Vector2 = view.get_global_transform_with_canvas() * local
	_check(Vector2(view.call("view_point", viewport_point)).distance_to(local) < 0.0001, "scaled canvas coordinate conversion")
	var presses: Array[Vector2] = []
	view.screen_pressed.connect(func(value: Vector2, _ray: Dictionary) -> void: presses.append(value))
	var touch := InputEventScreenTouch.new()
	touch.position = viewport_point
	touch.pressed = true
	view.call("_unhandled_input", touch)
	_check(presses.is_empty(), "default input does not emit presses")
	view.set("handle_presses", true)
	view.call("_unhandled_input", touch)
	_check(presses.size() == 1 and presses[0].is_equal_approx(local), "opt-in input emits view coordinates")
	var mouse := InputEventMouseButton.new()
	mouse.position = viewport_point
	mouse.pressed = true
	mouse.button_index = MOUSE_BUTTON_LEFT
	mouse.device = InputEvent.DEVICE_ID_EMULATION
	view.call("_unhandled_input", mouse)
	_check(presses.size() == 1, "emulated mouse does not duplicate touch")
	mouse.device = 0
	view.call("_unhandled_input", mouse)
	_check(presses.size() == 2, "physical mouse remains supported")
	_check(Vector2(view.call("world_to_screen", Vector3(0, 0, 1))) == Vector2.INF, "behind-camera mapping is invalid")
	_check(Vector3(view.call("screen_to_point", pixel, -1.0)) == Vector3.INF, "negative optical depth is invalid")
	var changes: Array[int] = []
	view.connect("session_generation_changed", func(value: int) -> void: changes.append(value))
	var previous: int = int(view.get("session_generation"))
	view.call("reset")
	_check(int(view.get("session_generation")) > previous, "reset updates generation before notification")
	await process_frame
	_check(changes.size() == 1 and changes[0] > previous, "reset forwards exactly one new generation")
	_check(view.screen_to_ray(Vector2.ZERO).is_empty(), "reset invalidates presented packet")
	view.free()
	print("SESSION RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
