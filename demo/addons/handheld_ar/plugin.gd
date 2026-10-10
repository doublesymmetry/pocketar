@tool
extends EditorPlugin

const ExportPlugin := preload("res://addons/handheld_ar/export_plugin.gd")
var exporter: EditorExportPlugin


func _enter_tree() -> void:
	exporter = ExportPlugin.new()
	add_export_plugin(exporter)


func _exit_tree() -> void:
	remove_export_plugin(exporter)
	exporter = null
