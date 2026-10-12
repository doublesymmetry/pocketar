extends "res://ar/device_harness.gd"

const HARNESS_ARGS: Script = preload("res://ar/harness_args.gd")
const CAMERA_SHADER: Shader = preload("res://addons/pocketar/camera.gdshader")
const STEPS: PackedStringArray = HARNESS_ARGS.STEPS

var args: PackedStringArray
var reference_material: ShaderMaterial
var reference_frame: int = -1
var seen_anchor_ids: Dictionary = {}
var forbidden_anchor_ids: Dictionary = {}
var reset_anchor_leaks: Dictionary = {}
var reset_samples_remaining: int = 0


func _init() -> void:
	output_dir = "user://harness-ios"
	selected_steps = PackedStringArray(STEPS)


func _ready() -> void:
	var validation: Dictionary = HARNESS_ARGS.validate(args)
	if not String(validation["error"]).is_empty():
		_step("arguments", "FAIL", String(validation["error"]))
		_finish()
		return
	for index: int in range(args.size()):
		match args[index]:
			"-harness-duration": duration = args[index + 1].to_float()
			"-harness-timeout": timeout_seconds = args[index + 1].to_float()
			"-harness-out": output_dir = args[index + 1]
			"-harness-steps": selected_steps = args[index + 1].split(",", false)
	if not args.has("-harness-timeout"):
		timeout_seconds = duration + 150.0
	var error: Error = DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(output_dir))
	if error != OK:
		_step("output", "FAIL", error_string(error))
		_finish()
		return
	if OS.get_name() != "iOS" or DisplayServer.get_name() == "headless" or not OS.is_debug_build():
		_step("arkit-session", "FAIL", "ARKit checks require a debug iOS device build with Metal rendering")
		_finish()
		return
	if view.interface == null or not view.interface.has_method("get_diagnostics"):
		_step("arkit-session", "FAIL", "ARKitInterface unavailable")
		_finish()
		return
	Engine.max_fps = 60
	view.packet_applied.connect(_observe_anchor_ids)
	get_tree().create_timer(timeout_seconds).timeout.connect(_timeout)
	_run.call_deferred()


func _run() -> void:
	if not await _session():
		for name: String in selected_steps:
			if name != "arkit-session":
				_step(name, "INFO" if name == "tracking" else "FAIL", "session prerequisite failed")
		_finish()
		return
	baseline = _diagnostics()
	var started: int = Time.get_ticks_usec()
	await get_tree().create_timer(duration).timeout
	if result_printed:
		return
	observed_seconds = float(Time.get_ticks_usec() - started) / 1000000.0
	observed = _diagnostics()
	for name: String in STEPS:
		if not selected_steps.has(name):
			continue
		if result_printed:
			return
		match name:
			"camera-image": _camera_image()
			"projection-form": _projection()
			"display-transform": _display_transform()
			"composite": await _composite()
			"packet-retention": await _packet_retention()
			"lifecycle": await _lifecycle()
			"tracking": _tracking()
	_finish()


func _session() -> bool:
	var deadline: int = Time.get_ticks_msec() + 45000
	while Time.get_ticks_msec() < deadline and not result_printed:
		var state: int = int(view.interface.call("get_session_state"))
		if state in [0, 4, 9] or (state in [6, 7] and _current_frame() >= 0):
			break
		await get_tree().create_timer(0.1).timeout
	var diagnostics: Dictionary = _diagnostics()
	if not _require("arkit-session", diagnostics, ["state_history", "configured", "renderer", "driver", "arkit_version", "ios_version", "device_model", "config"]):
		return false
	var history: Array = diagnostics["state_history"]
	var ready: int = history.find(5)
	var ok: bool = ready >= 0 and (history.find(6) > ready or history.find(7) > ready)
	ok = ok and bool(diagnostics["configured"]) and _current_frame() >= 0 and int(view.interface.call("get_session_state")) in [6, 7]
	ok = ok and RenderingServer.get_current_rendering_method() == "mobile" and RenderingServer.get_current_rendering_driver_name() == "metal"
	var config: Dictionary = diagnostics["config"]
	ok = ok and int(config.get("width", 0)) > 0 and int(config.get("height", 0)) > 0 and float(config.get("fps", 0)) > 0
	_step("arkit-session", "OK" if ok else "FAIL", "camera_authorization=%s state=%s configured=%s config=%s details=diagnostics.json" % [diagnostics.get("camera_authorization_name", "unknown"), view.interface.call("get_session_state"), diagnostics["configured"], JSON.stringify(config)])
	return ok


