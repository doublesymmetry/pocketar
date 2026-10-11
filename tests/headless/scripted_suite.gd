extends SceneTree

const SUITE: Script = preload("res://harness/scripted_suite.gd")


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var result: Dictionary = await SUITE.new().run(root)
	print("SCRIPTED RESULT %s %s" % ["PASS" if result["ok"] else "FAIL", result["detail"]])
	quit(0 if result["ok"] else 1)
