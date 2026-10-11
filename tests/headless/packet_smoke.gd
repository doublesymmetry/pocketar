extends SceneTree

var interface: SyntheticARInterface
var failures: int = 0


func _initialize() -> void:
	Engine.max_fps = 60
	_run.call_deferred()


func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("PACKET FAIL " + message)


func _run() -> void:
	_check(PocketAR.get_version() == "0.0.1-dev", "version")
	_check(PocketAR.get_backend() == "none", "no backend before initialization")
	interface = SyntheticARInterface.new()
	interface.set_intrinsics(520, 510, 301, 227, Vector2i(640, 480))
	interface.set_display_geometry(Vector2i(720, 960), 1, 0.17, 23.0)
	interface.set_camera_rate(1.0)
	XRServer.add_interface(interface)
	_check(interface.initialize(), "initialization")
	_check(PocketAR.get_backend() == "synthetic", "active backend")
	var first: ARFramePacket = interface.get_current_packet()
	_check(first != null and first.tracking_valid, "first packet")
	_check(interface.get_presented_packet() == null, "nothing presented before draw")
	_check(XRServer.primary_interface == interface and interface.get_view_count() == 1, "primary mono")
	_check(first.color_encoding == ARFramePacket.BT601_FULL, "encoding")
	_check(first.image_size == Vector2i(640, 480), "image size")
	_check(first.viewport_size == Vector2(720, 960), "viewport size")
	_check(first.timestamp_ns == first.timestamp_usec * 1000, "synthetic timestamp units")
	var display_rotation := Transform3D(Basis(Vector3.BACK, PI * 0.5), Vector3.ZERO)
	_check(first.physical_camera_transform == Transform3D.IDENTITY, "synthetic physical pose")
	_check(first.camera_transform.is_equal_approx(display_rotation), "display-oriented pose")
	_check(first.quarter_turns == 1, "packet display rotation")
	var calibration: Dictionary = first.intrinsics
	calibration["fx"] = 1
	_check(first.intrinsics["fx"] == 520, "returned intrinsics cannot mutate packet")
	var planes: Array[Dictionary] = first.planes
	planes.append({"plane_id": 99})
	_check(first.planes.is_empty(), "returned plane array cannot mutate packet")
	var projection: Projection = interface.get_projection_for_view(0, 720.0 / 960.0, 0.17, 23.0)
	for column: int in range(4):
		_check(projection[column].is_equal_approx(first.projection[column]), "XR and packet projection")
	interface.mark_presented()
	var next_pose := Transform3D(Basis.IDENTITY, Vector3(0.3, 0.1, 0))
	interface.set_pose(next_pose)
	for _frame: int in range(3):
		await process_frame
	_check(interface.get_current_packet() == first, "camera rate repeats the whole packet")
	_check(first.camera_transform.is_equal_approx(display_rotation), "pending pose cannot alter old packet")
	interface.set_camera_rate(240.0)
	for _frame: int in range(8):
		await process_frame
	var current: ARFramePacket = interface.get_current_packet()
	_check(current.frame_id > first.frame_id + 3, "advanced beyond CPU ring length")
	_check(current.physical_camera_transform == next_pose, "physical pose acquired with next image")
	_check(current.camera_transform.is_equal_approx(next_pose * display_rotation), "display pose acquired with next image")
	_check(current.timestamp_usec > first.timestamp_usec, "monotonic timestamps")
	_check(current.y_texture != first.y_texture and current.cbcr_texture != first.cbcr_texture, "old packet keeps its textures")
	_check(first.camera_transform.is_equal_approx(display_rotation), "old transform survives ring reuse")
	_check(interface.get_presented_packet() == first, "acquisition does not advance presented packet")
	var ray: Dictionary = PocketAR.screen_to_ray(first, Vector2(720, 960), Vector2(360, 480))
	_check(ray["origin"] == Vector3.ZERO and ray["frame_id"] == first.frame_id, "ray uses specified snapshot")
	interface.mark_presented()
	_check(interface.get_presented_packet() == current, "explicit presentation")
	for turns: int in range(4):
		var viewport_size := Vector2i(720, 960) if turns % 2 else Vector2i(960, 720)
		interface.set_display_geometry(viewport_size, turns, 0.17, 23.0)
		await process_frame
		await process_frame
		var rotated: ARFramePacket = interface.get_current_packet()
		var upright: Projection = rotated.projection
		_check(upright.x.x > 0 and upright.y.y > 0 and is_finite(upright.get_aspect()), "upright packet rotation %d" % turns)
		var xr_projection: Projection = interface.get_projection_for_view(0, float(viewport_size.x) / viewport_size.y, 0.17, 23.0)
		for column: int in range(4):
			_check(xr_projection[column].is_equal_approx(upright[column]), "upright XR rotation %d" % turns)
		var rotation := Transform3D(Basis(Vector3.BACK, turns * PI * 0.5), Vector3.ZERO)
		_check(rotated.camera_transform.is_equal_approx(rotated.physical_camera_transform * rotation), "display pose rotation %d" % turns)
	var generation: int = current.session_generation
	interface.uninitialize()
	_check(interface.initialize(), "reinitialization")
	_check(interface.get_current_packet().session_generation == generation + 1, "new session generation")
	interface.uninitialize()
	XRServer.remove_interface(interface)
	interface = null
	var replacement := SyntheticARInterface.new()
	XRServer.add_interface(replacement)
	_check(replacement.initialize(), "replacement initialization")
	_check(replacement.get_current_packet().session_generation > generation, "generation survives interface replacement")
	replacement.uninitialize()
	XRServer.remove_interface(replacement)
	replacement = null
	print("PACKET RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
