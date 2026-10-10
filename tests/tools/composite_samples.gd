extends SceneTree

var failures: int = 0


func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("COMPOSITE SAMPLE FAIL " + message)


func _initialize() -> void:
	var script: Script = load("res://ar/ar_harness.gd") as Script
	var harness: Node = script.new() as Node
	var source: Image = Image.create(8, 8, false, Image.FORMAT_R8)
	source.fill(Color.WHITE)
	var rendered: Image = Image.create(8, 8, false, Image.FORMAT_RGB8)
	rendered.fill(Color.BLACK)
	var result: Dictionary = harness.call("_camera_layer_check", rendered, source, Transform2D.IDENTITY, Rect2(0, 0, 8, 8))
	_check(not bool(result["valid"]), "bright source cannot pass with a black shader")
	rendered.fill(Color.WHITE)
	result = harness.call("_camera_layer_check", rendered, source, Transform2D.IDENTITY, Rect2(0, 0, 8, 8))
	_check(bool(result["valid"]) and not bool(result["dark"]), "bright rendered camera passes")
	for y: int in range(8):
		for x: int in range(4):
			source.set_pixel(x, y, Color.BLACK)
	rendered.fill(Color.BLACK)
	var crop := Transform2D(Vector2(0.5, 0), Vector2(0, 1), Vector2.ZERO)
	result = harness.call("_camera_layer_check", rendered, source, crop, Rect2(0, 0, 8, 8))
	_check(bool(result["valid"]) and bool(result["dark"]), "dark visible crop is informational despite bright pixels outside crop")
	harness.free()
	print("COMPOSITE SAMPLE RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
