class_name PocketARArgs
extends RefCounted
## Internal development launch-argument transport, outside the game AR API.


## Returns args (PackedStringArray) and error (String, empty on success).
## Reads POCKETAR_ARGS JSON only in iOS debug builds; otherwise uses OS user arguments.
## Invalid JSON returns empty args and an error. Callers validate their own flags.
static func read() -> Dictionary:
	if OS.get_name() == "iOS" and OS.is_debug_build() and OS.has_environment("POCKETAR_ARGS"):
		return _parse_environment(OS.get_environment("POCKETAR_ARGS"))
	return {"args": OS.get_cmdline_user_args(), "error": ""}


static func _parse_environment(value: String) -> Dictionary:
	var json := JSON.new()
	json.parse(value)
	var decoded: Variant = json.data
	if not decoded is Array or decoded.any(func(item: Variant) -> bool: return not item is String):
		return {"args": PackedStringArray(), "error": "POCKETAR_ARGS must be a JSON array of strings"}
	return {"args": PackedStringArray(decoded), "error": ""}
