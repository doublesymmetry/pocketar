@tool
extends EditorExportPlugin


func _get_name() -> String:
	return "HandheldAR"


func _supports_platform(platform: EditorExportPlatform) -> bool:
	return platform is EditorExportPlatformAndroid or platform is EditorExportPlatformIOS


func _get_android_libraries(_platform: EditorExportPlatform, debug: bool) -> PackedStringArray:
	var variant: String = "debug" if debug else "release"
	return PackedStringArray(["res://addons/handheld_ar/bin/android/handheld_ar-%s.aar" % variant])
