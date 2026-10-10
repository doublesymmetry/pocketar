class_name ARView
extends Control

signal screen_pressed(screen_point: Vector2, ray: Dictionary)
signal packet_applied(packet: ARFramePacket)

@export_range(-1, 3) var orientation_quarter_turns: int = -1
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

var interface: SyntheticARInterface
var applied_packet: ARFramePacket
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
	interface = SyntheticARInterface.new()
	XRServer.add_interface(interface)
	_update_geometry()
	if not interface.initialize():
		push_error("HandheldARSynthetic initialization failed")
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


func _update_geometry() -> void:
	var view_size := Vector2i(maxi(1, roundi(size.x)), maxi(1, roundi(size.y)))
	var turns: int = orientation_quarter_turns
	if turns < 0:
		turns = 1 if view_size.y > view_size.x else 0
	interface.set_display_geometry(view_size, turns, camera.near, camera.far)


func _apply_packet() -> void:
	var packet: ARFramePacket = interface.get_current_packet()
	if packet == null or packet == applied_packet:
		return
	virtual_scene.size = Vector2i(packet.viewport_size)
	var shader: ShaderMaterial = background.material as ShaderMaterial
	shader.set_shader_parameter("y_texture", packet.y_texture)
	shader.set_shader_parameter("cbcr_texture", packet.cbcr_texture)
	var uv: Transform2D = packet.display_transform
	var uv_matrix := Basis(Vector3(uv.x.x, uv.x.y, 0), Vector3(uv.y.x, uv.y.y, 0), Vector3(uv.origin.x, uv.origin.y, 1))
	shader.set_shader_parameter("display_transform", uv_matrix)
	shader.set_shader_parameter("color_encoding", packet.color_encoding)
	applied_packet = packet
	packet_applied.emit(packet)


func _mark_presented() -> void:
	interface.mark_presented()


func screen_to_ray(screen_point: Vector2) -> Dictionary:
	var packet: ARFramePacket = interface.get_presented_packet()
	if packet == null:
		return {}
	return HandheldAR.screen_to_ray(packet, size, screen_point)


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
