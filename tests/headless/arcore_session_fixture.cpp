#include "platform/android/arcore_interface.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/xr_server.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>

static ARCoreBridgeState bridge;
static int create_calls = 0;
static bool camera_acquired = false;

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
	api.ArSession_setCameraConfig = no_op;
	api.ArCameraConfig_destroy = no_op;
	api.ArCameraConfigList_destroy = no_op;
	api.ArCameraConfigFilter_destroy = no_op;
	api.ArConfig_create = no_op;
	api.ArConfig_setTextureUpdateMode = no_op;
	api.ArConfig_setUpdateMode = no_op;
	api.ArConfig_setPlaneFindingMode = no_op;
	api.ArConfig_setLightEstimationMode = no_op;
	api.ArConfig_setFocusMode = no_op;
	api.ArSession_configure = no_op;
	api.ArConfig_destroy = no_op;
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

namespace godot {

void ARCoreInterface::detect_gpu_camera() {}
void ARCoreInterface::set_camera_path(const String &) {}
void ARCoreInterface::set_gpu_validation_enabled(bool) {}
bool ARCoreInterface::submit_gpu_camera() { return false; }
void ARCoreInterface::finish_gpu_validation() {}

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
		session->extras.close();
		session->session = nullptr;
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
	}

	int finish() {
		check(reset_from_reason, "deferred tracking notification permits a game reset");
		check(events.slice(0, 3) == expected_events.slice(0, 3), "cold-start facade sequence is exactly CHECKING, READY, INITIALIZING with no PAUSED");
		check(events == expected_events, "deferred notifications preserve payload order and suppress unchanged values");
		session->_uninitialize();
		check(session->get_session_state() == PocketAR::PAUSED, "uninitializing a pending session reports PAUSED");
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
	GDREGISTER_CLASS(ARCoreSessionFixture);
}

extern "C" GDExtensionBool GDE_EXPORT arcore_session_fixture_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init(p_get_proc_address, p_library, r_initialization);
	init.register_initializer(initialize_fixture);
	init.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
	return init.init();
}
