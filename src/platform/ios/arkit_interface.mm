#include "arkit_interface.h"
#include "arkit_math.h"
#include "core/ar_ids.h"
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/xr_server.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#import <ARKit/ARKit.h>
#import <AVFoundation/AVFoundation.h>
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>
#include <sys/utsname.h>
#include <algorithm>
#include <cmath>
#include <set>

#if !__has_feature(objc_arc)
#error ARKit requires Objective-C ARC
#endif

@interface HARSessionDelegate : NSObject <ARSessionDelegate>
@property(nonatomic, assign) godot::ARKitInterface *owner;
@end

@implementation HARSessionDelegate
- (void)session:(ARSession *)session didFailWithError:(NSError *)error {
	if (self.owner) self.owner->session_failed(int(error.code), godot::String(error.localizedDescription.UTF8String));
}
- (void)sessionWasInterrupted:(ARSession *)session {
	if (self.owner) self.owner->session_interrupted(true);
}
- (void)sessionInterruptionEnded:(ARSession *)session {
	if (self.owner) self.owner->session_interrupted(false);
}
- (BOOL)sessionShouldAttemptRelocalization:(ARSession *)session {
	return self.owner && self.owner->get_relocalization_enabled();
}
@end

namespace godot {

struct ARKitInterface::Impl {
	ARSession *session = nil;
	ARWorldTrackingConfiguration *config = nil;
	HARSessionDelegate *delegate = nil;
	bool running = false, interrupted = false, permission_requested = false;
	bool reset_pending = false;
	int reset_samples = 0;
	double last_timestamp = 0, reset_cutoff = 0;
	uint64_t start_tick = 0, reset_tick = 0;
	bool first_frame = false, normal_tracking = false;
	uint64_t relocalization_tick = 0;
	bool relocalization_timeout_sent = false;
	UIInterfaceOrientation orientation = UIInterfaceOrientationUnknown;
};

static uint64_t tick_usec() { return Time::get_singleton()->get_ticks_usec(); }
static std::set<ARKitInterface *> active_interfaces;

void shutdown_arkit_interfaces() {
	while (!active_interfaces.empty()) (*active_interfaces.begin())->_uninitialize();
}

static Transform3D from_simd(const simd_float4x4 &p_matrix) {
	const auto &c = p_matrix.columns;
	return Transform3D(Basis(Vector3(c[0].x, c[0].y, c[0].z), Vector3(c[1].x, c[1].y, c[1].z), Vector3(c[2].x, c[2].y, c[2].z)), Vector3(c[3].x, c[3].y, c[3].z));
}

static Projection from_projection(const simd_float4x4 &p_matrix) {
	Projection result;
	for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) result[c][r] = p_matrix.columns[c][r];
	return result;
}

static int orientation_turns(UIInterfaceOrientation p_orientation) {
	switch (p_orientation) {
		case UIInterfaceOrientationPortrait: return 1;
		case UIInterfaceOrientationLandscapeLeft: return 2;
		case UIInterfaceOrientationPortraitUpsideDown: return 3;
		default: return 0;
	}
}

ARKitInterface::ARKitInterface() : impl(new Impl) {}
ARKitInterface::~ARKitInterface() { if (initialized) _uninitialize(); }

