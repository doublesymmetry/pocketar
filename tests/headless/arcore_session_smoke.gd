extends SceneTree


func _initialize() -> void:
	var fixture := ARCoreSessionFixture.new()
	await process_frame
	fixture.run()
	await process_frame
	quit(0 if fixture.finish() == 0 else 1)
