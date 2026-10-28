#include "platform/android/arcore_interface.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/xr_server.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <android/hardware_buffer.h>
#include <algorithm>

static ARCoreBridgeState bridge;
static int create_calls = 0;
static int projection_calls = 0;
static bool camera_acquired = false;
static int camera_acquire_calls = 0;
struct ArConfig_ {
	ArPlaneFindingMode planes = AR_PLANE_FINDING_MODE_HORIZONTAL;
	ArTextureUpdateMode texture = AR_TEXTURE_UPDATE_MODE_BIND_TO_TEXTURE_EXTERNAL_OES;
	ArUpdateMode update = AR_UPDATE_MODE_BLOCKING;
	ArLightEstimationMode light = AR_LIGHT_ESTIMATION_MODE_AMBIENT_INTENSITY;
	ArFocusMode focus = AR_FOCUS_MODE_FIXED;
};
static ArConfig config, effective_config;
static ArStatus configure_status = AR_SUCCESS;
static int configure_calls = 0, camera_config_calls = 0;

template <typename R, typename... Args>
static R no_op(Args...) { return R(); }

static void mock_session_creation() {
	auto &api = arcore_api();
	api.ArCameraConfigFilter_create = no_op;
	api.ArCameraConfigFilter_setFacingDirection = no_op;
	api.ArCameraConfigFilter_setTargetFps = no_op;
	api.ArCameraConfigFilter_setDepthSensorUsage = no_op;
	api.ArCameraConfigFilter_setStereoCameraUsage = no_op;
	api.ArCameraConfigList_create = no_op;
	api.ArCameraConfig_create = no_op;
	api.ArSession_getSupportedCameraConfigsWithFilter = no_op;
	api.ArCameraConfigList_getItem = no_op;
	api.ArSession_setCameraConfig = [](const ArSession *, const ArCameraConfig *) { ++camera_config_calls; return AR_SUCCESS; };
	api.ArCameraConfig_destroy = no_op;
	api.ArCameraConfigList_destroy = no_op;
	api.ArCameraConfigFilter_destroy = no_op;
	api.ArConfig_create = [](const ArSession *, ArConfig **r_config) { config = ArConfig(); *r_config = &config; };
	api.ArConfig_setTextureUpdateMode = [](const ArSession *, ArConfig *p_config, ArTextureUpdateMode p_mode) { p_config->texture = p_mode; };
	api.ArConfig_setUpdateMode = [](const ArSession *, ArConfig *p_config, ArUpdateMode p_mode) { p_config->update = p_mode; };
	api.ArConfig_setPlaneFindingMode = [](const ArSession *, ArConfig *p_config, ArPlaneFindingMode p_mode) { p_config->planes = p_mode; };
	api.ArConfig_setLightEstimationMode = [](const ArSession *, ArConfig *p_config, ArLightEstimationMode p_mode) { p_config->light = p_mode; };
	api.ArConfig_setFocusMode = [](const ArSession *, ArConfig *p_config, ArFocusMode p_mode) { p_config->focus = p_mode; };
	api.ArSession_configure = [](ArSession *, const ArConfig *p_config) {
		++configure_calls;
		if (configure_status == AR_SUCCESS) effective_config = *p_config;
		return configure_status;
	};
	api.ArSession_getConfig = [](ArSession *, ArConfig *r_config) { *r_config = effective_config; };
	api.ArConfig_getPlaneFindingMode = [](const ArSession *, const ArConfig *p_config, ArPlaneFindingMode *r_mode) { *r_mode = p_config->planes; };
	api.ArConfig_destroy = no_op;
	api.ArSession_pause = no_op;
	api.ArSession_destroy = no_op;
	api.ArFrame_create = no_op;
	api.ArPose_create = no_op;
	api.ArCameraIntrinsics_create = no_op;
	api.ArTrackableList_create = no_op;
	api.ArSession_setDisplayGeometry = no_op;
	api.ArSession_resume = no_op;
	api.ArSession_getCameraConfig = no_op;
	api.ArTrackDataList_create = no_op;
	api.ArSession_create = [](void *, void *, ArSession **r_session) { ++create_calls; *r_session = reinterpret_cast<ArSession *>(1); return AR_SUCCESS; };
	api.ArCameraConfigList_getSize = [](const ArSession *, const ArCameraConfigList *, int32_t *r_count) { *r_count = 1; };
	api.ArCameraConfig_getImageDimensions = [](const ArSession *, const ArCameraConfig *, int32_t *r_width, int32_t *r_height) { *r_width = 640; *r_height = 480; };
	api.ArCameraConfig_getTextureDimensions = api.ArCameraConfig_getImageDimensions;
	api.ArCameraConfig_getFpsRange = [](const ArSession *, const ArCameraConfig *, int32_t *r_min, int32_t *r_max) { *r_min = *r_max = 30; };
}

ARCoreBridgeState arcore_bridge_snapshot() { return bridge; }
void arcore_bridge_set_owner(godot::ARCoreInterface *) {}
ARCoreJNIEnv::ARCoreJNIEnv(JavaVM *p_vm) : vm(p_vm) { env = reinterpret_cast<JNIEnv *>(1); }
ARCoreJNIEnv::~ARCoreJNIEnv() {}

struct TestPlane {
	ArTrackingState state = AR_TRACKING_STATE_TRACKING;
	TestPlane *parent = nullptr;
	float polygon[8] = { -1, -1, 1, -1, 1, 1, -1, 1 };
	int references = 0;
};
static std::vector<TestPlane *> changed_planes;
static int polygon_reads = 0;
struct ArPose_ { float raw[7] = { 0, 0, 0, 1, 0, 0, 0 }; };
struct ArAnchor_ {
	ArTrackingState state = AR_TRACKING_STATE_TRACKING;
	ArPose pose;
	bool detached = false;
};
static ArStatus anchor_status = AR_SUCCESS;
static int anchor_creations = 0, anchor_detaches = 0, anchor_releases = 0, anchor_reads = 0, anchor_pose_reads = 0;
static const ArTrackable *anchor_trackable = nullptr;
static ArAnchor *created_anchor = nullptr;
static int64_t anchor_frame_timestamp = 1000;

static void mock_anchors() {
	auto &api = arcore_api();
	api.ArPose_create = [](const ArSession *, const float *p_raw, ArPose **r_pose) {
		*r_pose = new ArPose;
		if (p_raw) std::copy_n(p_raw, 7, (*r_pose)->raw);
	};
	api.ArPose_destroy = [](ArPose *p_pose) { delete p_pose; };
	api.ArPose_getPoseRaw = [](const ArSession *, const ArPose *p_pose, float *r_raw) { std::copy_n(p_pose->raw, 7, r_raw); };
	api.ArSession_acquireNewAnchor = [](ArSession *, const ArPose *p_pose, ArAnchor **r_anchor) {
		++anchor_creations;
		*r_anchor = nullptr;
		if (anchor_status != AR_SUCCESS) return anchor_status;
		created_anchor = *r_anchor = new ArAnchor;
		(*r_anchor)->pose = *p_pose;
		return AR_SUCCESS;
	};
	api.ArTrackable_acquireNewAnchor = [](ArSession *p_session, ArTrackable *p_trackable, ArPose *p_pose, ArAnchor **r_anchor) {
		anchor_trackable = p_trackable;
		return arcore_api().ArSession_acquireNewAnchor(p_session, p_pose, r_anchor);
	};
	api.ArAnchor_detach = [](ArSession *, ArAnchor *p_anchor) { ++anchor_detaches; p_anchor->detached = true; };
	api.ArAnchor_release = [](ArAnchor *p_anchor) { if (!p_anchor->detached) abort(); ++anchor_releases; delete p_anchor; };
	api.ArAnchor_getTrackingState = [](const ArSession *, const ArAnchor *p_anchor, ArTrackingState *r_state) { ++anchor_reads; *r_state = p_anchor->state; };
	api.ArAnchor_getPose = [](const ArSession *, const ArAnchor *p_anchor, ArPose *r_pose) { ++anchor_pose_reads; *r_pose = p_anchor->pose; };
}

