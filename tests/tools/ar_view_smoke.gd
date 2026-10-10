extends SceneTree

var failures: int = 0


func _initialize() -> void:
	_run.call_deferred()


func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("ARVIEW FAIL " + message)


func _run() -> void:
	var scene: PackedScene = load("res://addons/handheld_ar/ar_view.tscn") as PackedScene
	var view: ARView = scene.instantiate() as ARView
	root.add_child(view)
	_check(view.interface is SyntheticARInterface, "default backend remains synthetic")
	_check(view.applied_packet != null and view.background.visible and view.composite.visible, "first packet enables composition")
	view.interface.call("mark_presented")
	var ray: Dictionary = view.screen_to_ray(view.size * 0.5)
	_check(not ray.is_empty(), "presented packet supplies a ray")
	_check(view.raycast(view.size * 0.5).is_empty(), "no planes means no placement hits")
	view.interface.uninitialize()
	view.call("_apply_packet")
	_check(view.applied_packet == null, "old packet released when session stops")
	_check(not view.background.visible and not view.composite.visible, "old generation is hidden")
	var material: ShaderMaterial = view.background.material as ShaderMaterial
	_check(material.get_shader_parameter("y_texture") == null and material.get_shader_parameter("cbcr_texture") == null, "old textures released")
	_check(view.screen_to_ray(view.size * 0.5).is_empty(), "stopped session cannot produce a ray")
	_check(view.interface.initialize(), "session restarts")
	view.call("_apply_packet")
	_check(view.applied_packet != null and view.background.visible and view.composite.visible, "next generation enables composition")
	view.free()
	print("ARVIEW RESULT " + ("PASS" if failures == 0 else "FAIL"))
	quit(0 if failures == 0 else 1)
