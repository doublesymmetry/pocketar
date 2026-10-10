extends Node

const STEPS: Array[String] = ["arcore-session", "no-gl", "camera-image", "projection-form", "display-transform", "composite", "lifecycle", "tracking"]

var view: ARView
var dataset_owner: Node
var duration: float = 60.0
var timeout_seconds: float = 180.0
var output_dir: String = "user://harness/arcore"
var selected_steps: PackedStringArray = PackedStringArray(STEPS)
var failed: bool = false
var result_printed: bool = false
var baseline: Dictionary = {}
var observed: Dictionary = {}
var observed_seconds: float = 0.0
var started_usec: int = 0
var first_tracking_ms: float = -1.0
var probes_requested: bool = false


func _ready() -> void:
	started_usec = Time.get_ticks_usec()
	var args: PackedStringArray = OS.get_cmdline_user_args()
	probes_requested = args.has("-harness-probes")
	for index: int in range(args.size()):
		if args[index] in ["-harness-duration", "-harness-timeout", "-harness-out", "-harness-steps"]:
			if index + 1 >= args.size():
				_step("arguments", "FAIL", "missing value for " + args[index])
				_finish()
				return
			match args[index]:
				"-harness-duration": duration = args[index + 1].to_float()
				"-harness-timeout": timeout_seconds = args[index + 1].to_float()
				"-harness-out": output_dir = args[index + 1]
				"-harness-steps": selected_steps = args[index + 1].split(",", false)
	if not is_finite(duration) or duration <= 0 or not is_finite(timeout_seconds) or timeout_seconds <= 0:
		_step("arguments", "FAIL", "duration and timeout must be finite positive seconds")
		_finish()
		return
	if not args.has("-harness-timeout"):
		timeout_seconds = duration + (145.0 if probes_requested else 120.0)
	if selected_steps.is_empty():
		_step("arguments", "FAIL", "no checks selected")
		_finish()
		return
	for name: String in selected_steps:
		if not STEPS.has(name):
			_step("arguments", "FAIL", "unknown step: " + name)
			_finish()
			return
	var error: Error = DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(output_dir))
	if error != OK:
		_step("output", "FAIL", error_string(error))
		_finish()
		return
	if DisplayServer.get_name() == "headless" or OS.get_name() != "Android":
		_step("arcore-session", "FAIL", "ARCore checks require an Android device with Vulkan rendering")
		_finish()
		return
	if view.interface == null or not view.interface.has_method("get_diagnostics"):
		_step("arcore-session", "FAIL", "ARCoreInterface is unavailable")
		_finish()
		return
	Engine.max_fps = 60
	_log_device_diagnostics("start")
	var device_timer := Timer.new()
	device_timer.wait_time = 60.0
	device_timer.autostart = true
	device_timer.timeout.connect(_log_periodic_device_diagnostics)
	add_child(device_timer)
	get_tree().create_timer(timeout_seconds).timeout.connect(_timeout)
	_run.call_deferred()


func _process(_delta: float) -> void:
	if first_tracking_ms < 0 and view != null and view.interface != null and view.interface.has_method("get_session_state"):
		if int(view.interface.call("get_session_state")) == 7:
			first_tracking_ms = float(Time.get_ticks_usec() - started_usec) / 1000.0


func _run() -> void:
	var session_ok: bool = await _session()
	if not session_ok:
		for name: String in selected_steps:
			if name != "arcore-session":
				_step(name, "INFO" if name == "tracking" else "FAIL", "session prerequisite failed")
		_finish()
		return
	if probes_requested and view.interface.has_method("set_frame_probes_enabled"):
		view.interface.call("set_frame_probes_enabled", true)
		view.interface.call("set_point_cloud_enabled", true)
	baseline = _diagnostics()
	var started: int = Time.get_ticks_usec()
	if selected_steps.has("no-gl") or selected_steps.has("camera-image"):
		HarnessLog.write_line("ARCORE OBSERVATION duration=%.1f seconds" % duration)
		await _observe()
	observed_seconds = float(Time.get_ticks_usec() - started) / 1000000.0
	observed = _diagnostics()
	for name: String in selected_steps:
		if result_printed:
			return
		match name:
			"no-gl": _no_gl()
			"camera-image": _camera_image()
			"projection-form": _diagnostic_check(name, "projection_form_valid", "projection_form_samples")
			"display-transform": _diagnostic_check(name, "display_transform_valid", "display_transform_samples")
			"composite": await _composite()
			"lifecycle": await _lifecycle()
			"tracking": _tracking()
	if probes_requested:
		await _run_probes()
	_finish()


