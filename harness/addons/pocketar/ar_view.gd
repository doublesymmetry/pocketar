class_name ARView
extends Control
## Handheld AR camera, virtual scene and HUD in one Control.
##
## Instantiate res://addons/pocketar/ar_view.tscn; ARView.new() lacks its children.
## One active view owns the primary XR interface and a private [World3D].
## World units are metres, with camera +X right, +Y up and -Z forward.
## Keep XROrigin3D at identity and XR world scale at one. Requires Mobile rendering,
## Safe render threading and an SDR outer viewport. Set [member backend] and
## [member composition] before adding the view to the tree.

## An unconsumed touch or left mouse press, in view-local pixels, when [member handle_presses] is true.
## The ray uses the presented packet and can be empty while tracking is unavailable.
signal screen_pressed(screen_point: Vector2, ray: Dictionary)
## A new packet was applied at startup, before node _process callbacks, or at frame_pre_draw.
## Repeated packets do not emit. This is not a presentation notification; use placement methods for input.
signal packet_applied(packet: ARFramePacket)
## The backend state changed. Forwarded after the native operation finishes; handlers may call [method reset].
signal session_state_changed(state: PocketAR.SessionState)
## The tracking reason changed. Deferred like [signal session_state_changed].
signal tracking_reason_changed(reason: PocketAR.TrackingReason)
## The session identity changed. Discard old plane identities and clear or rebuild placements.
signal session_generation_changed(generation: int)
## The current plane snapshot/tracker set changed. Placement still uses the last presented packet.
signal planes_changed()

## Deferred: anchors were added, removed or stopped.
signal anchors_changed()

## How camera pixels and virtual objects are combined.
enum Composition {
	## Camera and objects share scene colour, grading and screen reads. Enable material debanding.
	IN_SCENE,
	## SDR camera below an HDR virtual scene. Camera pixels bypass the game's Environment effects.
	ISOLATED_CAMERA,
}

## Which AR session drives the view. Startup-only: set before adding the view to the tree.
enum Backend {
	## Simulated desktop camera without real planes.
	SYNTHETIC,
	## ARCore on Android.
	ARCORE,
	## ARKit on iOS.
	ARKIT,
	## Recorded replay; requires a loaded [ScriptedARInterface] assigned to [member interface] before startup.
	SCRIPTED,
}

## Startup-only composition. Changes after _ready report an error and retain the selected mode.
@export var composition: Composition = Composition.IN_SCENE:
	set(value):
		if _composition_locked and value != composition:
			push_error("ARView.composition cannot change after _ready")
			return
		composition = value
## Display rotation for simulated backends: clockwise quarter turns, or -1 for aspect-based automatic rotation.
## Native backends use the device's physical orientation.
@export_range(-1, 3) var orientation_quarter_turns: int = -1
## Startup-only session backend; see [enum Backend].
@export var backend: Backend = Backend.SYNTHETIC
## Handle unconsumed presses inside the view and emit [signal screen_pressed]. HUD Controls receive input first.
## Touch-emulated mouse events are ignored. False by default.
@export var handle_presses: bool = false
## Game environment. Live settings are copied before drawing; background mode and colour are ignored.
## The supplied resource is not modified. Camera pixels are affected only in IN_SCENE mode.
@export var environment: Environment
## Plane discovery mode, changeable while running and retained across reset.
## Disabling discovery preserves existing planes; their refinement differs by backend. This does not create anchors.
@export var plane_finding_mode: PocketAR.PlaneFindingMode = PocketAR.PLANE_FINDING_DISABLED:
	set(value):
		if value not in [PocketAR.PLANE_FINDING_DISABLED, PocketAR.PLANE_FINDING_HORIZONTAL, PocketAR.PLANE_FINDING_HORIZONTAL_AND_VERTICAL]:
			push_error("Invalid plane finding mode")
			return
		_plane_finding_mode = value
		if interface != null:
			interface.call("set_plane_finding_mode", value)
	get:
		return int(interface.call("get_plane_finding_mode")) if interface != null else _plane_finding_mode

## Native camera resolution preference. HIGHEST preserves the platform's original selection.
## Changing this can reset the native session; explicit Android configuration indices take precedence.
@export var camera_quality: PocketAR.CameraQuality = PocketAR.HIGHEST:
	set(value):
		if value < PocketAR.HIGHEST or value > PocketAR.LOWEST:
			push_error("Invalid camera quality")
			return
		camera_quality = value
		if interface != null and interface.has_method("set_camera_quality"):
			interface.call("set_camera_quality", value)
## Preferred iOS capture rate; zero keeps platform ordering. An unavailable rate uses the nearest supported rate.
## Android keeps its 30 fps camera filter. This does not set the game's rendering rate.
@export_enum("Platform default:0", "30 fps:30", "60 fps:60") var camera_frame_rate: int = 0:
	set(value):
		if value not in [0, 30, 60]:
			push_error("Invalid camera frame rate")
			return
		camera_frame_rate = value
		if interface != null and interface.has_method("set_camera_frame_rate"):
			interface.call("set_camera_frame_rate", value)