func _camera_image() -> void:
	var required: Array[String] = ["camera_path", "imports", "retirement_count", "native_release_count", "retirement_canary_enabled", "sample_checks", "material_sample_checks", "premature_release_count", "sample_after_free_count", "same_branch_failures", "texture_refusals", "buffer_drops", "retain_drop_count", "retain_drop_swaps_min", "retain_drop_swaps_max", "held_buffers", "buffer_high_water", "import_p50_us", "import_p95_us", "retire_p50_us", "retire_p95_us", "new_frame_count", "packets_published", "packets_presented", "config"]
	if not _require("camera-image", observed, required):
		return
	var packet: ARFramePacket = view.interface.call("get_presented_packet") as ARFramePacket
	var imported: bool = packet != null and packet.y_texture is Texture2DRD and packet.cbcr_texture is Texture2DRD and bool(packet.get("imported_chroma"))
	var config: Dictionary = observed["config"]
	if imported:
		imported = packet.y_texture.get_size() == Vector2(int(config.get("width", 0)), int(config.get("height", 0))) and packet.cbcr_texture.get_size() * 2.0 == packet.y_texture.get_size()
	var frames: int = _delta("new_frame_count")
	var published: int = _delta("packets_published")
	var hz: float = float(published) / maxf(observed_seconds, 0.001)
	var missing_fraction: float = float(maxi(0, frames - published)) / maxi(frames, 1)
	var ok: bool = imported and frames > 0 and published > 0 and hz >= float(config.get("fps", 0)) * 0.8 and missing_fraction <= 0.05
	ok = ok and observed["camera_path"] == "cvmetaltexturecache_texture2drd"
	ok = ok and _delta("imports") > 0 and _delta("retirement_count") > 0 and _delta("native_release_count") > 0 and _delta("packets_presented") > 0
	ok = ok and bool(observed["retirement_canary_enabled"]) and int(observed["sample_checks"]) > 0 and int(observed["material_sample_checks"]) > 0
	for key: String in ["premature_release_count", "sample_after_free_count", "same_branch_failures", "texture_refusals", "buffer_drops"]:
		ok = ok and int(observed[key]) == 0
	ok = ok and int(observed["retain_drop_count"]) > 0
	for key: String in ["import_p50_us", "import_p95_us", "retire_p50_us", "retire_p95_us"]:
		ok = ok and is_finite(float(observed[key])) and float(observed[key]) >= 0
	_step("camera-image", "OK" if ok else "FAIL", "seconds=%.3f frames=%d packets=%d hz=%.2f missing_fraction=%.4f imported=%s refusals=%s drops=%s retain_drop_swaps=%s..%s retain_count_heuristic=not_GPU_completion_proof details=diagnostics.json" % [observed_seconds, frames, published, hz, missing_fraction, imported, observed["texture_refusals"], observed["buffer_drops"], observed["retain_drop_swaps_min"], observed["retain_drop_swaps_max"]])


func _projection() -> void:
	if not _require("projection-form", observed, ["projection_form_valid", "packets_published", "projection_near_far_error", "projection_core_error_px", "orientations_seen", "projection_aspect_mismatch_count", "pose_error", "native_depth_zero_to_one"]):
		return
	var ok: bool = bool(observed["projection_form_valid"]) and int(observed["packets_published"]) > 0 and bool(observed["native_depth_zero_to_one"])
	ok = ok and _bounded(observed["projection_near_far_error"], 0.0001) and _bounded(observed["projection_core_error_px"], 0.25) and _bounded(observed["pose_error"], 0.0001)
	_step("projection-form", "OK" if ok else "FAIL", "samples=%s near_far_error=%s core_error_px=%s pose_error=%s orientations=%s aspect_mismatches=%s" % [observed["packets_published"], observed["projection_near_far_error"], observed["projection_core_error_px"], observed["pose_error"], observed.get("orientations_seen"), observed.get("projection_aspect_mismatch_count")])


func _display_transform() -> void:
	if not _require("display-transform", observed, ["display_transform_valid", "packets_published", "display_transform_error_px"]):
		return
	var ok: bool = bool(observed["display_transform_valid"]) and int(observed["packets_published"]) > 0 and _bounded(observed["display_transform_error_px"], 0.5)
	_step("display-transform", "OK" if ok else "FAIL", "samples=%s error_px=%s" % [observed["packets_published"], observed["display_transform_error_px"]])


func _bounded(value: Variant, limit: float) -> bool:
	return is_finite(float(value)) and float(value) >= 0.0 and float(value) < limit