void ARKitInterface::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_session_state"), &ARKitInterface::get_session_state);
	ClassDB::bind_method(D_METHOD("get_tracking_reason"), &ARKitInterface::get_tracking_reason);
	ClassDB::bind_method(D_METHOD("get_session_generation"), &ARKitInterface::get_session_generation);
	ClassDB::bind_method(D_METHOD("get_diagnostics"), &ARKitInterface::get_diagnostics);
	ClassDB::bind_method(D_METHOD("reset"), &ARKitInterface::reset);
	ClassDB::bind_method(D_METHOD("set_focus_auto", "enabled"), &ARKitInterface::set_focus_auto);
	ClassDB::bind_method(D_METHOD("set_light_estimation_enabled", "enabled"), &ARKitInterface::set_light_estimation_enabled);
	ClassDB::bind_method(D_METHOD("set_relocalization_enabled", "enabled"), &ARKitInterface::set_relocalization_enabled);
	ClassDB::bind_method(D_METHOD("get_relocalization_enabled"), &ARKitInterface::get_relocalization_enabled);
	ADD_SIGNAL(MethodInfo("relocalization_timeout", PropertyInfo(Variant::INT, "session_generation")));
	ClassDB::bind_method(D_METHOD("set_plane_detection", "mask"), &ARKitInterface::set_plane_detection);
	ClassDB::bind_method(D_METHOD("set_display_geometry", "size", "turns", "near", "far"), &ARKitInterface::set_display_geometry);
	ClassDB::bind_method(D_METHOD("get_current_packet"), &ARKitInterface::get_current_packet);
	ClassDB::bind_method(D_METHOD("get_presented_packet"), &ARKitInterface::get_presented_packet);
	ClassDB::bind_method(D_METHOD("mark_presented"), &ARKitInterface::mark_presented);
	ClassDB::bind_method(D_METHOD("application_pause"), &ARKitInterface::application_pause);
	ClassDB::bind_method(D_METHOD("application_resume"), &ARKitInterface::application_resume);
	ClassDB::bind_method(D_METHOD("harness_pause"), &ARKitInterface::application_pause);
	ClassDB::bind_method(D_METHOD("harness_resume"), &ARKitInterface::application_resume);
	ClassDB::bind_method(D_METHOD("live_hit_test", "view_pixels"), &ARKitInterface::live_hit_test);
	BIND_ENUM_CONSTANT(UNSUPPORTED); BIND_ENUM_CONSTANT(CHECKING); BIND_ENUM_CONSTANT(NEEDS_INSTALL);
	BIND_ENUM_CONSTANT(INSTALLING); BIND_ENUM_CONSTANT(PERMISSION_DENIED); BIND_ENUM_CONSTANT(READY);
	BIND_ENUM_CONSTANT(INITIALIZING); BIND_ENUM_CONSTANT(TRACKING); BIND_ENUM_CONSTANT(PAUSED); BIND_ENUM_CONSTANT(FAILED);
	BIND_ENUM_CONSTANT(NONE); BIND_ENUM_CONSTANT(BAD_STATE); BIND_ENUM_CONSTANT(INSUFFICIENT_LIGHT);
	BIND_ENUM_CONSTANT(EXCESSIVE_MOTION); BIND_ENUM_CONSTANT(INSUFFICIENT_FEATURES); BIND_ENUM_CONSTANT(CAMERA_UNAVAILABLE);
}

bool ARKitInterface::on_owner_thread() const { return [NSThread isMainThread] && RenderingServer::get_singleton()->is_on_render_thread(); }
void ARKitInterface::increment(const char *p_key) { diagnostics[p_key] = int64_t(diagnostics.get(p_key, 0)) + 1; }
void ARKitInterface::set_state(SessionState p_state) {
	if (state == p_state && !state_history.is_empty()) return;
	state = p_state;
	state_history.append(int(state));
	UtilityFunctions::print("ARKIT state=", int(state), " generation=", generation, " reason=", int(reason));
}

bool ARKitInterface::_initialize() {
	if (initialized) return true;
	impl.reset(new Impl);
	suspended = false;
	geometry_dirty = true;
	last_good_pose = Transform3D();
	tracking_status = XR_UNKNOWN_TRACKING;
	reason = NONE;
	diagnostics.clear(); timings.clear(); state_history.clear();
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs->get_current_rendering_driver_name() != "metal" || !rs->get_rendering_device()) {
		diagnostics["last_error"] = "ARKit zero-copy camera requires the Metal rendering driver";
		set_state(UNSUPPORTED);
		return false;
	}
	ERR_FAIL_COND_V_MSG(!on_owner_thread() || int(ProjectSettings::get_singleton()->get_setting_with_override("rendering/driver/threads/thread_model")) != 1, false, "ARKit requires the main thread and Safe rendering thread mode.");
	ERR_FAIL_COND_V_MSG(XRServer::get_singleton()->get_primary_interface().is_valid(), false, "Only one ARView may own the primary XR interface.");
	if (![ARWorldTrackingConfiguration isSupported]) {
		diagnostics["last_error"] = "ARWorldTrackingConfiguration is unsupported on this device";
		set_state(UNSUPPORTED);
		return false;
	}
	textures = arkit_texture_pool();
	if (textures.is_null()) { diagnostics["last_error"] = "Metal retain probe or CoreVideo texture cache failed"; set_state(UNSUPPORTED); return false; }
	for (const char *key : { "new_frame_count", "packets_published", "packets_presented", "repeated_updates", "dropped_frames", "timestamp_regressions", "projection_form_samples", "display_transform_samples", "pause_count", "resume_count", "reset_count", "retired_tracker_count", "orientation_transition_drops" }) diagnostics[key] = int64_t(0);
	diagnostics["projection_form_valid"] = true;
	diagnostics["display_transform_valid"] = true;
	diagnostics["native_depth_zero_to_one"] = true;
	diagnostics["projection_near_far_error"] = 0.0;
	diagnostics["projection_core_error_px"] = 0.0;
	diagnostics["display_transform_error_px"] = 0.0;
	diagnostics["pose_error"] = 0.0;
	diagnostics["configured"] = false;
	diagnostics["renderer"] = rs->get_current_rendering_method();
	diagnostics["driver"] = rs->get_current_rendering_driver_name();
	NSBundle *framework = [NSBundle bundleForClass:[ARSession class]];
	NSString *version = [framework objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
	NSString *build = [framework objectForInfoDictionaryKey:@"CFBundleVersion"];
	diagnostics["arkit_version"] = version ? String(version.UTF8String) : String("OS-bundled");
	diagnostics["arkit_build"] = build ? String(build.UTF8String) : String("unavailable");
	diagnostics["ios_version"] = String(NSProcessInfo.processInfo.operatingSystemVersionString.UTF8String);
	struct utsname hardware;
	diagnostics["device_model"] = uname(&hardware) == 0 ? String(hardware.machine) : String("unknown");
	head.instantiate();
	head->set_tracker_name("head");
	head->set_tracker_desc("ARKit display-oriented camera");
	head->set_tracker_type(XRServer::TRACKER_HEAD);
	XRServer::get_singleton()->add_tracker(head);
	generation = next_ar_id();
	initialized = true;
	active_interfaces.insert(this);
	impl->start_tick = tick_usec();
	set_primary(true);
	set_state(CHECKING);
	return true;
}

