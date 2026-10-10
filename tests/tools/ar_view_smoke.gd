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
	_check(view.world_3d != null and view.world_3d == view.scene_root.get_world_3d(), "exposes active scene world")
	_check(view.world_3d != root.find_world_3d(), "AR scene owns an isolated world")
	var game_environment := Environment.new()
	game_environment.background_mode = Environment.BG_COLOR
	game_environment.background_color = Color.MAGENTA
	game_environment.tonemap_exposure = 2.0
	view.environment = game_environment
	view.call("_apply_packet")
	var rendered_environment: Environment = view.world_environment.environment
	_check(rendered_environment != game_environment, "private render environment")
	_check(rendered_environment.background_mode == Environment.BG_CLEAR_COLOR and rendered_environment.background_color == Color(0, 0, 0, 0), "transparent black clear")
	_check(game_environment.background_mode == Environment.BG_COLOR and game_environment.background_color == Color.MAGENTA, "source background preserved")
	_check(rendered_environment.tonemap_exposure == 2.0, "other environment settings copied")
	game_environment.tonemap_exposure = 0.5
	game_environment.fog_aerial_perspective = 0.3
	view.call("_apply_packet")
	_check(rendered_environment.tonemap_exposure == 0.5 and is_equal_approx(rendered_environment.fog_aerial_perspective, 0.3), "live environment edits survive repeated packets")
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
