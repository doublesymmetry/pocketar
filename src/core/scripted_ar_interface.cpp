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
	ClassDB::bind_method(D_METHOD("application_pause"), &ScriptedARInterface::application_pause);
	ClassDB::bind_method(D_METHOD("application_resume"), &ScriptedARInterface::application_resume);
	ClassDB::bind_method(D_METHOD("get_session_state"), &ScriptedARInterface::get_session_state);
	ClassDB::bind_method(D_METHOD("get_tracking_reason"), &ScriptedARInterface::get_tracking_reason);
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
	if (initialized) advance_records(0);
	return OK;
}

bool ScriptedARInterface::_initialize() {
	if (initialized) return true;
	ERR_FAIL_COND_V_MSG(session.records.empty(), false, "Load a scripted fixture before initializing the backend.");
	if (!start("Scripted handheld camera")) return false;
	call_deferred("emit_signal", "session_state_changed", int(state));
	call_deferred("emit_signal", "session_generation_changed", generation);
	advance_records(playback_time);
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

void ScriptedARInterface::set_state(PocketAR::SessionState p_state) {
	if (state == p_state) return;
	state = p_state;
	call_deferred("emit_signal", "session_state_changed", int(state));
}

void ScriptedARInterface::set_reason(PocketAR::TrackingReason p_reason) {
	if (reason == p_reason) return;
	reason = p_reason;
	call_deferred("emit_signal", "tracking_reason_changed", int(reason));
}

void ScriptedARInterface::clear_planes() {
	const bool changed = !planes.empty();
	for (const auto &entry : plane_trackers) XRServer::get_singleton()->remove_tracker(entry.second);
	plane_trackers.clear();
	planes.clear();
	if (changed) call_deferred("emit_signal", "planes_changed");
}

void ScriptedARInterface::reset() {
	generation = next_ar_id();
	current_packet.unref();
	presented_packet.unref();
	latest_frame_index = -1;
	if (head.is_valid()) head->invalidate_pose("default");
	clear_planes();
	call_deferred("emit_signal", "session_generation_changed", generation);
	set_reason(PocketAR::NONE);
	set_state(PocketAR::INITIALIZING);
}

void ScriptedARInterface::application_pause() {
	if (head.is_valid()) head->invalidate_pose("default");
	set_state(PocketAR::PAUSED);
}

void ScriptedARInterface::application_resume() {
	if (state != PocketAR::PAUSED) return;
	set_reason(PocketAR::NONE);
	set_state(PocketAR::INITIALIZING);
}

int64_t ScriptedARInterface::get_plane_count() const {
	int64_t count = 0;
	for (const ARPlaneSnapshot &plane : planes) if (plane.tracking_state == ARPlaneTracker::TRACKING && plane.subsumed_by == 0) ++count;
	return count;
}

void ScriptedARInterface::apply_planes(const std::vector<pocketar::FixturePlane> &p_planes) {
	std::vector<ARPlaneSnapshot> snapshots;
	std::map<int64_t, Ref<ARPlaneTracker>> trackers;
	for (const pocketar::FixturePlane &plane : p_planes) {
		ARPlaneSnapshot snapshot;
		snapshot.plane_id = plane.id;
		snapshot.session_generation = generation;
		snapshot.alignment = plane.alignment;
		snapshot.transform = fixture_transform(plane.transform);
		snapshot.extents = Vector2(plane.extents[0], plane.extents[1]);
		snapshot.tracking_state = plane.tracking_state;
		snapshot.subsumed_by = plane.subsumed_by;
		for (const auto &vertex : plane.polygon) snapshot.polygon.push_back(Vector2(vertex[0], vertex[1]));
		snapshots.push_back(snapshot);
		const auto existing = plane_trackers.find(plane.id);
		Ref<ARPlaneTracker> tracker;
		if (existing != plane_trackers.end()) tracker = existing->second;
		else tracker.instantiate();
		tracker->update(snapshot);
		if (existing == plane_trackers.end()) XRServer::get_singleton()->add_tracker(tracker);
		trackers.emplace(plane.id, tracker);
	}
	for (const auto &entry : plane_trackers) if (trackers.count(entry.first) == 0) XRServer::get_singleton()->remove_tracker(entry.second);
	plane_trackers = std::move(trackers);
	const bool changed = snapshots != planes;
	planes = std::move(snapshots);
	if (changed) call_deferred("emit_signal", "planes_changed");
}

void ScriptedARInterface::advance_to(int64_t p_t_usec) {
	ERR_FAIL_COND_MSG(p_t_usec < playback_time, "Scripted time cannot move backwards; reload the fixture to rewind.");
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
			case pocketar::FixtureRecord::PAUSE: application_pause(); break;
			case pocketar::FixtureRecord::RESUME: application_resume(); break;
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
			case pocketar::FixtureRecord::FRAME:
				latest_frame_index = int64_t(index);
				set_reason(PocketAR::TrackingReason(record.tracking_reason));
				set_state(PocketAR::SessionState(record.session_state));
				if (record.has_planes) apply_planes(record.planes);
				new_frame = true;
				break;
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
	fill_packet(packet, fixture_transform(frame.camera_transform));
	packet->tracking_valid = frame.tracking_valid;
	packet->planes = planes;
	current_packet = packet;
	if (frame.tracking_valid && state != PocketAR::PAUSED) head->set_pose("default", packet->camera_transform, Vector3(), Vector3(), XRPose::XR_TRACKING_CONFIDENCE_HIGH);
	else head->invalidate_pose("default");
}

void ScriptedARInterface::_process() {
	if (geometry_dirty && latest_frame_index >= 0) make_packet();
}

}
