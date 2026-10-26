extends "res://ar/device_harness.gd"

const STEPS: Array[String] = ["arcore-session", "no-gl", "camera-image", "gpu-vs-cpu", "projection-form", "display-transform", "composite", "packet-retention", "tracking", "plane-finding", "lifecycle"]

var dataset_owner: Node
var started_usec: int = 0
var first_tracking_ms: float = -1.0


func _init() -> void:
	output_dir = "user://harness/arcore"
	selected_steps = PackedStringArray(STEPS)


func _ready() -> void:
	started_usec = Time.get_ticks_usec()
	var args: PackedStringArray = OS.get_cmdline_user_args()
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
		timeout_seconds = duration + 120.0
	if args.has("-harness-gpu-validate") and not selected_steps.has("gpu-vs-cpu"):
		selected_steps.append("gpu-vs-cpu")
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
	view.interface.call("set_diagnostics_enabled", true)
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
	if first_tracking_ms < 0 and view != null and view.interface != null:
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
	baseline = _diagnostics()
	var started: int = Time.get_ticks_usec()
	if selected_steps.has("no-gl") or selected_steps.has("camera-image") or (selected_steps.has("gpu-vs-cpu") and OS.get_cmdline_user_args().has("-harness-gpu-validate")):
		HarnessLog.write_line("ARCORE OBSERVATION duration=%.1f seconds" % duration)
		await _observe()
	observed_seconds = float(Time.get_ticks_usec() - started) / 1000000.0
	observed = _diagnostics()
	for name: String in STEPS:
		if not selected_steps.has(name):
			continue
		if result_printed:
			return
		match name:
			"no-gl": _no_gl()
			"camera-image": _camera_image()
			"gpu-vs-cpu": _gpu_vs_cpu()
			"projection-form": _diagnostic_check(name, "projection_form_valid", "projection_form_samples")
			"display-transform": _diagnostic_check(name, "display_transform_valid", "display_transform_samples")
			"composite": await _composite()
			"packet-retention": await _packet_retention()
			"plane-finding": await _plane_finding()
			"lifecycle": await _lifecycle()
			"tracking": await _tracking()
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
	var missing_gl: int = int(statuses.get("-7", 0))
	var texture_not_set: int = int(statuses.get("-6", 0))
	var updates: int = _delta("successful_updates")
	var ok: bool = updates > 0 and int(observed["egl_context_violations"]) == 0 and missing_gl == 0 and texture_not_set == 0
	ok = ok and int(view.interface.call("get_session_state")) in [6, 7]
	_step("no-gl", "OK" if ok else "FAIL", "seconds=%.3f requested_seconds=%.3f successful_updates=%d egl_context_violations=%d statuses=%s" % [observed_seconds, duration, updates, observed["egl_context_violations"], JSON.stringify(statuses)])


func _camera_image() -> void:
	if String(observed.get("camera_path", "cpu")) == "gpu":
		_gpu_camera_image()
		return
	var required: Array[String] = ["config", "config_list", "new_frame_count", "camera_image_count", "image_not_yet_available", "image_timestamp_mismatches", "image_timestamp_far_count", "image_timestamp_delta_min_ns", "image_timestamp_delta_mean_ns", "image_timestamp_delta_max_ns", "timestamp_regressions", "image_plane_layout", "image_acquire_p50_us", "image_acquire_p99_us", "repack_p50_us", "repack_p95_us", "stats_p50_us", "stats_p95_us", "upload_p50_us", "upload_p95_us", "repack_upload_p50_us", "repack_upload_p95_us", "texture_refusals", "staging_block_size_kb", "texture_upload_region_size_px"]
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
	ok = ok and int(observed.get("texture_refusals", -1)) == 0
	var summary: String = "frames=%d images=%d hz=%.2f minimum_hz=%.2f missing_per_1000=%.2f cpu=%sx%s layout=%s" % [frames, images, image_hz, minimum_hz, missing_per_1000, config["cpu_width"], config["cpu_height"], observed.get("image_layout", "unknown")]
	summary += " acquire_us_p50/p99=%.1f/%.1f" % [observed["image_acquire_p50_us"], observed["image_acquire_p99_us"]]
	for stage: String in ["repack", "stats", "upload", "repack_upload"]:
		summary += " %s_us_p50/p95=%.1f/%.1f" % [stage, observed[stage + "_p50_us"], observed[stage + "_p95_us"]]
	summary += " timestamp_mismatches=%s far=%s regressions=%s delta_ns_min/mean/max=%.0f/%.1f/%.0f" % [observed["image_timestamp_mismatches"], observed["image_timestamp_far_count"], observed["timestamp_regressions"], observed["image_timestamp_delta_min_ns"], observed["image_timestamp_delta_mean_ns"], observed["image_timestamp_delta_max_ns"]]
	summary += " staging_kb=%s region_px=%s refusals=%s" % [observed.get("staging_block_size_kb", "missing"), observed.get("texture_upload_region_size_px", "missing"), observed.get("texture_refusals", "missing")]
	summary = "path=cpu " + summary
	summary += " frame_setup_us_p50/p95=%.1f/%.1f" % [observed.get("frame_setup_p50_us", 0.0), observed.get("frame_setup_p95_us", 0.0)]
	_step("camera-image", "OK" if ok else "FAIL", summary)


