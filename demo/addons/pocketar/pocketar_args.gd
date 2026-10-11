class_name PocketARArgs
extends RefCounted


## Returns user arguments and a transport error; applications validate their flags.
static func read() -> Dictionary:
	if OS.get_name() == "iOS" and OS.is_debug_build() and OS.has_environment("POCKETAR_ARGS"):
		return _parse_environment(OS.get_environment("POCKETAR_ARGS"))
	return {"args": OS.get_cmdline_user_args(), "error": ""}


static func _parse_environment(value: String) -> Dictionary:
	var json := JSON.new()
	if json.parse(value) != OK:
		return {"args": PackedStringArray(), "error": "POCKETAR_ARGS must be a JSON array"}
	var decoded: Variant = json.data
	if not decoded is Array:
		return {"args": PackedStringArray(), "error": "POCKETAR_ARGS must be a JSON array"}
	var args := PackedStringArray()
	for item: Variant in decoded:
		if not item is String:
			return {"args": PackedStringArray(), "error": "POCKETAR_ARGS entries must be strings"}
		args.append(item)
	return {"args": args, "error": ""}
