extends SceneTree

const ARGS: Script = preload("res://ar/harness_args.gd")

var failures: int = 0


func _init() -> void:
	var valid: Dictionary = ARGS.parse_environment('["--log-file","user://harness-test.log","-harness-ar","-harness-duration","60","-harness-out","user://captures-test","-harness-steps","camera-image,composite"]')
	check(String(valid["error"]).is_empty() and valid["args"].size() == 9, "valid launch preserves argv")
	check(String(ARGS.validate(PackedStringArray(["--harness", "-harness-run-id", "round3", "-harness-ar", "-harness-steps", "packet-retention,tracking,lifecycle"]))["error"]).is_empty(), "run marker and retention step accepted")
	check(String(ARGS.validate(PackedStringArray(["-harness-cleanup", "round3", "-harness-out", "user://captures-round3"]))["error"]).is_empty(), "run-owned cleanup accepted")
	var invalid: Array[String] = [
		'null',
		'{}',
		'[1]',
		'["-harness-duration"]',
		'["-harness-duration","NaN"]',
		'["-harness-duration","0"]',
		'["-harness-duration","-1"]',
		'["-harness-duration","abc"]',
		'["-harness-out","user://a/b"]',
		'["-harness-out","user://../private"]',
		'["--log-file","/tmp/test.log"]',
		'["-harness-steps","unknown"]',
		'["-harness-steps","tracking,tracking"]',
		'["-harness-steps",""]',
		'["-harness-steps","tracking,"]',
		'["-harness-record","room.mp4"]',
		'["-harness-ar","-harness-ar"]',
		'["-harness-run-id","../private"]',
		'["-harness-cleanup","round3","-harness-out","user://captures-other"]',
	]
	for payload: String in invalid:
		check(not String(ARGS.parse_environment(payload)["error"]).is_empty(), "reject " + payload)
	print("IOS HARNESS ARGS " + ("PASS" if failures == 0 else "FAIL"))
	quit(1 if failures > 0 else 0)


func check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)
