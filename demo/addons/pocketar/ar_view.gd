class_name ARView
extends Control

signal screen_pressed(screen_point: Vector2, ray: Dictionary)
signal packet_applied(packet: ARFramePacket)
signal session_state_changed(state: PocketAR.SessionState)
signal tracking_reason_changed(reason: PocketAR.TrackingReason)
signal session_generation_changed(generation: int)
signal planes_changed()

enum Composition { IN_SCENE, ISOLATED_CAMERA }

@export var composition: Composition = Composition.IN_SCENE:
	set(value):
		if _composition_locked and value != composition:
			push_error("ARView.composition cannot change after _ready")
			return
		composition = value
@export_range(-1, 3) var orientation_quarter_turns: int = -1
@export_enum("Synthetic", "ARCore", "ARKit", "Scripted") var backend: int = 0
@export var handle_presses: bool = false
@export var environment: Environment
@export var plane_finding_mode: PocketAR.PlaneFindingMode = PocketAR.PLANE_FINDING_HORIZONTAL_AND_VERTICAL:
	set(value):
		if value not in [PocketAR.PLANE_FINDING_DISABLED, PocketAR.PLANE_FINDING_HORIZONTAL, PocketAR.PLANE_FINDING_HORIZONTAL_AND_VERTICAL]:
			push_error("Invalid plane finding mode")
			return
		_plane_finding_mode = value
		if interface != null:
			interface.call("set_plane_finding_mode", value)
	get:
		return int(interface.call("get_plane_finding_mode")) if interface != null else _plane_finding_mode

@onready var virtual_scene: SubViewport = $VirtualScene
@onready var camera: XRCamera3D = $VirtualScene/XROrigin3D/XRCamera3D
@onready var scene_root: Node3D = $VirtualScene/XROrigin3D/SceneRoot
@onready var hud: Control = $HUD/UI
@onready var world_environment: WorldEnvironment = $VirtualScene/WorldEnvironment
@onready var background: ColorRect = $CameraBackground
@onready var composite: TextureRect = $SceneComposite

var interface: XRInterfaceExtension
var applied_packet: ARFramePacket
var waiting_for_packet: bool = true
var camera_material: ShaderMaterial
var camera_quad: MeshInstance3D
var _composition_locked: bool = false
var _environment_properties: Array[StringName] = []
var _geometry_interface_id: int = 0
var _geometry_size := Vector2i.ZERO
var _geometry_turns: int = -1
var _geometry_clip_range := Vector2.ZERO
var _camera_projection := Projection()
var _camera_clip_range := Vector2.ZERO
var _plane_finding_mode: PocketAR.PlaneFindingMode = PocketAR.PLANE_FINDING_HORIZONTAL_AND_VERTICAL
var session_state: PocketAR.SessionState:
	get:
		return int(interface.call("get_session_state")) if interface != null else PocketAR.PAUSED
var tracking_reason: PocketAR.TrackingReason:
	get:
		return int(interface.call("get_tracking_reason")) if interface != null else PocketAR.NONE
var session_generation: int:
	get:
		return int(interface.call("get_session_generation")) if interface != null else 0
var _session_state: PocketAR.SessionState = PocketAR.PAUSED
var _tracking_reason: PocketAR.TrackingReason = PocketAR.NONE
var _session_generation: int = 0
var world_3d: World3D:
	get:
		return virtual_scene.find_world_3d()


func _ready() -> void:
	_composition_locked = true
	virtual_scene.own_world_3d = true
	camera_material = background.material as ShaderMaterial
	if composition == Composition.IN_SCENE:
		virtual_scene.use_hdr_2d = false
		composite.material = null
		camera_material = ShaderMaterial.new()
		camera_material.shader = preload("res://addons/pocketar/camera_3d.gdshader")
		# Mobile sorts priority before shader/material/depth; reserve 127 for the background.
		camera_material.render_priority = Material.RENDER_PRIORITY_MAX
		camera_quad = MeshInstance3D.new()
		camera_quad.mesh = QuadMesh.new()
		camera_quad.material_override = camera_material
		camera_quad.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		camera_quad.ignore_occlusion_culling = true
		camera_quad.hide()
		camera.add_child(camera_quad)
	world_environment.environment = Environment.new()
	for property: Dictionary in world_environment.environment.get_property_list():
		var name: StringName = property.name
		if int(property.usage) & PROPERTY_USAGE_STORAGE and name not in [&"background_mode", &"background_color"]:
			_environment_properties.append(name)
	_sync_environment()
	composite.texture = virtual_scene.get_texture()
	background.hide()
	composite.hide()
	if interface == null:
		if backend in [1, 2]:
			var native_class: String = "ARCoreInterface" if backend == 1 else "ARKitInterface"
			if not ClassDB.class_exists(native_class):
				push_error("%s is unavailable on this platform" % native_class)
				return
			interface = ClassDB.instantiate(native_class) as XRInterfaceExtension
		elif backend == 3:
			push_error("The Scripted backend requires a supplied interface")
			return
		else:
			interface = SyntheticARInterface.new()
	interface.call("set_plane_finding_mode", _plane_finding_mode)
	interface.connect("session_state_changed", _on_session_state_changed)
	interface.connect("tracking_reason_changed", _on_tracking_reason_changed)
	interface.connect("session_generation_changed", _on_session_generation_changed)
	interface.connect("planes_changed", planes_changed.emit)
	XRServer.add_interface(interface)
	_update_geometry()
	if not interface.initialize():
		push_error("%s initialization failed" % interface.get_name())
		return
	RenderingServer.frame_pre_draw.connect(_apply_packet)
	RenderingServer.frame_post_draw.connect(_mark_presented)
	_apply_packet()


