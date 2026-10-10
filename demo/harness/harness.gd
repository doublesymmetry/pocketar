extends Node

const STEPS: Array[String] = ["loaded", "xr-mono-2d", "projection", "color-invariance", "transparency", "frame-consistency", "resize"]
const BARS: Array[Color] = [Color.RED, Color.BLUE, Color.GREEN, Color.YELLOW, Color.CYAN, Color.MAGENTA, Color.WHITE, Color.BLACK]
const DEFAULT_K := Vector4(520.0, 510.0, 301.0, 227.0)
const IMAGE_SIZE := Vector2i(640, 480)

@onready var view: ARView = $ARView
var output_dir: String = "user://harness"
var selected_steps: PackedStringArray = PackedStringArray(STEPS)
var failed: bool = false
var result_printed: bool = false
var detail: String = ""
var button: Button
var button_presses: int = 0
var game_presses: int = 0
var capture_index: int = 0
var timeout_seconds: float = 240.0


func _ready() -> void:
	var args: PackedStringArray = OS.get_cmdline_user_args()
	for index: int in range(args.size()):
		if args[index] == "-harness-out" and index + 1 < args.size():
			output_dir = args[index + 1]
		elif args[index] == "-harness-steps" and index + 1 < args.size():
			selected_steps = args[index + 1].split(",", false)
		elif args[index] == "-harness-timeout" and index + 1 < args.size():
			timeout_seconds = maxf(1.0, args[index + 1].to_float())
	var error: Error = DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(output_dir))
	if error != OK:
		_step("output", false, "cannot create %s: %s" % [output_dir, error_string(error)])
		_finish()
		return
	get_tree().create_timer(timeout_seconds).timeout.connect(_timeout)
	if DisplayServer.get_name() == "headless":
		_step("rendering", false, "headless has no rendering; run tools/harness.sh in the GUI session")
		_finish()
		return
	for name: String in selected_steps:
		if not STEPS.has(name):
			_step("arguments", false, "unknown step: " + name)
			_finish()
			return
	if selected_steps.is_empty():
		_step("arguments", false, "no checks selected")
		_finish()
		return
	Engine.max_fps = 60
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	view.virtual_scene.msaa_3d = Viewport.MSAA_4X
	_make_button()
	view.screen_pressed.connect(_on_game_pressed)
	_run.call_deferred()


func _run() -> void:
	await _settle()
	for name: String in selected_steps:
		detail = ""
		var ok: bool = false
		match name:
			"loaded": ok = _loaded()
			"xr-mono-2d": ok = await _hud_check()
			"projection": ok = await _projection()
			"color-invariance": ok = await _color_invariance()
			"transparency": ok = await _transparency()
			"frame-consistency": ok = await _frame_consistency()
			"resize": ok = await _resize()
		_step(name, ok, detail)
	_finish()


func _step(name: String, ok: bool, message: String) -> void:
	failed = failed or not ok
	print("HARNESS STEP %s %s %s" % [name, "OK" if ok else "FAIL", message])


func _finish() -> void:
	if result_printed:
		return
	result_printed = true
	print("HARNESS RESULT " + ("FAIL" if failed else "PASS"))
	get_tree().quit(1 if failed else 0)


func _timeout() -> void:
	if not result_printed:
		_step("timeout", false, "check did not finish within %.0f seconds" % timeout_seconds)
		_finish()


func _settle(count: int = 4) -> void:
	for _frame: int in range(count):
		await get_tree().process_frame
		await RenderingServer.frame_post_draw


func _capture(name: String, save: bool = true) -> Image:
	await RenderingServer.frame_post_draw
	var image: Image = get_viewport().get_texture().get_image()
	if image == null or image.is_empty():
		_step("capture", false, "empty rendered image: " + name)
		return Image.create(1, 1, false, Image.FORMAT_RGBA8)
	if save:
		_save(image, name)
	return image


func _save(image: Image, name: String) -> void:
	capture_index += 1
	var path: String = output_dir.path_join("%03d-%s.png" % [capture_index, name])
	var error: Error = image.save_png(path)
	if error != OK:
		_step("capture", false, "%s: %s" % [path, error_string(error)])