## Pause camera tracking and virtual viewport updates explicitly. Visibility does not suspend AR.
## Resume retains the session generation and tracking map where the platform allows it.
@export var suspended: bool = false:
	set(value):
		suspended = value
		if interface != null:
			interface.call("set_suspended", value)
		if is_node_ready():
			_update_activity()

## Internal rendering viewport. Use [member world_3d] and [member scene_root] for game content.
@onready var virtual_scene: SubViewport = $VirtualScene
## XR camera. Set near/far clipping distances in metres after ready; its pose and projection belong to the backend.
@onready var camera: XRCamera3D = $VirtualScene/XROrigin3D/XRCamera3D
## Parent for virtual 3D content; available after ready. Keep this node's transform at identity.
@onready var scene_root: Node3D = $VirtualScene/XROrigin3D/SceneRoot
## Parent for ordinary HUD Controls above the AR scene; available after ready.
@onready var hud: Control = $HUD/UI
## Internal environment copy. Set [member environment] instead.
@onready var world_environment: WorldEnvironment = $VirtualScene/WorldEnvironment
## Internal camera canvas; do not replace its material.
@onready var background: ColorRect = $CameraBackground
## Internal viewport composite; do not replace its material.
@onready var composite: TextureRect = $SceneComposite

## Advanced backend access for platform controls and replay. Do not replace a running interface.
## ARView owns initialization, display geometry, packet application and presentation.
var interface: XRInterfaceExtension
## Internal last material packet; use [signal packet_applied] or placement methods.
var applied_packet: ARFramePacket
## Internal presentation state.
var waiting_for_packet: bool = true
## Internal camera material.
var camera_material: ShaderMaterial
## Internal in-scene camera mesh.
var camera_quad: MeshInstance3D
var _application_paused: bool = false
var _composition_locked: bool = false
var _environment_properties: Array[StringName] = []
var _geometry_interface_id: int = 0
var _geometry_size := Vector2i.ZERO
var _geometry_turns: int = -1
var _geometry_clip_range := Vector2.ZERO
var _camera_projection := Projection()
var _camera_clip_range := Vector2.ZERO
var _plane_finding_mode: PocketAR.PlaneFindingMode = PocketAR.PLANE_FINDING_DISABLED
## Current backend state, or PAUSED before a backend exists. Read immediately; notifications are deferred.
var session_state: PocketAR.SessionState:
	get:
		return int(interface.call("get_session_state")) if interface != null else PocketAR.PAUSED
## Current backend tracking reason, or NONE before a backend exists. NONE alone does not imply tracking.
var tracking_reason: PocketAR.TrackingReason:
	get:
		return int(interface.call("get_tracking_reason")) if interface != null else PocketAR.NONE
## Process-wide unique session identity, or zero before a backend exists. Reset invalidates old identities.
var session_generation: int:
	get:
		return int(interface.call("get_session_generation")) if interface != null else 0
var _session_state: PocketAR.SessionState = PocketAR.PAUSED
var _tracking_reason: PocketAR.TrackingReason = PocketAR.NONE
var _session_generation: int = 0
## Active private AR world for physics queries or a camera sharing the scene. Available after ready.
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
		if backend in [Backend.ARCORE, Backend.ARKIT]:
			var native_class: String = "ARCoreInterface" if backend == Backend.ARCORE else "ARKitInterface"
			if not ClassDB.class_exists(native_class):
				push_error("%s is unavailable on this platform" % native_class)
				return
			interface = ClassDB.instantiate(native_class) as XRInterfaceExtension
		elif backend == Backend.SCRIPTED:
			push_error("The Scripted backend requires a supplied interface")
			return
		else:
			interface = SyntheticARInterface.new()
	interface.call("set_plane_finding_mode", _plane_finding_mode)
	if interface.has_method("set_camera_quality"):
		interface.call("set_camera_quality", camera_quality)
	if interface.has_method("set_camera_frame_rate"):
		interface.call("set_camera_frame_rate", camera_frame_rate)
	interface.call("set_suspended", suspended)
	_update_activity()
	interface.connect("session_state_changed", _on_session_state_changed)
	interface.connect("tracking_reason_changed", _on_tracking_reason_changed)
	interface.connect("session_generation_changed", _on_session_generation_changed)
	interface.connect("planes_changed", planes_changed.emit)
	interface.connect("anchors_changed", anchors_changed.emit)
	XRServer.add_interface(interface)
	_update_geometry()
	if not interface.initialize():
		push_error("%s initialization failed" % interface.get_name())
		return
	get_tree().process_frame.connect(_apply_packet)
	RenderingServer.frame_pre_draw.connect(_apply_packet)
	RenderingServer.frame_post_draw.connect(_mark_presented)
	_apply_packet()