func _gpu_camera_image() -> void:
	if not _require("camera-image", observed, ["config", "new_frame_count", "gpu_submissions", "gpu_buffer_missing", "gpu_submit_errors", "gpu_submit_p50_us", "gpu_submit_p95_us", "gpu_completions", "gpu_completion_failures", "frame_setup_p50_us", "frame_setup_p95_us", "timestamp_regressions", "texture_refusals"]):
		return
	var config: Dictionary = observed["config"]
	if not _require("camera-image", config, ["fps_min", "texture_width", "texture_height"]):
		return
	var frames: int = _delta("new_frame_count")
	var images: int = _delta("gpu_submissions")
	var missing: int = maxi(_delta("gpu_buffer_missing"), frames - images)
	var fraction: float = float(missing) / float(maxi(1, frames))
	var hz: float = float(images) / maxf(0.001, observed_seconds)
	var minimum_hz: float = float(config["fps_min"]) * 0.8
	var ok: bool = frames > 0 and images > 0 and fraction <= 0.05 and minimum_hz > 0 and hz >= minimum_hz
	ok = ok and int(observed["gpu_submit_errors"]) == 0 and int(observed["timestamp_regressions"]) == 0 and int(observed["texture_refusals"]) == 0
	ok = ok and _delta("gpu_completions") > 0 and int(observed["gpu_completion_failures"]) == 0
	var summary: String = "path=gpu frames=%d submissions=%d hz=%.2f minimum_hz=%.2f missing_per_1000=%.2f gpu=%sx%s queue_us_p50/p95=%.1f/%.1f" % [frames, images, hz, minimum_hz, fraction * 1000, config["texture_width"], config["texture_height"], observed["gpu_submit_p50_us"], observed["gpu_submit_p95_us"]]
	summary += " frame_setup_us_p50/p95=%.1f/%.1f completed=%s failed=%s" % [observed["frame_setup_p50_us"], observed["frame_setup_p95_us"], observed["gpu_completions"], observed["gpu_completion_failures"]]
	summary += " cpu_images=%s validation_skipped_submissions=%s reason=%s" % [observed.get("camera_image_count", 0), observed.get("gpu_validation_skipped_submissions", 0), observed.get("camera_path_reason", "")]
	_step("camera-image", "OK" if ok else "FAIL", summary)


