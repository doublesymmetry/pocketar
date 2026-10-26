@tool
extends EditorPlugin
## Internal registration of PocketAR's export plugin. Enable PocketAR in Project Settings > Plugins.

const ExportPlugin := preload("res://addons/pocketar/export_plugin.gd")
var exporter: EditorExportPlugin


func _enter_tree() -> void:
	exporter = ExportPlugin.new()
	add_export_plugin(exporter)


func _exit_tree() -> void:
	remove_export_plugin(exporter)
	exporter = null
