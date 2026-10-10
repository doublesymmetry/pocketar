extends RefCounted

const STEPS: PackedStringArray = ["arkit-session", "camera-image", "projection-form", "display-transform", "composite", "lifecycle", "tracking"]


static func read() -> Dictionary:
	var args: PackedStringArray = OS.get_cmdline_user_args()
	if OS.get_name() != "iOS" or not OS.is_debug_build():
		return {"args": args, "error": ""}
	if OS.has_environment("HANDHELD_AR_ARGS"):
		return parse_environment(OS.get_environment("HANDHELD_AR_ARGS"))
	return validate(args) if args.has("-harness-ar") else {"args": args, "error": ""}


static func parse_environment(value: String) -> Dictionary:
	var decoded: Variant = JSON.parse_string(value)
	if not decoded is Array:
		return {"args": PackedStringArray(), "error": "HANDHELD_AR_ARGS must be a JSON array"}
	var args := PackedStringArray()
	for item: Variant in decoded:
		if not item is String:
			return {"args": args, "error": "HANDHELD_AR_ARGS entries must be strings"}
		args.append(item)
	return validate(args)


static func validate(args: PackedStringArray) -> Dictionary:
	var index: int = 0
	var seen: Dictionary = {}
	var error: String = ""
	while index < args.size():
		var flag: String = args[index]
		if seen.has(flag):
			error = "duplicate argument: " + flag
			break
		seen[flag] = true
		if flag in ["-harness-ar", "-ar-demo", "-harness", "-harness-probes", "-harness-lifecycle"]:
			index += 1
			continue
		if flag not in ["--log-file", "-harness-duration", "-harness-timeout", "-harness-out", "-harness-steps"]:
			error = "unsupported iOS harness argument: " + flag
			break
		if index + 1 >= args.size():
			error = "missing value for " + flag
			break
		var value: String = args[index + 1]
		match flag:
			"-harness-duration", "-harness-timeout":
				if not value.is_valid_float() or not is_finite(value.to_float()) or value.to_float() <= 0:
					error = flag + " must be finite positive seconds"
			"-harness-out", "--log-file":
				if RegEx.create_from_string("^user://[A-Za-z0-9_-][A-Za-z0-9_.-]*$").search(value) == null or value.contains(".."):
					error = flag + " requires a flat user:// path"
			"-harness-steps":
				var names: PackedStringArray = value.split(",", true)
				var unique: Dictionary = {}
				for name: String in names:
					if not STEPS.has(name) or unique.has(name):
						error = "invalid or duplicate iOS harness step: " + name
					unique[name] = true
		if not error.is_empty():
			break
		index += 2
	return {"args": args, "error": error}