void ARKitInterface::_uninitialize() {
	if (!initialized) return;
	ERR_FAIL_COND(!on_owner_thread());
	impl->delegate.owner = nullptr;
	impl->session.delegate = nil;
	[impl->session pause];
	impl->session = nil;
	impl->config = nil;
	impl->delegate = nil;
	impl->running = false;
	tracking_status = XR_NOT_TRACKING;
	clear_planes();
	current_packet.unref();
	presented_packet.unref();
	textures.unref();
	XRServer::get_singleton()->remove_tracker(head);
	head.unref();
	set_primary(false);
	initialized = false;
	active_interfaces.erase(this);
}

void ARKitInterface::run_session(bool p_reset) {
	ERR_FAIL_COND(!on_owner_thread());
	if (suspended) return;
	const AVAuthorizationStatus authorization = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
	if (authorization == AVAuthorizationStatusDenied || authorization == AVAuthorizationStatusRestricted) {
		if (impl->running) [impl->session pause];
		impl->running = false;
		current_packet.unref(); presented_packet.unref();
		head->invalidate_pose("default");
		tracking_status = XR_NOT_TRACKING;
		reason = CAMERA_UNAVAILABLE;
		set_state(PERMISSION_DENIED);
		return;
	}
	if (authorization == AVAuthorizationStatusNotDetermined) {
		if (!impl->permission_requested) {
			impl->permission_requested = true;
			// Completion is off-thread; _process observes authorization again on the main thread.
			[AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL granted) {}];
		}
		set_state(CHECKING);
		return;
	}
	if (!impl->session) {
		impl->session = [ARSession new];
		impl->delegate = [HARSessionDelegate new];
		impl->delegate.owner = this;
		impl->session.delegate = impl->delegate;
		impl->session.delegateQueue = dispatch_get_main_queue();
		impl->config = [ARWorldTrackingConfiguration new];
		impl->config.worldAlignment = ARWorldAlignmentGravity;
		impl->config.frameSemantics = ARFrameSemanticNone;
		impl->config.videoHDRAllowed = NO;
		NSArray<ARVideoFormat *> *formats = ARWorldTrackingConfiguration.supportedVideoFormats;
		if (formats.count) impl->config.videoFormat = formats[0];
	}
	impl->config.planeDetection = ARPlaneDetection(plane_detection);
	impl->config.autoFocusEnabled = focus_auto;
	impl->config.lightEstimationEnabled = light_estimation;
	Dictionary config;
	config["width"] = int(impl->config.videoFormat.imageResolution.width);
	config["height"] = int(impl->config.videoFormat.imageResolution.height);
	config["fps"] = int(impl->config.videoFormat.framesPerSecond);
	config["plane_detection"] = plane_detection;
	config["autofocus"] = focus_auto;
	config["light_estimation"] = light_estimation;
	diagnostics["config"] = config;
	set_state(READY);
	const uint64_t started = tick_usec();
	if (!bool(diagnostics["configured"])) impl->start_tick = started;
	if (p_reset || impl->reset_pending) impl->reset_cutoff = CACurrentMediaTime();
	[impl->session runWithConfiguration:impl->config options:(p_reset || impl->reset_pending) ? ARSessionRunOptionResetTracking | ARSessionRunOptionRemoveExistingAnchors : 0];
	timings[p_reset || impl->reset_pending ? "reset_ms" : "resume_ms"] = (tick_usec() - started) / 1000.0;
	impl->reset_pending = false;
	impl->running = true;
	tracking_status = XR_UNKNOWN_TRACKING;
	impl->interrupted = false;
	diagnostics["configured"] = true;
	diagnostics["session_id"] = String(impl->session.identifier.UUIDString.UTF8String);
	reason = BAD_STATE;
	increment("resume_count");
	set_state(INITIALIZING);
}