namespace godot {

class FixtureHardwareBufferTexture : public Texture2D {
	GDCLASS(FixtureHardwareBufferTexture, Texture2D)

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("submit_hardware_buffer", "buffer", "color_model", "color_range", "crop", "completion"), &FixtureHardwareBufferTexture::submit_hardware_buffer);
	}

public:
	Callable completion;
	int submit_hardware_buffer(int64_t, int, int, const Rect2i &, const Callable &p_completion) {
		completion = p_completion;
		return OK;
	}
};

class ARCoreSessionFixture : public RefCounted {
	GDCLASS(ARCoreSessionFixture, RefCounted)
	int failures = 0;
	int checks = 0;
	Ref<ARCoreInterface> session;
	Array events;
	Array expected_events;
	bool operation_active = false;
	bool reset_from_reason = false;
	Ref<ARCoreInterface> anchor_callback_session;
	void anchor_added(const StringName &p_name, int) {
		if (anchor_callback_session.is_valid() && String(p_name).begins_with("pocketar/anchor/")) anchor_callback_session->reset();
	}
	void anchor_pose_changed(const Ref<XRPose> &) {
		check(!camera_acquired, "anchor pose callback runs after native camera release");
		anchor_callback_session->reset();
	}
	void anchor_removed(const StringName &p_name, int) {
		if (String(p_name).begins_with("pocketar/anchor/")) anchor_callback_session->_uninitialize();
	}
	void check(bool p_condition, const String &p_name) {
		++checks;
		if (!p_condition) { ++failures; UtilityFunctions::printerr("SESSION FAIL ", p_name); }
	}
	void record_event(const String &p_name, int64_t p_value = 0) {
		check(!operation_active && !camera_acquired, p_name + String(" notification waits for the native operation to return"));
		Array event;
		event.append(p_name); event.append(p_value);
		events.append(event);
	}
	void expect_event(const String &p_name, int64_t p_value = 0) {
		Array event;
		event.append(p_name); event.append(p_value);
		expected_events.append(event);
	}
	void state_changed(int p_state) { record_event("state", p_state); }
	void generation_changed(int64_t p_generation) { record_event("generation", p_generation); }
	void reason_changed(int p_reason) {
		record_event("reason", p_reason);
		if (p_reason == PocketAR::EXCESSIVE_MOTION && !operation_active && !camera_acquired) {
			reset_from_reason = true;
			const int64_t previous_generation = session->get_session_generation();
			operation_active = true;
			session->reset();
			operation_active = false;
			check(session->get_session_generation() != previous_generation, "tracking callback can reset after camera release");
			expect_event("reason", PocketAR::NONE);
			expect_event("generation", session->get_session_generation());
			expect_event("state", PocketAR::CHECKING);
		}
	}
	void planes_changed() { record_event("planes"); }
	void check_gpu_completion_lifetime() {
		if (!ClassDB::class_has_integer_constant("ExternalTexture", "COMPLETION_COMPLETED")) return;
		static AHardwareBuffer_Desc buffer{ 640, 480, 1, AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420 };
		arcore_api().ArFrame_getHardwareBuffer = [](const ArSession *, const ArFrame *, void **r_buffer) { *r_buffer = &buffer; return AR_SUCCESS; };
		Ref<FixtureHardwareBufferTexture> texture;
		texture.instantiate();
		Ref<ARCoreInterface> gpu;
		gpu.instantiate();
		gpu->owner_thread = std::this_thread::get_id();
		gpu->initialized = true;
		gpu->head.instantiate();
		gpu->session = reinterpret_cast<ArSession *>(1);
		gpu->gpu_texture = texture;
		gpu->gpu_size = Vector2i(640, 480);
		gpu->submit_gpu_camera();
		const Callable completion = texture->completion;
		check(completion.is_valid(), "GPU submission supplies a completion");
		const int completed = ClassDB::class_get_integer_constant("ExternalTexture", "COMPLETION_COMPLETED");
		completion.call(completed);
		check(gpu->gpu_active, "completion reaches the live interface");
		gpu->session = nullptr;
		gpu->_uninitialize();
		gpu.unref();
		check(completion.is_valid(), "completion validity does not depend on the freed interface");
		completion.call(completed);
	}
	void check_activity() {
		Ref<ARCoreInterface> camera;
		camera.instantiate();
		camera->owner_thread = std::this_thread::get_id();
		camera->head.instantiate();
		camera->set_suspended(true);
		camera->initialized = true;
		mock_session_creation();
		bridge.resumed = true;
		bridge.state = PocketAR::CHECKING;
		camera->bridge_changed();
		check(camera->state == PocketAR::PAUSED && camera->session == nullptr, "explicit startup suspension reports paused while setup is pending");
		camera->reset();
		check(camera->state == PocketAR::PAUSED && camera->session == nullptr, "suspended reset preserves paused state while setup is pending");
		bridge.state = PocketAR::READY;
		camera->bridge_changed();
		check(camera->state == PocketAR::PAUSED && camera->session == nullptr, "suspended ready state does not create a session");
		camera->set_suspended(false);
		check(camera->running, "explicit resume starts a ready native session");
		ArSession *native = camera->session;
		const int64_t generation = camera->generation;
		camera->set_suspended(true);
		camera->application_pause();
		camera->application_resume();
		camera->bridge_changed();
		check(!camera->running && camera->state == PocketAR::PAUSED, "lifecycle and bridge resume cannot override explicit suspension");
		camera->application_pause();
		camera->set_suspended(false);
		check(!camera->running, "explicit resume waits for application resume");
		camera->application_resume();
		check(camera->running && camera->session == native && camera->generation == generation, "native resume retains session and generation");
		camera->set_camera_config_index(0);
		const int64_t explicit_generation = camera->generation;
		camera->set_camera_quality(PocketAR::LOWEST);
		check(camera->generation == explicit_generation && camera->camera_quality == PocketAR::LOWEST, "explicit camera index overrides quality without a reset");
		camera->set_camera_config_index(1);
		check(camera->state == PocketAR::UNSUPPORTED && camera->session == nullptr, "invalid camera index releases the failed session");
		const int64_t failed_generation = camera->generation;
		camera->set_camera_config_index(-1);
		check(camera->running && camera->session != nullptr && camera->generation != failed_generation, "automatic selection recovers after an invalid camera index");
		camera->set_suspended(true);
		camera->reset();
		check(camera->session == nullptr && camera->state == PocketAR::PAUSED, "native reset while suspended defers recreation");
		camera->set_suspended(false);
		check(camera->running, "resume after suspended reset recreates the session");
		camera->_uninitialize();
	}
	void check_incremental_planes() {
		Ref<ARCoreInterface> camera;
		camera.instantiate();
		camera->session = reinterpret_cast<ArSession *>(1);
		auto &api = arcore_api();
		api.ArFrame_getUpdatedTrackables = no_op;
		api.ArTrackableList_getSize = [](const ArSession *, const ArTrackableList *, int32_t *r_count) { *r_count = changed_planes.size(); };
		api.ArTrackableList_acquireItem = [](const ArSession *, const ArTrackableList *, int32_t p_index, ArTrackable **r_plane) { ++changed_planes[p_index]->references; *r_plane = reinterpret_cast<ArTrackable *>(changed_planes[p_index]); };
		api.ArTrackable_release = [](ArTrackable *p_plane) { --reinterpret_cast<TestPlane *>(p_plane)->references; };
		api.ArTrackable_getTrackingState = [](const ArSession *, const ArTrackable *p_plane, ArTrackingState *r_state) { *r_state = reinterpret_cast<const TestPlane *>(p_plane)->state; };
		api.ArPlane_getType = [](const ArSession *, const ArPlane *, ArPlaneType *r_type) { *r_type = AR_PLANE_HORIZONTAL_UPWARD_FACING; };
		api.ArPlane_getCenterPose = no_op;
		api.ArPose_getPoseRaw = [](const ArSession *, const ArPose *, float *r_pose) { std::fill_n(r_pose, 7, 0); r_pose[3] = 1; };
		api.ArPlane_getExtentX = [](const ArSession *, const ArPlane *, float *r_extent) { *r_extent = 2; };
		api.ArPlane_getExtentZ = api.ArPlane_getExtentX;
		api.ArPlane_getPolygonSize = [](const ArSession *, const ArPlane *, int32_t *r_size) { *r_size = 8; };
		api.ArPlane_getPolygon = [](const ArSession *, const ArPlane *p_plane, float *r_polygon) { ++polygon_reads; std::copy_n(reinterpret_cast<const TestPlane *>(p_plane)->polygon, 8, r_polygon); };
		api.ArPlane_acquireSubsumedBy = [](const ArSession *, const ArPlane *p_plane, ArPlane **r_parent) {
			TestPlane *parent = reinterpret_cast<const TestPlane *>(p_plane)->parent;
			if (parent) ++parent->references;
			*r_parent = reinterpret_cast<ArPlane *>(parent);
		};
		TestPlane first, parent;
		changed_planes = { &first };
		camera->refresh_planes();
		Ref<ARFramePacket> retained;
		retained.instantiate();
		camera->copy_planes(**retained);
		check(retained->planes.size() == 1 && polygon_reads == 1, "native discovery copies one plane");
		changed_planes.clear();
		camera->refresh_planes();
		check(polygon_reads == 1, "unchanged native planes never read polygons");
		first.state = AR_TRACKING_STATE_PAUSED;
		changed_planes = { &first };
		camera->refresh_planes();
		check(camera->planes.begin()->second.tracker->get_tracking_state() == ARPlaneTracker::PAUSED, "tracking transition reaches tracker before image publication");
		check(camera->planes.begin()->second.tracker->get_snapshot().polygon.ptr() == retained->planes[0].polygon.ptr(), "tracking-only update shares the original polygon allocation");
		check(int64_t(camera->diagnostics["plane_geometry_rebuilds"]) == 1, "tracking-only change reuses polygon storage");
		first.state = AR_TRACKING_STATE_TRACKING;
		first.parent = &parent;
		first.polygon[0] = -2;
		camera->refresh_planes();
		Ref<ARFramePacket> merged;
		merged.instantiate();
		camera->copy_planes(**merged);
		check(merged->planes.size() == 2 && camera->planes.at(reinterpret_cast<ArPlane *>(&first)).tracker->get_subsumed_by() == camera->planes.at(reinterpret_cast<ArPlane *>(&parent)).id, "subsumption discovers and copies an unlisted parent");
		check(retained->planes[0].polygon[0] == Vector2(-1, -1) && retained->planes[0].tracking_state == ARPlaneTracker::TRACKING, "native changes preserve retained packet geometry and state");
		first.state = AR_TRACKING_STATE_STOPPED;
		camera->refresh_planes();
		check(camera->planes.size() == 1 && first.references == 0, "stopped native plane releases ownership and tracker");
		camera->clear_planes();
		check(parent.references == 0, "reset releases cached parent ownership");
		camera->session = nullptr;
		changed_planes.clear();
		api.ArTrackable_release = no_op;
	}
	void check_frame_diagnostics() {
		Ref<ARCoreInterface> camera;
		camera.instantiate();
		camera->owner_thread = std::this_thread::get_id();
		camera->initialized = true;
		camera->head.instantiate();
		camera->texture_slots.resize(3);
		camera->session = reinterpret_cast<ArSession *>(1);
		camera->frame = reinterpret_cast<ArFrame *>(1);
		camera->extras.setup(camera->session, camera->generation);
		auto &api = arcore_api();
		api.ArFrame_getUpdatedTrackables = no_op;
		api.ArTrackableList_getSize = [](const ArSession *, const ArTrackableList *, int32_t *r_count) { *r_count = 0; };
		api.ArFrame_acquireCameraImage = [](ArSession *, ArFrame *, ArImage **r_image) { *r_image = reinterpret_cast<ArImage *>(1); return AR_SUCCESS; };
		api.ArImage_getTimestamp = [](const ArSession *, const ArImage *, int64_t *r_timestamp) { *r_timestamp = 1000; };
		api.ArImage_getWidth = [](const ArSession *, const ArImage *, int32_t *r_value) { *r_value = 2; };
		api.ArImage_getHeight = api.ArImage_getWidth;
		api.ArImage_getNumberOfPlanes = [](const ArSession *, const ArImage *, int32_t *r_count) { *r_count = 3; };
		api.ArImage_getFormat = [](const ArSession *, const ArImage *, ArImageFormat *r_format) { *r_format = AR_IMAGE_FORMAT_YUV_420_888; };
		api.ArImage_getPlaneData = [](const ArSession *, const ArImage *, int32_t, const uint8_t **r_data, int32_t *r_length) {
			static const uint8_t pixels[] = { 128, 128, 128, 128 };
			*r_data = pixels; *r_length = 4;
		};
		api.ArImage_getPlaneRowStride = [](const ArSession *, const ArImage *, int32_t p_plane, int32_t *r_stride) { *r_stride = p_plane == 0 ? 2 : 1; };
		api.ArImage_getPlanePixelStride = [](const ArSession *, const ArImage *, int32_t, int32_t *r_stride) { *r_stride = 1; };
		api.ArImage_release = no_op;
		projection_calls = 0;
		camera->build_packet(1000);
		const Ref<ARFramePacket> first = camera->current_packet;
		check(first.is_valid() && first->y_texture.is_valid(), "ordinary frames still publish camera textures");
		check(!bool(camera->get_diagnostics().get("diagnostics_enabled", true)), "frame diagnostics default off");
		check(projection_calls == 1 && !camera->diagnostics.has("image_plane_layout") && !camera->diagnostics.has("mean_y"), "ordinary frames skip oracle projections, stride dictionaries and luma sampling");
		camera->call("set_diagnostics_enabled", true);
		camera->diagnostics["projection_form_valid"] = true;
		camera->build_packet(1000);
		check(bool(camera->get_diagnostics().get("diagnostics_enabled", false)), "frame diagnostics explicitly enabled");
		check(projection_calls == 4 && bool(camera->diagnostics["projection_form_valid"]) && int64_t(camera->diagnostics["projection_form_samples"]) == 1, "enabled projection oracle passes");
		check(Array(camera->diagnostics.get("image_plane_layout", Array())).size() == 3 && double(camera->diagnostics.get("mean_y", 0)) == 128, "enabled diagnostics sample CPU image layout and luma");
		check(first != camera->current_packet && first->projection == camera->current_packet->projection, "diagnostics preserve retained packet calibration");
		camera->call("set_diagnostics_enabled", false);
		camera->build_packet(1000);
		check(projection_calls == 5 && int64_t(camera->diagnostics["projection_form_samples"]) == 1, "disabling diagnostics stops oracle collection");
		api.ArImage_getFormat = [](const ArSession *, const ArImage *, ArImageFormat *r_format) { *r_format = AR_IMAGE_FORMAT_INVALID; };
		camera->build_packet(1000);
		check(camera->get_session_state() == PocketAR::FAILED, "image format guard stays active with diagnostics off");
		camera->session = nullptr;
		camera->frame = nullptr;
		camera->_uninitialize();
	}
	void check_anchors() {
		mock_session_creation();
		mock_anchors();
		Ref<ARCoreInterface> camera;
		camera.instantiate();
		camera->owner_thread = std::this_thread::get_id();
		camera->initialized = camera->running = true;
		camera->generation = 900;
		camera->head.instantiate();
		camera->session = reinterpret_cast<ArSession *>(1);
		camera->frame = reinterpret_cast<ArFrame *>(1);
		camera->state = PocketAR::TRACKING;
		arcore_api().ArPose_create(camera->session, nullptr, &camera->scratch_pose);
		camera->extras.setup(camera->session, camera->generation);
		arcore_api().ArSession_update = no_op;
		arcore_api().ArFrame_destroy = no_op;
		arcore_api().ArFrame_getTimestamp = [](const ArSession *, const ArFrame *, int64_t *r_timestamp) { *r_timestamp = anchor_frame_timestamp; };
		arcore_api().ArSession_getRecordingStatus = [](ArSession *, ArRecordingStatus *r_status) { *r_status = AR_RECORDING_NONE; };
		arcore_api().ArSession_getPlaybackStatus = [](ArSession *, ArPlaybackStatus *r_status) { *r_status = AR_PLAYBACK_NONE; };
		arcore_api().ArTrackableList_getSize = [](const ArSession *, const ArTrackableList *, int32_t *r_size) { *r_size = 0; };
		arcore_api().ArCamera_getTrackingState = [](const ArSession *, const ArCamera *, ArTrackingState *r_tracking) { *r_tracking = AR_TRACKING_STATE_TRACKING; };
		arcore_api().ArCamera_getTrackingFailureReason = [](const ArSession *, const ArCamera *, ArTrackingFailureReason *r_reason) { *r_reason = AR_TRACKING_FAILURE_REASON_NONE; };
		arcore_api().ArFrame_acquireCameraImage = [](ArSession *, ArFrame *, ArImage **) { return AR_ERROR_NOT_YET_AVAILABLE; };
		bridge.state = PocketAR::READY;
		bridge.resumed = true;
		camera->current_packet.instantiate();
		camera->current_packet->session_generation = 900;
		camera->current_packet->tracking_valid = true;
		camera->copy_anchors(**camera->current_packet);
		check(camera->create_anchor(Transform3D(Basis().scaled(Vector3(2, 1, 1)), Vector3())).is_null() && anchor_creations == 0, "invalid pose never reaches native creation");
		camera->refresh_anchor_states();
		check(anchor_reads == 0, "empty registry does no native anchor work");
		const Transform3D placement(Basis(), Vector3(1, 2, -3));
		anchor_status = AR_ERROR_RESOURCE_EXHAUSTED;
		check(camera->create_anchor(placement).is_null() && anchor_releases == 0, "native refusal creates no owner");
		anchor_status = AR_SUCCESS;
		const Ref<ARAnchorTracker> anchor = camera->create_anchor(placement);
		check(anchor.is_valid() && anchor->get_anchor_id() == 1 && anchor->get_tracking_state() == ARAnchorTracker::PAUSED, "accepted anchor starts pending and refusal consumes no id");
		if (anchor.is_null()) return;
		ArAnchor *native = created_anchor;
		check(native->pose.raw[4] == 1 && native->pose.raw[5] == 2 && native->pose.raw[6] == -3, "native creation receives the requested world pose");
		camera->refresh_anchor_states();
		Ref<ARFramePacket> packet;
		packet.instantiate();
		packet->session_generation = 900;
		packet->tracking_valid = true;
		packet->frame_id = 1;
		camera->copy_anchors(**packet);
		camera->current_packet = packet;
		check(anchor->get_tracking_state() == ARAnchorTracker::PAUSED, "capturing a packet does not apply an anchor");
		camera->mark_applied(packet);
		check(anchor->get_tracking_state() == ARAnchorTracker::TRACKING && anchor->get_transform() == placement, "application publishes copied native pose");
		native->pose.raw[4] = 4;
		camera->last_timestamp = anchor_frame_timestamp;
		const int pose_reads = anchor_pose_reads;
		camera->_process();
		check(camera->current_packet == packet && anchor_pose_reads == pose_reads && anchor->get_transform().origin.x == 1, "repeated camera timestamp neither republishes nor samples an anchor pose");
		anchor_frame_timestamp += 1000;
		camera->_process();
		check(camera->current_packet == packet && anchor_pose_reads == pose_reads && anchor->get_transform().origin.x == 1, "dropped camera image neither publishes nor samples an anchor pose");
		const int attempted = anchor_creations;
		check(camera->create_anchor(placement).is_null() && anchor_creations == attempted, "dropped acquisition blocks creation from the old current packet");
		native->state = AR_TRACKING_STATE_PAUSED;
		camera->refresh_anchor_states();
		check(anchor->get_tracking_state() == ARAnchorTracker::PAUSED && anchor->get_transform() == placement, "native pause immediately invalidates and freezes the last pose");
		camera->mark_applied(packet);
		check(anchor->get_tracking_state() == ARAnchorTracker::PAUSED, "old packet cannot undo independent native pause");
		native->state = AR_TRACKING_STATE_TRACKING;
		camera->refresh_anchor_states();
		camera->gpu_active = true;
		anchor_frame_timestamp += 1000;
		camera->_process();
		const Ref<ARFramePacket> recovered = camera->current_packet;
		check(recovered != packet && recovered->anchors.size() == 1 && anchor->get_tracking_state() == ARAnchorTracker::PAUSED, "published camera packet samples anchor correction before application");
		camera->mark_applied(recovered);
		camera->mark_presented();
		check(anchor->get_transform().origin.x == 4 && packet->anchors[0].transform.origin.x == 1, "recovery applies fresh pose without changing retained values");
		camera->application_pause();
		check(anchor->get_tracking_state() == ARAnchorTracker::PAUSED && camera->owns_anchor(anchor), "application pause keeps native identity");
		camera->mark_applied(recovered);
		check(anchor->get_tracking_state() == ARAnchorTracker::PAUSED, "pre-suspension packet cannot restore tracking");
		camera->application_suspended = false;
		camera->running = true;
		camera->state = PocketAR::TRACKING;
		Ref<ARRaycastHit> hit;
		hit.instantiate();
		hit->session_generation = 900;
		hit->frame_id = 2;
		hit->plane_id = 1;
		hit->pose = placement;
		check(camera->create_anchor_from_hit(hit).is_null(), "pre-pause presented hit stays stale after resume");
		native->state = AR_TRACKING_STATE_STOPPED;
		camera->refresh_anchor_states();
		check(anchor->get_tracking_state() == ARAnchorTracker::STOPPED && anchor_detaches == 1 && anchor_releases == 1, "native terminal loss detaches and releases without camera publication");
		camera->remove_anchor(anchor);
		check(anchor_releases == 1, "removing a stopped anchor is idempotent");
		const int reads = anchor_reads;
		const int camera_reads = camera_acquire_calls;
		camera->_process();
		check(anchor_reads == reads && camera_acquire_calls == camera_reads, "retired registry returns to zero extra native work");
		Ref<ARFramePacket> fresh;
		fresh.instantiate();
		fresh->session_generation = 900;
		fresh->tracking_valid = true;
		fresh->frame_id = 3;
		camera->copy_anchors(**fresh);
		camera->current_packet = fresh;
		camera->mark_applied(fresh);
		camera->mark_presented();
		hit->frame_id = 3;
		TestPlane source, parent;
		source.parent = &parent;
		arcore_api().ArPlane_acquireSubsumedBy = [](const ArSession *, const ArPlane *p_plane, ArPlane **r_parent) {
			TestPlane *parent = reinterpret_cast<const TestPlane *>(p_plane)->parent;
			if (parent) ++parent->references;
			*r_parent = reinterpret_cast<ArPlane *>(parent);
		};
		arcore_api().ArTrackable_release = [](ArTrackable *p_plane) { --reinterpret_cast<TestPlane *>(p_plane)->references; };
		arcore_api().ArTrackable_getTrackingState = [](const ArSession *, const ArTrackable *p_plane, ArTrackingState *r_state) { *r_state = reinterpret_cast<const TestPlane *>(p_plane)->state; };
		Ref<ARPlaneTracker> plane_tracker;
		plane_tracker.instantiate();
		camera->planes.emplace(reinterpret_cast<ArPlane *>(&source), ARCoreInterface::PlaneEntry{1, plane_tracker});
		source.state = AR_TRACKING_STATE_STOPPED;
		check(camera->create_anchor_from_hit(hit).is_null() && parent.references == 0, "stopped source plane cannot be revived through a tracking parent");
		source.state = AR_TRACKING_STATE_TRACKING;
		parent.state = AR_TRACKING_STATE_PAUSED;
		check(camera->create_anchor_from_hit(hit).is_null() && parent.references == 0, "nontracking subsumption replacement refuses placement and balances its reference");
		parent.state = AR_TRACKING_STATE_TRACKING;
		anchor_status = AR_ERROR_RESOURCE_EXHAUSTED;
		check(camera->create_anchor_from_hit(hit).is_null() && parent.references == 0, "attached native refusal releases the acquired plane parent");
		anchor_status = AR_SUCCESS;
		const Ref<ARAnchorTracker> attached = camera->create_anchor_from_hit(hit);
		check(attached.is_valid() && attached->get_anchor_id() == 2 && attached->get_source_plane_id() == 1 && anchor_trackable == reinterpret_cast<ArTrackable *>(&parent) && parent.references == 0, "hit creation follows native subsumption and releases the acquired parent");
		created_anchor->state = AR_TRACKING_STATE_PAUSED;
		camera->refresh_anchor_states();
		const Ref<ARAnchorTracker> other = camera->create_anchor_from_hit(hit);
		check(other.is_null(), "independent anchor pause invalidates pre-pause hit samples");
		camera->copy_anchors(**fresh);
		if (other.is_valid()) camera->remove_anchor(other);
		camera->planes.clear();
		check(camera->create_anchor_from_hit(hit).is_null(), "missing current plane rejects an otherwise fresh hit");
		camera->remove_anchor(attached);
#ifdef DEBUG_ENABLED
		const int64_t debug_id = camera->set_debug_plane(placement, Vector2(2, 2));
		camera->copy_planes(**fresh);
		hit->plane_id = debug_id;
		anchor_trackable = nullptr;
		const Ref<ARAnchorTracker> debug_anchor = camera->create_anchor_from_hit(hit);
		check(debug_anchor.is_valid() && debug_anchor->get_source_plane_id() == debug_id && anchor_trackable == nullptr, "negative debug hit creates a native free anchor");
		camera->clear_debug_plane();
		check(camera->owns_anchor(debug_anchor), "removing debug geometry keeps its native anchor");
		camera->remove_anchor(debug_anchor);
#endif
		const int retired = anchor_releases;
		const Ref<ARAnchorTracker> pending = camera->create_anchor(placement);
		bridge.state = PocketAR::CHECKING;
		camera->head->set_pose("default", Transform3D(), Vector3(), Vector3(), XRPose::XR_TRACKING_CONFIDENCE_HIGH);
		anchor_callback_session = camera;
		camera->head->connect("pose_lost_tracking", callable_mp(this, &ARCoreSessionFixture::anchor_pose_changed));
		camera->reset();
		camera->head->disconnect("pose_lost_tracking", callable_mp(this, &ARCoreSessionFixture::anchor_pose_changed));
		anchor_callback_session.unref();
		check(pending->get_tracking_state() == ARAnchorTracker::STOPPED && anchor_detaches == retired + 1 && anchor_releases == retired + 1, "reset stops and releases pending native anchors");
		check(int64_t(camera->diagnostics["reset_count"]) == 1, "head invalidation callback coalesces a nested reset");
		camera->session = reinterpret_cast<ArSession *>(1);
		camera->running = true;
		camera->state = PocketAR::TRACKING;
		camera->current_packet.instantiate();
		camera->current_packet->session_generation = camera->generation;
		camera->current_packet->tracking_valid = true;
		camera->copy_anchors(**camera->current_packet);
		arcore_api().ArPose_create(camera->session, nullptr, &camera->scratch_pose);
		const Ref<ARAnchorTracker> pose_anchor = camera->create_anchor(placement);
		anchor_callback_session = camera;
		pose_anchor->connect("pose_changed", callable_mp(this, &ARCoreSessionFixture::anchor_pose_changed));
		Ref<ARFramePacket> pose_packet;
		pose_packet.instantiate();
		pose_packet->session_generation = camera->generation;
		pose_packet->tracking_valid = true;
		camera->copy_anchors(**pose_packet);
		camera->current_packet = pose_packet;
		camera->mark_applied(pose_packet);
		check(pose_anchor->get_tracking_state() == ARAnchorTracker::STOPPED && camera->native_anchors.empty() && camera->current_packet.is_null(), "pose callback reset cannot resume packet application");
		pose_anchor->disconnect("pose_changed", callable_mp(this, &ARCoreSessionFixture::anchor_pose_changed));
		camera->session = reinterpret_cast<ArSession *>(1);
		camera->running = true;
		camera->state = PocketAR::TRACKING;
		camera->current_packet.instantiate();
		camera->current_packet->session_generation = camera->generation;
		camera->current_packet->tracking_valid = true;
		camera->copy_anchors(**camera->current_packet);
		anchor_callback_session = camera;
		XRServer::get_singleton()->connect("tracker_added", callable_mp(this, &ARCoreSessionFixture::anchor_added));
		const Ref<ARAnchorTracker> reset_during_add = camera->create_anchor(placement);
		XRServer::get_singleton()->disconnect("tracker_added", callable_mp(this, &ARCoreSessionFixture::anchor_added));
		anchor_callback_session.unref();
		check(reset_during_add.is_null() && camera->native_anchors.empty() && anchor_releases == retired + 3, "engine registration callback can reset without leaking or reviving native anchor");
		camera->session = reinterpret_cast<ArSession *>(1);
		camera->running = true;
		camera->state = PocketAR::TRACKING;
		camera->current_packet.instantiate();
		camera->current_packet->session_generation = camera->generation;
		camera->current_packet->tracking_valid = true;
		camera->copy_anchors(**camera->current_packet);
		const Ref<ARAnchorTracker> shutdown_anchor = camera->create_anchor(placement);
		anchor_callback_session = camera;
		XRServer::get_singleton()->connect("tracker_removed", callable_mp(this, &ARCoreSessionFixture::anchor_removed));
		camera->reset();
		XRServer::get_singleton()->disconnect("tracker_removed", callable_mp(this, &ARCoreSessionFixture::anchor_removed));
		anchor_callback_session.unref();
		check(!camera->initialized && !camera->resetting && camera->session == nullptr && camera->head.is_null() && shutdown_anchor->get_tracking_state() == ARAnchorTracker::STOPPED, "anchor callback can uninitialize during reset without recreating a session");
	}

#ifdef DEBUG_ENABLED
	Ref<ARCoreInterface> debug_session;
	int debug_notifications = 0;
	void debug_planes_changed() { ++debug_notifications; }
	void check_debug_plane() {
		debug_session.instantiate();
		const Ref<ARCoreInterface> debug = debug_session;
		debug->owner_thread = std::this_thread::get_id();
		debug->initialized = true;
		debug->head.instantiate();
		debug->session = reinterpret_cast<ArSession *>(1);
		debug->frame = reinterpret_cast<ArFrame *>(1);
		debug->gpu_active = true;
		debug->call("set_diagnostics_enabled", true);
		debug->extras.setup(debug->session, debug->generation);
		debug->connect("planes_changed", callable_mp(this, &ARCoreSessionFixture::debug_planes_changed));
		auto &api = arcore_api();
		api.ArCamera_getTrackingState = [](const ArSession *, const ArCamera *, ArTrackingState *r_state) { *r_state = AR_TRACKING_STATE_TRACKING; };
		api.ArCamera_getTrackingFailureReason = [](const ArSession *, const ArCamera *, ArTrackingFailureReason *r_reason) { *r_reason = AR_TRACKING_FAILURE_REASON_NONE; };
		api.ArFrame_getUpdatedTrackables = no_op;
		api.ArTrackableList_getSize = [](const ArSession *, const ArTrackableList *, int32_t *r_count) { *r_count = 1; };
		api.ArTrackableList_acquireItem = [](const ArSession *, const ArTrackableList *, int32_t, ArTrackable **r_plane) { *r_plane = reinterpret_cast<ArTrackable *>(7); };
		api.ArTrackable_getTrackingState = [](const ArSession *, const ArTrackable *, ArTrackingState *r_state) { *r_state = AR_TRACKING_STATE_TRACKING; };
		api.ArPlane_getType = [](const ArSession *, const ArPlane *, ArPlaneType *r_type) { *r_type = AR_PLANE_HORIZONTAL_UPWARD_FACING; };
		api.ArPlane_getCenterPose = no_op;
		api.ArPlane_getExtentX = [](const ArSession *, const ArPlane *, float *r_extent) { *r_extent = 2; };
		api.ArPlane_getExtentZ = api.ArPlane_getExtentX;
		api.ArPlane_getPolygonSize = [](const ArSession *, const ArPlane *, int32_t *r_size) { *r_size = 8; };
		api.ArPlane_getPolygon = [](const ArSession *, const ArPlane *, float *r_vertices) {
			const float vertices[] = { -1, -1, 1, -1, 1, 1, -1, 1 };
			std::copy_n(vertices, 8, r_vertices);
		};
		api.ArPlane_acquireSubsumedBy = [](const ArSession *, const ArPlane *, ArPlane **r_parent) { *r_parent = nullptr; };
		const int64_t id = debug->set_debug_plane(Transform3D(Basis(), Vector3(0, -1, -3)), Vector2(2, 4));
		debug->refresh_planes();
		debug->build_packet(1000);
		const Ref<ARFramePacket> first = debug->current_packet;
		check(first.is_valid() && first->planes.size() == 2, "debug packet merges after native planes");
		if (first.is_valid() && first->planes.size() == 2) {
			check(first->planes[0].plane_id > 0 && !first->planes[0].debug && first->planes[1].plane_id == id && first->planes[1].debug, "debug and native identities remain distinct");
		}
		check(int64_t(debug->diagnostics.get("plane_reprojection_samples", 0)) == 2, "debug vertices do not enter native reprojection counts");
		check(debug->planes.size() == 1 && int64_t(debug->get_diagnostics()["plane_count"]) == 1, "debug plane does not enter native map or count");
		check(debug->extras.native_planes.size() == 1 && !debug->extras.native_planes.count(id), "debug plane does not enter extras native query map");
		check(debug->extras.recording_status == AR_RECORDING_NONE && debug->extras.playback_inputs.is_empty(), "debug injection does not start recording or inject playback inputs");
		debug->call("set_diagnostics_enabled", false);
		debug->build_packet(2000);
		check(first->planes == debug->current_packet->planes, "unchanged merged native packet geometry");
		check(int64_t(debug->diagnostics["plane_reprojection_samples"]) == 2, "ordinary GPU frames copy planes without reprojecting vertices");
		api.ArCamera_getTrackingState = [](const ArSession *, const ArCamera *, ArTrackingState *r_state) { *r_state = AR_TRACKING_STATE_PAUSED; };
		api.ArTrackableList_getSize = [](const ArSession *, const ArTrackableList *, int32_t *r_count) { *r_count = 1; };
		api.ArTrackable_getTrackingState = [](const ArSession *, const ArTrackable *, ArTrackingState *r_state) { *r_state = AR_TRACKING_STATE_STOPPED; };
		debug->refresh_planes();
		debug->build_packet(3000);
		check(debug->current_packet->planes.back().tracking_state == ARPlaneTracker::PAUSED && first->planes.back().tracking_state == ARPlaneTracker::TRACKING, "native tracking loss pauses only new debug snapshots");
		debug->mark_applied(debug->current_packet);
		debug->mark_presented();
		check(debug->presented_packet.is_valid(), "native pause starts with a presented debug packet");
		debug->set_state(PocketAR::PAUSED);
		check(debug->presented_packet.is_null(), "native pause drops presentation before post-draw");
		debug->mark_presented();
		check(debug->presented_packet.is_null(), "native pause cannot restore presented debug packet");
		debug->session = nullptr;
		debug->frame = nullptr;
		check(debug->planes.empty() && debug->current_packet->planes.size() == 1 && debug->current_packet->planes[0].debug, "reset notification has only a debug plane to remove");
		debug->reset();
		check(!debug->debug_plane.active() && debug->planes.empty() && debug->current_packet.is_null(), "pending native reset removes debug tracker synchronously");
		check(XRServer::get_singleton()->get_tracker("pocketar/plane/0/" + String::num_int64(id)).is_null(), "pending native reset unregisters debug tracker");
		debug->set_debug_plane(Transform3D(), Vector2(2, 4));
		Ref<ARFramePacket> packet;
		packet.instantiate();
		packet->tracking_valid = true;
		debug->copy_planes(**packet);
		check(debug->planes.empty() && packet->planes.size() == 1 && packet->planes[0].debug, "teardown notification has only a debug plane to remove");
		debug->_uninitialize();
		check(!debug->debug_plane.active() && debug->planes.empty(), "pending native uninitialize clears both tracker sources");
	}
#endif

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("run"), &ARCoreSessionFixture::run);
		ClassDB::bind_method(D_METHOD("finish"), &ARCoreSessionFixture::finish);
	}