func _observe() -> void:
	var deadline: int = Time.get_ticks_msec() + roundi(duration * 1000.0)
	var playback: bool = OS.get_cmdline_user_args().has("-harness-playback")
	if not playback:
		await get_tree().create_timer(duration).timeout
		return
	while Time.get_ticks_msec() < deadline and not result_printed:
		var diagnostics: Dictionary = view.interface.call("get_dataset_diagnostics") as Dictionary
		if bool(diagnostics.get("playback_finished", false)):
			_step("dataset-observation", "INFO", "playback finished before requested %.1f seconds; checks cover only the reported actual duration" % duration)
			return
		await get_tree().create_timer(0.1).timeout


func _run_probes() -> void:
	var sampled: Dictionary = observed.get("extras", {})
	_step("repeated-frame-probe", "INFO", "count=%s updated_planes=%s image_status=%s details=diagnostics.json" % [sampled.get("repeated_frame_probe_count", 0), sampled.get("repeated_frame_updated_plane_count", -1), sampled.get("repeated_frame_image_status", "unavailable")])
	if view.interface.has_method("set_frame_probes_enabled"):
		view.interface.call("set_frame_probes_enabled", false)
		view.interface.call("set_point_cloud_enabled", false)
	if not view.interface.has_method("begin_image_capacity_probe"):
		_step("image-capacity-probe", "INFO", "native probe unavailable")
	else:
		var request: Dictionary = view.interface.call("begin_image_capacity_probe") as Dictionary
		var complete: bool = false
		if bool(request.get("ok", false)):
			complete = await _wait_for_probe("image_capacity_probe_active", 20.0)
		var diagnostics: Dictionary = _diagnostics().get("extras", {})
		_step("image-capacity-probe", "INFO", "requested=%s wait_completed=%s max_held=%s details=diagnostics.json" % [request.get("ok", false), complete, diagnostics.get("image_capacity_max_held", 0)])
		if not complete and view.interface.has_method("cancel_image_capacity_probe"):
			view.interface.call("cancel_image_capacity_probe")
	if result_printed:
		return
	var extras: Dictionary = _diagnostics().get("extras", {})
	if bool(extras.get("image_capacity_probe_active", false)):
		_step("old-frame-probe", "INFO", "skipped: image capacity probe is still active")
		return
	if int(view.interface.call("get_session_state")) != 7:
		_step("old-frame-probe", "INFO", "skipped: camera is not tracking")
		return
	if not view.interface.has_method("begin_old_frame_probe"):
		_step("old-frame-probe", "INFO", "native probe unavailable")
		return
	view.interface.call("begin_old_frame_probe")
	extras = _diagnostics().get("extras", {})
	if not bool(extras.get("old_frame_probe_pending", false)):
		_step("old-frame-probe", "INFO", "not started; details=diagnostics.json")
		return
	var complete: bool = await _wait_for_probe("old_frame_probe_pending", 5.0)
	var diagnostics: Dictionary = _diagnostics().get("extras", {})
	_step("old-frame-probe", "INFO", "wait_completed=%s probe_complete=%s details=diagnostics.json" % [complete, diagnostics.get("old_frame_probe_complete", false)])


func _wait_for_probe(pending_key: String, seconds: float) -> bool:
	var deadline: int = Time.get_ticks_msec() + roundi(seconds * 1000.0)
	while Time.get_ticks_msec() < deadline and not result_printed:
		var diagnostics: Dictionary = _diagnostics().get("extras", {})
		if not diagnostics.has(pending_key):
			return false
		if not bool(diagnostics[pending_key]):
			return true
		await get_tree().create_timer(0.1).timeout
	return false


