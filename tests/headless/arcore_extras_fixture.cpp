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
struct ArImage_ {};
struct ArPose_ { std::array<float, 7> raw = { 0, 0, 0, 1, 0, 0, 0 }; };
struct ArTrackable_ { ArTrackableType type = AR_TRACKABLE_PLANE; ArTrackingState state = AR_TRACKING_STATE_TRACKING; };
struct ArPlane_ : ArTrackable_ { ArPose_ pose; ArPlane *parent = nullptr; };
struct ArAnchor_ { ArPose_ pose; };
struct ArHitResult_ { ArPose_ pose; ArPlane *plane = nullptr; };
struct ArHitResultList_ { ArPlane *plane = nullptr; };
struct ArTrackDataList_ {};
struct ArTrackableList_ {};
struct ArRecordingConfig_ {};
struct ArTrack_ {};

static int pose_refs, list_refs, hit_refs, plane_refs, anchor_refs, config_refs, track_refs, image_refs;
static int anchor_calls, recording_calls, illegal_recordings;
static int image_calls;
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
	api.ArFrame_acquireCameraImage = [](ArSession *, ArFrame *, ArImage **r_image) { *r_image = new ArImage_; image_refs++; image_calls++; return AR_SUCCESS; };
	api.ArImage_release = [](ArImage *p_image) { delete p_image; image_refs--; };
	api.ArImage_getTimestamp = [](const ArSession *, const ArImage *, int64_t *r_timestamp) { *r_timestamp = native_frame->timestamp; };
	api.ArTrackableList_create = [](const ArSession *, ArTrackableList **r_list) { *r_list = new ArTrackableList_; list_refs++; };
	api.ArTrackableList_destroy = [](ArTrackableList *p_list) { delete p_list; list_refs--; };
	api.ArFrame_getUpdatedTrackables = [](const ArSession *, const ArFrame *, ArTrackableType, ArTrackableList *) {};
	api.ArTrackableList_getSize = [](const ArSession *, const ArTrackableList *, int32_t *r_size) { *r_size = 0; };
	api.ArSession_getRecordingStatus = [](ArSession *, ArRecordingStatus *r_status) { *r_status = recording ? AR_RECORDING_OK : AR_RECORDING_NONE; };
	api.ArSession_getPlaybackStatus = [](ArSession *, ArPlaybackStatus *r_status) { *r_status = AR_PLAYBACK_NONE; };
	api.ArTrackable_getTrackingState = [](const ArSession *, const ArTrackable *p_trackable, ArTrackingState *r_state) { *r_state = p_trackable->state; };
	api.ArTrackable_getType = [](const ArSession *, const ArTrackable *p_trackable, ArTrackableType *r_type) { *r_type = p_trackable->type; };
	api.ArPlane_acquireSubsumedBy = [](const ArSession *, const ArPlane *p_plane, ArPlane **r_parent) { *r_parent = p_plane->parent; if (*r_parent) plane_refs++; };
	api.ArTrackable_release = [](ArTrackable *) { plane_refs--; };
	api.ArPlane_getCenterPose = [](const ArSession *, const ArPlane *p_plane, ArPose *r_pose) { *r_pose = p_plane->pose; };
	api.ArPlane_isPoseInPolygon = [](const ArSession *, const ArPlane *, const ArPose *, int32_t *r_inside) { *r_inside = 1; };
	api.ArTrackable_acquireNewAnchor = [](ArSession *, ArTrackable *, ArPose *p_pose, ArAnchor **r_anchor) {
		*r_anchor = new ArAnchor_{ *p_pose }; anchor_refs++; anchor_calls++; return AR_SUCCESS;
	};
	api.ArSession_acquireNewAnchor = [](ArSession *, const ArPose *p_pose, ArAnchor **r_anchor) {
		*r_anchor = new ArAnchor_{ *p_pose }; anchor_refs++; anchor_calls++; return AR_SUCCESS;
	};
	api.ArAnchor_getTrackingState = [](const ArSession *, const ArAnchor *, ArTrackingState *r_state) { *r_state = AR_TRACKING_STATE_TRACKING; };
	api.ArAnchor_getPose = [](const ArSession *, const ArAnchor *p_anchor, ArPose *r_pose) { *r_pose = p_anchor->pose; };
	api.ArAnchor_detach = [](ArSession *, ArAnchor *) {};
	api.ArAnchor_release = [](ArAnchor *p_anchor) { delete p_anchor; anchor_refs--; };
	api.ArHitResultList_create = [](const ArSession *, ArHitResultList **r_list) { *r_list = new ArHitResultList_; list_refs++; };
	api.ArHitResultList_destroy = [](ArHitResultList *p_list) { if (p_list->plane) plane_refs--; delete p_list; list_refs--; };
	api.ArFrame_hitTest = [](const ArSession *, const ArFrame *, float, float, ArHitResultList *r_list) { r_list->plane = query_plane; plane_refs++; };
	api.ArHitResultList_getSize = [](const ArSession *, const ArHitResultList *p_list, int32_t *r_size) { *r_size = p_list->plane ? 1 : 0; };
	api.ArHitResult_create = [](const ArSession *, ArHitResult **r_hit) { *r_hit = new ArHitResult_; hit_refs++; };
	api.ArHitResult_destroy = [](ArHitResult *p_hit) { if (p_hit->plane) plane_refs--; delete p_hit; hit_refs--; };
	api.ArHitResultList_getItem = [](const ArSession *, const ArHitResultList *p_list, int32_t, ArHitResult *r_hit) {
		r_hit->plane = p_list->plane; r_hit->pose = p_list->plane->pose; plane_refs++;
	};
	api.ArHitResult_acquireTrackable = [](const ArSession *, const ArHitResult *p_hit, ArTrackable **r_trackable) { *r_trackable = ArAsTrackable(p_hit->plane); plane_refs++; };
	api.ArHitResult_getHitPose = [](const ArSession *, const ArHitResult *p_hit, ArPose *r_pose) { *r_pose = p_hit->pose; };
	api.ArHitResult_getDistance = [](const ArSession *, const ArHitResult *, float *r_distance) { *r_distance = 2; };
	api.ArHitResult_acquireNewAnchor = [](ArSession *, ArHitResult *p_hit, ArAnchor **r_anchor) {
		*r_anchor = new ArAnchor_{ p_hit->pose }; anchor_refs++; anchor_calls++; return AR_SUCCESS;
	};
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
	static Ref<ARFramePacket> packet(int64_t p_generation, int64_t p_frame, const Vector3 &p_center = Vector3(), const Vector3 &p_camera = Vector3(0, 2, 0), int64_t p_parent = 0, bool p_tracking = true) {
		Ref<ARFramePacket> value;
		value.instantiate();
		value->session_generation = p_generation;
		value->frame_id = p_frame;
		value->timestamp_ns = p_frame * 100;
		value->tracking_valid = p_tracking;
		value->camera_transform.origin = p_camera;
		value->physical_camera_transform.origin = p_camera;
		ARPlaneSnapshot plane;
		plane.plane_id = 7;
		plane.session_generation = p_generation;
		plane.tracking_state = AR_TRACKING_STATE_TRACKING;
		plane.subsumed_by = p_parent;
		plane.transform.origin = p_center;
		for (const Vector2 &vertex : { Vector2(-1, -1), Vector2(1, -1), Vector2(1, 1), Vector2(-1, 1) }) plane.polygon.push_back(vertex);
		value->planes.push_back(plane);
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
		ArPlane_ parent;
		query_plane = &plane;
		ARCoreExtras extras;
		extras.setup(&session, 10);
		ArFrame_ frame{ 200 };
		native_frame = &frame;
		const Ref<ARFramePacket> old = ARCoreInterface::packet(10, 1);
		const Ref<ARRaycastHit> hit = raycast_planes(old, { Vector3(0, 2, 0), Vector3(0, -1, 0) })[0];
		Ref<ARFramePacket> current = ARCoreInterface::packet(10, 2, Vector3(3, 1, 0));
		plane.pose.raw[4] = 3;
		plane.pose.raw[5] = 1;
		extras.on_tick(&frame);
		extras.on_frame(&frame, 2, current, { { 7, &plane } });
		Dictionary anchor = extras.create_anchor(old, hit, false);
		check(bool(anchor["ok"]) && Transform3D(anchor["pose"]).origin.is_equal_approx(Vector3(3, 1, 0)), "retained packet hit rebases onto moved native plane");
		check(anchor_refs == 1 && pose_refs == 1, "temporary anchor pose released");
		Ref<ARFramePacket> stale = ARCoreInterface::packet(9, 1);
		Ref<ARRaycastHit> stale_hit = raycast_planes(stale, { Vector3(0, 2, 0), Vector3(0, -1, 0) })[0];
		const int before_stale = anchor_calls;
		Dictionary rejected = extras.create_anchor(stale, stale_hit, false);
		check(String(rejected["status"]) == "STALE_GENERATION" && anchor_calls == before_stale, "stale generation cannot create native anchor");
		plane.parent = &parent;
		current = ARCoreInterface::packet(10, 3, Vector3(3, 1, 0), Vector3(1, 3, 0), 8);
		extras.before_update();
		frame.timestamp = 300;
		extras.on_tick(&frame);
		extras.on_frame(&frame, 3, current, { { 7, &plane } });
		check(String(extras.create_anchor(old, hit, false)["status"]) == "STALE_TARGET", "subsumed target requires explicit fallback");
		Dictionary fallback = extras.create_anchor(old, hit, true);
		check(bool(fallback["ok"]) && bool(fallback["fallback"]) && Transform3D(fallback["pose"]).origin.is_equal_approx(Vector3(1, 1, 0)), "fallback preserves camera-relative hit");
		plane.parent = nullptr;
		plane.pose = ArPose_();
		current = ARCoreInterface::packet(10, 4);
		extras.before_update();
		frame.timestamp = 400;
		extras.on_tick(&frame);
		extras.on_frame(&frame, 4, current, { { 7, &plane } });
		Array live = extras.live_hit_test(Vector2(100, 100));
		check(live.size() == 1, "valid native plane hit returned");
		const Dictionary live_hit = live[0];
		check(int64_t(live_hit["frame_id"]) == 4 && int64_t(live_hit["session_generation"]) == 10, "live query uses latest packet identity");
		check(hit_refs == 1 && plane_refs == 1, "native query retains only its result reference");
		check(bool(extras.create_live_anchor(live_hit["query_id"])["ok"]), "live query can anchor before update");
		extras.before_update();
		check(hit_refs == 0 && plane_refs == 0, "next update releases native query references");
		check(!bool(extras.create_live_anchor(live_hit["query_id"])["ok"]), "unpublished update invalidates live queries");
		extras.on_tick(&frame);
		extras.on_repeated_frame(&frame, { { 7, &plane } });
		check(extras.live_hit_test(Vector2(100, 100)).size() == 1, "repeated camera timestamp keeps latest native query usable");
		extras.set_frame_probes_enabled(true);
		const int images_before = image_calls;
		extras.on_tick(&frame, false);
		check(image_calls == images_before, "GPU repeated frames do not acquire CPU images without validation");
		extras.on_tick(&frame);
		check(image_calls == images_before + 1 && image_refs == 0, "CPU repeated-frame probe still acquires and releases its image");
		extras.set_frame_probes_enabled(false);
		extras.before_update();
		frame.timestamp = 500;
		extras.on_tick(&frame);
		extras.on_repeated_frame(&frame, { { 7, &plane } });
		check(extras.live_hit_test(Vector2(100, 100)).is_empty(), "dropped new image cannot revive an older packet for live queries");
		current = ARCoreInterface::packet(10, 5);
		extras.on_frame(&frame, 5, current, { { 7, &plane } });
		check(String(extras.create_live_anchor(live_hit["query_id"])["status"]) == "STALE_QUERY", "old query id remains expired after another packet");
		check(extras.get_anchors().size() == 3, "created anchors remain queryable");
		check(extras.remove_anchor(anchor["anchor_id"]) && anchor_refs == 2, "removal releases a local anchor");
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
		current = ARCoreInterface::packet(10, 20, Vector3(), Vector3(0, 2, 0), 0, false);
		frame.timestamp = 2000;
		extras.on_frame(&frame, 20, current, { { 7, &plane } });
		check(bool(extras.begin_image_capacity_probe()["ok"]), "image capacity probe works before camera tracking");
		check(extras.probe_images(&frame) && image_refs == 1, "capacity probe deliberately holds an acquired image");
		extras.close();
		check(int64_t(extras.diagnostics()["record_inputs_cancelled"]) == 2, "session close cancels pending input");
		check(pose_refs == 0 && list_refs == 0 && hit_refs == 0 && plane_refs == 0 && anchor_refs == 0 && config_refs == 0 && track_refs == 0 && image_refs == 0, "close releases every owned native object");
		ARCoreExtras replacement;
		replacement.setup(&session, 11);
		current = ARCoreInterface::packet(11, 1);
		frame.timestamp = 100;
		replacement.on_tick(&frame);
		replacement.on_frame(&frame, 1, current, { { 7, &plane } });
		const Dictionary fresh_query = replacement.live_hit_test(Vector2(100, 100))[0];
		check(!bool(replacement.create_live_anchor(live_hit["query_id"])["ok"]), "query handles never alias a replacement interface");
		const Dictionary fresh_anchor = replacement.create_live_anchor(fresh_query["query_id"]);
		check(bool(fresh_anchor["ok"]) && !replacement.remove_anchor(anchor["anchor_id"]), "old anchor handle cannot remove replacement interface anchor");
		check(replacement.get_anchors().size() == 1, "replacement anchor survives stale handle");
		replacement.close();
		check(pose_refs == 0 && list_refs == 0 && hit_refs == 0 && plane_refs == 0 && anchor_refs == 0, "replacement interface also releases native references");
		UtilityFunctions::print("EXTRAS RESULT ", failures == 0 ? "PASS" : "FAIL", " ", checks, " checks");
		return failures;
	}
};

}

static void initialize_fixture(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
	GDREGISTER_CLASS(ARFramePacket);
	GDREGISTER_CLASS(ARRaycastHit);
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