func _exit_tree() -> void:
	if RenderingServer.frame_pre_draw.is_connected(_apply_packet):
		RenderingServer.frame_pre_draw.disconnect(_apply_packet)
	if RenderingServer.frame_post_draw.is_connected(_mark_presented):
		RenderingServer.frame_post_draw.disconnect(_mark_presented)
	if interface != null:
		var shader: ShaderMaterial = camera_material
		shader.set_shader_parameter("y_texture", null)
		shader.set_shader_parameter("cbcr_texture", null)
		shader.set_shader_parameter("rgb_texture", null)
		applied_packet = null
		interface.uninitialize()
		XRServer.remove_interface(interface)


func _process(_delta: float) -> void:
	_update_geometry()


func reset() -> void:
	if interface != null and interface.is_initialized():
		interface.call("reset")
		_apply_packet()


func _on_session_state_changed(state: PocketAR.SessionState) -> void:
	if _session_state != state:
		_session_state = state
		session_state_changed.emit(state)


func _on_tracking_reason_changed(reason: PocketAR.TrackingReason) -> void:
	if _tracking_reason != reason:
		_tracking_reason = reason
		tracking_reason_changed.emit(reason)


func _on_session_generation_changed(generation: int) -> void:
	if _session_generation != generation:
		_session_generation = generation
		session_generation_changed.emit(generation)


func _notification(what: int) -> void:
	if interface == null:
		return
	if what == NOTIFICATION_APPLICATION_PAUSED and interface.has_method("application_pause"):
		interface.call("application_pause")
	elif what == NOTIFICATION_APPLICATION_RESUMED and interface.has_method("application_resume"):
		interface.call("application_resume")


func _update_geometry() -> void:
	if interface == null:
		return
	var view_size := Vector2i(maxi(1, roundi(size.x)), maxi(1, roundi(size.y)))
	var turns: int = orientation_quarter_turns
	if turns < 0:
		turns = 1 if view_size.y > view_size.x else 0
	var clip_range := Vector2(camera.near, camera.far)
	if _geometry_interface_id == interface.get_instance_id() and _geometry_size == view_size and _geometry_turns == turns and _geometry_clip_range == clip_range:
		return
	interface.call("set_display_geometry", view_size, turns, camera.near, camera.far)
	_geometry_interface_id = interface.get_instance_id()
	_geometry_size = view_size
	_geometry_turns = turns
	_geometry_clip_range = clip_range