func _loaded() -> bool:
	detail = "%s backend=%s renderer=%s driver=%s" % [HandheldAR.get_version(), HandheldAR.get_backend(), RenderingServer.get_current_rendering_method(), RenderingServer.get_current_rendering_driver_name()]
	return HandheldAR.get_version() == "0.0.1-dev" and HandheldAR.get_backend() == "synthetic" and RenderingServer.get_current_rendering_method() == "mobile"


func _make_button() -> void:
	button = Button.new()
	button.position = Vector2(18, 34)
	button.size = Vector2(130, 52)
	button.text = "HUD"
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.15, 0.75, 0.35, 1.0)
	for state: String in ["normal", "hover", "pressed", "focus"]:
		button.add_theme_stylebox_override(state, style)
	button.pressed.connect(_on_button_pressed)
	view.hud.add_child(button)


func _on_button_pressed() -> void:
	button_presses += 1


func _on_game_pressed(_point: Vector2, _ray: Dictionary) -> void:
	game_presses += 1


func _reset_scene() -> void:
	for child: Node in view.scene_root.get_children():
		child.free()
	view.interface.set_frame_id_pose(false)
	view.interface.set_pose(Transform3D.IDENTITY)
	view.interface.set_camera_rate(30)
	view.environment.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	view.environment.tonemap_exposure = 1.0
	view.environment.fog_enabled = false
	view.environment.glow_enabled = false
	view.camera.near = 0.05
	view.camera.far = 100.0


func _layout(requested: Vector2i, turns: int = 0) -> void:
	var available: Vector2 = get_viewport().get_visible_rect().size
	var scale_factor: float = minf(1.0, minf(available.x / requested.x, available.y / requested.y))
	view.set_anchors_and_offsets_preset(Control.PRESET_TOP_LEFT)
	view.position = Vector2.ZERO
	view.size = Vector2(Vector2i(Vector2(requested) * scale_factor))
	view.orientation_quarter_turns = turns
	view.interface.set_display_geometry(Vector2i(view.size), turns, view.camera.near, view.camera.far)


func _material(color: Color, alpha: float = 1.0) -> ShaderMaterial:
	var shader := Shader.new()
	var alpha_code: String = "ALPHA = %.8f;" % alpha if alpha < 1.0 else ""
	shader.code = "shader_type spatial; render_mode cull_back, fog_disabled; void fragment() { ALBEDO = vec3(0.0); EMISSION = vec3(%.8f, %.8f, %.8f); %s }" % [color.r, color.g, color.b, alpha_code]
	var material := ShaderMaterial.new()
	material.shader = shader
	return material


func _marker(point: Vector3, size: float = 0.027, color: Color = Color.WHITE) -> MeshInstance3D:
	var marker := MeshInstance3D.new()
	var mesh := QuadMesh.new()
	mesh.size = Vector2.ONE * size
	marker.mesh = mesh
	marker.material_override = _material(color)
	marker.position = point
	view.scene_root.add_child(marker)
	return marker