void ARKitInterface::application_pause() {
	if (!initialized) return;
	ERR_FAIL_COND(!on_owner_thread());
	if (suspended) return;
	suspended = true;
	const uint64_t started = tick_usec();
	[impl->session pause];
	timings["pause_ms"] = (tick_usec() - started) / 1000.0;
	impl->running = false;
	current_packet.unref(); presented_packet.unref();
	tracking_status = XR_NOT_TRACKING;
	if (head.is_valid()) head->invalidate_pose("default");
	increment("pause_count");
	set_state(PAUSED);
}

void ARKitInterface::application_resume() {
	if (!initialized) return;
	ERR_FAIL_COND(!on_owner_thread());
	const bool was_suspended = suspended;
	suspended = false;
	if (was_suspended || !impl->running) run_session(false);
}

void ARKitInterface::session_interrupted(bool p_interrupted) {
	if (!initialized) return;
	ERR_FAIL_COND(!on_owner_thread());
	impl->interrupted = p_interrupted;
	diagnostics["interrupted"] = p_interrupted;
	reason = BAD_STATE;
	if (p_interrupted) {
		current_packet.unref(); presented_packet.unref();
		head->invalidate_pose("default");
		tracking_status = XR_NOT_TRACKING;
		set_state(PAUSED);
	} else if (!suspended) {
		impl->relocalization_tick = tick_usec();
		impl->relocalization_timeout_sent = false;
		tracking_status = XR_UNKNOWN_TRACKING;
		set_state(INITIALIZING);
	}
}

void ARKitInterface::session_failed(int p_code, const String &p_message) {
	if (!initialized) return;
	ERR_FAIL_COND(!on_owner_thread());
	impl->running = false;
	diagnostics["last_error"] = p_message;
	diagnostics["arkit_error_code"] = p_code;
	current_packet.unref(); presented_packet.unref();
	head->invalidate_pose("default");
	tracking_status = XR_NOT_TRACKING;
	reason = p_code == 103 ? CAMERA_UNAVAILABLE : BAD_STATE;
	set_state(p_code == 103 ? PERMISSION_DENIED : p_code == 100 ? UNSUPPORTED : FAILED);
}

void ARKitInterface::clear_planes() {
	for (auto &entry : planes) { XRServer::get_singleton()->remove_tracker(entry.second); increment("retired_tracker_count"); }
	planes.clear();
}

void ARKitInterface::reset() {
	ERR_FAIL_COND(!initialized || !on_owner_thread());
	generation = next_ar_id();
	current_packet.unref(); presented_packet.unref();
	clear_planes();
	head->invalidate_pose("default");
	tracking_status = XR_UNKNOWN_TRACKING;
	last_good_pose = Transform3D();
	impl->last_timestamp = 0;
	impl->reset_tick = tick_usec();
	impl->reset_pending = true;
	impl->reset_samples = 0;
	impl->relocalization_tick = 0;
	impl->relocalization_timeout_sent = false;
	diagnostics["reset_frame_samples"] = Array();
	geometry_dirty = true;
	increment("reset_count");
	run_session(true);
}

void ARKitInterface::set_focus_auto(bool p_enabled) {
	if (focus_auto == p_enabled) return;
	focus_auto = p_enabled;
	if (initialized) reset();
}
void ARKitInterface::set_light_estimation_enabled(bool p_enabled) {
	if (light_estimation == p_enabled) return;
	light_estimation = p_enabled;
	if (initialized) reset();
}
void ARKitInterface::set_plane_detection(int p_mask) {
	ERR_FAIL_COND(p_mask < 0 || p_mask > 3);
	if (plane_detection == p_mask) return;
	plane_detection = p_mask;
	if (initialized) reset();
}

void ARKitInterface::set_display_geometry(const Vector2i &p_size, int, double p_near, double p_far) {
	ERR_FAIL_COND(p_size.x <= 0 || p_size.y <= 0 || !std::isfinite(p_near) || !std::isfinite(p_far) || p_near <= 0 || p_far <= p_near);
	if (p_size != viewport_size || p_near != near_plane || p_far != far_plane) geometry_dirty = true;
	viewport_size = p_size;
	near_plane = p_near;
	far_plane = p_far;
}

