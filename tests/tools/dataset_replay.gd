extends SceneTree


func _initialize() -> void:
	var script: Script = load("res://ar/ar_demo.gd") as Script
	var demo: Node = script.new() as Node
	var view := ARView.new()
	view.size = Vector2(100, 100)
	demo.set("view", view)
	demo.call("_apply_tap", Vector2(50, 50), true, {"expected_distance_m": 1.0})
	var missing_hit_failed: bool = int(demo.get("dataset_failures")) == 1
	demo.call("_apply_tap", Vector2(50, 50), true, {"expected_distance_m": -1.0})
	var desk_case_informational: bool = int(demo.get("dataset_failures")) == 1
	demo.free()
	view.free()
	var passed: bool = missing_hit_failed and desk_case_informational
	print("DATASET REPLAY RESULT " + ("PASS" if passed else "FAIL"))
	quit(0 if passed else 1)