func _gpu_vs_cpu() -> void:
	if not bool(observed.get("gpu_validation_enabled", false)):
		_step("gpu-vs-cpu", "INFO", "not requested; use -harness-gpu-validate")
		return
	if String(observed.get("camera_path", "cpu")) != "gpu":
		_step("gpu-vs-cpu", "FAIL", "GPU path unavailable: " + String(observed.get("camera_path_reason", "unknown")))
		return
	if not _require("gpu-vs-cpu", observed, ["gpu_validation_samples", "gpu_validation_failures", "gpu_validation_worst_error_255", "gpu_validation_luma_range_255", "gpu_validation_chroma_magnitude_255"]):
		return
	var error: float = float(observed["gpu_validation_worst_error_255"])
	var ok: bool = int(observed["gpu_validation_samples"]) >= 3 and int(observed["gpu_validation_failures"]) == 0 and is_finite(error) and error >= 0 and error <= 6.0
	ok = ok and int(observed.get("gpu_completion_failures", 0)) == 0
	var luma_range: float = float(observed["gpu_validation_luma_range_255"])
	var chroma_magnitude: float = float(observed["gpu_validation_chroma_magnitude_255"])
	ok = ok and is_finite(luma_range) and is_finite(chroma_magnitude) and luma_range >= 0 and chroma_magnitude >= 0
	var informative: bool = luma_range >= 24.0 and chroma_magnitude >= 8.0
	var level: String = "FAIL" if not ok else ("OK" if informative else "INFO")
	_step("gpu-vs-cpu", level, "samples=%s reference=bt601_full worst_block_error_255=%.3f threshold=6 failures=%s luma_range_255=%.3f chroma_magnitude_255=%.3f sufficient_variation=%s details=diagnostics.json" % [observed["gpu_validation_samples"], error, observed["gpu_validation_failures"], luma_range, chroma_magnitude, informative])


func _diagnostic_check(name: String, valid_key: String, samples_key: String) -> void:
	if not _require(name, observed, [valid_key, samples_key]):
		return
	var valid: bool = bool(observed[valid_key]) and int(observed[samples_key]) > 0
	_step(name, "OK" if valid else "FAIL", "%s=%s samples=%s" % [valid_key, observed[valid_key], observed[samples_key]])


func _composite() -> void:
	var diagnostics: Dictionary = _diagnostics()
	if not _require("composite", diagnostics, ["packets_presented", "textures_updated", "last_error"]):
		return
	await RenderingServer.frame_post_draw
	var screen: Image = get_viewport().get_texture().get_image()
	var screen_saved: bool = _save_image(screen, "screen.png")
	var scene_visible: bool = view.scene_root.visible
	var hud_visible: bool = view.hud.visible
	view.scene_root.visible = false
	view.hud.visible = false
	await get_tree().process_frame
	await RenderingServer.frame_post_draw
	var camera_image: Image = get_viewport().get_texture().get_image()
	var camera_saved: bool = _save_image(camera_image, "camera-layer.png")
	view.scene_root.visible = scene_visible
	view.hud.visible = hud_visible
	var packet: ARFramePacket = view.interface.call("get_presented_packet") as ARFramePacket
	var visibility: Dictionary = {"valid": false, "dark": false, "expected_y": -1.0, "rendered_y": -1.0}
	if packet != null and packet.y_texture != null:
		visibility = _camera_layer_check(camera_image, packet.y_texture.get_image(), packet.display_transform, Rect2(view.global_position, view.size))
	elif packet != null and packet.rgb_texture != null:
		visibility = _camera_layer_check(camera_image, packet.rgb_texture.get_image(), packet.display_transform, Rect2(view.global_position, view.size), true)
	var ok: bool = screen_saved and camera_saved and int(diagnostics["packets_presented"]) > 0 and int(diagnostics["textures_updated"]) > 0 and String(diagnostics["last_error"]).is_empty()
	ok = ok and packet != null and (packet.rgb_texture != null or (packet.y_texture != null and packet.cbcr_texture != null)) and bool(visibility["valid"])
	var level: String = "FAIL" if not ok else ("INFO" if bool(visibility["dark"]) else "OK")
	var summary: String = "mapped_mean_y=%.3f rendered_mean_y=%.3f presented=%s textures_updated=%s error=%s captures=%s" % [visibility["expected_y"], visibility["rendered_y"], diagnostics["packets_presented"], diagnostics["textures_updated"], diagnostics["last_error"], output_dir]
	if String(diagnostics.get("camera_path", "cpu")) != "gpu":
		if not _require("composite", diagnostics, ["mean_y"]):
			return
		summary = "mean_y=%.3f " % diagnostics["mean_y"] + summary
	_step("composite", level, summary)