void ARKitInterface::copy_planes(void *p_frame, ARFramePacket &r_packet) {
	ARFrame *frame = (__bridge ARFrame *)p_frame;
	std::set<std::string> seen;
	for (ARAnchor *anchor in frame.anchors) {
		if (![anchor isKindOfClass:[ARPlaneAnchor class]]) continue;
		ARPlaneAnchor *plane = (ARPlaneAnchor *)anchor;
		const std::string key(plane.identifier.UUIDString.UTF8String);
		seen.insert(key);
		auto found = planes.find(key);
		ARPlaneSnapshot snapshot;
		if (found == planes.end()) {
			Ref<ARPlaneTracker> tracker;
			tracker.instantiate();
			snapshot.plane_id = next_ar_id();
			found = planes.emplace(key, tracker).first;
		} else snapshot.plane_id = found->second->get_plane_id();
		snapshot.session_generation = generation;
		snapshot.transform = from_simd(plane.transform);
		snapshot.alignment = plane.alignment == ARPlaneAnchorAlignmentVertical ? ARPlaneTracker::VERTICAL :
			snapshot.transform.basis.get_column(1).y < 0 ? ARPlaneTracker::HORIZONTAL_DOWNWARD_FACING : ARPlaneTracker::HORIZONTAL_UPWARD_FACING;
		snapshot.tracking_state = r_packet.tracking_valid ? ARPlaneTracker::TRACKING : ARPlaneTracker::PAUSED;
		ARPlaneGeometry *geometry = plane.geometry;
		snapshot.polygon.resize(geometry.boundaryVertexCount);
		Vector2 minimum, maximum;
		for (NSInteger i = 0; i < geometry.boundaryVertexCount; ++i) {
			const Vector2 vertex(geometry.boundaryVertices[i].x, geometry.boundaryVertices[i].z);
			snapshot.polygon[i] = vertex;
			minimum = i == 0 ? vertex : minimum.min(vertex);
			maximum = i == 0 ? vertex : maximum.max(vertex);
		}
		snapshot.extents = maximum - minimum;
		const bool added = found->second->get_plane_id() == 0;
		found->second->update(snapshot);
		if (added) XRServer::get_singleton()->add_tracker(found->second);
		r_packet.planes.push_back(snapshot);
	}
	for (auto it = planes.begin(); it != planes.end();) {
		if (seen.count(it->first)) { ++it; continue; }
		XRServer::get_singleton()->remove_tracker(it->second);
		increment("retired_tracker_count");
		it = planes.erase(it);
	}
}

