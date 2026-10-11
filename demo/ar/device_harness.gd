extends Node

var view: ARView
var duration: float = 60.0
var timeout_seconds: float = 180.0
var output_dir: String
var selected_steps: PackedStringArray
var failed: bool = false
var result_printed: bool = false
var baseline: Dictionary = {}
var observed: Dictionary = {}
var observed_seconds: float = 0.0


func _packet_retention() -> void:
	var probe: RefCounted = preload("res://ar/packet_retention.gd").new()
	var result: Dictionary = await probe.run(view)
	_step("packet-retention", "OK" if bool(result["ok"]) else "FAIL", JSON.stringify(result))


func _old_trackers_absent(generation: int) -> bool:
	var trackers: Dictionary = XRServer.get_trackers(XRServer.TRACKER_ANCHOR)
	for value: Variant in trackers.values():
		var tracker: ARPlaneTracker = value as ARPlaneTracker
		if tracker != null and tracker.session_generation != generation:
			return false
	return true


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
	result_printed = true
	HarnessLog.write_line("HARNESS RESULT " + ("FAIL" if failed else "PASS"))
	get_tree().quit(1 if failed else 0)
