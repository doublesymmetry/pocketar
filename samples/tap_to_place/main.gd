extends Control

const FIXTURE: String = "res://fixtures/table.jsonl"

var ar: ARView
var placements: Node3D
var planes: ARPlaneVisualizer
@onready var panel: PanelContainer = $Panel
@onready var status: Label = $Panel/Controls/Status
@onready var start_button: Button = $Panel/Controls/Buttons/Start
@onready var reset_button: Button = $Panel/Controls/Buttons/Reset
@onready var planes_button: CheckButton = $Panel/Controls/Planes


func _ready() -> void:
	start_button.pressed.connect(_start)
	reset_button.pressed.connect(_reset)
	planes_button.toggled.connect(func(enabled: bool) -> void: planes.visible = enabled)


func _start() -> void:
	if ar != null:
		if Engine.has_singleton("PocketARPlugin"):
			Engine.get_singleton("PocketARPlugin").requestSession()
		return
	ar = preload("res://addons/pocketar/ar_view.tscn").instantiate() as ARView
	ar.plane_finding_mode = PocketAR.PLANE_FINDING_HORIZONTAL_AND_VERTICAL
	match OS.get_name():
		"iOS": ar.backend = ARView.Backend.ARKIT
		"Android": ar.backend = ARView.Backend.ARCORE
		_:
			ar.backend = ARView.Backend.SCRIPTED
			var scripted := ScriptedARInterface.new()
			if scripted.load_fixture(FIXTURE) != OK:
				status.text = "Could not load the desktop fixture."
				ar.free()
				ar = null
				return
			scripted.advance_to(800000)
			ar.interface = scripted
	ar.composition = ARView.Composition.IN_SCENE
	ar.handle_presses = true
	ar.screen_pressed.connect(_place)
	ar.session_generation_changed.connect(_clear_placements)
	ar.session_state_changed.connect(_update_status)
	ar.tracking_reason_changed.connect(_update_status)
	add_child(ar)
	ar.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	placements = Node3D.new()
	ar.scene_root.add_child(placements)
	planes = ARPlaneVisualizer.new()
	planes.ar_view = ar
	ar.scene_root.add_child(planes)
	planes.visible = planes_button.button_pressed
	panel.reparent(ar.hud)
	reset_button.disabled = false
	planes_button.disabled = false
	_update_status()


func _update_status(_value: int = 0) -> void:
	var message: String
	match ar.session_state:
		PocketAR.UNSUPPORTED: message = "This device does not support AR."
		PocketAR.CHECKING: message = "Checking AR support and camera permission."
		PocketAR.NEEDS_INSTALL: message = "AR services need installation or an update. Tap Retry setup."
		PocketAR.INSTALLING: message = "Finish installing Google Play Services for AR."
		PocketAR.PERMISSION_DENIED:
			message = "Camera permission denied. Allow Camera in app settings."
			if OS.get_name() == "Android": message += " Or tap Retry setup."
		PocketAR.PAUSED: message = "AR interrupted. Return to the app to resume."
		PocketAR.FAILED: message = "AR setup failed. Reset the session."
		PocketAR.TRACKING: message = "Tap a detected plane to place a cube."
		_: message = "Move the phone slowly to find a plane."
	if ar.backend == ARView.Backend.SCRIPTED:
		message = "Desktop fixture: click the cyan wall or table to place a cube."
	status.text = "Camera: %s\nTracking: %s / %s\n%s" % [
		ARView.Backend.keys()[ar.backend], ClassDB.class_get_enum_constants("PocketAR", "SessionState")[ar.session_state],
		ClassDB.class_get_enum_constants("PocketAR", "TrackingReason")[ar.tracking_reason], message]
	start_button.text = "Retry setup"
	start_button.visible = OS.get_name() == "Android" and ar.session_state in [
		PocketAR.NEEDS_INSTALL, PocketAR.PERMISSION_DENIED, PocketAR.FAILED]


func _place(point: Vector2, _ray: Dictionary) -> void:
	var hits: Array[ARRaycastHit] = ar.raycast(point)
	if hits.is_empty():
		return
	var marker := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(0.1, 0.1, 0.1)
	marker.mesh = box
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = Color.CORAL
	marker.material_override = material
	placements.add_child(marker)
	marker.global_transform = hits[0].pose
	marker.position += marker.basis.y * 0.05


func _clear_placements(_generation: int) -> void:
	for child: Node in placements.get_children():
		child.queue_free()


func _reset() -> void:
	if ar.backend == ARView.Backend.SCRIPTED:
		var scripted := ar.interface as ScriptedARInterface
		scripted.load_fixture(FIXTURE)
		scripted.advance_to(800000)
	else:
		ar.reset()
