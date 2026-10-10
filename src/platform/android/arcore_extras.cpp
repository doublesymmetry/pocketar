#include "arcore_extras.h"
#include "core/ar_ids.h"
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <cmath>
#include <cstring>

namespace godot {

static constexpr uint8_t INPUT_TRACK_UUID[16] = { 0x61, 0xa9, 0x77, 0x25, 0x8b, 0xe4, 0x4e, 0x8f, 0xa1, 0x96, 0x6a, 0xf3, 0x8c, 0xa1, 0x07, 0x35 };
static constexpr const char *INPUT_TRACK_UUID_TEXT = "61a97725-8be4-4e8f-a196-6af38ca10735";
static constexpr const char *INPUT_TRACK_METADATA = "{\"format\":\"handheld-ar-input\",\"version\":1,\"payload\":\"application-owned bytes\"}";
static constexpr size_t MAX_PENDING_INPUTS = 128;
static constexpr int MAX_INPUT_BYTES = 65536;
static constexpr int MAX_INPUT_ATTEMPTS = 8;
static constexpr size_t MAX_LIVE_HITS = 128;

bool ARCoreExtras::usable() const {
	return session && owner_thread == std::this_thread::get_id();
}

bool ARCoreExtras::live_ready() const {
	return usable() && live_frame && latest_packet.is_valid() && latest_packet->get_tracking_valid() && latest_packet->get_session_generation() == generation;
}

void ARCoreExtras::increment(const char *p_key, int64_t p_amount) {
	counters[p_key] = int64_t(counters.get(p_key, 0)) + p_amount;
}

Dictionary ARCoreExtras::result(const String &p_status, ArStatus p_code) const {
	Dictionary value;
	value["ok"] = p_code == AR_SUCCESS && (p_status == "OK" || p_status == "QUEUED");
	value["status"] = p_status;
	value["ar_status"] = int(p_code);
	value["session_generation"] = generation;
	value["frame_id"] = frame_id;
	return value;
}

Transform3D ARCoreExtras::read_pose() const {
	float raw[7];
	api.ArPose_getPoseRaw(session, scratch_pose, raw);
	return Transform3D(Basis(Quaternion(raw[0], raw[1], raw[2], raw[3]).normalized()), Vector3(raw[4], raw[5], raw[6]));
}

void ARCoreExtras::setup(ArSession *p_session, int64_t p_generation) {
	ERR_FAIL_COND(session && !usable());
	close();
	session = p_session;
	owner_thread = std::this_thread::get_id();
	generation = p_generation;
	frame_id = 0;
	last_track_timestamp = 0;
	recording_status = AR_RECORDING_NONE;
	playback_status = AR_PLAYBACK_NONE;
	if (session) {
		api.ArPose_create(session, nullptr, &scratch_pose);
		api.ArTrackDataList_create(session, &track_data_list);
	}
}

void ARCoreExtras::before_update() {
	ERR_FAIL_COND(session && !usable());
	for (auto &entry : live_hits) {
		api.ArHitResult_destroy(entry.second);
	}
	live_hits.clear();
	native_planes.clear();
	live_frame = nullptr;
	recording_frame = nullptr;
}

void ARCoreExtras::close() {
	ERR_FAIL_COND(session && !usable());
	before_update();
	cancel_image_capacity_probe();
	if (old_probe_frame) {
		api.ArFrame_destroy(old_probe_frame);
		old_probe_frame = nullptr;
	}
	for (auto &entry : anchors) {
		api.ArAnchor_detach(session, entry.second);
		api.ArAnchor_release(entry.second);
	}
	anchors.clear();
	if (scratch_pose) {
		api.ArPose_destroy(scratch_pose);
		scratch_pose = nullptr;
	}
	if (track_data_list) {
		api.ArTrackDataList_destroy(track_data_list);
		track_data_list = nullptr;
	}
	if (!pending_inputs.empty()) {
		increment("record_inputs_cancelled", pending_inputs.size());
	}
	pending_inputs.clear();
	playback_inputs.clear();
	point_cloud.clear();
	latest_packet.unref();
	session = nullptr;
	recording_status = AR_RECORDING_NONE;
	playback_status = AR_PLAYBACK_NONE;
}

void ARCoreExtras::on_tick(ArFrame *p_frame, bool p_probe_cpu_image) {
	ERR_FAIL_COND(!usable() || !p_frame);
	recording_frame = p_frame;
	api.ArSession_getRecordingStatus(session, &recording_status);
	api.ArSession_getPlaybackStatus(session, &playback_status);
	int64_t timestamp = 0;
	api.ArFrame_getTimestamp(session, p_frame, &timestamp);
	if (old_probe_frame && timestamp > 0 && timestamp != old_probe_timestamp) {
		probe_old_frame(p_frame);
	}
	if (p_probe_cpu_image && frame_probes_enabled && !image_capacity_probe && timestamp > 0 && timestamp == last_track_timestamp) {
		probe_repeated_frame(p_frame, timestamp);
	}
	if (timestamp <= 0 || timestamp == last_track_timestamp) {
		return;
	}
	last_track_timestamp = timestamp;
	retry_inputs();
	if (playback_status == AR_PLAYBACK_OK || playback_status == AR_PLAYBACK_FINISHED) {
		read_playback_inputs(timestamp);
	}
}

void ARCoreExtras::on_frame(ArFrame *p_frame, int64_t p_frame_id, const Ref<ARFramePacket> &p_latest, const std::map<int64_t, ArPlane *> &p_planes) {
	ERR_FAIL_COND(!usable() || !p_frame || p_latest.is_null() || p_latest->get_session_generation() != generation);
	live_frame = p_frame;
	frame_id = p_frame_id;
	latest_packet = p_latest;
	native_planes = p_planes;
	point_cloud.clear();
	if (point_cloud_enabled) {
		copy_point_cloud();
	}
}

void ARCoreExtras::on_repeated_frame(ArFrame *p_frame, const std::map<int64_t, ArPlane *> &p_planes) {
	if (!usable() || !p_frame || latest_packet.is_null() || latest_packet->get_session_generation() != generation) {
		return;
	}
	int64_t timestamp = 0;
	api.ArFrame_getTimestamp(session, p_frame, &timestamp);
	if (timestamp != latest_packet->get_timestamp_ns()) {
		return;
	}
	live_frame = p_frame;
	native_planes = p_planes;
}

Dictionary ARCoreExtras::diagnostics() const {
	Dictionary value = counters.duplicate(true);
	value["session_generation"] = generation;
	value["frame_id"] = frame_id;
	value["recording_status"] = int(recording_status);
	value["playback_status"] = int(playback_status);
	value["playback_finished"] = playback_status == AR_PLAYBACK_FINISHED;
	value["anchor_count"] = int64_t(anchors.size());
	value["pending_input_count"] = int64_t(pending_inputs.size());
	value["input_track_uuid"] = INPUT_TRACK_UUID_TEXT;
	value["input_track_version"] = 1;
	value["input_retry_limit"] = MAX_INPUT_ATTEMPTS;
	value["point_cloud_enabled"] = point_cloud_enabled;
	value["frame_probes_enabled"] = frame_probes_enabled;
	value["image_capacity_probe_active"] = image_capacity_probe;
	value["image_capacity_held"] = int64_t(probe_images_held.size());
	value["old_frame_probe_pending"] = old_probe_frame != nullptr;
	return value;
}

Array ARCoreExtras::collect_hits(ArHitResultList *p_list, const Vector3 &p_origin) {
	Array results;
	int32_t count = 0;
	api.ArHitResultList_getSize(session, p_list, &count);
	for (int32_t i = 0; i < count && live_hits.size() < MAX_LIVE_HITS; i++) {
		ArHitResult *hit = nullptr;
		api.ArHitResult_create(session, &hit);
		api.ArHitResultList_getItem(session, p_list, i, hit);
		ArTrackable *trackable = nullptr;
		api.ArHitResult_acquireTrackable(session, hit, &trackable);
		ArTrackableType type = AR_TRACKABLE_NOT_VALID;
		if (trackable) {
			api.ArTrackable_getType(session, trackable, &type);
		}
		int64_t plane_id = 0;
		Transform3D pose;
		if (type == AR_TRACKABLE_PLANE) {
			ArPlane *plane = ArAsPlane(trackable);
			ArTrackingState tracking = AR_TRACKING_STATE_PAUSED;
			api.ArTrackable_getTrackingState(session, trackable, &tracking);
			ArPlane *parent = nullptr;
			api.ArPlane_acquireSubsumedBy(session, plane, &parent);
			const bool subsumed = parent != nullptr;
			if (parent) {
				api.ArTrackable_release(ArAsTrackable(parent));
			}
			int32_t inside = 0;
			api.ArHitResult_getHitPose(session, hit, scratch_pose);
			api.ArPlane_isPoseInPolygon(session, plane, scratch_pose, &inside);
			pose = read_pose();
			api.ArPlane_getCenterPose(session, plane, scratch_pose);
			const Transform3D center = read_pose();
			const Vector3 normal = center.basis.get_column(1);
			const bool camera_front = normal.dot(latest_packet->get_physical_camera_transform().origin - center.origin) > 0;
			if (tracking == AR_TRACKING_STATE_TRACKING && !subsumed && inside && camera_front && normal.dot(p_origin - center.origin) > 0) {
				for (const auto &entry : native_planes) {
					if (entry.second == plane) {
						plane_id = entry.first;
						break;
					}
				}
			}
		}
		if (trackable) {
			api.ArTrackable_release(trackable);
		}
		if (plane_id == 0) {
			api.ArHitResult_destroy(hit);
			continue;
		}
		float distance = 0;
		api.ArHitResult_getDistance(session, hit, &distance);
		const int64_t query_id = next_ar_id();
		live_hits.emplace(query_id, hit);
		Dictionary value = result("OK");
		value["query_id"] = query_id;
		value["plane_id"] = plane_id;
		value["distance"] = distance;
		value["pose"] = pose;
		results.push_back(value);
	}
	increment("live_query_count");
	increment("live_query_hits", results.size());
	return results;
}

Array ARCoreExtras::live_hit_test(const Vector2 &p_view_pixels) {
	if (!live_ready() || !p_view_pixels.is_finite()) {
		return {};
	}
	ArHitResultList *list = nullptr;
	api.ArHitResultList_create(session, &list);
	api.ArFrame_hitTest(session, live_frame, p_view_pixels.x, p_view_pixels.y, list);
	Array results = collect_hits(list, latest_packet->get_physical_camera_transform().origin);
	api.ArHitResultList_destroy(list);
	return results;
}

Array ARCoreExtras::live_hit_test_ray(const Vector3 &p_origin, const Vector3 &p_direction) {
	if (!live_ready() || !p_origin.is_finite() || !p_direction.is_finite() || p_direction.length_squared() < 1e-12) {
		return {};
	}
	const Vector3 direction = p_direction.normalized();
	const float origin_raw[3] = { p_origin.x, p_origin.y, p_origin.z };
	const float direction_raw[3] = { direction.x, direction.y, direction.z };
	ArHitResultList *list = nullptr;
	api.ArHitResultList_create(session, &list);
	api.ArFrame_hitTestRay(session, live_frame, origin_raw, direction_raw, list);
	Array results = collect_hits(list, p_origin);
	api.ArHitResultList_destroy(list);
	return results;
}

Dictionary ARCoreExtras::anchor_snapshot(int64_t p_id, ArAnchor *p_anchor) {
	ArTrackingState tracking;
	api.ArAnchor_getTrackingState(session, p_anchor, &tracking);
	api.ArAnchor_getPose(session, p_anchor, scratch_pose);
	Dictionary value = result("OK");
	value["anchor_id"] = p_id;
	value["tracking_state"] = int(tracking);
	value["pose"] = read_pose();
	return value;
}

Dictionary ARCoreExtras::keep_anchor(ArAnchor *p_anchor, ArStatus p_status, bool p_fallback) {
	if (p_status != AR_SUCCESS || !p_anchor) {
		if (p_anchor) {
			api.ArAnchor_release(p_anchor);
		}
		increment("anchor_create_failures");
		return result("NATIVE_ERROR", p_status == AR_SUCCESS ? AR_ERROR_FATAL : p_status);
	}
	const int64_t anchor_id = next_ar_id();
	anchors.emplace(anchor_id, p_anchor);
	Dictionary value = anchor_snapshot(anchor_id, p_anchor);
	value["fallback"] = p_fallback;
	increment("anchors_created");
	return value;
}

Dictionary ARCoreExtras::create_live_anchor(int64_t p_query_id) {
	if (!live_ready()) {
		return result("NOT_READY", AR_ERROR_NOT_TRACKING);
	}
	const auto found = live_hits.find(p_query_id);
	if (found == live_hits.end()) {
		return result("STALE_QUERY", AR_ERROR_DEADLINE_EXCEEDED);
	}
	ArAnchor *anchor = nullptr;
	const ArStatus status = api.ArHitResult_acquireNewAnchor(session, found->second, &anchor);
	return keep_anchor(anchor, status, false);
}

Dictionary ARCoreExtras::create_anchor(const Ref<ARFramePacket> &p_old_packet, const Ref<ARRaycastHit> &p_hit, bool p_allow_fallback) {
	if (p_old_packet.is_null() || p_hit.is_null()) {
		return result("INVALID_ARGUMENT", AR_ERROR_INVALID_ARGUMENT);
	}
	if (p_old_packet->get_session_generation() != generation || p_hit->get_session_generation() != generation) {
		return result("STALE_GENERATION", AR_ERROR_ILLEGAL_STATE);
	}
	if (!live_ready() || !p_old_packet->get_tracking_valid()) {
		return result("NOT_TRACKING", AR_ERROR_NOT_TRACKING);
	}
	if (p_hit->get_frame_id() != p_old_packet->get_frame_id()) {
		return result("INVALID_ARGUMENT", AR_ERROR_INVALID_ARGUMENT);
	}
	const ARPlaneSnapshot *old_plane = nullptr;
	const ARPlaneSnapshot *current_plane = nullptr;
	for (const ARPlaneSnapshot &plane : p_old_packet->get_plane_snapshots()) {
		if (plane.plane_id == p_hit->get_plane_id() && plane.session_generation == generation) {
			old_plane = &plane;
			break;
		}
	}
	for (const ARPlaneSnapshot &plane : latest_packet->get_plane_snapshots()) {
		if (plane.plane_id == p_hit->get_plane_id() && plane.session_generation == generation) {
			current_plane = &plane;
			break;
		}
	}
	if (!old_plane || old_plane->tracking_state != AR_TRACKING_STATE_TRACKING || old_plane->subsumed_by != 0) {
		return result("INVALID_TARGET", AR_ERROR_INVALID_ARGUMENT);
	}
	const auto native = native_planes.find(p_hit->get_plane_id());
	bool stale = !current_plane || current_plane->tracking_state != AR_TRACKING_STATE_TRACKING || current_plane->subsumed_by != old_plane->subsumed_by || native == native_planes.end();
	if (!stale) {
		ArTrackingState tracking;
		api.ArTrackable_getTrackingState(session, ArAsTrackable(native->second), &tracking);
		ArPlane *parent = nullptr;
		api.ArPlane_acquireSubsumedBy(session, native->second, &parent);
		stale = tracking != AR_TRACKING_STATE_TRACKING || parent;
		if (parent) {
			api.ArTrackable_release(ArAsTrackable(parent));
		}
	}
	if (stale && !p_allow_fallback) {
		return result("STALE_TARGET", AR_ERROR_NOT_TRACKING);
	}
	const Transform3D rebased = stale ? latest_packet->get_physical_camera_transform() * p_old_packet->get_physical_camera_transform().affine_inverse() * p_hit->get_pose() : current_plane->transform * old_plane->transform.affine_inverse() * p_hit->get_pose();
	const Quaternion rotation = rebased.basis.get_rotation_quaternion().normalized();
	const float raw[7] = { rotation.x, rotation.y, rotation.z, rotation.w, rebased.origin.x, rebased.origin.y, rebased.origin.z };
	ArPose *pose = nullptr;
	api.ArPose_create(session, raw, &pose);
	ArAnchor *anchor = nullptr;
	const ArStatus status = stale ? api.ArSession_acquireNewAnchor(session, pose, &anchor) : api.ArTrackable_acquireNewAnchor(session, ArAsTrackable(native->second), pose, &anchor);
	api.ArPose_destroy(pose);
	return keep_anchor(anchor, status, stale);
}

Array ARCoreExtras::get_anchors() {
	Array results;
	if (usable()) {
		for (const auto &entry : anchors) {
			results.push_back(anchor_snapshot(entry.first, entry.second));
		}
	}
	return results;
}

bool ARCoreExtras::remove_anchor(int64_t p_anchor_id) {
	if (!usable()) {
		return false;
	}
	const auto found = anchors.find(p_anchor_id);
	if (found == anchors.end()) {
		return false;
	}
	api.ArAnchor_detach(session, found->second);
	api.ArAnchor_release(found->second);
	anchors.erase(found);
	return true;
}

bool ARCoreExtras::valid_dataset_uri(const String &p_uri) {
	return p_uri.begins_with("file:///") && !p_uri.contains(" ") && !p_uri.contains("\n") && !p_uri.contains("\r") && !p_uri.contains("?") && !p_uri.contains("#");
}

Dictionary ARCoreExtras::start_recording(const String &p_file_uri) {
	if (!usable()) {
		return result("NOT_READY", AR_ERROR_SESSION_PAUSED);
	}
	if (!valid_dataset_uri(p_file_uri)) {
		return result("INVALID_URI", AR_ERROR_INVALID_ARGUMENT);
	}
	ArRecordingConfig *config = nullptr;
	ArTrack *track = nullptr;
	api.ArRecordingConfig_create(session, &config);
	api.ArRecordingConfig_setMp4DatasetUri(session, config, p_file_uri.utf8().get_data());
	api.ArRecordingConfig_setAutoStopOnPause(session, config, 1);
	api.ArTrack_create(session, &track);
	api.ArTrack_setId(session, track, INPUT_TRACK_UUID);
	api.ArTrack_setMetadata(session, track, reinterpret_cast<const uint8_t *>(INPUT_TRACK_METADATA), std::strlen(INPUT_TRACK_METADATA));
	api.ArTrack_setMimeType(session, track, "application/vnd.handheld-ar.input");
	api.ArRecordingConfig_addTrack(session, config, track);
	const ArStatus status = api.ArSession_startRecording(session, config);
	api.ArTrack_destroy(track);
	api.ArRecordingConfig_destroy(config);
	if (status == AR_SUCCESS) {
		increment("record_inputs_cancelled", pending_inputs.size());
		pending_inputs.clear();
	}
	api.ArSession_getRecordingStatus(session, &recording_status);
	counters["recording_start_status"] = int(status);
	return result(status == AR_SUCCESS ? "OK" : "NATIVE_ERROR", status);
}

Dictionary ARCoreExtras::stop_recording() {
	if (!usable()) {
		return result("NOT_READY", AR_ERROR_SESSION_PAUSED);
	}
	const ArStatus status = api.ArSession_stopRecording(session);
	api.ArSession_getRecordingStatus(session, &recording_status);
	increment("record_inputs_cancelled", pending_inputs.size());
	pending_inputs.clear();
	counters["recording_stop_status"] = int(status);
	return result(status == AR_SUCCESS ? "OK" : "NATIVE_ERROR", status);
}

Dictionary ARCoreExtras::set_playback_dataset(const String &p_file_uri) {
	if (!usable()) {
		return result("NOT_READY", AR_ERROR_SESSION_PAUSED);
	}
	if (!valid_dataset_uri(p_file_uri)) {
		return result("INVALID_URI", AR_ERROR_INVALID_ARGUMENT);
	}
	const ArStatus status = api.ArSession_setPlaybackDatasetUri(session, p_file_uri.utf8().get_data());
	api.ArSession_getPlaybackStatus(session, &playback_status);
	counters["playback_set_status"] = int(status);
	if (status == AR_SUCCESS) {
		before_update();
		last_track_timestamp = 0;
		playback_inputs.clear();
	}
	return result(status == AR_SUCCESS ? "OK" : "NATIVE_ERROR", status);
}

Dictionary ARCoreExtras::record_input(const PackedByteArray &p_payload) {
	if (!usable()) {
		return result("NOT_READY", AR_ERROR_SESSION_PAUSED);
	}
	if (p_payload.is_empty() || p_payload.size() > MAX_INPUT_BYTES) {
		return result("INVALID_PAYLOAD", AR_ERROR_INVALID_ARGUMENT);
	}
	api.ArSession_getRecordingStatus(session, &recording_status);
	if (recording_status != AR_RECORDING_OK) {
		return result("NOT_RECORDING", AR_ERROR_ILLEGAL_STATE);
	}
	if (pending_inputs.size() >= MAX_PENDING_INPUTS) {
		return result("QUEUE_FULL", AR_ERROR_RESOURCE_EXHAUSTED);
	}
	int attempts = 0;
	if (recording_frame && pending_inputs.empty()) {
		const ArStatus status = api.ArFrame_recordTrackData(session, recording_frame, INPUT_TRACK_UUID, p_payload.ptr(), p_payload.size());
		attempts = 1;
		if (status == AR_SUCCESS) {
			increment("record_inputs_written");
			return result("OK");
		}
		if (status != AR_ERROR_ILLEGAL_STATE) {
			increment("record_inputs_failed");
			return result("NATIVE_ERROR", status);
		}
	}
	pending_inputs.push_back({ p_payload.duplicate(), attempts });
	return result("QUEUED");
}

void ARCoreExtras::retry_inputs() {
	if (recording_status != AR_RECORDING_OK) {
		increment("record_inputs_cancelled", pending_inputs.size());
		pending_inputs.clear();
		return;
	}
	while (!pending_inputs.empty()) {
		PendingInput &pending = pending_inputs.front();
		const ArStatus status = api.ArFrame_recordTrackData(session, recording_frame, INPUT_TRACK_UUID, pending.payload.ptr(), pending.payload.size());
		pending.attempts++;
		if (status == AR_ERROR_ILLEGAL_STATE && pending.attempts < MAX_INPUT_ATTEMPTS) {
			increment("record_input_retries");
			break;
		}
		if (status == AR_SUCCESS) {
			increment("record_inputs_written");
		} else {
			increment("record_inputs_failed");
			counters["record_input_last_error"] = int(status);
		}
		pending_inputs.pop_front();
	}
}

void ARCoreExtras::read_playback_inputs(int64_t p_timestamp) {
	api.ArFrame_getUpdatedTrackData(session, recording_frame, INPUT_TRACK_UUID, track_data_list);
	int32_t count = 0;
	api.ArTrackDataList_getSize(session, track_data_list, &count);
	for (int32_t i = 0; i < count; i++) {
		ArTrackData *data = nullptr;
		api.ArTrackDataList_acquireItem(session, track_data_list, i, &data);
		const uint8_t *bytes = nullptr;
		int32_t size = 0;
		int64_t timestamp = 0;
		api.ArTrackData_getData(session, data, &bytes, &size);
		api.ArTrackData_getFrameTimestamp(session, data, &timestamp);
		if (size > 0 && size <= MAX_INPUT_BYTES && bytes && playback_inputs.size() < int64_t(MAX_PENDING_INPUTS)) {
			PackedByteArray payload;
			payload.resize(size);
			std::memcpy(payload.ptrw(), bytes, size);
			Dictionary value;
			value["session_generation"] = generation;
			value["timestamp_ns"] = timestamp;
			value["playback_timestamp_ns"] = p_timestamp;
			value["payload"] = payload;
			playback_inputs.push_back(value);
			increment("playback_inputs_read");
		} else if (size > 0) {
			increment("playback_inputs_dropped");
		}
		api.ArTrackData_release(data);
	}
}

Array ARCoreExtras::get_playback_inputs() {
	Array results = playback_inputs.duplicate(true);
	playback_inputs.clear();
	return results;
}

void ARCoreExtras::set_point_cloud_enabled(bool p_enabled) {
	point_cloud_enabled = p_enabled;
	if (!p_enabled) {
		point_cloud.clear();
	}
}

void ARCoreExtras::copy_point_cloud() {
	ArPointCloud *cloud = nullptr;
	const ArStatus status = api.ArFrame_acquirePointCloud(session, live_frame, &cloud);
	counters["point_cloud_status"] = int(status);
	if (status != AR_SUCCESS || !cloud) {
		if (cloud) {
			api.ArPointCloud_release(cloud);
		}
		return;
	}
	int32_t count = 0;
	int64_t timestamp = 0;
	api.ArPointCloud_getNumberOfPoints(session, cloud, &count);
	api.ArPointCloud_getTimestamp(session, cloud, &timestamp);
	PackedVector3Array positions;
	PackedFloat32Array confidences;
	PackedInt32Array ids;
	if (count > 0) {
		const float *points = nullptr;
		const int32_t *point_ids = nullptr;
		api.ArPointCloud_getData(session, cloud, &points);
		api.ArPointCloud_getPointIds(session, cloud, &point_ids);
		positions.resize(count);
		confidences.resize(count);
		ids.resize(count);
		for (int32_t i = 0; i < count; i++) {
			positions[i] = Vector3(points[4 * i], points[4 * i + 1], points[4 * i + 2]);
			confidences[i] = points[4 * i + 3];
			ids[i] = point_ids[i];
		}
	}
	api.ArPointCloud_release(cloud);
	point_cloud["session_generation"] = generation;
	point_cloud["frame_id"] = frame_id;
	point_cloud["timestamp_ns"] = timestamp;
	point_cloud["positions"] = positions;
	point_cloud["confidences"] = confidences;
	point_cloud["ids"] = ids;
}

Dictionary ARCoreExtras::begin_image_capacity_probe() {
	if (!usable() || !live_frame || latest_packet.is_null() || latest_packet->get_session_generation() != generation || old_probe_frame) {
		return result("NOT_READY", AR_ERROR_ILLEGAL_STATE);
	}
	if (image_capacity_probe) {
		return result("ALREADY_RUNNING", AR_ERROR_ILLEGAL_STATE);
	}
	image_capacity_probe = true;
	image_probe_ticks = 0;
	image_probe_timestamp = 0;
	counters["image_capacity_complete"] = false;
	counters["image_capacity_exhausted"] = false;
	counters["image_capacity_capped"] = false;
	counters["image_capacity_timed_out"] = false;
	counters["image_capacity_max_held"] = 0;
	return result("OK");
}

void ARCoreExtras::cancel_image_capacity_probe() {
	ERR_FAIL_COND(session && !usable());
	for (ArImage *image : probe_images_held) {
		api.ArImage_release(image);
	}
	probe_images_held.clear();
	image_capacity_probe = false;
}

bool ARCoreExtras::probe_images(ArFrame *p_frame) {
	if (!usable() || !image_capacity_probe || !p_frame) {
		return false;
	}
	if (++image_probe_ticks > 512) {
		counters["image_capacity_timed_out"] = true;
		cancel_image_capacity_probe();
		return true;
	}
	int64_t timestamp = 0;
	api.ArFrame_getTimestamp(session, p_frame, &timestamp);
	if (timestamp <= 0 || timestamp == image_probe_timestamp) {
		return true;
	}
	image_probe_timestamp = timestamp;
	ArImage *image = nullptr;
	const ArStatus status = api.ArFrame_acquireCameraImage(session, p_frame, &image);
	counters["image_capacity_last_status"] = int(status);
	if (status == AR_SUCCESS && image) {
		probe_images_held.push_back(image);
		counters["image_capacity_max_held"] = int64_t(probe_images_held.size());
		if (probe_images_held.size() < 64) {
			return true;
		}
		counters["image_capacity_capped"] = true;
	} else {
		if (image) {
			api.ArImage_release(image);
		}
		if (status == AR_ERROR_NOT_YET_AVAILABLE) {
			increment("image_capacity_not_yet_available");
			return true;
		}
		counters["image_capacity_exhausted"] = status == AR_ERROR_RESOURCE_EXHAUSTED;
	}
	counters["image_capacity_complete"] = true;
	cancel_image_capacity_probe();
	return true;
}

ArFrame *ARCoreExtras::begin_old_frame_probe(ArFrame *p_current) {
	if (!live_ready() || !p_current || old_probe_frame || image_capacity_probe) {
		return p_current;
	}
	ArFrame *replacement = nullptr;
	api.ArFrame_create(session, &replacement);
	if (!replacement) {
		return p_current;
	}
	old_probe_frame = p_current;
	api.ArFrame_getTimestamp(session, p_current, &old_probe_timestamp);
	const Transform3D camera = latest_packet->get_camera_transform();
	old_probe_ray = { camera.origin, -camera.basis.get_column(2) };
	old_probe_pixel = latest_packet->get_viewport_size() * 0.5;
	counters["old_frame_probe_complete"] = false;
	return replacement;
}

void ARCoreExtras::probe_old_frame(ArFrame *p_current) {
	ArHitResultList *list = nullptr;
	api.ArHitResultList_create(session, &list);
	const float origin[3] = { old_probe_ray.origin.x, old_probe_ray.origin.y, old_probe_ray.origin.z };
	const float direction[3] = { old_probe_ray.direction.x, old_probe_ray.direction.y, old_probe_ray.direction.z };
	int32_t count = 0;
	api.ArFrame_hitTestRay(session, old_probe_frame, origin, direction, list);
	api.ArHitResultList_getSize(session, list, &count);
	counters["old_frame_hit_test_ray_count"] = count;
	api.ArFrame_hitTestRay(session, p_current, origin, direction, list);
	api.ArHitResultList_getSize(session, list, &count);
	counters["current_frame_hit_test_ray_count"] = count;
	api.ArFrame_hitTest(session, old_probe_frame, old_probe_pixel.x, old_probe_pixel.y, list);
	api.ArHitResultList_getSize(session, list, &count);
	counters["old_frame_hit_test_count"] = count;
	api.ArFrame_hitTest(session, p_current, old_probe_pixel.x, old_probe_pixel.y, list);
	api.ArHitResultList_getSize(session, list, &count);
	counters["current_frame_hit_test_count"] = count;
	counters["old_frame_probe_complete"] = true;
	api.ArHitResultList_destroy(list);
	api.ArFrame_destroy(old_probe_frame);
	old_probe_frame = nullptr;
}

void ARCoreExtras::probe_repeated_frame(ArFrame *p_frame, int64_t p_timestamp) {
	ArTrackableList *list = nullptr;
	api.ArTrackableList_create(session, &list);
	api.ArFrame_getUpdatedTrackables(session, p_frame, AR_TRACKABLE_PLANE, list);
	int32_t count = 0;
	api.ArTrackableList_getSize(session, list, &count);
	api.ArTrackableList_destroy(list);
	counters["repeated_frame_updated_plane_count"] = count;
	ArImage *image = nullptr;
	const ArStatus status = api.ArFrame_acquireCameraImage(session, p_frame, &image);
	counters["repeated_frame_image_status"] = int(status);
	if (status == AR_SUCCESS && image) {
		int64_t timestamp = 0;
		api.ArImage_getTimestamp(session, image, &timestamp);
		counters["repeated_frame_image_timestamp_ns"] = timestamp;
		counters["repeated_frame_image_matches_frame"] = timestamp == p_timestamp;
	}
	if (image) {
		api.ArImage_release(image);
	}
	increment("repeated_frame_probe_count");
}

}