func _composite() -> void:
	var target := SubViewport.new()
	target.size = Vector2i(view.size)
	target.disable_3d = true
	target.use_hdr_2d = false
	target.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	var rect := ColorRect.new()
	rect.size = view.size
	reference_material = ShaderMaterial.new()
	reference_material.shader = CAMERA_SHADER
	rect.material = reference_material
	target.add_child(rect)
	add_child(target)
	var hud_visible: bool = view.hud.visible
	var scene_visible: bool = view.scene_root.visible
	view.hud.hide()
	view.scene_root.hide()
	view.packet_applied.connect(_sync_reference)
	_sync_reference(view.interface.call("get_current_packet") as ARFramePacket)
	await get_tree().process_frame
	await RenderingServer.frame_post_draw
	var screen: Image = get_viewport().get_texture().get_image()
	var camera: Image = target.get_texture().get_image()
	var presented_frame: int = _presented_frame()
	var comparison: Dictionary = _compare_camera(screen, camera)
	var ok: bool = presented_frame == reference_frame and presented_frame >= 0 and bool(comparison["valid"])
	ok = _save_image(screen, "screen.png") and ok
	ok = _save_image(camera, "camera-layer.png") and ok
	view.packet_applied.disconnect(_sync_reference)
	rect.material = null
	reference_material = null
	target.queue_free()
	view.hud.visible = hud_visible
	view.scene_root.visible = scene_visible
	_step("composite", "FAIL" if not ok else ("INFO" if bool(comparison["dark"]) else "OK"), "frame=%d reference=%d comparison=%s captures=%s" % [presented_frame, reference_frame, JSON.stringify(comparison), output_dir])


func _sync_reference(packet: ARFramePacket) -> void:
	if packet == null or reference_material == null:
		return
	var uv: Transform2D = packet.display_transform
	reference_material.set_shader_parameter("display_transform", Basis(Vector3(uv.x.x, uv.x.y, 0), Vector3(uv.y.x, uv.y.y, 0), Vector3(uv.origin.x, uv.origin.y, 1)))
	reference_material.set_shader_parameter("y_texture", packet.y_texture)
	reference_material.set_shader_parameter("cbcr_texture", packet.cbcr_texture)
	reference_material.set_shader_parameter("imported_chroma", packet.get("imported_chroma"))
	reference_frame = packet.frame_id
	view.interface.call("debug_check_camera_material", reference_material.get_rid())


func _compare_camera(screen: Image, camera: Image) -> Dictionary:
	var result: Dictionary = {"valid": false, "dark": false, "mean_y": 0.0, "max_error_levels": 0.0}
	if screen == null or camera == null or screen.is_empty() or camera.is_empty():
		return result
	var maximum: float = 0.0
	var luminance: float = 0.0
	for row: int in range(18):
		for column: int in range(24):
			var uv := Vector2((column + 0.5) / 24.0, (row + 0.5) / 18.0)
			var source_pixel := Vector2i(uv * Vector2(camera.get_size()))
			var screen_pixel := Vector2i(view.global_position) + source_pixel
			if not Rect2i(Vector2i.ZERO, screen.get_size()).has_point(screen_pixel):
				return result
			var expected: Color = camera.get_pixelv(source_pixel)
			var actual: Color = screen.get_pixelv(screen_pixel)
			maximum = maxf(maximum, maxf(absf(expected.r - actual.r), maxf(absf(expected.g - actual.g), absf(expected.b - actual.b))) * 255.0)
			luminance += (0.299 * expected.r + 0.587 * expected.g + 0.114 * expected.b) * 255.0
	result["mean_y"] = luminance / 432.0
	result["dark"] = float(result["mean_y"]) < 8.0
	result["max_error_levels"] = maximum
	# Mobile's normalized scene buffer quantizes dark camera pixels before tonemapping.
	result["valid"] = maximum <= (8.0 if view.composition == ARView.Composition.IN_SCENE else 2.0)
	return result


