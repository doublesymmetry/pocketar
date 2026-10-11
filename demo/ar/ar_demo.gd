extends Node

const AR_VIEW: PackedScene = preload("res://addons/handheld_ar/ar_view.tscn")
const IOS_HARNESS: Script = preload("res://ar/ios_ar_harness.gd")
const HARNESS_ARGS: Script = preload("res://ar/harness_args.gd")
const DEVICE_HARNESS: Script = preload("res://ar/ar_harness.gd")
const SESSION_STATES: Array[String] = ["unsupported", "checking", "needs-install", "installing", "permission-denied", "ready", "initializing", "tracking", "paused", "failed"]

var view: ARView
var status_label: Label
var plane_meshes: Dictionary = {}
var placements: Node3D
var displayed_generation: int = -1
var placement_message: String = "Tap a detected plane to place a cube."
var dataset_error: String = ""
var recording_active: bool = false
var playback_active: bool = false
var playback_finished_logged: bool = false
var dataset_failures: int = 0


func _ready() -> void:
	var launch: Dictionary = HARNESS_ARGS.read()
	var args: PackedStringArray = launch["args"]
	if not String(launch["error"]).is_empty():
		print("HARNESS STEP arguments FAIL " + String(launch["error"]))
		print("HARNESS RESULT FAIL")
		get_tree().quit(1)
		return
	view = AR_VIEW.instantiate() as ARView
	view.backend = 2 if OS.get_name() == "iOS" else (1 if OS.get_name() == "Android" else 0)
	var path_index: int = args.find("-harness-camera-path")
	if view.backend == 1 and ClassDB.class_exists("ARCoreInterface"):
		view.interface = ClassDB.instantiate("ARCoreInterface") as XRInterfaceExtension
		if path_index >= 0:
			if path_index + 1 >= args.size() or args[path_index + 1] not in ["auto", "cpu"]:
				dataset_error = "camera path must be auto or cpu"
			elif args[path_index + 1] == "cpu" and args.has("-harness-gpu-validate"):
				dataset_error = "GPU validation requires the auto camera path"
			else:
				view.interface.call("set_camera_path", args[path_index + 1])
		view.interface.call("set_gpu_validation_enabled", args.has("-harness-gpu-validate"))
	elif path_index >= 0 or args.has("-harness-gpu-validate"):
		dataset_error = "camera path and GPU validation require ARCore on Android"
	var config_index: int = args.find("-harness-camera-config")
	if config_index >= 0:
		if config_index + 1 >= args.size() or not args[config_index + 1].is_valid_int() or args[config_index + 1].to_int() < 0:
			dataset_error = "camera config must be a nonnegative integer"
		elif view.backend == 1 and ClassDB.class_exists("ARCoreInterface"):
			view.interface.call("set_camera_config_index", args[config_index + 1].to_int())
		else:
			dataset_error = "camera config requires ARCore on Android"
	add_child(view)
	placements = Node3D.new()
	view.scene_root.add_child(placements)
	_make_hud()
	view.screen_pressed.connect(_place_cube)
	if not await _prepare_dataset(args):
		placement_message = dataset_error
		if args.has("-harness-ar") or args.has("-harness"):
			HarnessLog.write_line("HARNESS STEP dataset FAIL " + dataset_error)
			HarnessLog.write_line("HARNESS RESULT FAIL")
			get_tree().quit(1)
		return
	if args.has("-harness-ar") or args.has("-harness"):
		var harness_script: Script = IOS_HARNESS if OS.get_name() == "iOS" else DEVICE_HARNESS
		var harness: Node = harness_script.new() as Node
		harness.set("view", view)
		if OS.get_name() == "iOS":
			harness.set("args", args)
		else:
			harness.set("dataset_owner", self)
		add_child(harness)


