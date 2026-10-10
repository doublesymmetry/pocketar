extends SceneTree

const RETENTION: Script = preload("res://ar/packet_retention.gd")
var failures: int = 0


func _init() -> void:
	var before: Dictionary = {"new_frame_count": 10, "packets_published": 10, "texture_refusals": 0, "ring_drops": 0}
	var after: Dictionary = {"new_frame_count": 70, "packets_published": 70, "texture_refusals": 0, "ring_drops": 0}
	_check(RETENTION.evaluate(before, after, 60, 60, 30)["ok"])
	_check(not RETENTION.evaluate(before, after, 60, 0, 0)["ok"])
	_check(not RETENTION.evaluate(before, after, 60, 6, 0)["ok"])
	after["packets_published"] = 69
	_check(not RETENTION.evaluate(before, after, 60, 59, 29)["ok"])
	after["packets_published"] = 70
	after["texture_refusals"] = 1
	_check(not RETENTION.evaluate(before, after, 60, 60, 30)["ok"])
	after["texture_refusals"] = 0
	after["ring_drops"] = 1
	_check(not RETENTION.evaluate(before, after, 60, 60, 30)["ok"])
	_check(not RETENTION.evaluate({}, {}, 0, 0, 0)["ok"])
	print("PACKET RETENTION CHECKS " + ("PASS" if failures == 0 else "FAIL"))
	quit(1 if failures else 0)


func _check(condition: bool) -> void:
	if not condition:
		failures += 1
		push_error("packet retention verdict incorrect")