func _hud_check() -> bool:
	_reset_scene()
	button.show()
	_marker(Vector3(0, 0, -2), 0.12, Color.WHITE)
	await _settle()
	var image: Image = await _capture("xr-mono-2d")
	var sample: Color = image.get_pixelv(Vector2i(button.position + Vector2(8, 8)))
	var color_ok: bool = _color_error(sample, Color(0.15, 0.75, 0.35)) <= 2.0 / 255.0
	var scene_image: Image = view.virtual_scene.get_texture().get_image()
	var visible_pixels: int = _count_alpha(scene_image)
	var old_button: int = button_presses
	var old_game: int = game_presses
	var click := InputEventMouseButton.new()
	click.position = button.position + button.size * 0.5
	click.global_position = click.position
	click.button_index = MOUSE_BUTTON_LEFT
	click.pressed = true
	Input.parse_input_event(click)
	await get_tree().process_frame
	click = click.duplicate() as InputEventMouseButton
	click.pressed = false
	Input.parse_input_event(click)
	await get_tree().process_frame
	var input_ok: bool = button_presses == old_button + 1 and game_presses == old_game
	var primary: bool = XRServer.primary_interface == view.interface and view.interface.get_view_count() == 1
	var packet: ARFramePacket = view.interface.get_presented_packet()
	var head: XRPositionalTracker = XRServer.get_tracker("head") as XRPositionalTracker
	var tracker_ok: bool = head != null and head.has_pose("default") and head.get_pose("default").transform.is_equal_approx(packet.camera_transform)
	var centre_h: Vector4 = packet.projection * Vector4(0, 0, -2, 1)
	var centre := Vector2((centre_h.x / centre_h.w + 1.0) * 0.5, (1.0 - centre_h.y / centre_h.w) * 0.5) * view.size
	var composited_3d: bool = _color_error(image.get_pixelv(Vector2i(centre)), Color.WHITE) <= 2.0 / 255.0
	composited_3d = composited_3d and visible_pixels < scene_image.get_width() * scene_image.get_height() / 4
	var point := Vector2(view.size.x - 18, view.size.y - 18)
	click = InputEventMouseButton.new()
	click.position = point
	click.global_position = point
	click.button_index = MOUSE_BUTTON_LEFT
	click.pressed = true
	Input.parse_input_event(click)
	click = click.duplicate() as InputEventMouseButton
	click.pressed = false
	Input.parse_input_event(click)
	await get_tree().process_frame
	input_ok = input_ok and game_presses == old_game + 1
	detail = "primary=%s mono=%s 3d_pixels=%d composite=%s HUD=%s input=%s head_pose=%s" % [primary, view.virtual_scene.use_xr, visible_pixels, composited_3d, color_ok, input_ok, tracker_ok]
	return primary and view.virtual_scene.use_xr and visible_pixels > 16 and composited_3d and color_ok and input_ok and tracker_ok


func _count_alpha(image: Image) -> int:
	var count: int = 0
	for y: int in range(image.get_height()):
		for x: int in range(image.get_width()):
			if image.get_pixel(x, y).a > 0.05:
				count += 1
	return count


func _predict(point: Vector3, k: Vector4, image_size: Vector2i, turns: int) -> Vector2:
	var image_point := Vector2(k.x * point.x / -point.z + k.z, k.w - k.y * point.y / -point.z)
	var extent := Vector2(image_size)
	match turns:
		1:
			image_point = Vector2(extent.y - image_point.y, image_point.x)
			extent = Vector2(extent.y, extent.x)
		2: image_point = extent - image_point
		3:
			image_point = Vector2(image_point.y, extent.x - image_point.x)
			extent = Vector2(extent.y, extent.x)
	var scale_factor: float = maxf(view.size.x / extent.x, view.size.y / extent.y)
	return (image_point - extent * 0.5) * scale_factor + view.size * 0.5


func _centroid(image: Image, predicted: Vector2, radius: int = 10) -> Vector3:
	var total: float = 0.0
	var weighted := Vector2.ZERO
	for y: int in range(maxi(0, floori(predicted.y) - radius), mini(image.get_height(), ceili(predicted.y) + radius + 1)):
		for x: int in range(maxi(0, floori(predicted.x) - radius), mini(image.get_width(), ceili(predicted.x) + radius + 1)):
			var weight: float = image.get_pixel(x, y).a
			total += weight
			weighted += Vector2(x + 0.5, y + 0.5) * weight
	if total < 1.0:
		return Vector3(-10000, -10000, total)
	return Vector3(weighted.x / total, weighted.y / total, total)


func _projection() -> bool:
	var worst: float = 0.0
	for requested: Vector2i in [Vector2i(960, 720), Vector2i(720, 960), Vector2i(1000, 600)]:
		for turns: int in [0, 1, 2, 3]:
			for k: Vector4 in [DEFAULT_K, Vector4(610, 477, 351, 203)]:
				var error: float = await _projection_case(requested, turns, k)
				worst = maxf(worst, error)
				if error >= 0.5:
					return false
	detail = "24 cases, 25 markers each; worst centroid error %.6f rendered pixels; rays < 0.0001 m" % worst
	return true