func _exit_tree() -> void:
	if get_tree().process_frame.is_connected(_apply_packet):
		get_tree().process_frame.disconnect(_apply_packet)
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
	if not suspended and not _application_paused:
		_update_geometry()


## Reset an initialized session, clear presentation and planes, stop anchors, and change the generation.
## Does nothing before initialization. Game-owned content must be cleared or rebuilt by the game.
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
	if what == NOTIFICATION_APPLICATION_PAUSED:
		_application_paused = true
		interface.call("application_pause")
		_update_activity()
	elif what == NOTIFICATION_APPLICATION_RESUMED:
		_application_paused = false
		interface.call("application_resume")
		_update_activity()


func _update_activity() -> void:
	virtual_scene.render_target_update_mode = SubViewport.UPDATE_DISABLED if suspended or _application_paused else SubViewport.UPDATE_ALWAYS


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
	if not suspended and not _application_paused:
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
	if packet != applied_packet or packet != interface.call("get_current_packet"):
		return
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
	if not suspended and not _application_paused:
		interface.call("mark_presented")


## Convert view-local pixels using the presented packet. Returns origin (world near-plane point in metres),
## direction (unit world vector), frame_id and session_generation, or {} without valid presented tracking.
func screen_to_ray(screen_point: Vector2) -> Dictionary:
	var packet: ARFramePacket = _presented_packet()
	if packet == null:
		return {}
	return PocketAR.screen_to_ray(packet, size, screen_point)


## Intersect presented, tracking, unsubsumed plane polygons from view-local pixels, nearest first.
## Only front faces (+Y normal) hit. [param alignments] is a PocketAR.AlignmentMask bitmask,
## not an ARPlaneTracker.Alignment ordinal. Returns [code][][/code] without valid presentation or a matching hit.
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
	if interface == null or suspended or _application_paused or size.x <= 0 or size.y <= 0:
		return null
	var packet: ARFramePacket = interface.call("get_presented_packet") as ARFramePacket
	if packet == null or not packet.tracking_valid or packet.session_generation != session_generation:
		return null
	return packet


## Project a world point in metres to view-local pixels using the presented packet.
## Returns Vector2.INF without valid presented tracking, for non-finite input or behind-camera points.
## Off-screen points can return coordinates outside the view; this does not test visibility or occlusion.
func world_to_screen(world_point: Vector3) -> Vector2:
	var packet: ARFramePacket = _presented_packet()
	if packet == null or not world_point.is_finite():
		return Vector2.INF
	var local: Vector3 = packet.camera_transform.affine_inverse() * world_point
	var clip: Vector4 = packet.projection * Vector4(local.x, local.y, local.z, 1.0)
	if clip.w <= 0.0:
		return Vector2.INF
	return Vector2(clip.x / clip.w + 1.0, 1.0 - clip.y / clip.w) * size * 0.5


## Convert viewport/canvas input coordinates, such as InputEventScreenTouch.position, to view-local pixels.
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


## Create a tracked physical location at a finite rigid world pose in metres.
## Starts PAUSED until a camera packet applies its pose. Failure returns null and warns.
func create_anchor(pose: Transform3D) -> ARAnchorTracker:
	if interface == null or not interface.is_initialized():
		push_warning("Cannot create anchor: not tracking.")
		return null
	return interface.call("create_anchor", pose) as ARAnchorTracker


## Create from a hit in the currently presented frame and uninterrupted tracking interval.
## Android attaches to the current plane; iOS adds an ordinary world anchor at the hit pose.
## The hit pose is submitted to the current native map; historical placement is approximate.
func create_anchor_from_hit(hit: ARRaycastHit) -> ARAnchorTracker:
	if interface == null or not interface.is_initialized():
		push_warning("Cannot create anchor: not tracking.")
		return null
	return interface.call("create_anchor_from_hit", hit) as ARAnchorTracker


## Stop and unregister this view's anchor, releasing native ownership. Does not free nodes.
func remove_anchor(anchor: ARAnchorTracker) -> void:
	if interface != null:
		interface.call("remove_anchor", anchor)


## Parent a stock tracker-following node under the private XROrigin3D.
## The node does not own the anchor. Use tracking_changed for visibility and interaction policy.
## Returns null with a warning for a foreign or stopped anchor.
func add_anchor_node(anchor: ARAnchorTracker) -> XRAnchor3D:
	if interface == null or not interface.call("owns_anchor", anchor):
		push_warning("Cannot add anchor node: foreign or stopped anchor.")
		return null
	var node := XRAnchor3D.new()
	node.tracker = anchor.get_tracker_name()
	node.show_when_tracked = false
	node.transform = anchor.transform
	scene_root.get_parent().add_child(node)
	return node
