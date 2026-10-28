extends SceneTree

var failures: int = 0

func _initialize() -> void:
	_run.call_deferred()

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("ANCHOR VIEW FAIL " + message)

func _run() -> void:
	var view: ARView = preload("res://addons/pocketar/ar_view.tscn").instantiate()
	root.add_child(view)
	var changes: Array[int] = [0]
	view.anchors_changed.connect(func() -> void: changes[0] += 1)
	var interface: SyntheticARInterface = view.interface as SyntheticARInterface
	interface.set_camera_rate(240)
	var pose := Transform3D(Basis(), Vector3(0, -0.7, -2))
	var anchor := view.create_anchor(pose)
	var node := view.add_anchor_node(anchor)
	_check(node != null and node.get_parent() is XROrigin3D, "helper has direct XR origin parent")
	_check(node.transform == pose and not node.show_when_tracked, "helper starts at requested pose with explicit visibility policy")
	var observed: Array[ARFramePacket] = []
	view.packet_applied.connect(func(packet: ARFramePacket) -> void: observed.append(packet))
	var invalidated: Array[ARFramePacket] = []
	anchor.pose_changed.connect(func(_pose: XRPose) -> void:
		invalidated.append(interface.get_current_packet())
		view.reset(), CONNECT_ONE_SHOT)
	for _frame: int in range(10):
		await process_frame
		if not invalidated.is_empty():
			break
	_check(anchor.tracking_state == ARAnchorTracker.STOPPED, "nested reset stops anchor")
	_check(invalidated.size() == 1 and view.applied_packet != invalidated[0] and not observed.has(invalidated[0]), "nested reset suppresses stale packet_applied")
	_check(is_instance_valid(node) and not node.get_has_tracking_data(), "reset retains game node without tracking")
	_check(view.add_anchor_node(anchor) == null, "stopped helper creation rejected")
	var retained := view.create_anchor(pose)
	var temporary := view.add_anchor_node(retained)
	temporary.free()
	_check(interface.owns_anchor(retained), "freeing helper does not release anchor")
	view.remove_anchor(retained)
	_check(retained.tracking_state == ARAnchorTracker.STOPPED, "explicit removal releases anchor")
	await process_frame
	_check(changes[0] > 0, "view forwards deferred anchor membership changes")
	view.free()
	await process_frame
	print("ANCHOR VIEW RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
