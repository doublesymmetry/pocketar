class_name ARPlaneVisualizer
extends Node3D

@export var ar_view: ARView
@export var enabled: bool = true:
	set(value):
		enabled = value
		visible = value

var plane_count: int:
	get:
		return _meshes.size()
var _meshes: Dictionary[int, MeshInstance3D] = {}


func _ready() -> void:
	visible = enabled
	if ar_view == null:
		push_error("ARPlaneVisualizer requires an ARView")
		return
	ar_view.packet_applied.connect(_update_planes)
	ar_view.session_generation_changed.connect(_generation_changed)
	ar_view.session_state_changed.connect(_state_changed)
	_update_planes(ar_view.applied_packet)


func _generation_changed(_generation: int) -> void:
	_update_planes(ar_view.applied_packet)


func _state_changed(_state: PocketAR.SessionState) -> void:
	_update_planes(ar_view.applied_packet)


func _clear() -> void:
	for mesh: MeshInstance3D in _meshes.values():
		mesh.free()
	_meshes.clear()


func _update_planes(packet: ARFramePacket) -> void:
	if ar_view.session_state != PocketAR.TRACKING or packet == null or not packet.tracking_valid or packet.session_generation != ar_view.session_generation:
		_clear()
		return
	var visible_ids: Dictionary[int, bool] = {}
	for plane: Dictionary in packet.planes:
		if plane.session_generation != ar_view.session_generation or plane.tracking_state != ARPlaneTracker.TRACKING or plane.subsumed_by != 0:
			continue
		var polygon: PackedVector2Array = plane.polygon
		if polygon.size() < 3:
			continue
		var id: int = plane.plane_id
		visible_ids[id] = true
		var mesh_instance: MeshInstance3D = _meshes.get(id)
		if mesh_instance == null:
			mesh_instance = MeshInstance3D.new()
			var material := StandardMaterial3D.new()
			material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			material.cull_mode = BaseMaterial3D.CULL_DISABLED
			material.albedo_color = Color(0.05, 0.75, 0.95, 0.24)
			mesh_instance.material_override = material
			add_child(mesh_instance)
			_meshes[id] = mesh_instance
		if mesh_instance.get_meta("polygon", PackedVector2Array()) != polygon:
			mesh_instance.mesh = _plane_mesh(polygon)
			mesh_instance.set_meta("polygon", polygon)
		mesh_instance.global_transform = plane.transform
	for id: int in _meshes.keys():
		if not visible_ids.has(id):
			_meshes[id].free()
			_meshes.erase(id)


func _plane_mesh(polygon: PackedVector2Array) -> ArrayMesh:
	var vertices := PackedVector3Array()
	var normals := PackedVector3Array()
	for point: Vector2 in polygon:
		vertices.append(Vector3(point.x, 0, point.y))
		normals.append(Vector3.UP)
	var indices: PackedInt32Array = Geometry2D.triangulate_polygon(polygon)
	if indices.is_empty():
		return null
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh
