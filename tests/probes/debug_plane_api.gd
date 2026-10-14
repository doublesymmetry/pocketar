extends Node


func _ready() -> void:
	var failures: Array[String] = []
	var expected: bool = ProjectSettings.get_setting("probe/expect_debug", false)
	var classes: Array[String] = ["SyntheticARInterface", "ScriptedARInterface"]
	if OS.get_name() == "Android":
		classes.append("ARCoreInterface")
	elif OS.get_name() == "iOS":
		classes.append("ARKitInterface")
	if OS.is_debug_build() != expected:
		failures.append("export mode does not match probe expectation")
	for name: String in classes:
		if not ClassDB.class_exists(name) or not ClassDB.can_instantiate(name):
			failures.append(name + " missing class")
			continue
		var interface: Object = ClassDB.instantiate(name)
		for method: String in ["reset", "get_session_state", "get_session_generation", "set_plane_finding_mode", "get_current_packet"]:
			if not ClassDB.class_has_method(name, method) or not interface.has_method(method):
				failures.append(name + " missing ordinary method " + method)
		for method: String in ["set_debug_plane", "clear_debug_plane"]:
			if ClassDB.class_has_method(name, method) != expected or interface.has_method(method) != expected:
				failures.append(name + " unexpected debug method " + method)
	var result: Dictionary = {"result": "PASS" if failures.is_empty() else "FAIL", "debug": expected, "platform": OS.get_name(), "classes": classes, "failures": failures}
	var text: String = "DEBUG PLANE API " + JSON.stringify(result)
	print(text)
	var label := Label.new()
	label.text = text
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(label)
	var file := FileAccess.open("user://debug-plane-api.json", FileAccess.WRITE)
	if file != null:
		file.store_string(JSON.stringify(result, "\t") + "\n")
	if OS.get_name() == "macOS":
		get_tree().quit(0 if failures.is_empty() else 1)
