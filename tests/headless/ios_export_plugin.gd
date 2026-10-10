extends SceneTree

const EXPORTER: Script = preload("res://addons/handheld_ar/export_plugin.gd")

var failures: int = 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	if not Engine.is_editor_hint():
		push_error("Run ios_export_plugin.gd with --editor.")
		quit(1)
		return
	while EditorInterface.get_resource_filesystem().is_scanning():
		await process_frame
	for version: String in ["9.0", "15.10", "", "bad", "16.bad", "16.0.0.1"]:
		var overrides: Dictionary = EXPORTER.ios_validation_overrides(version)
		check(overrides.has("custom_template/debug") and overrides.has("custom_template/release"), "reject unsupported version " + version)
		for path: String in overrides.values():
			check(not FileAccess.file_exists(path), "invalid preset cannot find a debug or release template")
	for version: String in ["16", "16.0", "16.0.1", "17.0", "26.0"]:
		check(EXPORTER.ios_validation_overrides(version).is_empty(), "preserve supported preset " + version)
	var exporter: EditorExportPlugin = EXPORTER.new()
	var directory: String = "/tmp/handheld-ar-export-test-%d" % Time.get_ticks_usec()
	DirAccess.make_dir_recursive_absolute(directory.path_join("sample"))
	var plist_path: String = directory.path_join("sample/sample-Info.plist")
	var original: String = '<plist><dict><key>NSCameraUsageDescription</key><string>My app uses the camera.</string><key>UIRequiredDeviceCapabilities</key><array><string>gps</string><string>iphone-ipad-minimum-performance-a12</string></array></dict></plist>'
	var file: FileAccess = FileAccess.open(plist_path, FileAccess.WRITE)
	file.store_string(original)
	file.close()
	exporter._end_generate_apple_embedded_project(directory.path_join("sample.xcodeproj"), false)
	check(FileAccess.get_file_as_string(plist_path) == original, "optional AR preserves user plist")
	exporter.set("_require_arkit", true)
	exporter._end_generate_apple_embedded_project(directory.path_join("sample.xcodeproj"), false)
	var required: String = FileAccess.get_file_as_string(plist_path)
	check(required.count("<string>arkit</string>") == 1, "required AR adds one capability")
	check(required.contains("<string>gps</string>") and required.contains("<string>iphone-ipad-minimum-performance-a12</string>"), "preserve user and engine capabilities")
	check(required.contains("<string>My app uses the camera.</string>"), "preserve camera purpose")
	exporter._end_generate_apple_embedded_project(directory.path_join("sample.xcodeproj"), false)
	check(FileAccess.get_file_as_string(plist_path) == required, "repeat merge is idempotent")
	exporter.set("_require_arkit", false)
	exporter._end_generate_apple_embedded_project(directory.path_join("sample.xcodeproj"), false)
	check(FileAccess.get_file_as_string(plist_path) == required, "optional AR preserves a supplied arkit capability")
	DirAccess.remove_absolute(plist_path)
	DirAccess.remove_absolute(directory.path_join("sample"))
	DirAccess.remove_absolute(directory)
	print("IOS EXPORT PLUGIN " + ("PASS" if failures == 0 else "FAIL"))
	quit(1 if failures > 0 else 0)


func check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)