void ARKitInterface::_process() {
	if (!initialized || suspended) return;
	ERR_FAIL_COND(!on_owner_thread());
	@autoreleasepool {
		const AVAuthorizationStatus authorization = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
		if (!impl->running || authorization != AVAuthorizationStatusAuthorized) {
			if (state != FAILED && state != UNSUPPORTED) run_session(false);
			if (!impl->running) return;
		}
		if (impl->interrupted) return;
		UIView *view = (__bridge UIView *)(void *)DisplayServer::get_singleton()->window_get_native_handle(DisplayServer::WINDOW_VIEW);
		UIInterfaceOrientation orientation = view.window.windowScene.effectiveGeometry.interfaceOrientation;
		if (orientation == UIInterfaceOrientationUnknown) orientation = impl->orientation != UIInterfaceOrientationUnknown ? impl->orientation :
			viewport_size.x > viewport_size.y ? UIInterfaceOrientationLandscapeRight : UIInterfaceOrientationPortrait;
		const int turns = orientation_turns(orientation);
		if (viewport_size.x != viewport_size.y && bool(turns % 2) != (viewport_size.y > viewport_size.x)) {
			increment("orientation_transition_drops");
			return;
		}
		if (orientation != impl->orientation) geometry_dirty = true;
		ARFrame *frame = impl->session.currentFrame;
		if (frame && impl->reset_cutoff > 0 && impl->reset_samples < 10) {
			Dictionary sample;
			sample["timestamp"] = frame.timestamp;
			sample["reset_cutoff"] = impl->reset_cutoff;
			sample["tracking_state"] = int(frame.camera.trackingState);
			sample["tracking_reason"] = int(frame.camera.trackingStateReason);
			sample["anchor_count"] = int64_t(frame.anchors.count);
			Array samples = diagnostics["reset_frame_samples"];
			samples.append(sample);
			++impl->reset_samples;
		}
		if (!frame || frame.timestamp <= impl->reset_cutoff) return;
		if (frame.timestamp < impl->last_timestamp) { increment("timestamp_regressions"); return; }
		const bool new_frame = frame.timestamp != impl->last_timestamp;
		if (!new_frame && !geometry_dirty) { increment("repeated_updates"); return; }
		if (new_frame) { increment("new_frame_count"); impl->last_timestamp = frame.timestamp; }
		ARCamera *camera = frame.camera;
		Ref<ARFramePacket> packet;
		packet.instantiate();
		const int64_t timestamp = llround(frame.timestamp * 1e9);
		ARKitTexturePool::Import imported;
		if (current_packet.is_valid() && current_packet->timestamp_ns == timestamp) {
			packet->y_texture = current_packet->y_texture;
			packet->cbcr_texture = current_packet->cbcr_texture;
		} else {
			if (!textures->import_image((void *)frame.capturedImage, imported)) {
				increment("dropped_frames");
				if (!textures->get_import_error().is_empty()) {
					diagnostics["last_error"] = textures->get_import_error();
					[impl->session pause];
					impl->running = false;
					tracking_status = XR_NOT_TRACKING;
					head->invalidate_pose("default");
					current_packet.unref(); presented_packet.unref();
					set_state(UNSUPPORTED);
				}
				return;
			}
			packet->y_texture = imported.y;
			packet->cbcr_texture = imported.cbcr;
		}
		packet->imported_chroma = true;
		packet->session_generation = generation;
		packet->frame_id = ++sequence;
		packet->timestamp_ns = timestamp;
		packet->timestamp_usec = llround(frame.timestamp * 1e6);
		packet->clock_domain = "arkit_host_time_seconds";
		packet->viewport_size = viewport_size;
		packet->image_size = Vector2i(camera.imageResolution.width, camera.imageResolution.height);
		packet->gpu_image_size = packet->image_size;
		packet->camera_fps = Vector2i(impl->config.videoFormat.framesPerSecond, impl->config.videoFormat.framesPerSecond);
		packet->quarter_turns = turns;
		packet->tracking_valid = camera.trackingState != ARTrackingStateNotAvailable;
		if (packet->tracking_valid) last_good_pose = from_simd(camera.transform);
		packet->physical_camera_transform = last_good_pose;
		packet->camera_transform = last_good_pose * Transform3D(arkit_view_rotation(turns), Vector3());
		packet->intrinsics = { camera.intrinsics.columns[0][0], camera.intrinsics.columns[1][1], camera.intrinsics.columns[2][0] + 0.5, camera.intrinsics.columns[2][1] + 0.5, packet->image_size };
		const CGSize size = CGSizeMake(viewport_size.x, viewport_size.y);
		const Projection native = from_projection([camera projectionMatrixForOrientation:orientation viewportSize:size zNear:0.1 zFar:100]);
		packet->projection = arkit_projection_depth(native, near_plane, far_plane);
		const CGAffineTransform uv = CGAffineTransformInvert([frame displayTransformForOrientation:orientation viewportSize:size]);
		packet->display_transform = Transform2D(uv.a, uv.b, uv.c, uv.d, uv.tx, uv.ty);
		const Projection oracle = arkit_intrinsics_projection(packet->intrinsics, viewport_size, turns, near_plane, far_plane);
		double projection_error = 0, pose_error = 0, uv_error = 0;
		const Transform3D apple_pose = from_simd(simd_inverse([camera viewMatrixForOrientation:orientation]));
		for (int c = 0; c < 4; ++c) for (int r = 0; r < 2; ++r) projection_error = std::max(projection_error, std::abs(double(oracle[c][r] - native[c][r])) * (r == 0 ? viewport_size.x : viewport_size.y) * 0.5);
		if (packet->tracking_valid) {
			pose_error = apple_pose.origin.distance_to(packet->camera_transform.origin);
			for (int c = 0; c < 3; ++c) pose_error = std::max(pose_error, double(apple_pose.basis.get_column(c).distance_to(packet->camera_transform.basis.get_column(c))));
		}
		const Transform2D uv_oracle = display_transform_for_view(packet->image_size, viewport_size, turns);
		bool uv_valid = packet->display_transform.is_finite() && std::abs(packet->display_transform.determinant()) > 1e-9;
		for (const Vector2 &corner : { Vector2(), Vector2(1, 0), Vector2(0, 1), Vector2(1, 1) }) {
			const Vector2 image_uv = packet->display_transform.xform(corner);
			uv_valid = uv_valid && image_uv.x >= -1e-5 && image_uv.x <= 1.00001 && image_uv.y >= -1e-5 && image_uv.y <= 1.00001;
			uv_error = std::max(uv_error, double(((image_uv - uv_oracle.xform(corner)) * Vector2(packet->image_size)).length()));
		}
		const bool projection_valid = native[0][0] > 0 && native[1][1] > 0 && std::isfinite(native[1][1] / native[0][0]) && std::abs(native[0][1]) < 1e-5 && std::abs(native[1][0]) < 1e-5 && std::abs(native[2][3] + 1) < 1e-5 && std::abs(native[3][3]) < 1e-5;
		diagnostics["projection_form_valid"] = bool(diagnostics["projection_form_valid"]) && projection_valid;
		diagnostics["display_transform_valid"] = bool(diagnostics["display_transform_valid"]) && uv_valid;
		diagnostics["native_depth_zero_to_one"] = bool(diagnostics["native_depth_zero_to_one"]) && std::abs(native[2][2] - 100.0 / (0.1 - 100)) < 1e-5 && std::abs(native[3][2] - 10.0 / (0.1 - 100)) < 1e-5;
		diagnostics["native_projection_z22"] = native[2][2]; diagnostics["native_projection_z32"] = native[3][2];
		diagnostics["projection_core_error_px"] = std::max(double(diagnostics["projection_core_error_px"]), projection_error);
		diagnostics["pose_error"] = std::max(double(diagnostics["pose_error"]), pose_error);
		diagnostics["display_transform_error_px"] = std::max(double(diagnostics["display_transform_error_px"]), uv_error);
		diagnostics["host_minus_frame_ms"] = (CACurrentMediaTime() - frame.timestamp) * 1000.0;
		diagnostics["orientation_turns"] = turns;
		diagnostics["committed_viewport"] = viewport_size;
		increment("projection_form_samples"); increment("display_transform_samples");
		reason = NONE;
		if (camera.trackingState == ARTrackingStateLimited) {
			switch (camera.trackingStateReason) {
				case ARTrackingStateReasonExcessiveMotion: reason = EXCESSIVE_MOTION; break;
				case ARTrackingStateReasonInsufficientFeatures: reason = INSUFFICIENT_FEATURES; break;
				default: reason = BAD_STATE; break;
			}
		} else if (!packet->tracking_valid) reason = BAD_STATE;
		const bool relocalizing = camera.trackingState == ARTrackingStateLimited && camera.trackingStateReason == ARTrackingStateReasonRelocalizing;
		diagnostics["relocalizing"] = relocalizing;
		if (relocalizing) {
			if (!impl->relocalization_tick) impl->relocalization_tick = tick_usec();
			const double elapsed = (tick_usec() - impl->relocalization_tick) / 1000.0;
			diagnostics["relocalization_ms"] = elapsed;
			if (elapsed >= 30000 && !impl->relocalization_timeout_sent) {
				impl->relocalization_timeout_sent = true;
				call_deferred("emit_signal", "relocalization_timeout", generation);
			}
		} else if (camera.trackingState == ARTrackingStateNormal) {
			impl->relocalization_tick = 0;
			impl->relocalization_timeout_sent = false;
		}
		tracking_status = camera.trackingState == ARTrackingStateNormal ? XR_NORMAL_TRACKING : packet->tracking_valid ? XR_INSUFFICIENT_FEATURES : XR_NOT_TRACKING;
		if (packet->tracking_valid) head->set_pose("default", packet->camera_transform, Vector3(), Vector3(), camera.trackingState == ARTrackingStateNormal ? XRPose::XR_TRACKING_CONFIDENCE_HIGH : XRPose::XR_TRACKING_CONFIDENCE_LOW);
		else head->invalidate_pose("default");
		copy_planes((__bridge void *)frame, *packet.ptr());
		if (!impl->first_frame) { timings["first_frame_ms"] = (tick_usec() - impl->start_tick) / 1000.0; impl->first_frame = true; }
		if (camera.trackingState == ARTrackingStateNormal) {
			if (!impl->normal_tracking) { timings["normal_tracking_ms"] = (tick_usec() - impl->start_tick) / 1000.0; impl->normal_tracking = true; }
			if (impl->reset_tick) { timings["reset_to_tracking_ms"] = (tick_usec() - impl->reset_tick) / 1000.0; impl->reset_tick = 0; }
		}
		impl->orientation = orientation;
		geometry_dirty = false;
		current_packet = packet;
		for (const Vector2 &clip_range : { Vector2(0.07, 17), Vector2(0.23, 131) }) {
			const PackedFloat64Array values = _get_projection_for_view(0, double(viewport_size.x) / viewport_size.y, clip_range.x, clip_range.y);
			Projection clipped;
			for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) clipped[c][r] = values[c * 4 + r];
			const Vector4 n = clipped.xform(Vector4(0, 0, -clip_range.x, 1));
			const Vector4 f = clipped.xform(Vector4(0, 0, -clip_range.y, 1));
			const double error = std::max(std::abs(double(n.z / n.w + 1)), std::abs(double(f.z / f.w - 1)));
			diagnostics["projection_near_far_error"] = std::max(double(diagnostics["projection_near_far_error"]), error);
		}
		increment("packets_published");
		set_state(camera.trackingState == ARTrackingStateNormal ? TRACKING : INITIALIZING);
	}
}

