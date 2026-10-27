extends SceneTree

const SUITE: Script = preload("res://suites/scripted_suite.gd")


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var ok: bool = true
	for mode: ARView.Composition in [ARView.Composition.IN_SCENE, ARView.Composition.ISOLATED_CAMERA]:
		var result: Dictionary = await SUITE.new().run(root, mode)
		ok = ok and bool(result["ok"])
		print("SCRIPTED %s %s %s" % [ARView.Composition.keys()[mode], "PASS" if result["ok"] else "FAIL", result["detail"]])
	quit(0 if ok else 1)
