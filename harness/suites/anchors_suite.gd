extends RefCounted

const AR_VIEW: PackedScene = preload("res://addons/pocketar/ar_view.tscn")
const TABLE_CENTER := Vector3(0.0, -0.7, -2.0)
const MARKER_COLOR := Color(0.5, 0.25, 0.75)
const OFFSETS: Array[Vector3] = [Vector3(0, 0.1, 0), Vector3(-0.16, 0.1, 0), Vector3(0.16, 0.1, 0)]

var failures: PackedStringArray = []
var worst_reprojection: float = 0.0


func run(host: Node, composition: ARView.Composition, output_dir: String) -> Dictionary:
	for portrait: bool in [false, true]:
		var name: String = "anchors-arkit" + ("-portrait" if portrait else "")
		await _run_once(host, composition, output_dir, name)
	return {"ok": failures.is_empty(), "detail": "Landscape/portrait, repeated camera packets, first-frame map correction and lifecycle; worst marker error %.4f px" % worst_reprojection if failures.is_empty() else "; ".join(failures)}


func _run_once(host: Node, composition: ARView.Composition, output_dir: String, name: String) -> void:
	var fixture: String = "res://suites/fixtures/" + name + ".jsonl"
	var header: Dictionary = JSON.parse_string(FileAccess.open(fixture, FileAccess.READ).get_line())
	var scripted := ScriptedARInterface.new()
	var loaded: Error = scripted.load_fixture(fixture)
	_check(loaded == OK, name + " loads")
	if loaded != OK:
		return
	scripted.advance_to(0)
	var view: ARView = AR_VIEW.instantiate() as ARView
	view.backend = ARView.Backend.SCRIPTED
	view.interface = scripted
	view.composition = composition
	view.plane_finding_mode = PocketAR.PLANE_FINDING_HORIZONTAL_AND_VERTICAL
	view.orientation_quarter_turns = int(header.quarter_turns)
	view.set_anchors_and_offsets_preset(Control.PRESET_TOP_LEFT)
	view.grow_horizontal = Control.GROW_DIRECTION_END
	view.grow_vertical = Control.GROW_DIRECTION_END
	view.position = Vector2.ZERO
	view.size = Vector2(header.view_size[0], header.view_size[1])
	host.add_child(view)
	view.virtual_scene.msaa_3d = Viewport.MSAA_4X
	await _draw(3)
	var hits: Array[ARRaycastHit] = view.raycast(view.world_to_screen(TABLE_CENTER))
	_check(not hits.is_empty(), name + " presents a table hit")
	if hits.is_empty():
		view.free()
		return
	var anchor: ARAnchorTracker = view.create_anchor_from_hit(hits[0])
	var spare: ARAnchorTracker = view.create_anchor(Transform3D(Basis.IDENTITY, Vector3(-0.35, -0.5, -2)))
	_check(anchor != null and spare != null, name + " accepts hit and free anchors")
	if anchor == null or spare == null:
		view.free()
		return
	_check(anchor.tracking_state == ARAnchorTracker.PAUSED and spare.tracking_state == ARAnchorTracker.PAUSED, name + " creations are pending")
	var root: XRAnchor3D = _follow(view, anchor)
	var spare_root: XRAnchor3D = _follow(view, spare)
	if root == null or spare_root == null:
		view.free()
		return
	_check(root.get_parent() is XROrigin3D and root.get_parent() == view.camera.get_parent(), name + " helper is directly under the camera's XR origin")
	var markers: Array[MeshInstance3D] = []
	for offset: Vector3 in OFFSETS:
		markers.append(_marker(root, offset, MARKER_COLOR))
	var fixed: MeshInstance3D = _marker(view.scene_root, Vector3.ZERO, Color(0.25, 0.75, 0.5))
	fixed.transform = anchor.transform * Transform3D(Basis.IDENTITY, OFFSETS[0])
	var initial: Transform3D = anchor.transform
	var records: Array[Dictionary] = []
	var prefix: String = output_dir.path_join(ARView.Composition.keys()[composition].to_lower() + "-" + name)
	scripted.advance_to(100000)
	await _draw(3)
	_check(anchor.tracking_state == ARAnchorTracker.TRACKING and root.visible, name + " first eligible packet enables the helper")
	var packet: ARFramePacket = scripted.get_presented_packet()
	var relative: Transform3D = packet.camera_transform.affine_inverse() * anchor.transform
	var screen: Vector2 = view.world_to_screen(fixed.global_position)
	await _checkpoint(view, anchor, root, fixed, markers, prefix, "before", records)
	for _frame: int in range(4):
		await _draw()
		_check(scripted.get_presented_packet() == packet and anchor.transform.is_equal_approx(initial) and root.transform.is_equal_approx(initial), name + " render repeats preserve the complete camera/anchor sample")
	scripted.advance_to(200000)
	await _draw(3)
	_check(anchor.transform.is_equal_approx(initial), name + " plane extent/centre growth does not drag an anchor")
	view.plane_finding_mode = PocketAR.PLANE_FINDING_DISABLED
	root.hide()
	fixed.hide()
	await _draw(2)
	var background: Image = view.virtual_scene.get_texture().get_image()
	root.show()
	await _draw(2)
	scripted.advance_to(300000)
	await _draw()
	var first_image: Image = view.virtual_scene.get_texture().get_image()
	var first_error: float = _centroid(first_image, background, screen, composition).distance_to(screen)
	worst_reprojection = maxf(worst_reprojection, first_error)
	_check(scripted.get_presented_packet().frame_id == 4, name + " first correction draw presents frame 4")
	_check(first_error < 0.75, "%s first correction draw marker reprojection %.4f px" % [name, first_error])
	_check(view.get_viewport().get_texture().get_image().save_png(prefix + "-first-correction.png") == OK, name + " saves first correction draw")
	records.append({"stage": "first-correction", "frame_id": 4, "marker_error_px": first_error})
	packet = scripted.get_presented_packet()
	var correction := Transform3D(Basis(Vector3.UP, deg_to_rad(5.0)), Vector3(0.2, 0, 0))
	_check(anchor.transform.is_equal_approx(correction * initial), name + " map correction applies while plane finding is disabled")
	_check((packet.camera_transform.affine_inverse() * anchor.transform).is_equal_approx(relative), name + " camera-relative anchor pose is invariant")
	_check(view.world_to_screen(markers[0].global_position).distance_to(screen) < 0.001, name + " anchor reprojection is invariant")
	_check(view.world_to_screen(fixed.global_position).distance_to(screen) > 2.0, name + " fixed world marker visibly shifts")
	await _checkpoint(view, anchor, root, fixed, markers, prefix, "corrected", records)
	var corrected: Transform3D = anchor.transform
	scripted.advance_to(400000)
	await _draw(3)
	_check(anchor.tracking_state == ARAnchorTracker.PAUSED and not root.visible and not root.get_has_tracking_data(), name + " independent pause invalidates and hides the helper")
	_check(spare.tracking_state == ARAnchorTracker.TRACKING and view.session_state == PocketAR.TRACKING, name + " one anchor can pause while camera and another anchor track")
	_check(anchor.transform.is_equal_approx(corrected), name + " pause retains the last usable transform")
	await _checkpoint(view, anchor, root, fixed, markers, prefix, "paused", records)
	scripted.advance_to(500000)
	await _draw(3)
	_check(anchor.tracking_state == ARAnchorTracker.PAUSED and not root.visible, name + " omitted slot updates preserve pause")
	_check(anchor.transform.is_equal_approx(corrected), name + " map correction during pause preserves the last usable pose")
	scripted.advance_to(600000)
	await _draw(3)
	_check(anchor.tracking_state == ARAnchorTracker.TRACKING and root.visible and root.get_has_tracking_data(), name + " first recovered sample shows the same helper")
	var recovery := Transform3D(Basis(Vector3.UP, deg_to_rad(-5.0)), Vector3(-0.2, 0, 0))
	_check(anchor.transform.is_equal_approx(recovery * initial), name + " recovery applies the map correction recorded during pause")
	await _checkpoint(view, anchor, root, fixed, markers, prefix, "recovered", records)
	view.plane_finding_mode = PocketAR.PLANE_FINDING_HORIZONTAL_AND_VERTICAL
	scripted.advance_to(700000)
	await _draw(3)
	_check(scripted.get_plane_count() == 0 and anchor.tracking_state == ARAnchorTracker.TRACKING and anchor.source_plane_id == 1, name + " source plane removal preserves anchor/provenance")
	view.remove_anchor(spare)
	_check(spare.tracking_state == ARAnchorTracker.STOPPED and not spare_root.visible and XRServer.get_tracker(spare.get_tracker_name()) == null, name + " explicit removal immediately stops/unregisters")
	scripted.advance_to(800000)
	await _draw(3)
	_check(spare.tracking_state == ARAnchorTracker.STOPPED and not spare_root.get_has_tracking_data(), name + " later packets cannot revive a removed anchor")
	await _checkpoint(view, anchor, root, fixed, markers, prefix, "removed-spare", records)
	scripted.advance_to(900000)
	_check(anchor.tracking_state == ARAnchorTracker.STOPPED and not root.visible, name + " reset immediately stops and hides retained anchors")
	scripted.advance_to(1000000)
	await _draw(3)
	_check(anchor.tracking_state == ARAnchorTracker.STOPPED and XRServer.get_tracker(anchor.get_tracker_name()) == null and not root.get_has_tracking_data(), name + " next generation cannot revive the old helper")
	await _checkpoint(view, anchor, root, fixed, markers, prefix, "reset", records)
	var evidence: FileAccess = FileAccess.open(prefix + ".json", FileAccess.WRITE)
	_check(evidence != null, name + " opens numeric evidence")
	if evidence != null:
		evidence.store_string(JSON.stringify(records, "\t") + "\n")
	view.free()


