class_name HarnessLog
extends RefCounted


static func format_line(message: String) -> String:
	var line: String = message.replace("\n", "\\n").replace("\r", "\\r")
	if line.to_utf8_buffer().size() > 900:
		line = line.left(200) + " ... [abridged]"
	return line


static func write_line(message: String) -> void:
	print(format_line(message))
