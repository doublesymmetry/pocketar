extends SceneTree

var failures: int = 0


func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("CAMERA DIAGNOSTICS FAIL " + message)


func _initialize() -> void:
	var script: Script = load("res://ar/ar_harness.gd") as Script
	var harness: Node = script.new() as Node
	var observed: Dictionary = {
		"config": {"fps_min": 30, "fps_max": 30, "cpu_width": 1920, "cpu_height": 1080},
		"config_list": [{"index": 0}],
		"new_frame_count": 1800, "camera_image_count": 1770, "image_not_yet_available": 30,
		"image_timestamp_mismatches": 1760, "image_timestamp_far_count": 0,
		"image_timestamp_delta_min_ns": 0, "image_timestamp_delta_mean_ns": 4340000.0,
		"image_timestamp_delta_max_ns": 9180000, "timestamp_regressions": 0,
		"image_layout": "NV21",
		"texture_refusals": 0, "ring_drops": 0,
		"staging_block_size_kb": 4096, "texture_upload_region_size_px": 1024,
		"image_plane_layout": [
			{"row_stride": 1920, "pixel_stride": 1, "data_length": 2073600},
			{"row_stride": 1920, "pixel_stride": 2, "data_length": 1036799},
			{"row_stride": 1920, "pixel_stride": 2, "data_length": 1036799}],
		"image_acquire_p50_us": 2.0, "image_acquire_p99_us": 20.0,
	}
	for stage: String in ["repack", "stats", "upload", "repack_upload"]:
		observed[stage + "_p50_us"] = 100.0
		observed[stage + "_p95_us"] = 200.0
	harness.set("observed", observed)
	harness.set("observed_seconds", 60.0)
	harness.call("_camera_image")
	_check(not bool(harness.get("failed")), "small timestamp offsets must pass")
	for invalid: Dictionary in [
		{"image_timestamp_far_count": 1},
		{"timestamp_regressions": 1},
		{"camera_image_count": 1000},
		{"image_not_yet_available": 100},
		{"texture_refusals": 1},
		{"ring_drops": 1},
	]:
		var data: Dictionary = observed.duplicate(true)
		data.merge(invalid, true)
		harness.set("observed", data)
		harness.set("failed", false)
		harness.call("_camera_image")
		_check(bool(harness.get("failed")), "invalid diagnostics must fail: " + str(invalid))
	var gpu: Dictionary = {"camera_path": "gpu", "camera_path_reason": "submission accepted", "config": {"fps_min": 30, "texture_width": 1920, "texture_height": 1080}, "new_frame_count": 1800, "gpu_submissions": 1800, "gpu_buffer_missing": 0, "gpu_submit_errors": 0, "gpu_submit_p50_us": 20.0, "gpu_submit_p95_us": 40.0, "timestamp_regressions": 0, "texture_refusals": 0}
	harness.set("observed", gpu)
	harness.set("failed", false)
	harness.call("_camera_image")
	_check(not bool(harness.get("failed")), "GPU camera needs no CPU image diagnostics")
	for invalid: Dictionary in [{"gpu_submissions": 1000}, {"gpu_buffer_missing": 100}, {"gpu_submit_errors": 1}, {"timestamp_regressions": 1}]:
		var data: Dictionary = gpu.duplicate(true)
		data.merge(invalid, true)
		harness.set("observed", data)
		harness.set("failed", false)
		harness.call("_camera_image")
		_check(bool(harness.get("failed")), "invalid GPU diagnostics must fail: " + str(invalid))
	var validation: Dictionary = {"camera_path": "gpu", "gpu_validation_enabled": true, "gpu_validation_samples": 3, "gpu_validation_failures": 0, "gpu_validation_best": "bt601_full", "gpu_validation_worst_block_error_255": 6.0, "gpu_validation_candidates": {}}
	harness.set("observed", validation)
	harness.set("failed", false)
	harness.call("_gpu_vs_cpu")
	_check(not bool(harness.get("failed")), "6/255 validation boundary passes")
	for invalid: Dictionary in [{"camera_path": "cpu"}, {"gpu_validation_samples": 0}, {"gpu_validation_samples": 2}, {"gpu_validation_failures": 1}, {"gpu_validation_worst_block_error_255": 6.01}, {"gpu_validation_worst_block_error_255": NAN}]:
		var data: Dictionary = validation.duplicate(true)
		data.merge(invalid, true)
		harness.set("observed", data)
		harness.set("failed", false)
		harness.call("_gpu_vs_cpu")
		_check(bool(harness.get("failed")), "invalid GPU comparison must fail: " + str(invalid))
	harness.free()
	for prefix: String in ["HARNESS STEP camera-image OK ", "ARCORE DIAGNOSTICS "]:
		for payload: String in ["a".repeat(900), "🙂".repeat(500), "a\nb\rc", "short"]:
			var line: String = HarnessLog.format_line(prefix + payload)
			_check(line.to_utf8_buffer().size() < 1000, "log byte limit")
			_check(line.begins_with(prefix), "parser prefix preserved")
			_check(not line.contains("\n") and not line.contains("\r"), "one log line")
	print("CAMERA DIAGNOSTICS RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
