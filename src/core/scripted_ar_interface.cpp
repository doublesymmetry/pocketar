#include "scripted_ar_interface.h"
#include "ar_ids.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/xr_server.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {
namespace {

Transform3D fixture_transform(const pocketar::FixtureTransform &p_matrix) {
	Basis basis;
	for (int column = 0; column < 3; ++column) basis.set_column(column, Vector3(p_matrix[column * 4], p_matrix[column * 4 + 1], p_matrix[column * 4 + 2]));
	return Transform3D(basis, Vector3(p_matrix[12], p_matrix[13], p_matrix[14]));
}

}

void ScriptedARInterface::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_fixture", "path"), &ScriptedARInterface::load_fixture);
	ClassDB::bind_method(D_METHOD("advance_to", "t_usec"), &ScriptedARInterface::advance_to);
	ClassDB::bind_method(D_METHOD("get_pending_inputs"), &ScriptedARInterface::get_pending_inputs);
	ClassDB::bind_method(D_METHOD("reset"), &ScriptedARInterface::reset);
	ClassDB::bind_method(D_METHOD("get_session_state"), &ScriptedARInterface::get_session_state);
	ClassDB::bind_method(D_METHOD("get_tracking_reason"), &ScriptedARInterface::get_tracking_reason);
	ClassDB::bind_method(D_METHOD("get_plane_geometry_rebuild_count"), &ScriptedARInterface::get_plane_geometry_rebuild_count);
	ClassDB::bind_method(D_METHOD("get_plane_count"), &ScriptedARInterface::get_plane_count);
	ClassDB::bind_method(D_METHOD("get_semantics"), &ScriptedARInterface::get_semantics);
}

Error ScriptedARInterface::load_fixture(const String &p_path) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) return FileAccess::get_open_error();
	ERR_FAIL_COND_V_MSG(file->get_length() > 64 * 1024 * 1024, ERR_INVALID_DATA, "Scripted fixtures are limited to 64 MiB.");
	const CharString text = file->get_as_text().utf8();
	pocketar::ScriptedSession parsed;
	std::string error;
	if (!pocketar::parse_scripted_session(std::string(text.get_data(), text.length()), parsed, error)) {
		ERR_PRINT(String(error.c_str()));
		return ERR_PARSE_ERROR;
	}
	session = std::move(parsed);
	intrinsics = { session.intrinsics[0], session.intrinsics[1], session.intrinsics[2], session.intrinsics[3], Vector2(session.image_size[0], session.image_size[1]) };
	cursor = 0;
	playback_time = 0;
	pending_inputs.clear();
	reset();
	if (initialized && !is_paused()) advance_records(0);
	return OK;
}

bool ScriptedARInterface::_initialize() {
	if (initialized) return true;
	ERR_FAIL_COND_V_MSG(session.records.empty(), false, "Load a scripted fixture before initializing the backend.");
	if (!start("Scripted handheld camera")) return false;
	if (is_paused()) state = PocketAR::PAUSED;
	call_deferred("emit_signal", "session_state_changed", int(state));
	call_deferred("emit_signal", "session_generation_changed", generation);
	if (is_paused()) activity_changed();
	else advance_records(playback_time);
	return true;
}

void ScriptedARInterface::_uninitialize() {
	if (!initialized) return;
	clear_planes();
	stop();
	pending_inputs.clear();
	latest_frame_index = -1;
	cursor = 0;
	playback_time = 0;
	generation = next_ar_id();
	call_deferred("emit_signal", "session_generation_changed", generation);
	set_state(PocketAR::PAUSED);
}

bool ScriptedARInterface::anchor_plane_tracking(int64_t p_id) const {
	if (p_id < 0) return SimulatedARInterface::anchor_plane_tracking(p_id);
	for (size_t attempts = 0; attempts < planes.size(); ++attempts) {
		const auto entry = plane_trackers.find(p_id);
		if (entry == plane_trackers.end() || entry->second->get_tracking_state() != ARPlaneTracker::TRACKING) return false;
		if (!entry->second->get_subsumed_by()) return true;
		p_id = entry->second->get_subsumed_by();
	}
	return false;
}

void ScriptedARInterface::set_state(PocketAR::SessionState p_state) {
	if (state == p_state) return;
#ifdef DEBUG_ENABLED
	if (p_state != PocketAR::TRACKING) {
		debug_plane.pause();
		if (debug_plane.active()) presented_packet.unref();
	}
#endif
	state = p_state;
	if (state == PocketAR::FAILED || state == PocketAR::UNSUPPORTED) anchors.clear(false);
	else if (state != PocketAR::TRACKING) anchors.pause();
	call_deferred("emit_signal", "session_state_changed", int(state));
}

void ScriptedARInterface::set_reason(PocketAR::TrackingReason p_reason) {
	if (reason == p_reason) return;
	reason = p_reason;
	call_deferred("emit_signal", "tracking_reason_changed", int(reason));
}