func _projection_case(requested: Vector2i, turns: int, k: Vector4) -> float:
	_reset_scene()
	button.hide()
	view.camera.near = 0.17
	view.camera.far = 23.0
	_layout(requested, turns)
	view.interface.set_intrinsics(k.x, k.y, k.z, k.w, IMAGE_SIZE)
	var points: Array[Vector3] = []
	for row: int in range(5):
		for col: int in range(5):
			var point := Vector3((col - 2) * 0.21, (row - 2) * 0.16, -2.0)
			points.append(point)
			_marker(point)
	await _settle()
	var capture_name: String = "projection-%dx%d-r%d-fx%d" % [int(view.size.x), int(view.size.y), turns, int(k.x)]
	await _capture(capture_name)
	var scene_image: Image = view.virtual_scene.get_texture().get_image()
	_save(scene_image, capture_name + "-scene-alpha")
	var worst: float = 0.0
	for point: Vector3 in points:
		var predicted: Vector2 = _predict(point, k, IMAGE_SIZE, turns)
		var centroid: Vector3 = _centroid(scene_image, predicted)
		var error: float = Vector2(centroid.x, centroid.y).distance_to(predicted)
		worst = maxf(worst, error)
		var ray: Dictionary = view.screen_to_ray(predicted)
		if ray.is_empty():
			detail = capture_name + " returned no ray"
			return INF
		var origin: Vector3 = ray["origin"]
		var direction: Vector3 = ray["direction"]
		var distance: float = (point - origin).cross(direction).length()
		if error >= 0.5 or distance >= 0.0001:
			detail = "%s point=%s predicted=%s centroid=%s error=%.6f ray_distance=%.8f" % [capture_name, point, predicted, centroid, error, distance]
			return INF
	return worst


func _color_error(a: Color, b: Color) -> float:
	return maxf(absf(a.r - b.r), maxf(absf(a.g - b.g), absf(a.b - b.b)))


func _color_invariance() -> bool:
	_reset_scene()
	button.hide()
	_layout(Vector2i(640, 480))
	view.interface.set_intrinsics(DEFAULT_K.x, DEFAULT_K.y, DEFAULT_K.z, DEFAULT_K.w, IMAGE_SIZE)
	await _settle()
	var worst: float = 0.0
	var baseline: Array[Color] = []
	for tone: int in [Environment.TONE_MAPPER_LINEAR, Environment.TONE_MAPPER_FILMIC, Environment.TONE_MAPPER_ACES, Environment.TONE_MAPPER_AGX]:
		for exposure: float in [0.5, 2.0]:
			for effect: String in ["none", "fog", "glow"]:
				view.environment.tonemap_mode = tone
				view.environment.tonemap_exposure = exposure
				view.environment.fog_enabled = effect == "fog"
				view.environment.fog_density = 0.2
				view.environment.glow_enabled = effect == "glow"
				await _settle(2)
				var image: Image = await _capture("color-t%d-e%.1f-%s" % [tone, exposure, effect])
				for bar: int in range(8):
					var pixel := Vector2i(Vector2((bar + 0.5) / 8.0, 100.5 / 480.0) * view.size)
					var actual: Color = image.get_pixelv(pixel)
					var error: float = _color_error(actual, BARS[bar])
					if baseline.size() < 8:
						baseline.append(actual)
					else:
						error = maxf(error, _color_error(actual, baseline[bar]))
					worst = maxf(worst, error)
					if error > 1.001 / 255.0:
						detail = "tone=%d exposure=%.1f effect=%s bar=%d rgb=%s expected=%s error=%.3f/255" % [tone, exposure, effect, bar, actual, BARS[bar], error * 255]
						return false
	detail = "24 environment variants; red/blue chroma and all 8 bars <= %.3f/255" % (worst * 255)
	return true


func _cube(point: Vector3, color: Color, alpha: float) -> MeshInstance3D:
	var cube := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = Vector3.ONE * 0.30
	cube.mesh = mesh
	cube.material_override = _material(color.srgb_to_linear(), alpha)
	cube.position = point
	view.scene_root.add_child(cube)
	return cube