func _lifecycle() -> void:
	var before: Dictionary = _diagnostics()
	if not _require("lifecycle", before, ["pause_count", "resume_count", "reset_count", "new_frame_count", "session_generation", "timings"]):
		return
	var ok: bool = true
	for cycle: int in range(2):
		if result_printed:
			return
		view.interface.call("application_pause")
		var paused: Dictionary = _diagnostics()
		ok = ok and int(view.interface.call("get_session_state")) == 8
		await get_tree().create_timer(0.3).timeout
		ok = ok and int(_diagnostics().get("new_frame_count", -1)) == int(paused["new_frame_count"])
		var frame_id: int = _current_frame()
		view.interface.call("application_resume")
		var resumed: bool = await _wait_for_packet(int(view.interface.call("get_session_generation")), frame_id)
		ok = ok and resumed
	for cycle: int in range(2):
		if result_printed:
			return
		var old_generation: int = int(view.interface.call("get_session_generation"))
		for identifier: String in seen_anchor_ids:
			forbidden_anchor_ids[identifier] = true
		reset_samples_remaining = 5
		view.interface.call("reset")
		var generation: int = int(view.interface.call("get_session_generation"))
		ok = ok and generation > old_generation and _presented_frame() == -1 and _old_trackers_absent(generation)
		var resumed: bool = await _wait_for_packet(generation, -1)
		ok = ok and resumed and _old_trackers_absent(generation)
		await get_tree().create_timer(2.0).timeout
		ok = ok and reset_anchor_leaks.is_empty() and reset_samples_remaining == 0
	var after: Dictionary = _diagnostics()
	ok = ok and int(after.get("pause_count", -1)) >= int(before["pause_count"]) + 2 and int(after.get("resume_count", -1)) >= int(before["resume_count"]) + 2 and int(after.get("reset_count", -1)) == int(before["reset_count"]) + 2
	ok = ok and _old_trackers_absent(int(view.interface.call("get_session_generation")))
	for key: String in ["premature_release_count", "sample_after_free_count", "same_branch_failures", "texture_refusals", "buffer_drops"]:
		ok = ok and int(after.get(key, -1)) == 0
	ok = ok and int(after.get("held_buffers", 8)) <= 7 and int(after.get("buffer_high_water", 8)) <= 7
	var timings: Dictionary = after.get("timings", {})
	ok = _require("lifecycle", timings, ["first_frame_ms", "pause_ms", "resume_ms", "reset_ms"]) and ok
	_step("lifecycle", "OK" if ok else "FAIL", "pause_count=%s resume_count=%s reset_count=%s prior_native_ids=%d reappeared=%d timings=%s" % [after.get("pause_count"), after.get("resume_count"), after.get("reset_count"), forbidden_anchor_ids.size(), reset_anchor_leaks.size(), JSON.stringify(timings)])


func _observe_anchor_ids(packet: ARFramePacket) -> void:
	var identifiers: Dictionary = view.interface.call("get_native_plane_identifiers") as Dictionary
	for identifier: String in identifiers.values():
		seen_anchor_ids[identifier] = true
		if forbidden_anchor_ids.has(identifier):
			reset_anchor_leaks[identifier] = true
	if reset_samples_remaining > 0:
		HarnessLog.write_line("ARKIT RESET generation=%s frame=%s anchor_count=%s" % [packet.session_generation, packet.frame_id, _diagnostics()["native_anchor_count"]])
		reset_samples_remaining -= 1


func _wait_for_packet(generation: int, after_frame: int) -> bool:
	var deadline: int = Time.get_ticks_msec() + 12000
	while Time.get_ticks_msec() < deadline and not result_printed:
		if _packet_matches(generation, after_frame):
			return true
		await get_tree().create_timer(0.1).timeout
	return false


func _packet_matches(generation: int, after_frame: int) -> bool:
	var packet: ARFramePacket = view.interface.call("get_presented_packet") as ARFramePacket
	return packet != null and packet.session_generation == generation and packet.frame_id > after_frame


func _current_frame() -> int:
	var packet: ARFramePacket = view.interface.call("get_current_packet") as ARFramePacket
	return packet.frame_id if packet != null else -1


func _presented_frame() -> int:
	var packet: ARFramePacket = view.interface.call("get_presented_packet") as ARFramePacket
	return packet.frame_id if packet != null else -1


func _tracking() -> void:
	var diagnostics: Dictionary = _diagnostics()
	var hits: Array = view.raycast(view.size * 0.5)
	var hit_info: Dictionary = {}
	if not hits.is_empty():
		var hit: ARRaycastHit = hits[0] as ARRaycastHit
		hit_info = {"plane_id": hit.plane_id, "distance": hit.distance, "frame_id": hit.frame_id, "session_generation": hit.session_generation}
	var live: Variant = view.interface.call("live_hit_test", view.size * 0.5)
	_step("tracking", "INFO", "state=%s reason=%s planes=%s centre_hit=%s live_hit=%s timings=%s" % [view.interface.call("get_session_state"), view.interface.call("get_tracking_reason"), diagnostics.get("plane_count"), JSON.stringify(hit_info), str(live), JSON.stringify(diagnostics.get("timings", {}))])


func _finish() -> void:
	if result_printed:
		return
	if view != null and view.interface != null and view.interface.has_method("get_diagnostics"):
		var diagnostics: Dictionary = _diagnostics()
		HarnessLog.write_line("ARKIT DIAGNOSTICS details=" + output_dir.path_join("diagnostics.json"))
		var file: FileAccess = FileAccess.open(output_dir.path_join("diagnostics.json"), FileAccess.WRITE)
		if file == null:
			_step("output", "FAIL", "cannot save diagnostics: " + error_string(FileAccess.get_open_error()))
		else:
			file.store_string(JSON.stringify(diagnostics, "\t"))
			file.close()
	super._finish()
