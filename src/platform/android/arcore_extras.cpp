#include "arcore_extras.h"
#include "core/ar_ids.h"
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <algorithm>
#include <cstring>

namespace godot {

static constexpr uint8_t INPUT_TRACK_UUID[16] = { 0x61, 0xa9, 0x77, 0x25, 0x8b, 0xe4, 0x4e, 0x8f, 0xa1, 0x96, 0x6a, 0xf3, 0x8c, 0xa1, 0x07, 0x35 };
static constexpr const char *INPUT_TRACK_METADATA = "{\"format\":\"pocketar-input\",\"version\":1,\"payload\":\"application-owned bytes\"}";
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
	return read_arcore_pose(api, session, scratch_pose);
}

void ARCoreExtras::setup(ArSession *p_session, int64_t p_generation) {
	ERR_FAIL_COND(session && !usable());
	close();
	session = p_session;
	owner_thread = std::this_thread::get_id();
	generation = p_generation;
	frame_id = 0;
	last_track_timestamp = 0;
	last_playback_input_timestamp = 0;
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

void ARCoreExtras::on_tick(ArFrame *p_frame) {
	ERR_FAIL_COND(!usable() || !p_frame);
	recording_frame = p_frame;
	api.ArSession_getRecordingStatus(session, &recording_status);
	api.ArSession_getPlaybackStatus(session, &playback_status);
	int64_t timestamp = 0;
	api.ArFrame_getTimestamp(session, p_frame, &timestamp);
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
	value["pending_input_count"] = int64_t(pending_inputs.size());
	value["point_cloud_enabled"] = point_cloud_enabled;
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
	api.ArTrack_setMimeType(session, track, "application/vnd.pocketar.input");
	api.ArRecordingConfig_addTrack(session, config, track);
	const ArStatus status = api.ArSession_startRecording(session, config);
	api.ArTrack_destroy(track);
	api.ArRecordingConfig_destroy(config);
	if (status == AR_SUCCESS) {
		increment("record_inputs_cancelled", pending_inputs.size());
		pending_inputs.clear();
	}
	api.ArSession_getRecordingStatus(session, &recording_status);
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
	if (status == AR_SUCCESS) {
		before_update();
		last_track_timestamp = 0;
		last_playback_input_timestamp = 0;
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
			break;
		}
		if (status == AR_SUCCESS) {
			increment("record_inputs_written");
		} else {
			increment("record_inputs_failed");
		}
		pending_inputs.pop_front();
	}
}

void ARCoreExtras::read_playback_inputs(int64_t p_timestamp) {
	api.ArFrame_getUpdatedTrackData(session, recording_frame, INPUT_TRACK_UUID, track_data_list);
	int32_t count = 0;
	api.ArTrackDataList_getSize(session, track_data_list, &count);
	// ARCore 1.56 playback hands the same track data back on later frames, although its header
	// says each item is attached to one frame; deliver each recorded frame's inputs once.
	int64_t newest = last_playback_input_timestamp;
	for (int32_t i = 0; i < count; i++) {
		ArTrackData *data = nullptr;
		api.ArTrackDataList_acquireItem(session, track_data_list, i, &data);
		const uint8_t *bytes = nullptr;
		int32_t size = 0;
		int64_t timestamp = 0;
		api.ArTrackData_getData(session, data, &bytes, &size);
		api.ArTrackData_getFrameTimestamp(session, data, &timestamp);
		if (timestamp <= last_playback_input_timestamp) {
			increment("playback_inputs_repeated");
			api.ArTrackData_release(data);
			continue;
		}
		newest = std::max(newest, timestamp);
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
	last_playback_input_timestamp = newest;
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

}