func _session() -> bool:
	var deadline: int = Time.get_ticks_msec() + 45000
	var diagnostics: Dictionary = {}
	while Time.get_ticks_msec() < deadline:
		diagnostics = _diagnostics()
		var state: int = int(view.interface.call("get_session_state"))
		var packet: ARFramePacket = view.interface.call("get_current_packet") as ARFramePacket
		if state in [6, 7] and packet != null:
			break
		if state in [0, 4, 9]:
			break
		await get_tree().create_timer(0.1).timeout
	var required: Array[String] = ["state_history", "configured", "renderer", "arcore_sdk_version", "arcore_apk_version", "config"]
	if not _require("arcore-session", diagnostics, required):
		return false
	var history: Array = diagnostics["state_history"]
	var ready_index: int = history.find(5)
	var initialized_index: int = history.find(6)
	var tracking_index: int = history.find(7)
	var packet: ARFramePacket = view.interface.call("get_current_packet") as ARFramePacket
	var ok: bool = ready_index >= 0 and (initialized_index > ready_index or tracking_index > ready_index) and bool(diagnostics["configured"]) and packet != null
	ok = ok and RenderingServer.get_current_rendering_method() == "mobile" and RenderingServer.get_current_rendering_driver_name() == "vulkan"
	_step("arcore-session", "OK" if ok else "FAIL", "ready=%s initialized=%s tracking=%s sdk=%s apk=%s renderer=%s details=diagnostics.json" % [ready_index, initialized_index, tracking_index, diagnostics["arcore_sdk_version"], diagnostics["arcore_apk_version"], diagnostics["renderer"]])
	return ok


func _no_gl() -> void:
	if not _require("no-gl", observed, ["successful_updates", "update_status_counts", "egl_context_violations"]):
		return
	var statuses: Dictionary = observed["update_status_counts"]
	var missing_gl: int = int(statuses.get("-7", statuses.get(-7, 0)))
	var texture_not_set: int = int(statuses.get("-6", statuses.get(-6, 0)))
	var updates: int = _delta("successful_updates")
	var ok: bool = updates > 0 and int(observed["egl_context_violations"]) == 0 and missing_gl == 0 and texture_not_set == 0
	ok = ok and int(view.interface.call("get_session_state")) in [6, 7]
	_step("no-gl", "OK" if ok else "FAIL", "seconds=%.3f requested_seconds=%.3f successful_updates=%d egl_context_violations=%d statuses=%s" % [observed_seconds, duration, updates, observed["egl_context_violations"], JSON.stringify(statuses)])


func _camera_image() -> void:
	var required: Array[String] = ["config", "config_list", "new_frame_count", "camera_image_count", "image_not_yet_available", "image_timestamp_mismatches", "image_timestamp_far_count", "image_timestamp_delta_min_ns", "image_timestamp_delta_mean_ns", "image_timestamp_delta_max_ns", "timestamp_regressions", "image_plane_layout", "image_acquire_p50_us", "image_acquire_p99_us", "repack_p50_us", "repack_p95_us", "stats_p50_us", "stats_p95_us", "upload_p50_us", "upload_p95_us", "repack_upload_p50_us", "repack_upload_p95_us"]
	if not _require("camera-image", observed, required):
		return
	var config: Dictionary = observed["config"]
	if not _require("camera-image", config, ["fps_min", "fps_max", "cpu_width", "cpu_height"]):
		return
	var frames: int = _delta("new_frame_count")
	var images: int = _delta("camera_image_count")
	var unavailable: int = _delta("image_not_yet_available")
	var missing: int = maxi(unavailable, frames - images)
	var missing_per_1000: float = float(missing) * 1000.0 / float(maxi(1, frames))
	var image_hz: float = float(images) / maxf(0.001, observed_seconds)
	var minimum_hz: float = float(config["fps_min"]) * 0.8
	var layout: Array = observed["image_plane_layout"]
	var layout_valid: bool = layout.size() == 3
	for plane: Dictionary in layout:
		layout_valid = layout_valid and int(plane.get("row_stride", 0)) > 0 and int(plane.get("pixel_stride", 0)) > 0 and int(plane.get("data_length", 0)) > 0
	var ok: bool = frames > 0 and images > 0 and missing_per_1000 <= 50.0 and minimum_hz > 0 and image_hz >= minimum_hz
	ok = ok and int(observed["image_timestamp_far_count"]) == 0 and int(observed["timestamp_regressions"]) == 0 and layout_valid and not Array(observed["config_list"]).is_empty()
	var summary: String = "frames=%d images=%d hz=%.2f minimum_hz=%.2f missing_per_1000=%.2f cpu=%sx%s layout=%s" % [frames, images, image_hz, minimum_hz, missing_per_1000, config["cpu_width"], config["cpu_height"], observed.get("image_layout", "unknown")]
	summary += " acquire_us_p50/p99=%.1f/%.1f" % [observed["image_acquire_p50_us"], observed["image_acquire_p99_us"]]
	for stage: String in ["repack", "stats", "upload", "repack_upload"]:
		summary += " %s_us_p50/p95=%.1f/%.1f" % [stage, observed[stage + "_p50_us"], observed[stage + "_p95_us"]]
	summary += " timestamp_mismatches=%s far=%s regressions=%s delta_ns_min/mean/max=%.0f/%.1f/%.0f" % [observed["image_timestamp_mismatches"], observed["image_timestamp_far_count"], observed["timestamp_regressions"], observed["image_timestamp_delta_min_ns"], observed["image_timestamp_delta_mean_ns"], observed["image_timestamp_delta_max_ns"]]
	_step("camera-image", "OK" if ok else "FAIL", summary)


