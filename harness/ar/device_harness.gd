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


func _centre_hit_agreement() -> Dictionary:
	var point: Vector2 = view.size * 0.5
	var errors: Array[float] = []
	var different_planes: int = 0
	for sample: int in range(20):
		await get_tree().create_timer(0.1).timeout
		var ours: Array = view.raycast(point)
		var native_hit: Dictionary = {}
		for entry: Variant in view.interface.call("live_hit_test", point):
			if entry is Dictionary and int(entry.get("plane_id", 0)) != 0 and entry.has("pose"):
				native_hit = entry
				break
		if ours.is_empty() or native_hit.is_empty():
			continue
		var hit: ARRaycastHit = ours[0] as ARRaycastHit
		errors.append(hit.pose.origin.distance_to((native_hit["pose"] as Transform3D).origin))
		if hit.plane_id != int(native_hit["plane_id"]):
			different_planes += 1
	errors.sort()
	if errors.is_empty():
		return {"samples": 0}
	return {"samples": errors.size(), "different_planes": different_planes, "median_m": errors[int(errors.size() / 2.0)], "max_m": errors[-1]}


func _plane_finding() -> void:
	var original: PocketAR.PlaneFindingMode = view.plane_finding_mode
	var generation: int = view.session_generation
	var before: Dictionary = _diagnostics()
	if not _require("plane-finding", before, ["effective_plane_finding_mode", "plane_count", "config"]):
		return
	var samples: Array[Dictionary] = [{"mode": int(before["effective_plane_finding_mode"]), "planes": int(before["plane_count"])}]
	var packets: Array[Array] = []
	var ok: bool = true
	for mode: PocketAR.PlaneFindingMode in [PocketAR.PLANE_FINDING_HORIZONTAL, PocketAR.PLANE_FINDING_DISABLED, PocketAR.PLANE_FINDING_HORIZONTAL_AND_VERTICAL]:
		var previous: ARFramePacket = view.interface.call("get_current_packet") as ARFramePacket
		view.plane_finding_mode = mode
		await get_tree().create_timer(3.0).timeout
		var after: Dictionary = _diagnostics()
		var packet: ARFramePacket = view.interface.call("get_current_packet") as ARFramePacket
		var effective: int = int(after.get("effective_plane_finding_mode", -1))
		ok = ok and view.plane_finding_mode == mode and effective == mode and view.session_generation == generation
		ok = ok and after.get("config", {}) == before["config"] and previous != null and packet != null
		if previous != null and packet != null:
			ok = ok and packet.frame_id > previous.frame_id
		packets.append(packet.planes if packet != null else [])
		samples.append({"mode": effective, "planes": int(after.get("plane_count", -1))})
	view.plane_finding_mode = original
	var restored: int = int(_diagnostics().get("effective_plane_finding_mode", -1))
	ok = ok and view.plane_finding_mode == original and restored == original
	var scene_changed: bool = packets[1] != packets[2]
	var file: FileAccess = FileAccess.open(output_dir.path_join("plane-finding.json"), FileAccess.WRITE)
	if file == null:
		ok = false
	else:
		file.store_string(JSON.stringify({"samples": samples, "planes": packets, "restored_mode": restored}, "  "))
	_step("plane-finding", ("OK" if scene_changed else "INFO") if ok else "FAIL", "config_verified=%s samples=%s restored=%d scene_changed=%s; move over textured surfaces to exercise discovery; details=plane-finding.json" % [ok, JSON.stringify(samples), restored, scene_changed])


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