func _transparency() -> bool:
	_reset_scene()
	button.hide()
	_layout(Vector2i(640, 480))
	view.interface.set_intrinsics(DEFAULT_K.x, DEFAULT_K.y, DEFAULT_K.z, DEFAULT_K.w, IMAGE_SIZE)
	await _settle()
	var background_image: Image = await _capture("transparency-background")
	var opaque_color := Color(0.8, 0.2, 0.1)
	var half_color := Color(0.15, 0.8, 0.9)
	var opaque: MeshInstance3D = _cube(Vector3(-0.5, 0, -2), opaque_color, 1.0)
	_cube(Vector3(0.5, 0, -2), half_color, 0.5)
	await _settle()
	var image: Image = await _capture("transparency")
	var scene_image: Image = view.virtual_scene.get_texture().get_image()
	for index: int in range(2):
		var point := Vector3(-0.5 if index == 0 else 0.5, 0, -1.85)
		var pixel := Vector2i(_predict(point, DEFAULT_K, IMAGE_SIZE, 0))
		var alpha: float = 1.0 if index == 0 else 0.5
		var color: Color = opaque_color if index == 0 else half_color
		var expected: Color = background_image.get_pixelv(pixel).lerp(color, alpha)
		if _color_error(image.get_pixelv(pixel), expected) > 3.0 / 255.0 or absf(scene_image.get_pixelv(pixel).a - alpha) > 0.02:
			detail = "cube %d at %s output=%s expected=%s viewport=%s" % [index, pixel, image.get_pixelv(pixel), expected, scene_image.get_pixelv(pixel)]
			return false
	opaque.rotation.z = 0.21
	await _settle()
	image = await _capture("transparency-msaa")
	scene_image = view.virtual_scene.get_texture().get_image()
	var edge_samples: int = 0
	var worst: float = 0.0
	var centre: Vector2 = _predict(opaque.position, DEFAULT_K, IMAGE_SIZE, 0)
	for y: int in range(maxi(0, int(centre.y) - 70), mini(int(view.size.y), int(centre.y) + 70)):
		for x: int in range(maxi(0, int(centre.x) - 70), mini(int(view.size.x), int(centre.x) + 70)):
			var alpha: float = scene_image.get_pixel(x, y).a
			if alpha > 0.05 and alpha < 0.95:
				edge_samples += 1
				var expected: Color = background_image.get_pixel(x, y).lerp(opaque_color, alpha)
				worst = maxf(worst, _color_error(image.get_pixel(x, y), expected))
	if edge_samples < 8 or worst > 4.0 / 255.0:
		detail = "MSAA edge samples=%d worst halo error=%.3f/255" % [edge_samples, worst * 255]
		return false
	view.environment.glow_enabled = true
	view.environment.glow_intensity = 2.0
	opaque.material_override = _material(Color(8, 8, 8))
	await _settle()
	image = await _capture("glow-over-camera")
	scene_image = view.virtual_scene.get_texture().get_image()
	var alpha_zero_light: int = 0
	for y: int in range(scene_image.get_height()):
		for x: int in range(scene_image.get_width()):
			var color: Color = scene_image.get_pixel(x, y)
			if color.a < 0.001 and maxf(color.r, maxf(color.g, color.b)) > 0.01:
				alpha_zero_light += 1
	print("HARNESS STEP glow-over-camera KNOWN alpha-zero glow is discarded by the composite; %d lit alpha-zero pixels" % alpha_zero_light)
	detail = "opaque and 50%% cubes <= 3/255; %d MSAA samples <= %.3f/255" % [edge_samples, worst * 255]
	return true


