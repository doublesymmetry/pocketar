extends SceneTree

const ARGS: Script = preload("res://addons/pocketar/pocketar_args.gd")

var failures: int = 0


func _init() -> void:
	var user_args: PackedStringArray = OS.get_cmdline_user_args()
	var app_args := PackedStringArray(["-game-level", "snow field", "-game-seed", "42"])
	var app: Dictionary = ARGS._parse_environment('["-game-level","snow field","-game-seed","42"]')
	check(String(app["error"]).is_empty() and app["args"] == app_args, "app flags pass through unchanged")
	var repeated: Dictionary = ARGS._parse_environment('["--custom","","--custom","comma,value"]')
	check(String(repeated["error"]).is_empty() and repeated["args"] == PackedStringArray(["--custom", "", "--custom", "comma,value"]), "transport does not validate app values or repeated flags")
	for payload: String in ["null", "{}", "[1]", '["-valid",false]', '["unterminated"']:
		var malformed: Dictionary = ARGS._parse_environment(payload)
		check(not String(malformed["error"]).is_empty() and malformed["args"].is_empty(), "malformed transport fails without partial arguments: " + payload)
	var empty: Dictionary = ARGS._parse_environment("[]")
	check(String(empty["error"]).is_empty() and empty["args"].is_empty(), "empty argument array is valid")
	if OS.get_name() != "iOS":
		OS.set_environment("POCKETAR_ARGS", '["-ios-only"]')
		var launch: Dictionary = ARGS.read()
		check(String(launch["error"]).is_empty() and launch["args"] == user_args, "desktop reads user arguments and ignores iOS environment")
		OS.unset_environment("POCKETAR_ARGS")
	print("POCKETAR ARGS " + ("PASS" if failures == 0 else "FAIL"))
	quit(1 if failures > 0 else 0)


func check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)
