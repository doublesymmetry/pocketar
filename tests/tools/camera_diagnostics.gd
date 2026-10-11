extends SceneTree

class Harness extends "res://ar/ar_harness.gd":
	var results: Array[Dictionary] = []

	func _step(name: String, level: String, message: String) -> void:
		results.append({"name": name, "level": level, "message": message})
		super._step(name, level, message)

var failures: int = 0


func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("CAMERA DIAGNOSTICS FAIL " + message)


func _initialize() -> void:
	var harness := Harness.new()
	var observed: Dictionary = {
		"config": {"fps_min": 30, "fps_max": 30, "cpu_width": 1920, "cpu_height": 1080},
		"config_list": [{"index": 0}],
		"new_frame_count": 1800, "camera_image_count": 1770, "image_not_yet_available": 30,
		"image_timestamp_mismatches": 1760, "image_timestamp_far_count": 0,
		"image_timestamp_delta_min_ns": 0, "image_timestamp_delta_mean_ns": 4340000.0,
		"image_timestamp_delta_max_ns": 9180000, "timestamp_regressions": 0,
		"image_layout": "NV21",
		"texture_refusals": 0,
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
	]:
		var data: Dictionary = observed.duplicate(true)
		data.merge(invalid, true)
		harness.set("observed", data)
		harness.set("failed", false)
		harness.call("_camera_image")
		_check(bool(harness.get("failed")), "invalid diagnostics must fail: " + str(invalid))
	var gpu: Dictionary = {"camera_path": "gpu", "camera_path_reason": "conversion completed", "config": {"fps_min": 30, "texture_width": 1920, "texture_height": 1080}, "new_frame_count": 1800, "gpu_submissions": 1800, "gpu_buffer_missing": 0, "gpu_submit_errors": 0, "gpu_submit_p50_us": 20.0, "gpu_submit_p95_us": 40.0, "gpu_completions": 1800, "gpu_completion_failures": 0, "frame_setup_p50_us": 200.0, "frame_setup_p95_us": 400.0, "timestamp_regressions": 0, "texture_refusals": 0}
	harness.set("observed", gpu)
	harness.set("failed", false)
	harness.call("_camera_image")
	_check(not bool(harness.get("failed")), "GPU camera needs no CPU image diagnostics")
	for invalid: Dictionary in [{"gpu_submissions": 1000}, {"gpu_buffer_missing": 100}, {"gpu_submit_errors": 1}, {"gpu_completions": 0}, {"gpu_completion_failures": 1}, {"timestamp_regressions": 1}]:
		var data: Dictionary = gpu.duplicate(true)
		data.merge(invalid, true)
		harness.set("observed", data)
		harness.set("failed", false)
		harness.call("_camera_image")
		_check(bool(harness.get("failed")), "invalid GPU diagnostics must fail: " + str(invalid))
	harness.observed = {
		"camera_path": "gpu", "gpu_validation_enabled": true,
		"gpu_validation_samples": 3, "gpu_validation_failures": 0,
		"gpu_validation_luma_range_255": 80.0, "gpu_validation_chroma_magnitude_255": 30.0,
		"gpu_validation_worst_error_255": 12.0,
	}
	check_level(harness, "FAIL", "CPU parity error exceeds threshold")
	harness.observed["gpu_validation_worst_error_255"] = 6.0
	check_level(harness, "OK", "inclusive parity threshold")
	harness.observed["gpu_validation_luma_range_255"] = 5.0
	check_level(harness, "INFO", "flat luma cannot certify parity")
	harness.observed["gpu_validation_luma_range_255"] = 80.0
	harness.observed["gpu_validation_chroma_magnitude_255"] = 1.0
	check_level(harness, "INFO", "neutral chroma cannot certify parity")
	harness.observed["gpu_validation_worst_error_255"] = 6.1
	check_level(harness, "FAIL", "low variation cannot hide parity errors")
	harness.observed["gpu_validation_worst_error_255"] = NAN
	check_level(harness, "FAIL", "nonfinite parity error")
	var validation: Dictionary = harness.observed.duplicate(true)
	validation["gpu_validation_worst_error_255"] = 6.0
	for invalid: Dictionary in [{"camera_path": "cpu"}, {"gpu_validation_samples": 0}, {"gpu_validation_samples": 2}, {"gpu_validation_failures": 1}, {"gpu_completion_failures": 1}]:
		var data: Dictionary = validation.duplicate(true)
		data.merge(invalid, true)
		harness.observed = data
		check_level(harness, "FAIL", "invalid GPU comparison: " + str(invalid))
	harness.free()
	for prefix: String in ["HARNESS STEP camera-image OK ", "ARCORE DIAGNOSTICS "]:
		for payload: String in ["a".repeat(900), "🙂".repeat(500), "a\nb\rc", "short"]:
			var line: String = HarnessLog.format_line(prefix + payload)
			_check(line.to_utf8_buffer().size() < 1000, "log byte limit")
			_check(line.begins_with(prefix), "parser prefix preserved")
			_check(not line.contains("\n") and not line.contains("\r"), "one log line")
	print("CAMERA DIAGNOSTICS RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)


func check_level(harness: Harness, expected: String, message: String) -> void:
	harness.results.clear()
	harness._gpu_vs_cpu()
	var result: Dictionary = harness.results.back()
	_check(result["name"] == "gpu-vs-cpu" and result["level"] == expected, message + ": " + str(result))