func _diagnostic_check(name: String, valid_key: String, samples_key: String) -> void:
	if not _require(name, observed, [valid_key, samples_key]):
		return
	var valid: bool = bool(observed[valid_key]) and int(observed[samples_key]) > 0
	_step(name, "OK" if valid else "FAIL", "%s=%s samples=%s" % [valid_key, observed[valid_key], observed[samples_key]])


func _composite() -> void:
	var diagnostics: Dictionary = _diagnostics()
	if not _require("composite", diagnostics, ["mean_y", "packets_presented", "textures_updated", "last_error"]):
		return
	await RenderingServer.frame_post_draw
	var screen: Image = get_viewport().get_texture().get_image()
	var screen_saved: bool = _save_image(screen, "screen.png")
	var scene_visible: bool = view.composite.visible
	var hud_visible: bool = view.hud.visible
	view.composite.visible = false
	view.hud.visible = false
	await get_tree().process_frame
	await RenderingServer.frame_post_draw
	var camera_image: Image = get_viewport().get_texture().get_image()
	var camera_saved: bool = _save_image(camera_image, "camera-layer.png")
	view.composite.visible = scene_visible
	view.hud.visible = hud_visible
	var packet: ARFramePacket = view.interface.call("get_presented_packet") as ARFramePacket
	var visibility: Dictionary = {"valid": false, "dark": false, "expected_y": -1.0, "rendered_y": -1.0}
	if packet != null and packet.y_texture != null:
		visibility = _camera_layer_check(camera_image, packet.y_texture.get_image(), packet.display_transform, Rect2(view.global_position, view.size))
	var ok: bool = screen_saved and camera_saved and int(diagnostics["packets_presented"]) > 0 and int(diagnostics["textures_updated"]) > 0 and String(diagnostics["last_error"]).is_empty()
	ok = ok and packet != null and packet.y_texture != null and packet.cbcr_texture != null and bool(visibility["valid"])
	var level: String = "FAIL" if not ok else ("INFO" if bool(visibility["dark"]) else "OK")
	_step("composite", level, "mean_y=%.3f mapped_mean_y=%.3f rendered_mean_y=%.3f presented=%s textures_updated=%s error=%s captures=%s" % [diagnostics["mean_y"], visibility["expected_y"], visibility["rendered_y"], diagnostics["packets_presented"], diagnostics["textures_updated"], diagnostics["last_error"], output_dir])


func _camera_layer_check(rendered: Image, source_y: Image, mapping: Transform2D, camera_rect: Rect2) -> Dictionary:
	var result: Dictionary = {"valid": false, "dark": false, "expected_y": -1.0, "rendered_y": -1.0}
	if rendered == null or source_y == null or rendered.is_empty() or source_y.is_empty():
		return result
	var source_sum: float = 0.0
	var rendered_sum: float = 0.0
	for row: int in range(18):
		for column: int in range(24):
			var uv := Vector2((column + 0.5) / 24.0, (row + 0.5) / 18.0)
			var source_uv: Vector2 = mapping * uv
			var pixel: Vector2 = camera_rect.position + camera_rect.size * uv
			if not source_uv.is_finite() or not Rect2(Vector2.ZERO, Vector2.ONE).has_point(source_uv) or not Rect2(Vector2.ZERO, Vector2(rendered.get_size())).has_point(pixel):
				return result
			var source_pixel := Vector2i(source_uv * Vector2(source_y.get_size()))
			source_sum += source_y.get_pixelv(source_pixel).r * 255.0
			var color: Color = rendered.get_pixelv(Vector2i(pixel))
			rendered_sum += (0.299 * color.r + 0.587 * color.g + 0.114 * color.b) * 255.0
	result["expected_y"] = source_sum / 432.0
	result["rendered_y"] = rendered_sum / 432.0
	result["dark"] = float(result["expected_y"]) < 8.0
	result["valid"] = bool(result["dark"]) or float(result["rendered_y"]) > 2.0
	return result