func _prepare_dataset(args: PackedStringArray) -> bool:
	if not dataset_error.is_empty():
		return false
	var record_index: int = args.find("-harness-record")
	var playback_index: int = args.find("-harness-playback")
	if record_index < 0 and playback_index < 0:
		return true
	if record_index >= 0 and playback_index >= 0:
		dataset_error = "recording and playback are mutually exclusive"
		return false
	var index: int = maxi(record_index, playback_index)
	var filename: String = args[index + 1] if index + 1 < args.size() else ""
	var valid_name := RegEx.create_from_string("^[A-Za-z0-9_-][A-Za-z0-9_.-]*\\.mp4$")
	if valid_name.search(filename) == null or not Engine.has_singleton("HandheldARPlugin"):
		dataset_error = "dataset needs a simple .mp4 filename and the Android plugin"
		return false
	var plugin: Object = Engine.get_singleton("HandheldARPlugin")
	var directory: String = String(plugin.call("getExternalFilesDir"))
	if not directory.begins_with("/"):
		dataset_error = "private external storage is unavailable"
		return false
	var path: String = directory.path_join(filename)
	if view.interface == null:
		dataset_error = "ARCoreInterface is unavailable"
		return false
	if FileAccess.file_exists(path) != (playback_index >= 0):
		dataset_error = "playback dataset is missing" if playback_index >= 0 else "recording dataset already exists"
		return false
	var deadline: int = Time.get_ticks_msec() + 45000
	while view.interface.call("get_current_packet") == null and Time.get_ticks_msec() < deadline:
		await get_tree().create_timer(0.1).timeout
	if view.interface.call("get_current_packet") == null:
		dataset_error = "no camera packet arrived before the dataset deadline"
		return false
	var uri: String = "file://" + path.uri_encode().replace("%2F", "/")
	if record_index >= 0:
		var result: Dictionary = view.interface.call("start_recording", uri) as Dictionary
		recording_active = bool(result.get("ok", false))
		HarnessLog.write_line("HARNESS STEP dataset-record INFO uri=%s result=%s" % [uri, JSON.stringify(result)])
		if not recording_active:
			dataset_error = "ARCore recording did not start: " + JSON.stringify(result)
			return false
		get_tree().create_timer(2.0).timeout.connect(_record_centre_tap)
	else:
		playback_active = true
		view.interface.call("start_playback", uri)
		HarnessLog.write_line("HARNESS STEP dataset-playback INFO uri=%s" % uri)
	return true


func _record_centre_tap() -> void:
	if recording_active:
		_place_cube(view.size * 0.5, {})


func finish_dataset() -> void:
	if recording_active:
		var result: Dictionary = view.interface.call("stop_recording") as Dictionary
		recording_active = false
		HarnessLog.write_line("HARNESS STEP dataset-record-end INFO " + JSON.stringify(result))


func _consume_playback_inputs() -> void:
	var inputs: Array = view.interface.call("get_playback_inputs") as Array
	for entry: Dictionary in inputs:
		var payload: PackedByteArray = entry.get("payload", PackedByteArray())
		var decoded: Variant = JSON.parse_string(payload.get_string_from_utf8())
		if not decoded is Dictionary:
			HarnessLog.write_line("HARNESS STEP dataset-input INFO invalid JSON payload")
			continue
		var event: Dictionary = decoded
		var coordinates: Variant = event.get("uv", [])
		if int(event.get("version", 0)) != 1 or event.get("type", "") != "tap" or not coordinates is Array or coordinates.size() != 2:
			HarnessLog.write_line("HARNESS STEP dataset-input INFO unsupported input payload")
			continue
		if not (coordinates[0] is float or coordinates[0] is int) or not (coordinates[1] is float or coordinates[1] is int):
			continue
		var uv := Vector2(float(coordinates[0]), float(coordinates[1]))
		if not uv.is_finite() or uv.x < 0 or uv.x > 1 or uv.y < 0 or uv.y > 1:
			continue
		_apply_tap(uv * view.size, true, event)
	var diagnostics: Dictionary = view.interface.call("get_dataset_diagnostics") as Dictionary
	if bool(diagnostics.get("playback_finished", false)) and not playback_finished_logged:
		playback_finished_logged = true
		HarnessLog.write_line("HARNESS STEP dataset-playback-end INFO status=%s inputs_read=%s inputs_dropped=%s" % [diagnostics.get("playback_status", -1), diagnostics.get("playback_inputs_read", 0), diagnostics.get("playback_inputs_dropped", 0)])


func _make_hud() -> void:
	var panel := PanelContainer.new()
	panel.position = Vector2(18, 18)
	panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.03, 0.04, 0.06, 0.8)
	style.content_margin_left = 16
	style.content_margin_right = 16
	style.content_margin_top = 12
	style.content_margin_bottom = 12
	panel.add_theme_stylebox_override("panel", style)
	status_label = Label.new()
	status_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	status_label.add_theme_font_size_override("font_size", 20)
	panel.add_child(status_label)
	view.hud.add_child(panel)
	var reset_button := Button.new()
	reset_button.text = "Reset session"
	reset_button.position = Vector2(18, 164)
	reset_button.custom_minimum_size = Vector2(170, 52)
	reset_button.pressed.connect(_reset_session)
	view.hud.add_child(reset_button)