func _camera_layer_check(rendered: Image, source_y: Image, mapping: Transform2D, camera_rect: Rect2, rgb_input: bool = false) -> Dictionary:
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
			var source_color: Color = source_y.get_pixelv(source_pixel)
			source_sum += (0.299 * source_color.r + 0.587 * source_color.g + 0.114 * source_color.b) * 255.0 if rgb_input else source_color.r * 255.0
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
	var before: Dictionary = _diagnostics()
	if not _require("lifecycle", before, ["pause_count", "resume_count", "reset_count", "successful_updates"]):
		return
	var ok: bool = true
	var durations: Array[Dictionary] = []
	for cycle: int in range(2):
		var started: int = Time.get_ticks_usec()
		view.interface.call("application_pause")
		var pause_ms: float = float(Time.get_ticks_usec() - started) / 1000.0
		var paused: Dictionary = _diagnostics()
		ok = ok and int(view.interface.call("get_session_state")) == 8
		await get_tree().create_timer(0.3).timeout
		var after_pause: Dictionary = _diagnostics()
		ok = ok and int(after_pause.get("successful_updates", -1)) == int(paused.get("successful_updates", -2))
		var frame: ARFramePacket = view.interface.call("get_current_packet") as ARFramePacket
		var frame_id: int = frame.frame_id if frame != null else -1
		started = Time.get_ticks_usec()
		view.interface.call("application_resume")
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
	if not _require("lifecycle", after, ["pause_count", "resume_count", "reset_count", "stale_tracker_count"]):
		return
	ok = ok and int(after["pause_count"]) >= int(before["pause_count"]) + 2 and int(after["resume_count"]) >= int(before["resume_count"]) + 2 and int(after["reset_count"]) == int(before["reset_count"]) + 2
	ok = ok and int(after["stale_tracker_count"]) == 0
	if not _require("lifecycle", after, ["egl_context_violations", "update_status_counts"]):
		return
	var statuses: Dictionary = after["update_status_counts"]
	ok = ok and int(after["egl_context_violations"]) == 0 and int(statuses.get("-7", 0)) == 0 and int(statuses.get("-6", 0)) == 0
	for index: int in range(durations.size()):
		HarnessLog.write_line("ARCORE lifecycle %d %s" % [index, JSON.stringify(durations[index])])
	_step("lifecycle", "OK" if ok else "FAIL", "pauses=%s resumes=%s resets=%s stale_trackers=%s" % [after["pause_count"], after["resume_count"], after["reset_count"], after["stale_tracker_count"]])


func _wait_for_packet(generation: int, after_frame: int) -> bool:
	var deadline: int = Time.get_ticks_msec() + 12000
	while Time.get_ticks_msec() < deadline and not result_printed:
		var packet: ARFramePacket = view.interface.call("get_current_packet") as ARFramePacket
		if packet != null and packet.session_generation == generation and packet.frame_id > after_frame:
			return true
		await get_tree().create_timer(0.1).timeout
	return false


func _tracking() -> void:
	var diagnostics: Dictionary = _diagnostics()
	var hits: Array = view.raycast(view.size * 0.5)
	var hit_info: Dictionary = {}
	if not hits.is_empty():
		var hit: ARRaycastHit = hits[0] as ARRaycastHit
		hit_info = {"plane_id": hit.plane_id, "distance": hit.distance, "frame_id": hit.frame_id, "session_generation": hit.session_generation}
	var agreement: Dictionary = await _centre_hit_agreement()
	_step("tracking", "INFO", "state=%s reason=%s time_to_first_tracking_ms=%.3f clock=harness_start planes=%s centre_hit=%s native_agreement=%s" % [view.interface.call("get_session_state"), view.interface.call("get_tracking_reason"), first_tracking_ms, diagnostics.get("plane_count", "unavailable"), JSON.stringify(hit_info), JSON.stringify(agreement)])


func _log_device_diagnostics(phase: String) -> void:
	if not Engine.has_singleton("PocketARPlugin"):
		HarnessLog.write_line("ARCORE DEVICE %s unavailable" % phase)
		return
	var plugin: Object = Engine.get_singleton("PocketARPlugin")
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
	super._finish()
