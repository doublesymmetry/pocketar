@tool
extends EditorExportPlugin

const AR_REQUIRED_OPTION: String = "handheld_ar/ar_required"


func _get_name() -> String:
	return "HandheldAR"


func _supports_platform(platform: EditorExportPlatform) -> bool:
	return platform is EditorExportPlatformAndroid or platform is EditorExportPlatformIOS


func _get_export_options(platform: EditorExportPlatform) -> Array[Dictionary]:
	if platform is EditorExportPlatformAndroid:
		return [{"option": {"name": AR_REQUIRED_OPTION, "type": TYPE_BOOL}, "default_value": false}]
	return []


func _get_export_options_overrides(platform: EditorExportPlatform) -> Dictionary:
	if platform is EditorExportPlatformAndroid:
		return {"gradle_build/use_gradle_build": true}
	return {}


func _get_android_libraries(_platform: EditorExportPlatform, debug: bool) -> PackedStringArray:
	var variant: String = "debug" if debug else "release"
	return PackedStringArray(["res://addons/handheld_ar/bin/android/handheld_ar-%s.aar" % variant])


func _get_android_dependencies(_platform: EditorExportPlatform, _debug: bool) -> PackedStringArray:
	return PackedStringArray(["com.google.ar:core:1.56.0"])


func _get_android_manifest_element_contents(_platform: EditorExportPlatform, _debug: bool) -> String:
	if get_option(AR_REQUIRED_OPTION):
		return '<uses-feature tools:node="replace" android:name="android.hardware.camera.ar" android:required="true" />'
	return ""


func _get_android_manifest_application_element_contents(_platform: EditorExportPlatform, _debug: bool) -> String:
	var ar_mode: String = "required" if get_option(AR_REQUIRED_OPTION) else "optional"
	return '<meta-data tools:node="replace" android:name="com.google.ar.core" android:value="%s" />' % ar_mode