func _lifecycle() -> void:
	if dataset_owner != null:
		dataset_owner.call("finish_dataset")
	if not view.interface.has_method("harness_pause") or not view.interface.has_method("harness_resume"):
		_step("lifecycle", "FAIL", "owner lifecycle hooks unavailable")
		return
	var before: Dictionary = _diagnostics()
	if not _require("lifecycle", before, ["pause_count", "resume_count", "reset_count", "updates_while_paused", "successful_updates"]):
		return
	var ok: bool = true
	var durations: Array[Dictionary] = []
	for cycle: int in range(2):
		var started: int = Time.get_ticks_usec()
		view.interface.call("harness_pause")
		var pause_ms: float = float(Time.get_ticks_usec() - started) / 1000.0
		var paused: Dictionary = _diagnostics()
		ok = ok and int(view.interface.call("get_session_state")) == 8
		await get_tree().create_timer(0.3).timeout
		var after_pause: Dictionary = _diagnostics()
		ok = ok and int(after_pause.get("successful_updates", -1)) == int(paused.get("successful_updates", -2))
		var frame: ARFramePacket = view.interface.call("get_current_packet") as ARFramePacket
		var frame_id: int = frame.frame_id if frame != null else -1
		started = Time.get_ticks_usec()
		view.interface.call("harness_resume")
		var resumed: bool = await _wait_for_packet(int(view.interface.call("get_session_generation")), frame_id)
		ok = ok and resumed
		durations.append({"cycle": cycle, "pause_ms": pause_ms, "resume_to_packet_ms": float(Time.get_ticks_usec() - started) / 1000.0})
	for cycle: int in range(2):
		var old_generation: int = int(view.interface.call("get_session_generation"))
		var started: int = Time.get_ticks_usec()
		view.interface.call("reset")
		_log_device_diagnostics("reset%d" % cycle)
		var generation: int = int(view.interface.call("get_session_generation"))
		ok = ok and _old_trackers_absent(generation)
		var resumed: bool = await _wait_for_packet(generation, -1)
		ok = ok and generation > old_generation and resumed and _old_trackers_absent(generation)
		durations.append({"reset": cycle, "old_generation": old_generation, "generation": generation, "reset_to_packet_ms": float(Time.get_ticks_usec() - started) / 1000.0})
	var after: Dictionary = _diagnostics()
	if not _require("lifecycle", after, ["pause_count", "resume_count", "reset_count", "updates_while_paused", "stale_tracker_count"]):
		return
	ok = ok and int(after["pause_count"]) >= int(before["pause_count"]) + 2 and int(after["resume_count"]) >= int(before["resume_count"]) + 2 and int(after["reset_count"]) == int(before["reset_count"]) + 2
	ok = ok and int(after["updates_while_paused"]) == 0 and int(after["stale_tracker_count"]) == 0
	if not _require("lifecycle", after, ["egl_context_violations", "update_status_counts"]):
		return
	var statuses: Dictionary = after["update_status_counts"]
	ok = ok and int(after["egl_context_violations"]) == 0 and int(statuses.get("-7", statuses.get(-7, 0))) == 0 and int(statuses.get("-6", statuses.get(-6, 0))) == 0
	for index: int in range(durations.size()):
		HarnessLog.write_line("ARCORE lifecycle %d %s" % [index, JSON.stringify(durations[index])])
	_step("lifecycle", "OK" if ok else "FAIL", "pauses=%s resumes=%s resets=%s updates_while_paused=%s stale_trackers=%s" % [after["pause_count"], after["resume_count"], after["reset_count"], after["updates_while_paused"], after["stale_tracker_count"]])


func _wait_for_packet(generation: int, after_frame: int) -> bool:
	var deadline: int = Time.get_ticks_msec() + 12000
	while Time.get_ticks_msec() < deadline and not result_printed:
		var packet: ARFramePacket = view.interface.call("get_current_packet") as ARFramePacket
		if packet != null and packet.session_generation == generation and packet.frame_id > after_frame:
			return true
		await get_tree().create_timer(0.1).timeout
	return false


func _old_trackers_absent(generation: int) -> bool:
	var trackers: Dictionary = XRServer.get_trackers(XRServer.TRACKER_ANCHOR)
	for value: Variant in trackers.values():
		var tracker: ARPlaneTracker = value as ARPlaneTracker
		if tracker != null and tracker.session_generation != generation:
			return false
	return true


