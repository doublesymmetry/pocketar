extends SceneTree

var failures: int = 0


func _initialize() -> void:
	_run.call_deferred()


func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("ARVIEW FAIL " + message)


func _run() -> void:
	for mode: ARView.Composition in [ARView.Composition.IN_SCENE, ARView.Composition.ISOLATED_CAMERA]:
		_check_mode(mode)
	print("ARVIEW RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)


func _check_mode(mode: ARView.Composition) -> void:
	var scene: PackedScene = load("res://addons/pocketar/ar_view.tscn") as PackedScene
	var view: ARView = scene.instantiate() as ARView
	_check(view.get("composition") == 0, "in-scene composition is the default")
	view.composition = mode
	root.add_child(view)
	var in_scene: bool = mode == ARView.Composition.IN_SCENE
	_check(view.virtual_scene.transparent_bg and view.virtual_scene.use_hdr_2d != in_scene, "composition viewport format")
	_check((view.composite.material == null) == in_scene, "composition uses the matching canvas transfer")
	if in_scene:
		_check(view.camera_quad != null and not view.background.visible, "in-scene camera replaces the canvas")
		_check(view.camera_quad.material_override == view.camera_material, "active spatial material")
		_check(view.camera_material.render_priority == Material.RENDER_PRIORITY_MAX, "camera follows ordinary opaques")
		_check(view.camera_quad.ignore_occlusion_culling and view.camera_quad.cast_shadow == GeometryInstance3D.SHADOW_CASTING_SETTING_OFF, "camera has no occlusion culling or shadow casting")
	else:
		_check(view.camera_quad == null and view.camera_material == view.background.material, "active canvas material")
	view.composition = ARView.Composition.ISOLATED_CAMERA if in_scene else ARView.Composition.IN_SCENE
	_check(view.composition == mode, "composition change after ready is rejected")
	_check(view.world_3d != null and view.world_3d == view.scene_root.get_world_3d(), "exposes active scene world")
	_check(view.world_3d != root.find_world_3d(), "AR scene owns an isolated world")
	var game_environment := Environment.new()
	game_environment.background_mode = Environment.BG_COLOR
	game_environment.background_color = Color.MAGENTA
	game_environment.tonemap_exposure = 2.0
	view.environment = game_environment
	view.call("_apply_packet")
	var rendered_environment: Environment = view.world_environment.environment
	_check(rendered_environment != game_environment, "private render environment")
	_check(rendered_environment.background_mode == Environment.BG_CLEAR_COLOR and rendered_environment.background_color == Color(0, 0, 0, 0), "transparent black clear")
	_check(game_environment.background_mode == Environment.BG_COLOR and game_environment.background_color == Color.MAGENTA, "source background preserved")
	_check(rendered_environment.tonemap_exposure == 2.0, "other environment settings copied")
	game_environment.tonemap_exposure = 0.5
	game_environment.fog_aerial_perspective = 0.3
	view.call("_apply_packet")
	_check(rendered_environment.tonemap_exposure == 0.5 and is_equal_approx(rendered_environment.fog_aerial_perspective, 0.3), "live environment edits survive repeated packets")
	_check(view.interface is SyntheticARInterface, "default backend remains synthetic")
	_check(view.applied_packet != null and _camera_visible(view) and view.composite.visible, "first packet enables composition")
	view.interface.call("mark_presented")
	var ray: Dictionary = view.screen_to_ray(view.size * 0.5)
	_check(not ray.is_empty(), "presented packet supplies a ray")
	_check(view.raycast(view.size * 0.5).is_empty(), "no planes means no placement hits")
	view.interface.uninitialize()
	view.call("_apply_packet")
	_check(view.applied_packet == null, "old packet released when session stops")
	_check(not _camera_visible(view) and not view.composite.visible, "old generation is hidden")
	var material: ShaderMaterial = view.camera_material
	_check(material.get_shader_parameter("y_texture") == null and material.get_shader_parameter("cbcr_texture") == null and material.get_shader_parameter("rgb_texture") == null, "old textures released")
	_check(view.screen_to_ray(view.size * 0.5).is_empty(), "stopped session cannot produce a ray")
	_check(view.interface.initialize(), "session restarts")
	view.call("_apply_packet")
	_check(view.applied_packet != null and _camera_visible(view) and view.composite.visible, "next generation enables composition")
	view.camera.near = 0.17
	view.camera.far = 23.0
	for focal: Vector2 in [Vector2(610, 477), Vector2(5000000, 4000000)]:
		view.interface.call("set_intrinsics", focal.x, focal.y, 351.0, 203.0, Vector2i(640, 480))
		view.call("_update_geometry")
		view.reset()
		if in_scene:
			var bounds: AABB = view.camera_quad.custom_aabb
			for corner: int in range(8):
				var point: Vector3 = bounds.get_endpoint(corner)
				var clip: Vector4 = view.applied_packet.projection * Vector4(point.x, point.y, point.z, 1)
				_check(clip.w > 0 and absf(clip.x) < clip.w and absf(clip.y) < clip.w and absf(clip.z) < clip.w, "camera AABB stays inside asymmetric far=23 frustum at focal=%s" % focal)
	var harness: Node = load("res://harness/harness.gd").new() as Node
	harness.set("view", view)
	var backdrop: Image = Image.create(32, 32, false, Image.FORMAT_RGBA8)
	backdrop.fill(Color.RED)
	var rendered: Image = backdrop.duplicate() as Image
	var centroid: Vector3 = harness.call("_centroid", rendered, backdrop, Vector2(16, 16), 10)
	_check(centroid.z == 0.0 and int(harness.call("_count_difference", rendered, backdrop)) == 0, "opaque camera alone cannot pass geometry measurements")
	var marker_color := Color(0.5, 0.25, 0.75)
	if not in_scene:
		marker_color = marker_color.srgb_to_linear()
	rendered.fill_rect(Rect2i(14, 14, 4, 4), marker_color)
	centroid = harness.call("_centroid", rendered, backdrop, Vector2(16, 16), 10)
	_check(Vector2(centroid.x, centroid.y).distance_to(Vector2(16, 16)) < 0.01 and int(harness.call("_count_difference", rendered, backdrop)) == 16, "geometry measurements detect the marker against an opaque camera")
	harness.free()
	var generation: int = view.session_generation
	view.reset()
	_check(view.applied_packet.session_generation > generation and view.screen_to_ray(view.size * 0.5).is_empty(), "synthetic reset replaces the image and invalidates presentation")
	var native_interface: XRInterfaceExtension = view.interface
	var source := PacketSource.new()
	source.view = view
	view.interface = source
	view.call("_update_geometry")
	view.call("_update_geometry")
	_check(source.geometry_updates == 1, "unchanged display geometry is sent once to a new interface")
	view.camera.near = 0.2
	view.call("_update_geometry")
	view.camera.far = 31.0
	view.call("_update_geometry")
	view.orientation_quarter_turns = 2
	view.call("_update_geometry")
	view.set_anchors_preset(Control.PRESET_TOP_LEFT)
	view.size += Vector2(1, 1)
	view.call("_update_geometry")
	_check(source.geometry_updates == 5, "clip range, orientation and resize each update display geometry")
	view.call("_apply_packet")
	source.packet = ARFramePacket.new()
	source.checked_after_applied = false
	view.call("_apply_packet")
	_check(view.applied_packet == source.packet and not _camera_visible(view), "first textureless packet keeps the camera hidden")
	_check(source.applied == source.packet and source.checked_after_applied, "packet retained and marked before the active material check")
	source.packet = null
	view.call("_apply_packet")
	_check(view.applied_packet == null and not _camera_visible(view) and not view.composite.visible, "interruption clears a textureless packet")
	view.interface = native_interface
	view.free()


func _camera_visible(view: ARView) -> bool:
	return view.camera_quad.visible if view.composition == ARView.Composition.IN_SCENE else view.background.visible


class PacketSource extends XRInterfaceExtension:
	var view: ARView
	var packet: ARFramePacket
	var applied: ARFramePacket
	var checked_after_applied: bool = false
	var geometry_updates: int = 0


	func set_display_geometry(_size: Vector2i, _turns: int, _near: float, _far: float) -> void:
		geometry_updates += 1


	func _is_initialized() -> bool:
		return true


	func get_current_packet() -> ARFramePacket:
		return packet


	func mark_applied(value: ARFramePacket) -> void:
		applied = value


	func debug_check_camera_material(material: RID) -> void:
		checked_after_applied = applied == packet and view.applied_packet == packet and material == view.camera_material.get_rid()