func _process(_delta: float) -> void:
	if view == null or view.interface == null:
		return
	var native: bool = view.interface.has_method("get_session_state")
	var state: String = "synthetic"
	var reason: int = 0
	var packet: ARFramePacket = view.interface.call("get_presented_packet") as ARFramePacket
	var generation: int = packet.session_generation if packet != null else 0
	if native:
		var state_index: int = int(view.interface.call("get_session_state"))
		state = SESSION_STATES[state_index] if state_index >= 0 and state_index < SESSION_STATES.size() else "unknown"
		reason = int(view.interface.call("get_tracking_reason"))
		generation = int(view.interface.call("get_session_generation"))
	if generation != displayed_generation:
		for child: Node in placements.get_children():
			child.queue_free()
		displayed_generation = generation
		playback_finished_logged = false
	_update_planes(generation)
	if playback_active:
		_consume_playback_inputs()
	status_label.text = "%s  |  reason %d\ngeneration %d  |  %d fps  |  %d planes\n%s" % [state, reason, generation, Engine.get_frames_per_second(), plane_meshes.size(), placement_message]


func _update_planes(generation: int) -> void:
	var visible_ids: Dictionary = {}
	var trackers: Dictionary = XRServer.get_trackers(XRServer.TRACKER_ANCHOR)
	for value: Variant in trackers.values():
		var tracker: ARPlaneTracker = value as ARPlaneTracker
		if tracker == null or tracker.session_generation != generation or tracker.tracking_state != ARPlaneTracker.TRACKING or tracker.subsumed_by != 0:
			continue
		var polygon: PackedVector2Array = tracker.polygon
		if polygon.size() < 3:
			continue
		var id: int = tracker.plane_id
		visible_ids[id] = true
		var mesh_instance: MeshInstance3D = plane_meshes.get(id) as MeshInstance3D
		if mesh_instance == null:
			mesh_instance = MeshInstance3D.new()
			var material := StandardMaterial3D.new()
			material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			material.cull_mode = BaseMaterial3D.CULL_DISABLED
			material.albedo_color = Color(0.05, 0.75, 0.95, 0.24)
			mesh_instance.material_override = material
			view.scene_root.add_child(mesh_instance)
			plane_meshes[id] = mesh_instance
		if mesh_instance.get_meta("polygon", PackedVector2Array()) != polygon:
			mesh_instance.mesh = _plane_mesh(polygon)
			mesh_instance.set_meta("polygon", polygon)
		mesh_instance.transform = tracker.transform
	for id: Variant in plane_meshes.keys():
		if not visible_ids.has(id):
			var obsolete: MeshInstance3D = plane_meshes[id] as MeshInstance3D
			obsolete.queue_free()
			plane_meshes.erase(id)


func _plane_mesh(polygon: PackedVector2Array) -> ArrayMesh:
	var vertices := PackedVector3Array()
	var normals := PackedVector3Array()
	for point: Vector2 in polygon:
		vertices.append(Vector3(point.x, 0, point.y))
		normals.append(Vector3.UP)
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_INDEX] = Geometry2D.triangulate_polygon(polygon)
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh


func _place_cube(point: Vector2, _ray: Dictionary) -> void:
	_apply_tap(point, false, {})


func _apply_tap(point: Vector2, replay: bool, recorded: Dictionary) -> void:
	var hits: Array = view.raycast(point)
	var distance: float = -1.0 if hits.is_empty() else float(hits[0].distance)
	if recording_active and not replay:
		var uv: Vector2 = point / view.size
		var event: Dictionary = {"version": 1, "type": "tap", "uv": [uv.x, uv.y], "expected_distance_m": distance}
		var result: Dictionary = view.interface.call("record_input", JSON.stringify(event).to_utf8_buffer()) as Dictionary
		HarnessLog.write_line("HARNESS STEP dataset-input INFO recorded=%s result=%s" % [JSON.stringify(event), JSON.stringify(result)])
	if replay:
		var expected: float = float(recorded.get("expected_distance_m", -1.0))
		var tolerance: float = maxf(0.15, expected * 0.25)
		var agrees: bool = expected < 0 or (distance >= 0 and absf(expected - distance) <= tolerance)
		if not agrees:
			dataset_failures += 1
		HarnessLog.write_line("HARNESS STEP dataset-input %s replay_distance_m=%.3f recorded_distance_m=%.3f tolerance_m=%.3f agrees=%s" % ["INFO" if agrees else "FAIL", distance, expected, tolerance, agrees])
	if hits.is_empty():
		placement_message = "No tracked plane at this point."
		return
	var hit: ARRaycastHit = hits[0] as ARRaycastHit
	var cube := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = Vector3.ONE * 0.08
	cube.mesh = mesh
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = Color(1.0, 0.55, 0.08)
	cube.material_override = material
	cube.transform = hit.pose
	cube.position += hit.pose.basis.y * 0.04
	placements.add_child(cube)
	placement_message = "Placed on plane %d, %.2f m away." % [hit.plane_id, hit.distance]


func _reset_session() -> void:
	if view.interface.has_method("reset"):
		view.interface.call("reset")
	else:
		view.interface.uninitialize()
		view.interface.initialize()
	placement_message = "Session reset. Move the camera to find planes."