func _tracking() -> void:
	var diagnostics: Dictionary = _diagnostics()
	var hits: Array = view.raycast(view.size * 0.5)
	var hit_info: Dictionary = {}
	if not hits.is_empty():
		var hit: ARRaycastHit = hits[0] as ARRaycastHit
		hit_info = {"plane_id": hit.plane_id, "distance": hit.distance, "frame_id": hit.frame_id, "session_generation": hit.session_generation}
	_step("tracking", "INFO", "state=%s reason=%s time_to_first_tracking_ms=%.3f clock=harness_start planes=%s centre_hit=%s" % [view.interface.call("get_session_state"), view.interface.call("get_tracking_reason"), first_tracking_ms, diagnostics.get("plane_count", "unavailable"), JSON.stringify(hit_info)])


func _log_device_diagnostics(phase: String) -> void:
	if not Engine.has_singleton("HandheldARPlugin"):
		HarnessLog.write_line("ARCORE DEVICE %s unavailable" % phase)
		return
	var plugin: Object = Engine.get_singleton("HandheldARPlugin")
	if not bool(plugin.call("has_java_method", "getDiagnostics")):
		HarnessLog.write_line("ARCORE DEVICE %s getDiagnostics unavailable" % phase)
		return
	var diagnostics: String = String(plugin.call("getDiagnostics"))
	var values: Dictionary = JSON.parse_string(diagnostics) as Dictionary
	HarnessLog.write_line("ARCORE DEVICE %s pss_kib=%s thermal=%s plugged=%s details=device-%s.json" % [phase, values.get("process_pss_kib", "unavailable"), values.get("thermal_status", "unavailable"), values.get("battery_plugged", "unavailable"), phase])
	var file: FileAccess = FileAccess.open(output_dir.path_join("device-%s.json" % phase), FileAccess.WRITE)
	if file != null:
		file.store_string(diagnostics)
	else:
		_step("output", "FAIL", "cannot save device diagnostics: " + error_string(FileAccess.get_open_error()))


func _log_periodic_device_diagnostics() -> void:
	if not result_printed:
		var elapsed_seconds: int = roundi(float(Time.get_ticks_usec() - started_usec) / 1000000.0)
		_log_device_diagnostics("%ds" % elapsed_seconds)


func _diagnostics() -> Dictionary:
	return view.interface.call("get_diagnostics") as Dictionary


func _delta(key: String) -> int:
	return int(observed.get(key, 0)) - int(baseline.get(key, 0))


func _require(step: String, data: Dictionary, keys: Array[String]) -> bool:
	var missing: PackedStringArray = []
	for key: String in keys:
		if not data.has(key):
			missing.append(key)
	if not missing.is_empty():
		_step(step, "FAIL", "missing diagnostics: " + ",".join(missing))
		return false
	return true


func _save_image(image: Image, name: String) -> bool:
	if image == null or image.is_empty():
		return false
	return image.save_png(output_dir.path_join(name)) == OK


func _step(name: String, level: String, message: String) -> void:
	if result_printed:
		return
	failed = failed or level == "FAIL"
	HarnessLog.write_line("HARNESS STEP %s %s %s" % [name, level, message])


func _timeout() -> void:
	if not result_printed:
		_step("timeout", "FAIL", "exceeded %.1f seconds" % timeout_seconds)
		_finish()


func _finish() -> void:
	if result_printed:
		return
	if dataset_owner != null:
		dataset_owner.call("finish_dataset")
		var dataset_failures: int = int(dataset_owner.get("dataset_failures"))
		if dataset_failures > 0:
			_step("dataset-assertions", "FAIL", "%d recorded hit assertions failed" % dataset_failures)
	if OS.get_name() == "Android":
		_log_device_diagnostics("end")
	if view != null and view.interface != null and view.interface.has_method("get_diagnostics"):
		var diagnostics: Dictionary = _diagnostics()
		var file: FileAccess = FileAccess.open(output_dir.path_join("diagnostics.json"), FileAccess.WRITE)
		if file != null:
			file.store_string(JSON.stringify(diagnostics, "\t"))
			HarnessLog.write_line("ARCORE DIAGNOSTICS saved=diagnostics.json")
		else:
			_step("output", "FAIL", "cannot save diagnostics: " + error_string(FileAccess.get_open_error()))
	result_printed = true
	HarnessLog.write_line("HARNESS RESULT " + ("FAIL" if failed else "PASS"))
	get_tree().quit(1 if failed else 0)