public:
	void run() {
		operation_active = true;
		session.instantiate();
		check(session->has_method("create_anchor") && session->has_method("create_anchor_from_hit") && session->has_method("remove_anchor") && session->has_method("owns_anchor") && session->has_signal("anchors_changed"), "native anchor API is registered");
		session->owner_thread = std::this_thread::get_id();
		session->initialized = true;
		session->head.instantiate();
		session->diagnostics["texture_upload_region_size_px"] = 1024;
		bridge.context = reinterpret_cast<jobject>(1);
		bridge.resumed = false;
		const bool signals = session->has_signal("session_state_changed") && session->has_signal("session_generation_changed") && session->has_signal("tracking_reason_changed") && session->has_signal("planes_changed");
		check(signals, "native session signals are registered");
		if (signals) {
			session->connect("session_state_changed", callable_mp(this, &ARCoreSessionFixture::state_changed));
			session->connect("session_generation_changed", callable_mp(this, &ARCoreSessionFixture::generation_changed));
			session->connect("tracking_reason_changed", callable_mp(this, &ARCoreSessionFixture::reason_changed));
			session->connect("planes_changed", callable_mp(this, &ARCoreSessionFixture::planes_changed));
		}
		mock_session_creation();
		session->call_deferred("emit_signal", "session_state_changed", int(PocketAR::CHECKING));
		expect_event("state", PocketAR::CHECKING);
		bridge.state = PocketAR::READY;
		bridge.state_history.push_back(PocketAR::READY);
		session->bridge_changed();
		check(session->get_session_state() == PocketAR::READY, "cold-start status publishes READY before nativeResume");
		check(create_calls == 0, "status waits for nativeResume before creating the session");
		expect_event("state", PocketAR::READY);
		bridge.resumed = true;
		session->bridge_changed();
		check(create_calls == 1 && session->get_session_state() == PocketAR::INITIALIZING, "cold-start resume creates and initializes the session");
		expect_event("state", PocketAR::INITIALIZING);
		check(effective_config.planes == AR_PLANE_FINDING_MODE_DISABLED, "default config disables plane discovery");
		const int64_t mode_generation = session->get_session_generation();
		ArSession *native_session = session->session;
		ArConfig *native_config = session->config;
		for (auto mode : { PocketAR::PLANE_FINDING_HORIZONTAL, PocketAR::PLANE_FINDING_DISABLED, PocketAR::PLANE_FINDING_HORIZONTAL_AND_VERTICAL }) {
			session->set_plane_finding_mode(mode);
			check(session->get_plane_finding_mode() == mode && int(session->get_diagnostics()["effective_plane_finding_mode"]) == int(mode), "mode applies and reads back from native config");
			check(session->session == native_session && session->config == native_config && session->get_session_generation() == mode_generation && session->running, "mode keeps session, config, generation and running state");
			check(effective_config.texture == AR_TEXTURE_UPDATE_MODE_EXPOSE_HARDWARE_BUFFER && effective_config.update == AR_UPDATE_MODE_LATEST_CAMERA_IMAGE && effective_config.light == AR_LIGHT_ESTIMATION_MODE_DISABLED && effective_config.focus == AR_FOCUS_MODE_AUTO, "mode preserves other config fields");
		}
		check(create_calls == 1 && camera_config_calls == 1 && configure_calls == 4, "runtime modes configure without recreating or selecting a camera");
		session->set_plane_finding_mode(PocketAR::PLANE_FINDING_HORIZONTAL_AND_VERTICAL);
		check(configure_calls == 4, "unchanged mode does not reconfigure");
		configure_status = AR_ERROR_UNSUPPORTED_CONFIGURATION;
		session->set_plane_finding_mode(PocketAR::PLANE_FINDING_DISABLED);
		check(session->get_plane_finding_mode() == PocketAR::PLANE_FINDING_HORIZONTAL_AND_VERTICAL && int(session->get_diagnostics()["effective_plane_finding_mode"]) == AR_PLANE_FINDING_MODE_HORIZONTAL_AND_VERTICAL && session->running, "failed configuration preserves applied mode and running session");
		configure_status = AR_SUCCESS;
		session->set_plane_finding_mode(PocketAR::PLANE_FINDING_DISABLED);
		session->application_pause();
		expect_event("state", PocketAR::PAUSED);
		check(session->get_plane_finding_mode() == PocketAR::PLANE_FINDING_DISABLED, "pause preserves disabled mode");
		session->application_resume();
		expect_event("state", PocketAR::INITIALIZING);
		check(effective_config.planes == AR_PLANE_FINDING_MODE_DISABLED, "resume preserves disabled mode");
		session->reset();
		expect_event("state", PocketAR::PAUSED);
		expect_event("generation", session->get_session_generation());
		expect_event("state", PocketAR::READY);
		expect_event("state", PocketAR::INITIALIZING);
		check(create_calls == 2 && effective_config.planes == AR_PLANE_FINDING_MODE_DISABLED && session->get_plane_finding_mode() == PocketAR::PLANE_FINDING_DISABLED, "reset recreates with disabled mode");
		session->extras.close();
		session->session = nullptr;
		session->config = nullptr;
		session->running = false;
		create_calls = 0;
		arcore_api().ArSession_create = [](void *, void *, ArSession **) { ++create_calls; return AR_ERROR_FATAL; };
		bridge.resumed = false;
		for (int pending : { PocketAR::CHECKING, PocketAR::NEEDS_INSTALL, PocketAR::INSTALLING, PocketAR::PERMISSION_DENIED, PocketAR::UNSUPPORTED, PocketAR::FAILED }) {
			bridge.state = pending;
			session->state = PocketAR::TRACKING;
			session->reason = PocketAR::CAMERA_UNAVAILABLE;
			const int64_t old_generation = session->get_session_generation();
			session->reset();
			check(int(session->get_session_state()) == pending, "reset immediately preserves bridge state " + String::num_int64(pending));
			check(create_calls == 0, "pending reset does not create a native session");
			check(session->get_session_generation() != old_generation, "pending reset invalidates generation");
			expect_event("reason", PocketAR::NONE);
			expect_event("generation", session->get_session_generation());
			expect_event("state", pending);
			session->reset();
			expect_event("generation", session->get_session_generation());
		}
		bridge.state = PocketAR::READY;
		for (bool app_suspended : { false, true }) {
			bridge.resumed = app_suspended;
			session->application_suspended = app_suspended;
			session->state = PocketAR::TRACKING;
			session->reset();
			check(session->get_session_state() == PocketAR::PAUSED, "reset stays paused until both lifecycle gates permit running");
			check(create_calls == 0, "paused reset does not create a native session");
			expect_event("generation", session->get_session_generation());
			expect_event("state", PocketAR::PAUSED);
		}
		bridge.resumed = true;
		session->application_resume();
		expect_event("state", PocketAR::READY);
		expect_event("state", PocketAR::FAILED);
		check(create_calls == 1, "resume after paused reset attempts native creation without a new availability event");
		session->state = PocketAR::CHECKING;
		bridge.resumed = false;
		bridge.state_history.push_back(PocketAR::READY);
		session->bridge_changed();
		check(session->get_session_state() == PocketAR::READY, "availability status precedes resume without false PAUSED");
		expect_event("state", PocketAR::READY);
		bridge.resumed = true;
		session->bridge_changed();
		check(create_calls == 2, "resuming after a deferred availability event still attempts native creation");
		expect_event("state", PocketAR::FAILED);
		Ref<ARPlaneTracker> tracker;
		tracker.instantiate();
		ARPlaneSnapshot plane;
		plane.plane_id = 23;
		plane.session_generation = session->get_session_generation();
		tracker->update(plane);
		XRServer::get_singleton()->add_tracker(tracker);
		session->planes.emplace(reinterpret_cast<ArPlane *>(1), ARCoreInterface::PlaneEntry{ plane.plane_id, tracker });
		arcore_api().ArTrackable_release = [](ArTrackable *) {};
		session->clear_planes();
		check(session->planes.empty(), "clearing copied native planes removes their trackers immediately");
		expect_event("planes");
		session->clear_planes();
		bridge.state = PocketAR::CHECKING;
		bridge.resumed = false;
		session->session = reinterpret_cast<ArSession *>(1);
		arcore_api().ArFrame_acquireCamera = [](const ArSession *, const ArFrame *, ArCamera **r_camera) { ++camera_acquire_calls; camera_acquired = true; *r_camera = reinterpret_cast<ArCamera *>(1); };
		arcore_api().ArCamera_getTrackingState = [](const ArSession *, const ArCamera *, ArTrackingState *r_tracking) { *r_tracking = AR_TRACKING_STATE_PAUSED; };
		arcore_api().ArCamera_getTrackingFailureReason = [](const ArSession *, const ArCamera *, ArTrackingFailureReason *r_reason) { *r_reason = AR_TRACKING_FAILURE_REASON_EXCESSIVE_MOTION; };
		arcore_api().ArCamera_getDisplayOrientedPose = [](const ArSession *, const ArCamera *, ArPose *) {};
		arcore_api().ArCamera_getPose = arcore_api().ArCamera_getDisplayOrientedPose;
		arcore_api().ArPose_getPoseRaw = [](const ArSession *, const ArPose *, float *r_pose) { std::fill_n(r_pose, 7, 0.0f); r_pose[3] = 1.0f; };
		arcore_api().ArCamera_getProjectionMatrix = [](const ArSession *, const ArCamera *, float p_near, float p_far, float *r_matrix) {
			++projection_calls;
			std::fill_n(r_matrix, 16, 0.0f);
			r_matrix[0] = r_matrix[5] = 1.0f; r_matrix[11] = -1.0f;
			r_matrix[10] = -(p_far + p_near) / (p_far - p_near);
			r_matrix[14] = -2.0f * p_far * p_near / (p_far - p_near);
		};
		arcore_api().ArCamera_getImageIntrinsics = [](const ArSession *, const ArCamera *, ArCameraIntrinsics *) {};
		arcore_api().ArCameraIntrinsics_getFocalLength = [](const ArSession *, const ArCameraIntrinsics *, float *r_x, float *r_y) { *r_x = *r_y = 1.0f; };
		arcore_api().ArCameraIntrinsics_getPrincipalPoint = arcore_api().ArCameraIntrinsics_getFocalLength;
		arcore_api().ArCameraIntrinsics_getImageDimensions = [](const ArSession *, const ArCameraIntrinsics *, int32_t *r_width, int32_t *r_height) { *r_width = *r_height = 2; };
		arcore_api().ArCamera_release = [](ArCamera *) { camera_acquired = false; };
		arcore_api().ArFrame_transformCoordinates2d = [](const ArSession *, const ArFrame *, ArCoordinates2dType, int32_t p_count, const float *p_values, ArCoordinates2dType, float *r_values) { std::copy_n(p_values, p_count * 2, r_values); };
		arcore_api().ArFrame_acquireCameraImage = [](ArSession *, ArFrame *, ArImage **) { return AR_ERROR_NOT_YET_AVAILABLE; };
		session->build_packet(1000000);
		check(session->get_tracking_reason() == PocketAR::EXCESSIVE_MOTION && session->get_session_state() == PocketAR::INITIALIZING, "frame updates tracking state and reason immediately");
		check(!camera_acquired, "frame releases its acquired native camera before returning");
		expect_event("reason", PocketAR::EXCESSIVE_MOTION);
		expect_event("state", PocketAR::INITIALIZING);
		session->session = nullptr;
		check(events.is_empty(), "reset, lifecycle, plane and frame notifications are all deferred");
		operation_active = false;
		check_gpu_completion_lifetime();
		check_incremental_planes();
		check_frame_diagnostics();
#ifdef DEBUG_ENABLED
		check_debug_plane();
#endif
		check_anchors();
	}

	int finish() {
#ifdef DEBUG_ENABLED
		check(debug_notifications == 4, "debug merged notifications cover add, tracking loss, reset and teardown only");
#endif
		check(reset_from_reason, "deferred tracking notification permits a game reset");
		check(events == expected_events, "deferred notifications preserve payload order and suppress unchanged values");
		session->_uninitialize();
		check(session->get_session_state() == PocketAR::PAUSED, "uninitializing a pending session reports PAUSED");
		check_activity();
		UtilityFunctions::print("SESSION RESULT ", failures == 0 ? "PASS" : "FAIL", " ", checks, " checks");
		return failures;
	}
};

}

static void initialize_fixture(godot::ModuleInitializationLevel p_level) {
	using namespace godot;
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
	GDREGISTER_CLASS(ARFramePacket);
	GDREGISTER_CLASS(ARPlaneTracker);
	GDREGISTER_CLASS(ARAnchorTracker);
	GDREGISTER_CLASS(ARRaycastHit);
	GDREGISTER_CLASS(ARCoreInterface);
	GDREGISTER_CLASS(FixtureHardwareBufferTexture);
	GDREGISTER_CLASS(ARCoreSessionFixture);
}

extern "C" GDExtensionBool GDE_EXPORT arcore_session_fixture_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init(p_get_proc_address, p_library, r_initialization);
	init.register_initializer(initialize_fixture);
	init.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
	return init.init();
}