void ScriptedARInterface::clear_planes() {
	bool changed = !planes.empty();
#ifdef DEBUG_ENABLED
	changed = debug_plane.clear() || changed;
	published_debug_plane = ARPlaneSnapshot();
#endif
	for (const auto &entry : plane_trackers) XRServer::get_singleton()->remove_tracker(entry.second);
	plane_trackers.clear();
	planes.clear();
	latest_planes_index = -1;
	applied_planes_index = -1;
	if (changed) call_deferred("emit_signal", "planes_changed");
}

void ScriptedARInterface::reset() {
	replay_failed = false;
	generation = next_ar_id();
	anchors.clear();
	map_transform = Transform3D();
	current_packet.unref();
	presented_packet.unref();
	latest_frame_index = -1;
	if (head.is_valid()) head->invalidate_pose("default");
	clear_planes();
	call_deferred("emit_signal", "session_generation_changed", generation);
	set_reason(PocketAR::NONE);
	set_state(is_paused() ? PocketAR::PAUSED : PocketAR::INITIALIZING);
}

void ScriptedARInterface::activity_changed() {
	if (is_paused()) {
		current_packet.unref(); presented_packet.unref();
		head->invalidate_pose("default");
		set_state(PocketAR::PAUSED);
	} else {
		geometry_dirty = true;
		set_reason(PocketAR::NONE);
		set_state(latest_frame_index >= 0 ? PocketAR::SessionState(session.records[size_t(latest_frame_index)].session_state) : PocketAR::INITIALIZING);
	}
}

int64_t ScriptedARInterface::get_plane_count() const {
	int64_t count = 0;
	for (const ARPlaneSnapshot &plane : planes) if (plane.tracking_state == ARPlaneTracker::TRACKING && plane.subsumed_by == 0) ++count;
	return count;
}

void ScriptedARInterface::apply_planes(const std::vector<pocketar::FixturePlane> &p_planes) {
	if (applied_planes_index == latest_planes_index && applied_plane_mode == plane_finding_mode && applied_plane_map == map_transform) return;
	const bool map_changed = applied_plane_map != map_transform;
	applied_planes_index = latest_planes_index;
	applied_plane_mode = plane_finding_mode;
	applied_plane_map = map_transform;
	std::vector<ARPlaneSnapshot> snapshots;
	std::map<int64_t, Ref<ARPlaneTracker>> trackers;
	if (plane_finding_mode == PocketAR::PLANE_FINDING_DISABLED) snapshots = planes;
	else if (plane_finding_mode == PocketAR::PLANE_FINDING_HORIZONTAL) {
		for (const ARPlaneSnapshot &plane : planes) if (plane.alignment == ARPlaneTracker::VERTICAL) snapshots.push_back(plane);
	}
	for (const pocketar::FixturePlane &plane : p_planes) {
		if (plane_finding_mode == PocketAR::PLANE_FINDING_DISABLED) break;
		if (plane_finding_mode == PocketAR::PLANE_FINDING_HORIZONTAL && plane.alignment == ARPlaneTracker::VERTICAL) continue;
		ARPlaneSnapshot snapshot;
		snapshot.plane_id = plane.id;
		snapshot.session_generation = generation;
		snapshot.alignment = plane.alignment;
		snapshot.transform = fixture_transform(plane.transform);
		snapshot.extents = Vector2(plane.extents[0], plane.extents[1]);
		snapshot.tracking_state = plane.tracking_state;
		snapshot.subsumed_by = plane.subsumed_by;
		const auto previous = plane_trackers.find(plane.id);
		if (previous != plane_trackers.end()) snapshot.polygon = previous->second->get_polygon();
		bool same_polygon = snapshot.polygon.size() == int64_t(plane.polygon.size());
		for (size_t i = 0; same_polygon && i < plane.polygon.size(); ++i) same_polygon = snapshot.polygon.get(i) == Vector2(plane.polygon[i][0], plane.polygon[i][1]);
		if (!same_polygon) {
			snapshot.polygon = PackedVector2Array();
			snapshot.polygon.resize(plane.polygon.size());
			for (size_t i = 0; i < plane.polygon.size(); ++i) snapshot.polygon[i] = Vector2(plane.polygon[i][0], plane.polygon[i][1]);
			++plane_geometry_rebuild_count;
		}
		snapshots.push_back(snapshot);
	}
	for (const ARPlaneSnapshot &snapshot : snapshots) {
		const auto existing = plane_trackers.find(snapshot.plane_id);
		Ref<ARPlaneTracker> tracker;
		if (existing != plane_trackers.end()) tracker = existing->second;
		else tracker.instantiate();
		ARPlaneSnapshot mapped = snapshot;
		mapped.transform = map_transform * snapshot.transform;
		tracker->update(mapped);
		if (existing == plane_trackers.end()) XRServer::get_singleton()->add_tracker(tracker);
		trackers.emplace(snapshot.plane_id, tracker);
	}
	for (const auto &entry : plane_trackers) if (trackers.count(entry.first) == 0) XRServer::get_singleton()->remove_tracker(entry.second);
	plane_trackers = std::move(trackers);
	const bool changed = snapshots != planes || (map_changed && !snapshots.empty());
	planes = std::move(snapshots);
	if (changed) call_deferred("emit_signal", "planes_changed");
}

