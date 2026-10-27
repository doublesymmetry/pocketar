extends RefCounted

var packets: Array[ARFramePacket] = []
var retaining: bool = false
var advances: int = 0
var last_frame: int = -1
var last_timestamp: int = -1


func observe(packet: ARFramePacket) -> void:
	if packet.frame_id != last_frame or packet.timestamp_ns != last_timestamp:
		advances += 1
	last_frame = packet.frame_id
	last_timestamp = packet.timestamp_ns
	if retaining:
		packets.append(packet)
		if packets.size() > 8:
			packets.pop_front()


func run(view: ARView) -> Dictionary:
	view.packet_applied.connect(observe)
	await view.get_tree().create_timer(2.0).timeout
	var before_advances: int = advances
	advances = 0
	retaining = true
	var before: Dictionary = view.interface.call("get_diagnostics") as Dictionary
	await view.get_tree().create_timer(1.0).timeout
	var halfway_advances: int = advances
	await view.get_tree().create_timer(1.0).timeout
	var after: Dictionary = view.interface.call("get_diagnostics") as Dictionary
	view.packet_applied.disconnect(observe)
	var result: Dictionary = evaluate(before, after, before_advances, advances, advances - halfway_advances)
	result["retained_packets"] = packets.size()
	packets.clear()
	return result


static func evaluate(before: Dictionary, after: Dictionary, before_advances: int, retained_advances: int, tail_advances: int) -> Dictionary:
	for key: String in ["new_frame_count", "packets_published", "texture_refusals"]:
		if not before.has(key) or not after.has(key):
			return {"ok": false, "missing": key}
	var frames: int = int(after["new_frame_count"]) - int(before["new_frame_count"])
	var published: int = int(after["packets_published"]) - int(before["packets_published"])
	var drops: int = maxi(0, frames - published)
	for key: String in ["ring_drops", "buffer_drops", "dropped_frames", "orientation_transition_drops"]:
		drops = maxi(drops, int(after.get(key, 0)) - int(before.get(key, 0)))
	var refusals: int = int(after["texture_refusals"]) - int(before["texture_refusals"])
	return {"ok": drops == 0 and refusals == 0 and (before_advances == 0 or tail_advances > 0), "baseline_advances": before_advances, "retained_advances": retained_advances, "final_second_advances": tail_advances, "dropped": drops, "texture_refusals": refusals}
