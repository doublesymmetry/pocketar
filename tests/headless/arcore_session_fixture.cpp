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
		arcore_api().ArFrame_acquireCamera = [](const ArSession *, const ArFrame *, ArCamera **r_camera) { camera_acquired = true; *r_camera = reinterpret_cast<ArCamera *>(1); };
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
	}

	int finish() {
#ifdef DEBUG_ENABLED
		check(debug_notifications == 4, "debug merged notifications cover add, tracking loss, reset and teardown only");
#endif
		check(reset_from_reason, "deferred tracking notification permits a game reset");
		check(events.slice(0, 3) == expected_events.slice(0, 3), "cold-start facade sequence is exactly CHECKING, READY, INITIALIZING with no PAUSED");
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