void ScriptedARInterface::advance_to(int64_t p_t_usec) {
	ERR_FAIL_COND_MSG(p_t_usec < playback_time, "Scripted time cannot move backwards; reload the fixture to rewind.");
	if (is_paused() || replay_failed) return;
	if (initialized) advance_records(p_t_usec);
	else playback_time = p_t_usec;
}

void ScriptedARInterface::advance_records(int64_t p_t_usec) {
	bool new_frame = false;
	while (cursor < session.records.size() && session.records[cursor].t_usec <= p_t_usec) {
		const size_t index = cursor++;
		const pocketar::FixtureRecord &record = session.records[index];
		switch (record.kind) {
			case pocketar::FixtureRecord::RESET: reset(); new_frame = false; break;
			case pocketar::FixtureRecord::PAUSE: set_state(PocketAR::PAUSED); break;
			case pocketar::FixtureRecord::RESUME: set_reason(PocketAR::NONE); set_state(PocketAR::INITIALIZING); break;
			case pocketar::FixtureRecord::INPUT: {
				Dictionary input;
				input["t_usec"] = record.t_usec;
				input["kind"] = String(record.input_kind.c_str());
				input["finger"] = record.finger;
				input["uv"] = Vector2(record.uv[0], record.uv[1]);
				input["target_frame"] = record.target_frame;
				input["session_generation"] = generation;
				pending_inputs.push_back(input);
				break;
			}
			case pocketar::FixtureRecord::FRAME: {
				const int64_t frame_generation = generation;
				if (record.has_map_transform) map_transform = fixture_transform(record.map_transform);
				latest_frame_index = int64_t(index);
				set_reason(PocketAR::TrackingReason(record.tracking_reason));
				set_state(PocketAR::SessionState(record.session_state));
				if (generation != frame_generation || !initialized || is_paused()) return;
				for (const pocketar::FixtureAnchorUpdate &update : record.anchor_updates) {
					const auto entry = simulated_anchors.find(update.slot);
					if (entry == simulated_anchors.end()) {
						ERR_PRINT("Anchor update targets an unknown or stopped slot.");
						replay_failed = true;
						current_packet.unref();
						presented_packet.unref();
						set_state(PocketAR::FAILED);
						return;
					}
					entry->second.state = update.tracking_state;
					if (update.tracking_state == ARAnchorTracker::STOPPED) anchors.stop(update.slot);
					else if (update.tracking_state == ARAnchorTracker::PAUSED) anchors.pause(update.slot);
					if (generation != frame_generation) return;
				}
				frame_anchor_epoch = anchors.get_epoch();
				if (record.has_planes) latest_planes_index = int64_t(index);
				if (latest_planes_index >= 0) apply_planes(session.records[size_t(latest_planes_index)].planes);
				if (generation != frame_generation || !initialized || is_paused()) return;
				new_frame = true;
				break;
			}
		}
	}
	playback_time = p_t_usec;
	if ((new_frame || geometry_dirty) && latest_frame_index >= 0) make_packet();
}

TypedArray<Dictionary> ScriptedARInterface::get_pending_inputs() {
	TypedArray<Dictionary> result = pending_inputs;
	pending_inputs = TypedArray<Dictionary>();
	return result;
}

void ScriptedARInterface::make_packet() {
	const pocketar::FixtureRecord &frame = session.records[size_t(latest_frame_index)];
	Ref<ARFramePacket> packet;
	packet.instantiate();
	packet->session_generation = generation;
	packet->frame_id = frame.frame_id;
	packet->timestamp_usec = frame.t_usec;
	packet->timestamp_ns = frame.t_usec * 1000;
	fill_packet(packet, map_transform * fixture_transform(frame.camera_transform));
	packet->tracking_valid = frame.tracking_valid;
	packet->planes = planes;
	for (ARPlaneSnapshot &plane : packet->planes) plane.transform = map_transform * plane.transform;
	sample_anchors(packet);
	packet->anchor_epoch = frame_anchor_epoch;
#ifdef DEBUG_ENABLED
	publish_debug_plane(packet, frame.tracking_valid && state == PocketAR::TRACKING);
#endif
	packet->reuse_plane_values(current_packet);
	current_packet = packet;
	if (frame.tracking_valid && state != PocketAR::PAUSED) head->set_pose("default", packet->camera_transform, Vector3(), Vector3(), XRPose::XR_TRACKING_CONFIDENCE_HIGH);
	else head->invalidate_pose("default");
}

void ScriptedARInterface::_process() {
	if (!initialized || is_paused() || replay_failed) return;
	if (latest_frame_index < 0) advance_records(playback_time);
	if (geometry_dirty && latest_frame_index >= 0) make_packet();
}

}