func _follow(view: ARView, anchor: ARAnchorTracker) -> XRAnchor3D:
	var node: XRAnchor3D = view.add_anchor_node(anchor)
	_check(node != null, "owned pending anchor accepts a stock helper")
	if node != null:
		node.visible = anchor.tracking_state == ARAnchorTracker.TRACKING
		node.tracking_changed.connect(func(tracked: bool) -> void: node.visible = tracked)
	return node


func _marker(parent: Node3D, offset: Vector3, color: Color) -> MeshInstance3D:
	var marker := MeshInstance3D.new()
	var mesh := QuadMesh.new()
	mesh.size = Vector2.ONE * 0.045
	marker.mesh = mesh
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = color
	marker.material_override = material
	parent.add_child(marker)
	marker.position = offset
	return marker


func _checkpoint(view: ARView, anchor: ARAnchorTracker, root: XRAnchor3D, fixed: MeshInstance3D, markers: Array[MeshInstance3D], prefix: String, stage: String, records: Array[Dictionary]) -> void:
	var packet: ARFramePacket = view.interface.call("get_presented_packet") as ARFramePacket
	_check(packet != null and packet == view.applied_packet, stage + " uses the presented packet")
	if packet == null:
		return
	var tracked: bool = anchor.tracking_state == ARAnchorTracker.TRACKING
	root.hide()
	fixed.hide()
	await _draw(2)
	var background: Image = view.virtual_scene.get_texture().get_image()
	root.visible = tracked
	await _draw(2)
	var image: Image = view.virtual_scene.get_texture().get_image()
	var errors: Array[float] = []
	for index: int in range(markers.size()):
		var marker: MeshInstance3D = markers[index]
		if tracked:
			var point: Vector2 = view.world_to_screen(marker.global_position)
			var error: float = _centroid(image, background, point, view.composition).distance_to(point)
			errors.append(error)
			worst_reprojection = maxf(worst_reprojection, error)
			_check(error < 0.75, "%s rendered marker %d reprojection %.4f px" % [stage, index, error])
	root.hide()
	fixed.show()
	await _draw(2)
	image = view.virtual_scene.get_texture().get_image()
	var fixed_screen: Vector2 = view.world_to_screen(fixed.global_position)
	var fixed_error: float = _centroid(image, background, fixed_screen, view.composition, Color(0.25, 0.75, 0.5)).distance_to(fixed_screen)
	_check(fixed_error < 0.75, "%s fixed reference reprojection %.4f px" % [stage, fixed_error])
	root.visible = tracked
	await _draw(2)
	_check(view.get_viewport().get_texture().get_image().save_png(prefix + "-" + stage + ".png") == OK, stage + " saves lossless PNG")
	var anchor_screen: Vector2 = view.world_to_screen(root.global_transform * OFFSETS[0])
	records.append({"stage": stage, "frame_id": packet.frame_id, "tracking_state": anchor.tracking_state, "camera_relative": var_to_str(packet.camera_transform.affine_inverse() * anchor.transform), "anchor_screen": [anchor_screen.x, anchor_screen.y], "fixed_screen": [fixed_screen.x, fixed_screen.y], "marker_errors_px": errors, "fixed_error_px": fixed_error})


