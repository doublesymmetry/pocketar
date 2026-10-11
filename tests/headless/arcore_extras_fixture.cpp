#include "platform/android/arcore_extras.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <array>
#include <cstring>
#include <vector>

using namespace godot;

struct ArSession_ {};
struct ArFrame_ { int64_t timestamp = 0; };
struct ArPose_ { std::array<float, 7> raw = { 0, 0, 0, 1, 0, 0, 0 }; };
struct ArTrackable_ { ArTrackableType type = AR_TRACKABLE_PLANE; ArTrackingState state = AR_TRACKING_STATE_TRACKING; };
struct ArPlane_ : ArTrackable_ { ArPose_ pose; ArPlane *parent = nullptr; };
struct ArHitResult_ { ArPose_ pose; ArPlane *plane = nullptr; };
struct ArHitResultList_ { ArPlane *plane = nullptr; };
struct ArTrackDataList_ {};
struct ArRecordingConfig_ {};
struct ArTrack_ {};

static int pose_refs, list_refs, hit_refs, plane_refs, config_refs, track_refs;
static int recording_calls, illegal_recordings;
static bool recording;
static bool auto_stop;
static ArPlane *query_plane;
static ArFrame *native_frame;
static std::vector<uint8_t> recorded_payload;

static void install_api() {
	ARCoreApi &api = arcore_api();
	api.ArPose_create = [](const ArSession *, const float *p_raw, ArPose **r_pose) {
		*r_pose = new ArPose_;
		if (p_raw) std::memcpy((*r_pose)->raw.data(), p_raw, sizeof(float) * 7);
		pose_refs++;
	};
	api.ArPose_destroy = [](ArPose *p_pose) { delete p_pose; pose_refs--; };
	api.ArPose_getPoseRaw = [](const ArSession *, const ArPose *p_pose, float *r_raw) { std::memcpy(r_raw, p_pose->raw.data(), sizeof(float) * 7); };
	api.ArTrackDataList_create = [](const ArSession *, ArTrackDataList **r_list) { *r_list = new ArTrackDataList_; list_refs++; };
	api.ArTrackDataList_destroy = [](ArTrackDataList *p_list) { delete p_list; list_refs--; };
	api.ArFrame_getTimestamp = [](const ArSession *, const ArFrame *p_frame, int64_t *r_timestamp) { *r_timestamp = p_frame->timestamp; };
	api.ArSession_getRecordingStatus = [](ArSession *, ArRecordingStatus *r_status) { *r_status = recording ? AR_RECORDING_OK : AR_RECORDING_NONE; };
	api.ArSession_getPlaybackStatus = [](ArSession *, ArPlaybackStatus *r_status) { *r_status = AR_PLAYBACK_NONE; };
	api.ArTrackable_getTrackingState = [](const ArSession *, const ArTrackable *p_trackable, ArTrackingState *r_state) { *r_state = p_trackable->state; };
	api.ArTrackable_getType = [](const ArSession *, const ArTrackable *p_trackable, ArTrackableType *r_type) { *r_type = p_trackable->type; };
	api.ArPlane_acquireSubsumedBy = [](const ArSession *, const ArPlane *p_plane, ArPlane **r_parent) { *r_parent = p_plane->parent; if (*r_parent) plane_refs++; };
	api.ArTrackable_release = [](ArTrackable *) { plane_refs--; };
	api.ArPlane_getCenterPose = [](const ArSession *, const ArPlane *p_plane, ArPose *r_pose) { *r_pose = p_plane->pose; };
	api.ArPlane_isPoseInPolygon = [](const ArSession *, const ArPlane *, const ArPose *, int32_t *r_inside) { *r_inside = 1; };
	api.ArHitResultList_create = [](const ArSession *, ArHitResultList **r_list) { *r_list = new ArHitResultList_; list_refs++; };
	api.ArHitResultList_destroy = [](ArHitResultList *p_list) { if (p_list->plane) plane_refs--; delete p_list; list_refs--; };
	api.ArFrame_hitTest = [](const ArSession *, const ArFrame *, float, float, ArHitResultList *r_list) { r_list->plane = query_plane; plane_refs++; };
	api.ArFrame_hitTestRay = [](const ArSession *, const ArFrame *, const float *, const float *, ArHitResultList *r_list) { r_list->plane = query_plane; plane_refs++; };
	api.ArHitResultList_getSize = [](const ArSession *, const ArHitResultList *p_list, int32_t *r_size) { *r_size = p_list->plane ? 1 : 0; };
	api.ArHitResult_create = [](const ArSession *, ArHitResult **r_hit) { *r_hit = new ArHitResult_; hit_refs++; };
	api.ArHitResult_destroy = [](ArHitResult *p_hit) { if (p_hit->plane) plane_refs--; delete p_hit; hit_refs--; };
	api.ArHitResultList_getItem = [](const ArSession *, const ArHitResultList *p_list, int32_t, ArHitResult *r_hit) {
		r_hit->plane = p_list->plane; r_hit->pose = p_list->plane->pose; plane_refs++;
	};
	api.ArHitResult_acquireTrackable = [](const ArSession *, const ArHitResult *p_hit, ArTrackable **r_trackable) { *r_trackable = ArAsTrackable(p_hit->plane); plane_refs++; };
	api.ArHitResult_getHitPose = [](const ArSession *, const ArHitResult *p_hit, ArPose *r_pose) { *r_pose = p_hit->pose; };
	api.ArHitResult_getDistance = [](const ArSession *, const ArHitResult *, float *r_distance) { *r_distance = 2; };
	api.ArRecordingConfig_create = [](const ArSession *, ArRecordingConfig **r_config) { *r_config = new ArRecordingConfig_; config_refs++; };
	api.ArRecordingConfig_destroy = [](ArRecordingConfig *p_config) { delete p_config; config_refs--; };
	api.ArRecordingConfig_setMp4DatasetUri = [](const ArSession *, ArRecordingConfig *, const char *) {};
	api.ArRecordingConfig_setAutoStopOnPause = [](const ArSession *, ArRecordingConfig *, int32_t p_enabled) { auto_stop = p_enabled == 1; };
	api.ArTrack_create = [](const ArSession *, ArTrack **r_track) { *r_track = new ArTrack_; track_refs++; };
	api.ArTrack_destroy = [](ArTrack *p_track) { delete p_track; track_refs--; };
	api.ArTrack_setId = [](const ArSession *, ArTrack *, const uint8_t *) {};
	api.ArTrack_setMetadata = [](const ArSession *, ArTrack *, const uint8_t *, size_t) {};
	api.ArTrack_setMimeType = [](const ArSession *, ArTrack *, const char *) {};
	api.ArRecordingConfig_addTrack = [](const ArSession *, ArRecordingConfig *, const ArTrack *) {};
	api.ArSession_startRecording = [](ArSession *, const ArRecordingConfig *) { recording = true; return AR_SUCCESS; };
	api.ArSession_stopRecording = [](ArSession *) { recording = false; return AR_SUCCESS; };
	api.ArFrame_recordTrackData = [](ArSession *, const ArFrame *p_frame, const uint8_t *, const void *p_bytes, size_t p_size) {
		recording_calls++;
		if (p_frame != native_frame) return AR_ERROR_DEADLINE_EXCEEDED;
		if (illegal_recordings-- > 0) return AR_ERROR_ILLEGAL_STATE;
		const auto *bytes = static_cast<const uint8_t *>(p_bytes);
		recorded_payload.assign(bytes, bytes + p_size);
		return AR_SUCCESS;
	};
}

