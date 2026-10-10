class_name ARView
extends Control

signal screen_pressed(screen_point: Vector2, ray: Dictionary)
signal packet_applied(packet: ARFramePacket)

@export_range(-1, 3) var orientation_quarter_turns: int = -1
@export_enum("Synthetic", "ARCore") var backend: int = 0
@export var environment: Environment:
	set(value):
		environment = value
		if is_node_ready():
			world_environment.environment = value

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
var world_3d: World3D:
	get:
		return virtual_scene.world_3d


func _ready() -> void:
	virtual_scene.own_world_3d = true
	if environment == null:
		environment = Environment.new()
		environment.background_mode = Environment.BG_CLEAR_COLOR
	world_environment.environment = environment
	composite.texture = virtual_scene.get_texture()
	background.hide()
	composite.hide()
	if interface == null:
		if backend == 1:
			if not ClassDB.class_exists("ARCoreInterface"):
				push_error("ARCoreInterface is unavailable on this platform")
				return
			interface = ClassDB.instantiate("ARCoreInterface") as XRInterfaceExtension
		else:
			interface = SyntheticARInterface.new()
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
		interface.uninitialize()
		XRServer.remove_interface(interface)


func _process(_delta: float) -> void:
	_update_geometry()


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
	interface.call("set_display_geometry", view_size, turns, camera.near, camera.far)


func _apply_packet() -> void:
	var packet: ARFramePacket = interface.call("get_current_packet") as ARFramePacket
	var shader: ShaderMaterial = background.material as ShaderMaterial
	if packet == null:
		if applied_packet != null:
			shader.set_shader_parameter("y_texture", null)
			shader.set_shader_parameter("cbcr_texture", null)
			applied_packet = null
			waiting_for_packet = true
			background.hide()
			composite.hide()
		return
	if packet == applied_packet:
		return
	virtual_scene.size = Vector2i(packet.viewport_size)
	shader.set_shader_parameter("y_texture", packet.y_texture)
	shader.set_shader_parameter("cbcr_texture", packet.cbcr_texture)
	var uv: Transform2D = packet.display_transform
	var uv_matrix := Basis(Vector3(uv.x.x, uv.x.y, 0), Vector3(uv.y.x, uv.y.y, 0), Vector3(uv.origin.x, uv.origin.y, 1))
	shader.set_shader_parameter("display_transform", uv_matrix)
	shader.set_shader_parameter("color_encoding", packet.color_encoding)
	applied_packet = packet
	if waiting_for_packet:
		waiting_for_packet = false
		background.show()
		composite.show()
	packet_applied.emit(packet)


func _mark_presented() -> void:
	interface.call("mark_presented")


func screen_to_ray(screen_point: Vector2) -> Dictionary:
	if interface == null:
		return {}
	var packet: ARFramePacket = interface.call("get_presented_packet") as ARFramePacket
	if packet == null or not packet.tracking_valid:
		return {}
	return HandheldAR.screen_to_ray(packet, size, screen_point)


func raycast(screen_point: Vector2) -> Array:
	if interface == null:
		return []
	var packet: ARFramePacket = interface.call("get_presented_packet") as ARFramePacket
	if packet == null or not packet.tracking_valid:
		return []
	return HandheldAR.raycast_planes(packet, HandheldAR.screen_to_ray(packet, size, screen_point))


func _unhandled_input(event: InputEvent) -> void:
	var point := Vector2.ZERO
	if event is InputEventScreenTouch and event.pressed:
		point = event.position - global_position
	elif event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		point = event.position - global_position
	else:
		return
	if Rect2(Vector2.ZERO, size).has_point(point):
		screen_pressed.emit(point, screen_to_ray(point))
		get_viewport().set_input_as_handled()
