#include "arcore_interface.h"
#include "core/ar_ids.h"
#include "yuv_repack.h"
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/xr_server.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <android/log.h>
#include <chrono>
#include <cmath>

namespace godot {

#ifdef DEBUG_ENABLED
int64_t ARCoreInterface::set_debug_plane(const Transform3D &p_transform, const Vector2 &p_extents) {
	ERR_FAIL_COND_V(initialized && !on_owner_thread(), 0);
	return debug_plane.set(p_transform, p_extents);
}

void ARCoreInterface::clear_debug_plane() {
	ERR_FAIL_COND(initialized && !on_owner_thread());
	debug_plane.request_clear();
}
#endif

void ARCoreInterface::_bind_methods() {
#ifdef DEBUG_ENABLED
	ClassDB::bind_method(D_METHOD("set_debug_plane", "transform", "extents"), &ARCoreInterface::set_debug_plane);
	ClassDB::bind_method(D_METHOD("clear_debug_plane"), &ARCoreInterface::clear_debug_plane);
#endif
	ClassDB::bind_method(D_METHOD("get_session_state"), &ARCoreInterface::get_session_state);
	ClassDB::bind_method(D_METHOD("get_tracking_reason"), &ARCoreInterface::get_tracking_reason);
	ClassDB::bind_method(D_METHOD("get_session_generation"), &ARCoreInterface::get_session_generation);
	ClassDB::bind_method(D_METHOD("get_diagnostics"), &ARCoreInterface::get_diagnostics);
	ClassDB::bind_method(D_METHOD("reset"), &ARCoreInterface::reset);
	ClassDB::bind_method(D_METHOD("set_plane_finding_mode", "mode"), &ARCoreInterface::set_plane_finding_mode);
	ClassDB::bind_method(D_METHOD("get_plane_finding_mode"), &ARCoreInterface::get_plane_finding_mode);
	ClassDB::bind_method(D_METHOD("set_camera_config_index", "index"), &ARCoreInterface::set_camera_config_index);
	ClassDB::bind_method(D_METHOD("set_camera_path", "path"), &ARCoreInterface::set_camera_path);
	ClassDB::bind_method(D_METHOD("set_gpu_validation_enabled", "enabled"), &ARCoreInterface::set_gpu_validation_enabled);
	ClassDB::bind_method(D_METHOD("set_display_geometry", "size", "turns", "near", "far"), &ARCoreInterface::set_display_geometry);
	ClassDB::bind_method(D_METHOD("get_current_packet"), &ARCoreInterface::get_current_packet);
	ClassDB::bind_method(D_METHOD("get_presented_packet"), &ARCoreInterface::get_presented_packet);
	ClassDB::bind_method(D_METHOD("mark_presented"), &ARCoreInterface::mark_presented);
	ClassDB::bind_method(D_METHOD("mark_applied", "packet"), &ARCoreInterface::mark_applied);
	ClassDB::bind_method(D_METHOD("application_pause"), &ARCoreInterface::application_pause);
	ClassDB::bind_method(D_METHOD("application_resume"), &ARCoreInterface::application_resume);
	ClassDB::bind_method(D_METHOD("get_dataset_diagnostics"), &ARCoreInterface::get_dataset_diagnostics);
	ClassDB::bind_method(D_METHOD("live_hit_test", "view_pixels"), &ARCoreInterface::live_hit_test);
	ClassDB::bind_method(D_METHOD("live_hit_test_ray", "origin", "direction"), &ARCoreInterface::live_hit_test_ray);
	ClassDB::bind_method(D_METHOD("start_recording", "file_uri"), &ARCoreInterface::start_recording);
	ClassDB::bind_method(D_METHOD("stop_recording"), &ARCoreInterface::stop_recording);
	ClassDB::bind_method(D_METHOD("start_playback", "file_uri"), &ARCoreInterface::start_playback);
	ClassDB::bind_method(D_METHOD("record_input", "payload"), &ARCoreInterface::record_input);
	ClassDB::bind_method(D_METHOD("get_playback_inputs"), &ARCoreInterface::get_playback_inputs);
	ClassDB::bind_method(D_METHOD("set_point_cloud_enabled", "enabled"), &ARCoreInterface::set_point_cloud_enabled);
	ADD_SIGNAL(MethodInfo("session_state_changed", PropertyInfo(Variant::INT, "state")));
	ADD_SIGNAL(MethodInfo("tracking_reason_changed", PropertyInfo(Variant::INT, "reason")));
	ADD_SIGNAL(MethodInfo("session_generation_changed", PropertyInfo(Variant::INT, "generation")));
	ADD_SIGNAL(MethodInfo("planes_changed"));
}

bool ARCoreInterface::on_owner_thread() const { return std::this_thread::get_id() == owner_thread; }
void ARCoreInterface::increment(const char *p_key, int64_t p_amount) { diagnostics[p_key] = int64_t(diagnostics.get(p_key, 0)) + p_amount; }
void ARCoreInterface::set_state(PocketAR::SessionState p_state) {
	const bool changed = state != p_state;
	if (!changed && !state_history.is_empty()) return;
#ifdef DEBUG_ENABLED
	if (p_state != PocketAR::TRACKING) {
		debug_plane.pause();
		if (debug_plane.active()) presented_packet.unref();
	}
#endif
	state = p_state;
	state_history.append(int(state));
	UtilityFunctions::print("ARCORE state=", int(state), " generation=", generation, " reason=", int(reason));
	if (changed) call_deferred("emit_signal", "session_state_changed", int(state));
}
void ARCoreInterface::set_reason(PocketAR::TrackingReason p_reason) {
	if (reason == p_reason) return;
	reason = p_reason;
	call_deferred("emit_signal", "tracking_reason_changed", int(reason));
}
void ARCoreInterface::fail(ArStatus p_status, const String &p_operation) {
	diagnostics["last_error"] = p_operation + String(" status=") + String::num_int64(p_status);
	UtilityFunctions::printerr("ARCORE ", String(diagnostics["last_error"]).replace("\n", "\\n").replace("\r", "\\r").left(200));
	set_state((p_status == AR_ERROR_UNSUPPORTED_CONFIGURATION || p_status == AR_UNAVAILABLE_DEVICE_NOT_COMPATIBLE) ? PocketAR::UNSUPPORTED :
		(p_status == AR_UNAVAILABLE_ARCORE_NOT_INSTALLED || p_status == AR_UNAVAILABLE_APK_TOO_OLD) ? PocketAR::NEEDS_INSTALL :
		p_status == AR_ERROR_CAMERA_PERMISSION_NOT_GRANTED ? PocketAR::PERMISSION_DENIED : PocketAR::FAILED);
}

bool ARCoreInterface::_initialize() {
	if (initialized) return true;
	ERR_FAIL_COND_V_MSG(OS::get_singleton()->get_thread_caller_id() != OS::get_singleton()->get_main_thread_id() || !RenderingServer::get_singleton()->is_on_render_thread(), false, "ARCore requires Safe rendering thread mode on the main thread.");
	ERR_FAIL_COND_V_MSG(XRServer::get_singleton()->get_primary_interface().is_valid(), false, "Only one ARView may own the primary XR interface.");
	owner_thread = std::this_thread::get_id();
	RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
	ERR_FAIL_NULL_V_MSG(rd, false, "ARCore CPU upload requires the Mobile/Vulkan renderer.");
	frame_delay = rd->get_frame_delay();
	texture_slots.resize(frame_delay + 3);
	for (const char *key : {"successful_updates", "egl_context_violations", "new_frame_count", "camera_image_count", "image_not_yet_available", "image_timestamp_mismatches", "image_timestamp_far_count", "timestamp_regressions", "projection_form_samples", "display_transform_samples", "packets_published", "packets_presented", "textures_updated", "pause_count", "resume_count", "reset_count", "stale_tracker_count", "image_resource_exhausted", "image_deadline_exceeded"}) diagnostics[key] = int64_t(0);
	for (const char *key : {"image_timestamp_delta_min_ns", "image_timestamp_delta_max_ns", "image_timestamp_delta_mean_ns"}) diagnostics.erase(key);
	diagnostics["arcore_sdk_version"] = "1.56.0";
	diagnostics["renderer"] = String(ProjectSettings::get_singleton()->get_setting_with_override("rendering/renderer/rendering_method")) + "/" + rd->get_device_name();
	diagnostics["texture_refusals"] = int64_t(0);
	diagnostics["texture_upload_region_size_px"] = ProjectSettings::get_singleton()->get_setting_with_override("rendering/rendering_device/staging_buffer/texture_upload_region_size_px");
	diagnostics["staging_block_size_kb"] = ProjectSettings::get_singleton()->get_setting_with_override("rendering/rendering_device/staging_buffer/block_size_kb");
	diagnostics["configured"] = false;
	diagnostics["projection_form_valid"] = true;
	diagnostics["display_transform_valid"] = true;
	diagnostics["last_error"] = "";
	diagnostics["mean_y"] = 0.0;
	detect_gpu_camera();
	std::string error;
	if (!api.load(error)) {
		diagnostics["last_error"] = String(error.c_str());
		set_state(PocketAR::FAILED);
		return false;
	}
	head.instantiate();
	head->set_tracker_name("head");
	head->set_tracker_desc("ARCore display-oriented camera");
	head->set_tracker_type(XRServer::TRACKER_HEAD);
	XRServer::get_singleton()->add_tracker(head);
	initialized = true;
#ifdef DEBUG_ENABLED
	debug_plane.clear();
#endif
	generation = next_ar_id();
	reset_tick = tick_usec();
	set_primary(true);
	arcore_bridge_set_owner(this);
	call_deferred("emit_signal", "session_state_changed", int(state));
	call_deferred("emit_signal", "session_generation_changed", generation);
	bridge_changed();
	return true;
}

ARCoreInterface::~ARCoreInterface() { if (initialized) _uninitialize(); }
void ARCoreInterface::_uninitialize() {
	if (!initialized) return;
	ERR_FAIL_COND(!on_owner_thread());
	arcore_bridge_set_owner(nullptr);
	current_packet.unref();
	presented_packet.unref();
	release_session();
	set_state(PocketAR::PAUSED);
	applied_packet.unref();
	texture_slots.clear();
	gpu_texture.unref();
	gpu_active = false;
	XRServer::get_singleton()->remove_tracker(head);
	head.unref();
	set_primary(false);
	initialized = false;
}

void ARCoreInterface::bridge_changed() {
	if (!initialized) return;
	ERR_FAIL_COND_MSG(!on_owner_thread(), "Kotlin must post ARCore lifecycle callbacks through runOnRenderThread.");
	const auto bridge = arcore_bridge_snapshot();
	diagnostics["arcore_apk_version"] = String(bridge.apk_version.c_str());
	const bool new_status = bridge_history_size < bridge.state_history.size();
	for (; bridge_history_size < bridge.state_history.size(); ++bridge_history_size) state_history.append(bridge.state_history[bridge_history_size]);
	if (new_status && bridge.state == PocketAR::READY && state != PocketAR::TRACKING && state != PocketAR::INITIALIZING) set_state(application_suspended ? PocketAR::PAUSED : PocketAR::READY);
	if (!bridge.resumed || application_suspended || bridge.state != PocketAR::READY) {
		pause_session();
		if (bridge.state != PocketAR::READY) set_state(PocketAR::SessionState(bridge.state));
		return;
	}
	if (!session && (state == PocketAR::READY || state == PocketAR::PAUSED)) {
		create_session();
	} else if (session && !running && state != PocketAR::FAILED && state != PocketAR::UNSUPPORTED && state != PocketAR::PERMISSION_DENIED && tick_usec() >= retry_at) {
		resume_session();
	}
}

static Dictionary camera_config_values(ARCoreApi &p_api, ArSession *p_session, ArCameraConfig *p_config) {
	int32_t width, height, texture_width, texture_height, fps_min, fps_max;
	p_api.ArCameraConfig_getImageDimensions(p_session, p_config, &width, &height);
	p_api.ArCameraConfig_getTextureDimensions(p_session, p_config, &texture_width, &texture_height);
	p_api.ArCameraConfig_getFpsRange(p_session, p_config, &fps_min, &fps_max);
	Dictionary values;
	values["cpu_width"] = width; values["cpu_height"] = height;
	values["texture_width"] = texture_width; values["texture_height"] = texture_height;
	values["fps_min"] = fps_min; values["fps_max"] = fps_max;
	return values;
}

bool ARCoreInterface::choose_camera_config() {
	ArCameraConfigFilter *filter = nullptr;
	ArCameraConfigList *list = nullptr;
	ArCameraConfig *candidate = nullptr;
	api.ArCameraConfigFilter_create(session, &filter);
	api.ArCameraConfigFilter_setFacingDirection(session, filter, AR_CAMERA_CONFIG_FACING_DIRECTION_BACK);
	api.ArCameraConfigFilter_setTargetFps(session, filter, AR_CAMERA_CONFIG_TARGET_FPS_30);
	api.ArCameraConfigFilter_setDepthSensorUsage(session, filter, AR_CAMERA_CONFIG_DEPTH_SENSOR_USAGE_DO_NOT_USE);
	api.ArCameraConfigFilter_setStereoCameraUsage(session, filter, AR_CAMERA_CONFIG_STEREO_CAMERA_USAGE_DO_NOT_USE);
	api.ArCameraConfigList_create(session, &list);
	api.ArCameraConfig_create(session, &candidate);
	api.ArSession_getSupportedCameraConfigsWithFilter(session, filter, list);
	int32_t count = 0;
	api.ArCameraConfigList_getSize(session, list, &count);
	Array available;
	int selected = -1;
	int64_t best_area = -1, best_gpu_area = INT64_MAX;
	for (int i = 0; i < count; ++i) {
		api.ArCameraConfigList_getItem(session, list, i, candidate);
		Dictionary item = camera_config_values(api, session, candidate);
		item["index"] = i;
		const int w = item["cpu_width"], h = item["cpu_height"], tw = item["texture_width"], th = item["texture_height"];
		available.append(item);
		const int64_t area = int64_t(w) * h, texture_area = int64_t(tw) * th;
		const bool aspect_matches = int64_t(w) * th == int64_t(tw) * h;
		const bool prefer_gpu = gpu_texture.is_valid();
		if ((prefer_gpu && (selected < 0 || texture_area > best_gpu_area || (texture_area == best_gpu_area && area < best_area))) ||
			(!prefer_gpu && aspect_matches && (area > best_area || (area == best_area && texture_area < best_gpu_area)))) {
			selected = i; best_area = area; best_gpu_area = texture_area;
		}
	}
	if (camera_config_index >= 0) selected = camera_config_index < count ? camera_config_index : -1;
	diagnostics["config_list"] = available;
	ArStatus status = AR_ERROR_UNSUPPORTED_CONFIGURATION;
	if (selected >= 0) {
		api.ArCameraConfigList_getItem(session, list, selected, candidate);
		const uint64_t start = tick_usec();
		status = api.ArSession_setCameraConfig(session, candidate);
		timings["set_camera_config"] = (tick_usec() - start) / 1000.0;
		const Dictionary selected_config = available[selected];
		cpu_size = Vector2i(selected_config["cpu_width"], selected_config["cpu_height"]);
		gpu_size = Vector2i(selected_config["texture_width"], selected_config["texture_height"]);
		fps_range = Vector2i(selected_config["fps_min"], selected_config["fps_max"]);
		diagnostics["config"] = selected_config;
	}
	api.ArCameraConfig_destroy(candidate);
	api.ArCameraConfigList_destroy(list);
	api.ArCameraConfigFilter_destroy(filter);
	if (status != AR_SUCCESS) { fail(status, "camera config"); return false; }
	UtilityFunctions::print("ARCORE config_count=", available.size(), " selected=", selected, " selection=", camera_config_index >= 0 ? "explicit" : gpu_texture.is_valid() ? "largest_gpu" : "largest_cpu", " cpu=", cpu_size, " gpu=", gpu_size, " details=diagnostics.json");
	return true;
}

void ARCoreInterface::refresh_camera_config() {
	ArCameraConfig *config = nullptr;
	api.ArCameraConfig_create(session, &config);
	api.ArSession_getCameraConfig(session, config);
	const Dictionary effective = camera_config_values(api, session, config);
	cpu_size = Vector2i(effective["cpu_width"], effective["cpu_height"]);
	gpu_size = Vector2i(effective["texture_width"], effective["texture_height"]);
	fps_range = Vector2i(effective["fps_min"], effective["fps_max"]);
	api.ArCameraConfig_destroy(config);
	diagnostics["config"] = effective;
}

bool ARCoreInterface::create_session() {
	const auto bridge = arcore_bridge_snapshot();
	if (bridge.state != PocketAR::READY) {
		set_state(PocketAR::SessionState(bridge.state));
		return false;
	}
	if (!bridge.resumed || application_suspended) {
		set_state(PocketAR::PAUSED);
		return false;
	}
	set_state(PocketAR::READY);
	if (int(diagnostics.get("texture_upload_region_size_px", 64)) < 512) {
		UtilityFunctions::push_warning("ARCore camera upload region size is below 512 px; use 1024 px with staging_buffer/block_size_kb=4096 for lower upload cost.");
	}
	ARCoreJNIEnv jni(bridge.vm);
	if (!jni.env || !bridge.context) { fail(AR_ERROR_FATAL, "JNI context"); return false; }
	uint64_t start = tick_usec();
	ArStatus status = api.ArSession_create(jni.env, bridge.context, &session);
	timings["create"] = (tick_usec() - start) / 1000.0;
	if (status != AR_SUCCESS) { fail(status, "create"); return false; }
	if (!choose_camera_config()) { release_session(); return false; }
	api.ArConfig_create(session, &config);
	api.ArConfig_setTextureUpdateMode(session, config, AR_TEXTURE_UPDATE_MODE_EXPOSE_HARDWARE_BUFFER);
	api.ArConfig_setUpdateMode(session, config, AR_UPDATE_MODE_LATEST_CAMERA_IMAGE);
	api.ArConfig_setPlaneFindingMode(session, config, ArPlaneFindingMode(plane_finding_mode));
	api.ArConfig_setLightEstimationMode(session, config, AR_LIGHT_ESTIMATION_MODE_DISABLED);
	api.ArConfig_setFocusMode(session, config, AR_FOCUS_MODE_AUTO);
	start = tick_usec();
	status = api.ArSession_configure(session, config);
	timings["configure"] = (tick_usec() - start) / 1000.0;
	if (status != AR_SUCCESS) { fail(status, "configure"); release_session(); return false; }
	refresh_plane_finding_mode();
	api.ArFrame_create(session, &frame);
	api.ArPose_create(session, nullptr, &scratch_pose);
	api.ArCameraIntrinsics_create(session, &camera_intrinsics);
	api.ArTrackableList_create(session, &trackable_list);
	rotation = bridge.rotation;
	if (viewport_size.x <= 1 || viewport_size.y <= 1) viewport_size = Vector2i(bridge.width, bridge.height);
	api.ArSession_setDisplayGeometry(session, rotation, viewport_size.x, viewport_size.y);
	geometry_revision = bridge.geometry_revision;
	geometry_dirty = false;
	extras.setup(session, generation);
	if (!playback_dataset.is_empty()) {
		const Dictionary playback = extras.set_playback_dataset(playback_dataset);
		diagnostics["playback_start"] = playback;
		if (!bool(playback.get("ok", false))) { fail(AR_ERROR_PLAYBACK_FAILED, "set playback dataset"); release_session(); return false; }
	}
	diagnostics["configured"] = true;
	resume_session();
	return true;
}

void ARCoreInterface::resume_session() {
	if (!session || running || application_suspended) return;
	const auto bridge = arcore_bridge_snapshot();
	if (bridge.state != PocketAR::READY || !bridge.resumed) return;
	const uint64_t start = tick_usec();
	const ArStatus status = api.ArSession_resume(session);
	timings["resume"] = (tick_usec() - start) / 1000.0;
	if (status == AR_SUCCESS) {
		running = true;
		refresh_camera_config();
		refresh_plane_finding_mode();
		retry_at = 0;
		retry_delay = 100000;
		increment("resume_count");
		set_state(PocketAR::INITIALIZING);
	} else if (status == AR_ERROR_CAMERA_NOT_AVAILABLE) {
		set_reason(PocketAR::CAMERA_UNAVAILABLE);
		retry_at = tick_usec() + retry_delay;
		retry_delay = std::min<uint64_t>(retry_delay * 2, 2000000);
		set_state(PocketAR::PAUSED);
	} else fail(status, "resume");
}

void ARCoreInterface::pause_session() {
	gpu_validation_pending = false;
	if (!session || !running) return;
	extras.before_update();
	const uint64_t start = tick_usec();
	const ArStatus status = api.ArSession_pause(session);
	timings["pause"] = (tick_usec() - start) / 1000.0;
	running = false;
	increment("pause_count");
	if (state != PocketAR::FAILED && state != PocketAR::UNSUPPORTED && state != PocketAR::PERMISSION_DENIED) set_state(PocketAR::PAUSED);
	if (status != AR_SUCCESS) fail(status, "pause");
}

void ARCoreInterface::clear_planes() {
	bool changed = !planes.empty();
#ifdef DEBUG_ENABLED
	changed = debug_plane.clear() || changed;
#endif
	for (auto &item : planes) {
		XRServer::get_singleton()->remove_tracker(item.second.tracker);
		api.ArTrackable_release(reinterpret_cast<ArTrackable *>(item.first));
	}
	planes.clear();
	if (changed) call_deferred("emit_signal", "planes_changed");
}

void ARCoreInterface::release_session() {
	if (!session) {
#ifdef DEBUG_ENABLED
		clear_planes();
#endif
		return;
	}
	pause_session();
	extras.close();
	clear_planes();
	if (trackable_list) api.ArTrackableList_destroy(trackable_list);
	if (camera_intrinsics) api.ArCameraIntrinsics_destroy(camera_intrinsics);
	if (scratch_pose) api.ArPose_destroy(scratch_pose);
	if (frame) api.ArFrame_destroy(frame);
	if (config) api.ArConfig_destroy(config);
	config = nullptr;
	diagnostics.erase("effective_plane_finding_mode");
	trackable_list = nullptr; camera_intrinsics = nullptr; scratch_pose = nullptr; frame = nullptr;
	ArSession *old_session = session;
	session = nullptr;
	const auto stats = destroy_stats;
	JavaVM *vm = arcore_bridge_snapshot().vm;
	const auto destroy = api.ArSession_destroy;
	++stats->active;
	std::thread([old_session, vm, stats, destroy]() {
		ARCoreJNIEnv jni(vm);
		const auto start = std::chrono::steady_clock::now();
		if (jni.env) destroy(old_session);
		const int64_t elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
		stats->last_usec = elapsed;
		++stats->count;
		--stats->active;
		__android_log_print(ANDROID_LOG_INFO, "godot", "ARCORE destroy %.3f ms attached=%d", elapsed / 1000.0, jni.env != nullptr);
	}).detach();
	diagnostics["configured"] = false;
}

void ARCoreInterface::reset() {
	ERR_FAIL_COND(!initialized || !on_owner_thread());
	generation = next_ar_id();
	increment("reset_count");
	reset_tick = tick_usec();
	first_tracking_tick = 0;
	gpu_buffer_misses = 0;
	gpu_active = false;
	if (gpu_texture.is_valid()) {
		diagnostics["camera_path"] = "cpu";
		diagnostics["camera_path_reason"] = "awaiting first hardware buffer completion";
	}
	current_packet.unref(); presented_packet.unref();
	release_session();
	last_timestamp = 0;
	sequence = 0;
	plane_sequence = 0;
	last_good_pose = Transform3D();
	head->invalidate_pose("default");
	set_reason(PocketAR::NONE);
	tracking_status = XR_UNKNOWN_TRACKING;
	call_deferred("emit_signal", "session_generation_changed", generation);
	create_session();
}
void ARCoreInterface::application_pause() {
	if (!initialized) return;
	ERR_FAIL_COND(!on_owner_thread());
	application_suspended = true;
	pause_session();
}
void ARCoreInterface::application_resume() {
	if (!initialized) return;
	ERR_FAIL_COND(!on_owner_thread());
	application_suspended = false;
	bridge_changed();
}
void ARCoreInterface::start_playback(const String &p_uri) {
	ERR_FAIL_COND(!initialized || !on_owner_thread() || !p_uri.begins_with("file://"));
	playback_dataset = p_uri;
	reset();
}
void ARCoreInterface::set_camera_config_index(int p_index) {
	ERR_FAIL_COND(p_index < -1);
	if (camera_config_index == p_index) return;
	camera_config_index = p_index;
	if (session) reset();
}
static_assert(int(PocketAR::PLANE_FINDING_DISABLED) == AR_PLANE_FINDING_MODE_DISABLED && int(PocketAR::PLANE_FINDING_HORIZONTAL) == AR_PLANE_FINDING_MODE_HORIZONTAL &&
		int(PocketAR::PLANE_FINDING_HORIZONTAL_AND_VERTICAL) == AR_PLANE_FINDING_MODE_HORIZONTAL_AND_VERTICAL, "PlaneFindingMode is passed to ARCore unchanged");
void ARCoreInterface::refresh_plane_finding_mode() {
	api.ArSession_getConfig(session, config);
	ArPlaneFindingMode effective;
	api.ArConfig_getPlaneFindingMode(session, config, &effective);
	diagnostics["effective_plane_finding_mode"] = int(effective);
}
void ARCoreInterface::set_plane_finding_mode(PocketAR::PlaneFindingMode p_mode) {
	ERR_FAIL_COND(p_mode != PocketAR::PLANE_FINDING_DISABLED && p_mode != PocketAR::PLANE_FINDING_HORIZONTAL && p_mode != PocketAR::PLANE_FINDING_HORIZONTAL_AND_VERTICAL);
	ERR_FAIL_COND(initialized && !on_owner_thread());
	if (plane_finding_mode == p_mode) return;
	if (session) {
		api.ArSession_getConfig(session, config);
		api.ArConfig_setPlaneFindingMode(session, config, ArPlaneFindingMode(p_mode));
		const ArStatus status = api.ArSession_configure(session, config);
		refresh_plane_finding_mode();
		ERR_FAIL_COND_MSG(status != AR_SUCCESS, "ARCore plane finding configure status=" + String::num_int64(status));
	}
	plane_finding_mode = p_mode;
}
void ARCoreInterface::set_display_geometry(const Vector2i &p_size, int, double p_near, double p_far) {
	ERR_FAIL_COND(p_size.x <= 0 || p_size.y <= 0 || !std::isfinite(p_near) || !std::isfinite(p_far) || p_near <= 0 || p_far <= p_near);
	if (viewport_size != p_size) { viewport_size = p_size; geometry_dirty = true; }
	near_plane = p_near; far_plane = p_far;
}
Vector2 ARCoreInterface::_get_render_target_size() { return current_packet.is_valid() ? current_packet->get_viewport_size() : Vector2(viewport_size); }
Transform3D ARCoreInterface::_get_camera_transform() { return current_packet.is_valid() ? current_packet->get_camera_transform() : last_good_pose; }
Transform3D ARCoreInterface::_get_transform_for_view(uint32_t, const Transform3D &p_transform) { return p_transform * _get_camera_transform(); }
PackedFloat64Array ARCoreInterface::_get_projection_for_view(uint32_t, double, double p_near, double p_far) {
	PackedFloat64Array values;
	values.resize(16);
	Projection projection = current_packet.is_valid() ? current_packet->get_projection() : Projection();
	for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) values[c * 4 + r] = projection[c][r];
	if (p_near > 0 && p_far > p_near) {
		values[10] = -(p_far + p_near) / (p_far - p_near);
		values[14] = -2 * p_far * p_near / (p_far - p_near);
	}
	return values;
}
void ARCoreInterface::mark_applied(const Ref<ARFramePacket> &p_packet) {
	ERR_FAIL_COND(!on_owner_thread());
	ERR_FAIL_COND(p_packet.is_valid() && p_packet != current_packet);
	applied_packet = p_packet;
}
void ARCoreInterface::mark_presented() {
#ifdef DEBUG_ENABLED
	if (debug_plane.presentation_paused()) { presented_packet.unref(); return; }
#endif
	if (applied_packet.is_valid() && applied_packet->get_session_generation() == generation) {
		presented_packet = applied_packet;
		increment("packets_presented");
	}
}
void ARCoreInterface::_end_frame() {
	++rd_swaps;
	for (auto &slot : texture_slots) if (texture_slot_active(slot)) slot.last_use = rd_swaps;
}
bool ARCoreInterface::texture_slot_active(const TextureSlot &p_slot) const {
	for (const auto &packet : {current_packet, presented_packet, applied_packet}) {
		if (packet.is_valid() && ((p_slot.y.is_valid() && packet->get_y_texture() == p_slot.y) || (p_slot.uv.is_valid() && packet->get_cbcr_texture() == p_slot.uv))) return true;
	}
	return false;
}
ARCoreInterface::TextureSlot *ARCoreInterface::find_texture_slot() {
	for (auto &slot : texture_slots) {
		if (slot.y.is_null()) return &slot;
		if (pocketar::texture_slot_retired(rd_swaps, slot.last_use, frame_delay, texture_slot_active(slot))) return &slot;
	}
	return nullptr;
}
Dictionary ARCoreInterface::get_diagnostics() const {
	Dictionary result = diagnostics.duplicate(true);
	result["plane_finding_mode"] = int(plane_finding_mode);
	result["extras"] = extras.diagnostics();
	Dictionary measured = timings.duplicate(true);
	measured["destroy"] = destroy_stats->last_usec.load() / 1000.0;
	result["timings"] = measured;
	result["destroy_count"] = destroy_stats->count.load();
	result["destroy_workers"] = destroy_stats->active.load();
	result["state_history"] = state_history.duplicate();
	result["update_status_counts"] = update_counts.duplicate();
	result["plane_count"] = int64_t(planes.size());
	int stale_trackers = 0;
	const Dictionary trackers = XRServer::get_singleton()->get_trackers(XRServer::TRACKER_ANCHOR);
	const Array tracker_names = trackers.keys();
	for (int i = 0; i < tracker_names.size(); ++i) {
		Object *object = trackers[tracker_names[i]];
		ARPlaneTracker *tracker = Object::cast_to<ARPlaneTracker>(object);
		if (tracker && tracker->get_session_generation() != generation) ++stale_trackers;
	}
	result["stale_tracker_count"] = stale_trackers;
	result["session_generation"] = generation;
	result["image_acquire_p50_us"] = timing_percentile(acquire_times, 0.50);
	result["image_acquire_p99_us"] = timing_percentile(acquire_times, 0.99);
	result["repack_p50_us"] = timing_percentile(repack_times, 0.50);
	result["repack_p95_us"] = timing_percentile(repack_times, 0.95);
	result["stats_p50_us"] = timing_percentile(stats_times, 0.50);
	result["stats_p95_us"] = timing_percentile(stats_times, 0.95);
	result["upload_p50_us"] = timing_percentile(upload_times, 0.50);
	result["upload_p95_us"] = timing_percentile(upload_times, 0.95);
	result["repack_upload_p50_us"] = timing_percentile(repack_upload_times, 0.50);
	result["repack_upload_p95_us"] = timing_percentile(repack_upload_times, 0.95);
	result["gpu_submit_p50_us"] = timing_percentile(gpu_submit_times, 0.50);
	result["gpu_submit_p95_us"] = timing_percentile(gpu_submit_times, 0.95);
	result["frame_setup_p50_us"] = timing_percentile(frame_setup_times, 0.50);
	result["frame_setup_p95_us"] = timing_percentile(frame_setup_times, 0.95);
	result["gpu_validation_enabled"] = gpu_validate;
	return result;
}
}
