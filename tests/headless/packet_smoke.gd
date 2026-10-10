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
	_check(HandheldAR.get_version() == "0.0.1-dev", "version")
	_check(HandheldAR.get_backend() == "synthetic", "backend")
	interface = SyntheticARInterface.new()
	interface.set_intrinsics(520, 510, 301, 227, Vector2i(640, 480))
	interface.set_display_geometry(Vector2i(720, 960), 1, 0.17, 23.0)
	interface.set_camera_rate(1.0)
	XRServer.add_interface(interface)
	_check(interface.initialize(), "initialization")
	var first: ARFramePacket = interface.get_current_packet()
	_check(first != null and first.tracking_valid, "first packet")
	_check(interface.get_presented_packet() == null, "nothing presented before draw")
	_check(XRServer.primary_interface == interface and interface.get_view_count() == 1, "primary mono")
	_check(first.color_encoding == ARFramePacket.BT601_FULL, "encoding")
	_check(first.image_size == Vector2i(640, 480), "image size")
	_check(first.viewport_size == Vector2(720, 960), "viewport size")
	var projection: Projection = interface.get_projection_for_view(0, 720.0 / 960.0, 0.17, 23.0)
	for column: int in range(4):
		_check(projection[column].is_equal_approx(first.projection[column]), "XR and packet projection")
	interface.mark_presented()
	var next_pose := Transform3D(Basis.IDENTITY, Vector3(0.3, 0.1, 0))
	interface.set_pose(next_pose)
	for _frame: int in range(3):
		await process_frame
	_check(interface.get_current_packet() == first, "camera rate repeats the whole packet")
	_check(first.camera_transform == Transform3D.IDENTITY, "pending pose cannot alter old packet")
	interface.set_camera_rate(240.0)
	for _frame: int in range(8):
		await process_frame
	var current: ARFramePacket = interface.get_current_packet()
	_check(current.frame_id > first.frame_id + 3, "advanced beyond CPU ring length")
	_check(current.camera_transform == next_pose, "pose acquired with next image")
	_check(current.timestamp_usec > first.timestamp_usec, "monotonic timestamps")
	_check(current.y_texture != first.y_texture and current.cbcr_texture != first.cbcr_texture, "old packet keeps its textures")
	_check(first.camera_transform == Transform3D.IDENTITY, "old transform survives ring reuse")
	_check(interface.get_presented_packet() == first, "acquisition does not advance presented packet")
	var ray: Dictionary = HandheldAR.screen_to_ray(first, Vector2(720, 960), Vector2(360, 480))
	_check(ray["origin"] == Vector3.ZERO and ray["frame_id"] == first.frame_id, "ray uses specified snapshot")
	interface.mark_presented()
	_check(interface.get_presented_packet() == current, "explicit presentation")
	var generation: int = current.session_generation
	interface.uninitialize()
	_check(interface.initialize(), "reinitialization")
	_check(interface.get_current_packet().session_generation == generation + 1, "new session generation")
	interface.uninitialize()
	XRServer.remove_interface(interface)
	interface = null
	print("PACKET RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