func _centroid(image: Image, background: Image, point: Vector2, composition: ARView.Composition, marker_color: Color = MARKER_COLOR) -> Vector2:
	var total: float = 0.0
	var weighted := Vector2.ZERO
	var color: Color = marker_color.srgb_to_linear()
	for y: int in range(maxi(0, floori(point.y) - 12), mini(image.get_height(), ceili(point.y) + 13)):
		for x: int in range(maxi(0, floori(point.x) - 12), mini(image.get_width(), ceili(point.x) + 13)):
			var backdrop: Color = background.get_pixel(x, y)
			var actual: Color = image.get_pixel(x, y)
			if composition == ARView.Composition.IN_SCENE:
				backdrop = backdrop.srgb_to_linear()
				actual = actual.srgb_to_linear()
			var contrast: float = _color_error(color, backdrop)
			var weight: float = clampf(_color_error(actual, backdrop) / contrast, 0.0, 1.0)
			total += weight
			weighted += Vector2(x + 0.5, y + 0.5) * weight
	return weighted / total if total >= 1.0 else Vector2.INF


func _color_error(a: Color, b: Color) -> float:
	return maxf(absf(a.r - b.r), maxf(absf(a.g - b.g), absf(a.b - b.b)))


func _draw(count: int = 1) -> void:
	for _frame: int in range(count):
		await RenderingServer.frame_post_draw


func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
		printerr("ANCHORS FAIL " + message)
