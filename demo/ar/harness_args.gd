extends RefCounted

const STEPS: PackedStringArray = ["arkit-session", "camera-image", "projection-form", "display-transform", "composite", "packet-retention", "tracking", "lifecycle"]
const RENDERING_STEPS: PackedStringArray = ["loaded", "xr-mono-2d", "projection", "camera-transform", "color-invariance", "transparency", "frame-consistency", "resize"]


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
		if flag in ["-harness-ar", "-ar-demo", "-harness", "--harness"]:
			index += 1
			continue
		if flag not in ["--log-file", "-harness-duration", "-harness-timeout", "-harness-out", "-harness-steps", "-harness-run-id", "-harness-cleanup"]:
			error = "unsupported iOS harness argument: " + flag
			break
		if index + 1 >= args.size():
			error = "missing value for " + flag
			break
		var value: String = args[index + 1]
		match flag:
			"-harness-run-id", "-harness-cleanup":
				if RegEx.create_from_string("^[A-Za-z0-9_-]+$").search(value) == null:
					error = flag + " requires a run identifier"
			"-harness-duration", "-harness-timeout":
				if not value.is_valid_float() or not is_finite(value.to_float()) or value.to_float() <= 0:
					error = flag + " must be finite positive seconds"
			"-harness-out", "--log-file":
				if RegEx.create_from_string("^user://[A-Za-z0-9_-][A-Za-z0-9_.-]*$").search(value) == null or value.contains(".."):
					error = flag + " requires a flat user:// path"
			"-harness-steps":
				var allowed: PackedStringArray = STEPS if args.has("-harness-ar") else RENDERING_STEPS
				if not args.has("--harness") and not args.has("-harness-ar"):
					allowed = STEPS
				var names: PackedStringArray = value.split(",", true)
				var unique: Dictionary = {}
				for name: String in names:
					if not allowed.has(name) or unique.has(name):
						error = "invalid or duplicate iOS harness step: " + name
					unique[name] = true
		if not error.is_empty():
			break
		index += 2
	if error.is_empty() and args.has("-harness-cleanup"):
		var run_id: String = args[args.find("-harness-cleanup") + 1]
		if args.has("-harness-out") and not args[args.find("-harness-out") + 1].ends_with("-" + run_id):
			error = "cleanup output must belong to this run"
	return {"args": args, "error": error}


static func cleanup(args: PackedStringArray) -> bool:
	if not args.has("-harness-cleanup"):
		return false
	if args.has("--"):
		args = args.slice(args.find("--") + 1)
	var validation: Dictionary = validate(args)
	if OS.get_name() != "iOS" or not String(validation["error"]).is_empty():
		printerr("HARNESS CLEANUP FAIL invalid iOS cleanup request")
		return true
	var run_id: String = args[args.find("-harness-cleanup") + 1]
	var directory: String = "user://harness-" + run_id
	if args.has("-harness-out"):
		directory = args[args.find("-harness-out") + 1]
	var ok: bool = true
	if DirAccess.dir_exists_absolute(directory):
		var dir: DirAccess = DirAccess.open(directory)
		if dir == null or not dir.get_directories().is_empty():
			ok = false
		else:
			for name: String in dir.get_files():
				ok = DirAccess.remove_absolute(directory.path_join(name)) == OK and ok
			ok = DirAccess.remove_absolute(directory) == OK and ok
	var log_path: String = "user://harness-" + run_id + ".log"
	if FileAccess.file_exists(log_path):
		ok = DirAccess.remove_absolute(log_path) == OK and ok
	# Release builds flush error output; iOS does not exit to flush ordinary prints.
	printerr("HARNESS CLEANUP %s %s" % [run_id, "OK" if ok else "FAIL"])
	return true
