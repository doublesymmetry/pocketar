extends SceneTree


func _initialize() -> void:
	var fixture := ARCoreExtrasFixture.new()
	quit(0 if fixture.run() == 0 else 1)