namespace godot {

class ARCoreInterface {
public:
	static Ref<ARFramePacket> packet(int64_t p_generation, int64_t p_frame) {
		Ref<ARFramePacket> value;
		value.instantiate();
		value->session_generation = p_generation;
		value->frame_id = p_frame;
		value->timestamp_ns = p_frame * 100;
		value->tracking_valid = true;
		value->physical_camera_transform.origin = Vector3(0, 2, 0);
		return value;
	}
};

class ARCoreExtrasFixture : public RefCounted {
	GDCLASS(ARCoreExtrasFixture, RefCounted)
	int failures = 0;
	int checks = 0;
	void check(bool p_condition, const String &p_name) {
		checks++;
		if (!p_condition) { failures++; UtilityFunctions::printerr("EXTRAS FAIL ", p_name); }
	}

protected:
	static void _bind_methods() { ClassDB::bind_method(D_METHOD("run"), &ARCoreExtrasFixture::run); }

public:
	int run() {
		install_api();
		ArSession_ session;
		ArPlane_ plane;
		query_plane = &plane;
		ARCoreExtras extras;
		extras.setup(&session, 10);
		ArFrame_ frame{ 200 };
		native_frame = &frame;
		Ref<ARFramePacket> current = ARCoreInterface::packet(10, 4);
		extras.before_update();
		frame.timestamp = 400;
		extras.on_tick(&frame);
		extras.on_frame(&frame, 4, current, { { 7, &plane } });
		Array live = extras.live_hit_test(Vector2(100, 100));
		check(live.size() == 1, "valid native plane hit returned");
		const Dictionary live_hit = live[0];
		check(int64_t(live_hit["frame_id"]) == 4 && int64_t(live_hit["session_generation"]) == 10, "live query uses latest packet identity");
		check(hit_refs == 1 && plane_refs == 1, "native query retains only its result reference");
		const Array ray_hits = extras.live_hit_test_ray(Vector3(0, 2, 0), Vector3(0, -2, 0));
		check(ray_hits.size() == 1 && int64_t(Dictionary(ray_hits[0])["plane_id"]) == 7, "live ray query returns a native plane hit");
		check(extras.live_hit_test_ray(Vector3(0, 2, 0), Vector3()).is_empty(), "live ray query rejects zero direction");
		extras.before_update();
		check(hit_refs == 0 && plane_refs == 0, "next update releases native query references");
		check(extras.live_hit_test(Vector2(100, 100)).is_empty() && extras.live_hit_test_ray(Vector3(0, 2, 0), Vector3(0, -1, 0)).is_empty(), "unpublished update invalidates live queries");
		extras.on_tick(&frame);
		extras.on_repeated_frame(&frame, { { 7, &plane } });
		check(extras.live_hit_test(Vector2(100, 100)).size() == 1, "repeated camera timestamp keeps latest native query usable");
		extras.before_update();
		frame.timestamp = 500;
		extras.on_tick(&frame);
		extras.on_repeated_frame(&frame, { { 7, &plane } });
		check(extras.live_hit_test(Vector2(100, 100)).is_empty(), "dropped new image cannot revive an older packet for live queries");
		current = ARCoreInterface::packet(10, 5);
		extras.on_frame(&frame, 5, current, { { 7, &plane } });
		const Dictionary next_query = extras.live_hit_test(Vector2(100, 100))[0];
		check(int64_t(next_query["query_id"]) != int64_t(live_hit["query_id"]), "old query id remains expired after another packet");
		check(bool(extras.start_recording("file:///private/test.mp4")["ok"]) && auto_stop, "recording stops automatically on pause");
		check(config_refs == 0 && track_refs == 0, "recording configuration references released");
		PackedByteArray payload;
		payload.push_back(1); payload.push_back(2); payload.push_back(3);
		illegal_recordings = 2;
		check(String(extras.record_input(payload)["status"]) == "QUEUED", "illegal recording state queues input");
		payload[0] = 99;
		for (int i = 0; i < 2; i++) {
			extras.before_update();
			frame.timestamp += 100;
			extras.on_tick(&frame);
		}
		check(recording_calls == 3 && recorded_payload == std::vector<uint8_t>({ 1, 2, 3 }), "retry preserves copied payload and writes once");
		extras.on_tick(&frame);
		check(recording_calls == 3, "repeated camera frame does not duplicate input");
		illegal_recordings = 100;
		extras.record_input(payload);
		for (int i = 0; i < 8; i++) { extras.before_update(); frame.timestamp += 100; extras.on_tick(&frame); }
		check(int64_t(extras.diagnostics()["pending_input_count"]) == 0 && int64_t(extras.diagnostics()["record_inputs_failed"]) == 1, "illegal-state retries are bounded and reported");
		extras.record_input(payload);
		recording = false;
		extras.before_update();
		extras.start_recording("file:///private/second.mp4");
		check(int64_t(extras.diagnostics()["pending_input_count"]) == 0 && int64_t(extras.diagnostics()["record_inputs_cancelled"]) == 1, "new recording cannot inherit queued input from prior file");
		extras.record_input(payload);
		extras.before_update();
		extras.close();
		check(int64_t(extras.diagnostics()["record_inputs_cancelled"]) == 2, "session close cancels pending input");
		check(pose_refs == 0 && list_refs == 0 && hit_refs == 0 && plane_refs == 0 && config_refs == 0 && track_refs == 0, "close releases every owned native object");
		ARCoreExtras replacement;
		replacement.setup(&session, 11);
		current = ARCoreInterface::packet(11, 1);
		frame.timestamp = 100;
		replacement.on_tick(&frame);
		replacement.on_frame(&frame, 1, current, { { 7, &plane } });
		const Dictionary fresh_query = replacement.live_hit_test(Vector2(100, 100))[0];
		check(int64_t(fresh_query["query_id"]) != int64_t(live_hit["query_id"]) && int64_t(fresh_query["query_id"]) != int64_t(next_query["query_id"]), "query handles never alias a replacement interface");
		replacement.close();
		check(pose_refs == 0 && list_refs == 0 && hit_refs == 0 && plane_refs == 0, "replacement interface also releases native references");
		UtilityFunctions::print("EXTRAS RESULT ", failures == 0 ? "PASS" : "FAIL", " ", checks, " checks");
		return failures;
	}
};

}

static void initialize_fixture(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
	GDREGISTER_CLASS(ARFramePacket);
	GDREGISTER_CLASS(ARCoreExtrasFixture);
}

extern "C" {
GDExtensionBool GDE_EXPORT arcore_extras_fixture_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init(p_get_proc_address, p_library, r_initialization);
	init.register_initializer(initialize_fixture);
	init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init.init();
}
}