func _apply_packet() -> void:
	_sync_environment()
	var packet: ARFramePacket = interface.call("get_current_packet") as ARFramePacket
	var shader: ShaderMaterial = camera_material
	if packet == null:
		if applied_packet != null:
			shader.set_shader_parameter("y_texture", null)
			shader.set_shader_parameter("cbcr_texture", null)
			shader.set_shader_parameter("rgb_texture", null)
			applied_packet = null
			waiting_for_packet = true
			background.hide()
			if camera_quad != null:
				camera_quad.hide()
			composite.hide()
		if interface.is_initialized() and interface.has_method("mark_applied"):
			interface.call("mark_applied", null)
		_check_camera_material(shader)
		return
	if packet == applied_packet:
		_check_camera_material(shader)
		return
	if virtual_scene.size != Vector2i(packet.viewport_size):
		virtual_scene.size = Vector2i(packet.viewport_size)
	var clip_range := Vector2(camera.near, camera.far)
	if camera_quad != null and (_camera_projection != packet.projection or _camera_clip_range != clip_range):
		# POSITION bypasses projection, but frustum culling still tests the mesh's AABB.
		var centre: Vector4 = packet.projection.inverse() * Vector4(0, 0, -0.99, 1)
		var point := Vector3(centre.x, centre.y, centre.z) / centre.w
		var width: float = camera.near * 0.001
		for plane: int in range(6):
			width = minf(width, absf(packet.projection.get_projection_plane(plane).distance_to(point)) * 0.5)
		var extent := Vector3.ONE * width
		camera_quad.custom_aabb = AABB(point - extent * 0.5, extent)
		_camera_projection = packet.projection
		_camera_clip_range = clip_range
	shader.set_shader_parameter("y_texture", packet.y_texture)
	shader.set_shader_parameter("cbcr_texture", packet.cbcr_texture)
	shader.set_shader_parameter("rgb_texture", packet.rgb_texture)
	shader.set_shader_parameter("rgb_input", packet.rgb_texture != null)
	var uv: Transform2D = packet.display_transform
	var uv_matrix := Basis(Vector3(uv.x.x, uv.x.y, 0), Vector3(uv.y.x, uv.y.y, 0), Vector3(uv.origin.x, uv.origin.y, 1))
	shader.set_shader_parameter("display_transform", uv_matrix)
	shader.set_shader_parameter("imported_chroma", packet.imported_chroma)
	applied_packet = packet
	if interface.has_method("mark_applied"):
		interface.call("mark_applied", packet)
	_check_camera_material(shader)
	if waiting_for_packet:
		waiting_for_packet = false
		composite.show()
	var has_image: bool = packet.rgb_texture != null or (packet.y_texture != null and packet.cbcr_texture != null)
	if camera_quad != null:
		camera_quad.visible = has_image
	else:
		background.visible = has_image
	packet_applied.emit(packet)


func _check_camera_material(shader: ShaderMaterial) -> void:
	if interface.is_initialized() and OS.is_debug_build() and interface.has_method("debug_check_camera_material"):
		interface.call("debug_check_camera_material", shader.get_rid())


func _sync_environment() -> void:
	var target: Environment = world_environment.environment
	if environment == null:
		environment = Environment.new()
	target.background_mode = Environment.BG_CLEAR_COLOR
	target.background_color = Color(0, 0, 0, 0)
	# Environment setters do not emit changed, so copy live settings before each draw.
	for name: StringName in _environment_properties:
		var value: Variant = environment.get(name)
		if target.get(name) != value:
			target.set(name, value)


func _mark_presented() -> void:
	interface.call("mark_presented")


func screen_to_ray(screen_point: Vector2) -> Dictionary:
	var packet: ARFramePacket = _presented_packet()
	if packet == null:
		return {}
	return PocketAR.screen_to_ray(packet, size, screen_point)


func raycast(screen_point: Vector2, alignments: int = PocketAR.ALIGNMENT_MASK_ALL) -> Array[ARRaycastHit]:
	var packet: ARFramePacket = _presented_packet()
	if packet == null:
		return []
	var hits: Array[ARRaycastHit] = PocketAR.raycast_planes(packet, PocketAR.screen_to_ray(packet, size, screen_point))
	var filtered: Array[ARRaycastHit] = []
	for hit: ARRaycastHit in hits:
		if alignments & (1 << hit.alignment):
			filtered.append(hit)
	return filtered


func _presented_packet() -> ARFramePacket:
	if interface == null or size.x <= 0 or size.y <= 0:
		return null
	var packet: ARFramePacket = interface.call("get_presented_packet") as ARFramePacket
	if packet == null or not packet.tracking_valid or packet.session_generation != session_generation:
		return null
	return packet


func world_to_screen(world_point: Vector3) -> Vector2:
	var packet: ARFramePacket = _presented_packet()
	if packet == null or not world_point.is_finite():
		return Vector2.INF
	var local: Vector3 = packet.camera_transform.affine_inverse() * world_point
	var clip: Vector4 = packet.projection * Vector4(local.x, local.y, local.z, 1.0)
	if clip.w <= 0.0:
		return Vector2.INF
	return Vector2(clip.x / clip.w + 1.0, 1.0 - clip.y / clip.w) * size * 0.5


func view_point(event_position: Vector2) -> Vector2:
	return get_global_transform_with_canvas().affine_inverse() * event_position


func _unhandled_input(event: InputEvent) -> void:
	if not handle_presses:
		return
	var point := Vector2.ZERO
	if event is InputEventScreenTouch and event.pressed:
		point = view_point(event.position)
	elif event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT and event.device != InputEvent.DEVICE_ID_EMULATION:
		point = view_point(event.position)
	else:
		return
	if Rect2(Vector2.ZERO, size).has_point(point):
		screen_pressed.emit(point, screen_to_ray(point))
		get_viewport().set_input_as_handled()