Vector2 ARKitInterface::_get_render_target_size() { return current_packet.is_valid() ? current_packet->get_viewport_size() : Vector2(viewport_size); }
Transform3D ARKitInterface::_get_camera_transform() { return current_packet.is_valid() ? current_packet->get_camera_transform() : Transform3D(); }
Transform3D ARKitInterface::_get_transform_for_view(uint32_t, const Transform3D &p_transform) { return p_transform * _get_camera_transform(); }

PackedFloat64Array ARKitInterface::_get_projection_for_view(uint32_t, double p_aspect, double p_near, double p_far) {
	PackedFloat64Array values;
	values.resize(16);
	Projection projection = current_packet.is_valid() ? current_packet->get_projection() : Projection();
	if (std::isfinite(p_near) && std::isfinite(p_far) && p_near > 0 && p_far > p_near) projection = arkit_projection_depth(projection, p_near, p_far);
	if (current_packet.is_valid() && std::isfinite(p_aspect) && p_aspect > 0) {
		const Vector2 size = current_packet->get_viewport_size();
		diagnostics["projection_aspect_mismatch"] = std::abs(p_aspect - size.x / size.y) > 1e-3;
		projection = arkit_projection_aspect(projection, current_packet->get_image_size(), size, current_packet->get_quarter_turns(), p_aspect);
	}
	for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) values[c * 4 + r] = projection[c][r];
	return values;
}

