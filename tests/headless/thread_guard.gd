extends SceneTree


func _initialize() -> void:
	var interface := SyntheticARInterface.new()
	XRServer.add_interface(interface)
	var initialized: bool = interface.initialize()
	var expect_rejected: bool = OS.get_cmdline_user_args().has("reject")
	var passed: bool = initialized != expect_rejected
	if initialized:
		interface.uninitialize()
	XRServer.remove_interface(interface)
	print("THREAD GUARD RESULT " + ("PASS" if passed else "FAIL"))
	quit(0 if passed else 1)
