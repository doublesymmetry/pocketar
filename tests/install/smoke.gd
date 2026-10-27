extends SceneTree


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var scene: Node = load(ProjectSettings.get_setting("application/run/main_scene")).instantiate()
	root.add_child(scene)
	var ar: ARView = scene as ARView
	if ar == null:
		(scene.get_node("Panel/Controls/Buttons/Start") as Button).pressed.emit()
		ar = scene.get("ar") as ARView
	for frame: int in range(20):
		await RenderingServer.frame_post_draw
	if ar == null or ar.interface == null or not ar.interface.is_initialized():
		push_error("Installed ARView did not initialize")
		quit(1)
		return
	var packet: ARFramePacket = ar.interface.call("get_presented_packet") as ARFramePacket
	var point: Vector2 = ar.size * 0.5
	var config := ConfigFile.new()
	if config.load("res://addons/pocketar/plugin.cfg") != OK or config.get_value("plugin", "version") != PocketAR.get_version():
		push_error("Installed native and plugin versions differ")
		quit(1)
		return
	if packet == null or not packet.tracking_valid or ar.screen_to_ray(point).is_empty():
		push_error("Installed ARView did not present a tracked frame")
		quit(1)
		return
	if scene != ar:
		scene.call("_place", point, ar.screen_to_ray(point))
		if (scene.get("placements") as Node3D).get_child_count() != 1:
			push_error("Installed sample could not place on its desktop plane")
			quit(1)
			return
	await RenderingServer.frame_post_draw
	var capture: Image = root.get_texture().get_image()
	if capture == null or capture.is_empty() or capture.get_pixel(capture.get_width() / 4, capture.get_height() / 2).is_equal_approx(capture.get_pixel(capture.get_width() * 3 / 4, capture.get_height() / 2)):
		push_error("Installed scene rendered an empty or uniform image")
		quit(1)
		return
	if capture.save_png(OS.get_cmdline_user_args()[0]) != OK:
		push_error("Could not save the install capture")
		quit(1)
		return
	print("INSTALL RESULT PASS backend=%s version=%s" % [PocketAR.get_backend(), PocketAR.get_version()])
	quit()