bool ARKitInterface::_pre_draw_viewport(const RID &) {
	return current_packet.is_valid() && current_packet->get_session_generation() == generation && textures->check_sample(current_packet);
}

void ARKitInterface::mark_presented() {
	if (current_packet.is_valid() && current_packet->get_session_generation() == generation) { presented_packet = current_packet; increment("packets_presented"); }
}

Array ARKitInterface::live_hit_test(const Vector2 &p_pixels) {
	Array result;
	ERR_FAIL_COND_V(!initialized || !on_owner_thread(), result);
	if (!impl->running || impl->interrupted || !p_pixels.is_finite() || current_packet.is_null()) return result;
	const Vector2 size = current_packet->get_viewport_size();
	if (p_pixels.x < 0 || p_pixels.y < 0 || p_pixels.x > size.x || p_pixels.y > size.y) return result;
	@autoreleasepool {
		ARFrame *frame = impl->session.currentFrame;
		if (!frame || frame.timestamp <= impl->reset_cutoff || frame.camera.trackingState == ARTrackingStateNotAvailable) return result;
		const Vector2 uv = current_packet->get_display_transform().xform(p_pixels / current_packet->get_viewport_size());
		ARRaycastQuery *query = [frame raycastQueryFromPoint:CGPointMake(uv.x, uv.y) allowingTarget:ARRaycastTargetExistingPlaneGeometry alignment:ARRaycastTargetAlignmentAny];
		for (ARRaycastResult *hit in [impl->session raycast:query]) {
			Dictionary entry;
			entry["query_id"] = next_ar_id();
			entry["session_generation"] = generation;
			entry["frame_id"] = llround(frame.timestamp * 1e9) == current_packet->get_timestamp_ns() ? current_packet->get_frame_id() : int64_t(0);
			entry["timestamp_ns"] = int64_t(llround(frame.timestamp * 1e9));
			entry["live"] = true;
			entry["pose"] = from_simd(hit.worldTransform);
			entry["distance"] = from_simd(hit.worldTransform).origin.distance_to(from_simd(frame.camera.transform).origin);
			const auto plane = hit.anchor ? planes.find(std::string(hit.anchor.identifier.UUIDString.UTF8String)) : planes.end();
			entry["plane_id"] = plane != planes.end() ? plane->second->get_plane_id() : int64_t(0);
			result.append(entry);
		}
	}
	return result;
}

Dictionary ARKitInterface::get_diagnostics() const {
	Dictionary result = diagnostics.duplicate(true);
	if (textures.is_valid()) result.merge(textures->get_diagnostics(), true);
	result["timings"] = timings.duplicate(true);
	result["state_history"] = state_history.duplicate();
	result["plane_count"] = int64_t(planes.size());
	result["session_generation"] = generation;
	result["state"] = int(state);
	result["reason"] = int(reason);
	int stale = 0;
	const Dictionary trackers = XRServer::get_singleton()->get_trackers(XRServer::TRACKER_ANCHOR);
	const Array names = trackers.keys();
	for (int i = 0; i < names.size(); ++i) {
		Object *object = trackers[names[i]];
		ARPlaneTracker *plane = Object::cast_to<ARPlaneTracker>(object);
		if (plane && plane->get_session_generation() != generation) ++stale;
	}
	result["stale_tracker_count"] = stale;
	return result;
}

}