func _frame_consistency() -> bool:
	_reset_scene()
	button.hide()
	_layout(Vector2i(640, 480))
	view.interface.set_intrinsics(DEFAULT_K.x, DEFAULT_K.y, DEFAULT_K.z, DEFAULT_K.w, IMAGE_SIZE)
	view.interface.set_camera_rate(30)
	await _settle()
	var background_image: Image = await _capture("frame-consistency-background")
	view.interface.set_frame_id_pose(true)
	_marker(Vector3(0, 0, -2), 0.032, Color(0.5, 0.25, 0.75).srgb_to_linear())
	await _settle()
	var images: Array[Image] = []
	var previous_id: int = -1
	var previous_packet: ARFramePacket
	var repeats: int = 0
	var advances: int = 0
	var started: int = Time.get_ticks_usec()
	var worst_error: float = 0.0
	for frame: int in range(120):
		var image: Image = await _capture("frame-%03d" % frame, false)
		images.append(image)
		var packet: ARFramePacket = view.interface.get_presented_packet()
		var decoded: int = 0
		for bit: int in range(16):
			var pixel := Vector2i(Vector2((bit + 0.5) / 16.0, 12.0 / 480.0) * view.size)
			var value: Color = image.get_pixelv(pixel)
			if minf(value.r, minf(value.g, value.b)) > 0.8:
				decoded |= 1 << bit
			elif maxf(value.r, maxf(value.g, value.b)) > 0.2:
				detail = "ambiguous stripe at frame=%d bit=%d rgb=%s" % [frame, bit, value]
				_save(image, "frame-consistency-failure")
				return false
		var marker_position := Vector2((decoded % 16 + 0.5) * view.size.x / 16.0, view.size.y * 0.5)
		var centroid: Vector3 = _difference_centroid(image, background_image, marker_position)
		var pixel_error: float = Vector2(centroid.x, centroid.y).distance_to(marker_position)
		worst_error = maxf(worst_error, pixel_error)
		var implied: int = floori(centroid.x / view.size.x * 16.0)
		if decoded != (packet.frame_id & 65535) or implied != decoded % 16 or pixel_error >= 0.5:
			detail = "render=%d stripe=%d packet=%d marker_block=%d error=%.6f" % [frame, decoded, packet.frame_id, implied, pixel_error]
			_save(image, "frame-consistency-failure")
			return false
		if decoded == previous_id:
			repeats += 1
			if packet != previous_packet:
				detail = "repeated id replaced the immutable packet"
				return false
		elif previous_id >= 0:
			advances += 1
		previous_id = decoded
		previous_packet = packet
	var elapsed: float = (Time.get_ticks_usec() - started) / 1000000.0
	for frame: int in range(images.size()):
		_save(images[frame], "frame-%03d" % frame)
	view.interface.set_frame_id_pose(false)
	detail = "120 renders, camera=30 target_render=60 observed=%.1f fps repeated=%d advanced=%d marker_error=%.6f px" % [120.0 / elapsed, repeats, advances, worst_error]
	return repeats > 0 and advances >= 30


func _difference_centroid(image: Image, background_image: Image, predicted: Vector2) -> Vector3:
	var total: float = 0.0
	var weighted := Vector2.ZERO
	var marker_color := Color(0.5, 0.25, 0.75)
	for y: int in range(maxi(0, floori(predicted.y) - 12), mini(int(view.size.y), ceili(predicted.y) + 13)):
		for x: int in range(maxi(0, floori(predicted.x) - 12), mini(int(view.size.x), ceili(predicted.x) + 13)):
			var background_color: Color = background_image.get_pixel(x, y)
			var contrast: float = _color_error(marker_color, background_color)
			var weight: float = clampf(_color_error(image.get_pixel(x, y), background_color) / contrast, 0.0, 1.0)
			total += weight
			weighted += Vector2(x + 0.5, y + 0.5) * weight
	if total < 1.0:
		return Vector3(-10000, -10000, total)
	return Vector3(weighted.x / total, weighted.y / total, total)


func _resize() -> bool:
	var mobile: bool = OS.has_feature("mobile")
	var initial_size: Vector2i = DisplayServer.window_get_size()
	for cycle: int in range(2):
		for portrait: bool in [true, false]:
			var requested := Vector2i(720, 960) if portrait else Vector2i(960, 720)
			if mobile:
				DisplayServer.screen_set_orientation(DisplayServer.SCREEN_PORTRAIT if portrait else DisplayServer.SCREEN_LANDSCAPE)
			else:
				DisplayServer.window_set_size(requested)
			await get_tree().create_timer(0.35).timeout
			await _settle()
			var actual: Vector2i = DisplayServer.window_get_size()
			if (actual.y > actual.x) != portrait:
				detail = "orientation change failed: requested=%s actual=%s cycle=%d" % [requested, actual, cycle]
				return false
			var error: float = await _projection_case(actual, 1 if portrait else 0, DEFAULT_K)
			if error >= 0.5:
				return false
			if not await _hud_check():
				return false
	if not mobile:
		DisplayServer.window_set_size(initial_size)
	detail = "portrait and landscape twice; projection and HUD passed after each change"
	return true
