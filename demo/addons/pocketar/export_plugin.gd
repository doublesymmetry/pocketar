@tool
extends EditorExportPlugin
## Internal export integration. The user-facing pocketar/ar_required option defaults to false.
## Android exports use Gradle, the matching PocketAR AAR and the ARCore Maven dependency.
## iOS exports require a camera usage description and a deployment target of at least 16.0.

const AR_REQUIRED_OPTION: String = "pocketar/ar_required"
const IOS_VERSION_ERROR: String = "PocketAR requires application/min_ios_version 16.0 or newer."

var _require_arkit: bool = false


func _get_name() -> String:
	return "PocketAR"


func _supports_platform(platform: EditorExportPlatform) -> bool:
	return platform is EditorExportPlatformAndroid or platform is EditorExportPlatformIOS


func _get_export_options(platform: EditorExportPlatform) -> Array[Dictionary]:
	if _supports_platform(platform):
		return [{"option": {"name": AR_REQUIRED_OPTION, "type": TYPE_BOOL}, "default_value": false}]
	return []


func _get_export_options_overrides(platform: EditorExportPlatform) -> Dictionary:
	if platform is EditorExportPlatformAndroid:
		return {"gradle_build/use_gradle_build": true}
	if platform is EditorExportPlatformIOS:
		return ios_validation_overrides(String(get_option("application/min_ios_version")))
	return {}


static func ios_validation_overrides(version: String) -> Dictionary:
	var pattern: RegEx = RegEx.new()
	pattern.compile("^[0-9]+(\\.[0-9]+){0,2}$")
	if pattern.search(version) != null and int(version.get_slice(".", 0)) >= 16:
		return {}
	# Godot 4.7 export warnings cannot abort; missing templates fail can_export().
	var unavailable: String = "res://.pocketar/requires-ios-16.0-or-newer/no-export-template.zip"
	return {"custom_template/debug": unavailable, "custom_template/release": unavailable}


func _get_export_option_warning(platform: EditorExportPlatform, option: String) -> String:
	if not platform is EditorExportPlatformIOS:
		return ""
	var warnings: PackedStringArray = []
	if option == AR_REQUIRED_OPTION or option == "application/min_ios_version":
		if not ios_validation_overrides(String(get_option("application/min_ios_version"))).is_empty():
			warnings.append(IOS_VERSION_ERROR)
	if option == AR_REQUIRED_OPTION or option == "privacy/camera_usage_description":
		if String(get_option("privacy/camera_usage_description")).strip_edges().is_empty():
			warnings.append("Set privacy/camera_usage_description before exporting PocketAR for iOS.")
	return "\n".join(warnings)


func _export_begin(features: PackedStringArray, _is_debug: bool, _path: String, _flags: int) -> void:
	_require_arkit = features.has("ios") and bool(get_option(AR_REQUIRED_OPTION))
	if features.has("ios"):
		add_apple_embedded_platform_linker_flags("-framework ARKit -framework Metal -framework CoreVideo -framework AVFoundation")


func _end_generate_apple_embedded_project(path: String, _will_build_archive: bool) -> void:
	if not _require_arkit:
		return
	var name: String = path.get_file().get_basename()
	var plist_path: String = path.get_base_dir().path_join(name).path_join(name + "-Info.plist")
	var contents: String = FileAccess.get_file_as_string(plist_path)
	var capabilities: RegEx = RegEx.new()
	capabilities.compile("<key>UIRequiredDeviceCapabilities</key>\\s*<array>([\\s\\S]*?)</array>")
	var found: RegExMatch = capabilities.search(contents)
	if found == null:
		push_error("PocketAR could not find UIRequiredDeviceCapabilities in " + plist_path)
		return
	if found.get_string(1).contains("<string>arkit</string>"):
		return
	var end: int = found.get_end(1)
	contents = contents.substr(0, end) + "\t<string>arkit</string>\n\t" + contents.substr(end)
	var file: FileAccess = FileAccess.open(plist_path, FileAccess.WRITE)
	if file == null:
		push_error("PocketAR could not update " + plist_path)
		return
	file.store_string(contents)


func _get_android_libraries(_platform: EditorExportPlatform, debug: bool) -> PackedStringArray:
	var variant: String = "debug" if debug else "release"
	return PackedStringArray(["res://addons/pocketar/bin/android/pocketar-%s.aar" % variant])


func _get_android_dependencies(_platform: EditorExportPlatform, _debug: bool) -> PackedStringArray:
	return PackedStringArray(["com.google.ar:core:1.56.0"])


func _get_android_manifest_element_contents(_platform: EditorExportPlatform, _debug: bool) -> String:
	if get_option(AR_REQUIRED_OPTION):
		return '<uses-feature tools:node="replace" android:name="android.hardware.camera.ar" android:required="true" />'
	return ""


func _get_android_manifest_application_element_contents(_platform: EditorExportPlatform, _debug: bool) -> String:
	var ar_mode: String = "required" if get_option(AR_REQUIRED_OPTION) else "optional"
	return '<meta-data tools:node="replace" android:name="com.google.ar.core" android:value="%s" />' % ar_mode
