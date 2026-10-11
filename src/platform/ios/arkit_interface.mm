#include "arkit_interface.h"
#include "arkit_math.h"
#include "core/ar_ids.h"
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/os.hpp>
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
	ARKitOrientationTransition orientation_transition;
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

static AVAuthorizationStatus camera_authorization(Dictionary &r_diagnostics) {
	const AVAuthorizationStatus authorization = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
	r_diagnostics["camera_authorization"] = int(authorization);
	switch (authorization) {
		case AVAuthorizationStatusNotDetermined: r_diagnostics["camera_authorization_name"] = "not_determined"; break;
		case AVAuthorizationStatusRestricted: r_diagnostics["camera_authorization_name"] = "restricted"; break;
		case AVAuthorizationStatusDenied: r_diagnostics["camera_authorization_name"] = "denied"; break;
		case AVAuthorizationStatusAuthorized: r_diagnostics["camera_authorization_name"] = "authorized"; break;
	}
	return authorization;
}

ARKitInterface::ARKitInterface() : impl(new Impl) {}
ARKitInterface::~ARKitInterface() { if (initialized) _uninitialize(); }

void ARKitInterface::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_session_state"), &ARKitInterface::get_session_state);
	ClassDB::bind_method(D_METHOD("get_tracking_reason"), &ARKitInterface::get_tracking_reason);
	ClassDB::bind_method(D_METHOD("get_session_generation"), &ARKitInterface::get_session_generation);
	ClassDB::bind_method(D_METHOD("get_diagnostics"), &ARKitInterface::get_diagnostics);
	ClassDB::bind_method(D_METHOD("get_native_plane_identifiers"), &ARKitInterface::get_native_plane_identifiers);
	ClassDB::bind_method(D_METHOD("get_native_anchor_count"), &ARKitInterface::get_native_anchor_count);
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
	ClassDB::bind_method(D_METHOD("mark_applied", "packet"), &ARKitInterface::mark_applied);
	ClassDB::bind_method(D_METHOD("mark_presented"), &ARKitInterface::mark_presented);
	ClassDB::bind_method(D_METHOD("debug_check_camera_material", "material"), &ARKitInterface::debug_check_camera_material);
	ClassDB::bind_method(D_METHOD("debug_set_camera_material", "material"), &ARKitInterface::debug_set_camera_material);
	ClassDB::bind_method(D_METHOD("application_pause"), &ARKitInterface::application_pause);
	ClassDB::bind_method(D_METHOD("application_resume"), &ARKitInterface::application_resume);
	ClassDB::bind_method(D_METHOD("harness_pause"), &ARKitInterface::application_pause);
	ClassDB::bind_method(D_METHOD("harness_resume"), &ARKitInterface::application_resume);
	ClassDB::bind_method(D_METHOD("live_hit_test", "view_pixels"), &ARKitInterface::live_hit_test);
	ADD_SIGNAL(MethodInfo("session_state_changed", PropertyInfo(Variant::INT, "state")));
	ADD_SIGNAL(MethodInfo("tracking_reason_changed", PropertyInfo(Variant::INT, "reason")));
	ADD_SIGNAL(MethodInfo("session_generation_changed", PropertyInfo(Variant::INT, "generation")));
	ADD_SIGNAL(MethodInfo("planes_changed"));
	BIND_ENUM_CONSTANT(UNSUPPORTED); BIND_ENUM_CONSTANT(CHECKING); BIND_ENUM_CONSTANT(NEEDS_INSTALL);
	BIND_ENUM_CONSTANT(INSTALLING); BIND_ENUM_CONSTANT(PERMISSION_DENIED); BIND_ENUM_CONSTANT(READY);
	BIND_ENUM_CONSTANT(INITIALIZING); BIND_ENUM_CONSTANT(TRACKING); BIND_ENUM_CONSTANT(PAUSED); BIND_ENUM_CONSTANT(FAILED);
	BIND_ENUM_CONSTANT(NONE); BIND_ENUM_CONSTANT(BAD_STATE); BIND_ENUM_CONSTANT(INSUFFICIENT_LIGHT);
	BIND_ENUM_CONSTANT(EXCESSIVE_MOTION); BIND_ENUM_CONSTANT(INSUFFICIENT_FEATURES); BIND_ENUM_CONSTANT(CAMERA_UNAVAILABLE);
}

bool ARKitInterface::on_owner_thread() const { return [NSThread isMainThread] && RenderingServer::get_singleton()->is_on_render_thread(); }
void ARKitInterface::increment(const char *p_key) { diagnostics[p_key] = int64_t(diagnostics.get(p_key, 0)) + 1; }
void ARKitInterface::set_state(SessionState p_state) {
	const bool changed = state != p_state;
	if (!changed && !state_history.is_empty()) return;
	state = p_state;
	state_history.append(int(state));
	UtilityFunctions::print("ARKIT state=", int(state), " generation=", generation, " reason=", int(reason));
	if (changed) call_deferred("emit_signal", "session_state_changed", int(state));
}
void ARKitInterface::set_reason(TrackingReason p_reason) {
	if (reason == p_reason) return;
	reason = p_reason;
	call_deferred("emit_signal", "tracking_reason_changed", int(reason));
}

bool ARKitInterface::_initialize() {
	if (initialized) return true;
	impl.reset(new Impl);
	suspended = false;
	geometry_dirty = true;
	last_good_pose = Transform3D();
	tracking_status = XR_UNKNOWN_TRACKING;
	set_reason(NONE);
	diagnostics.clear(); timings.clear(); state_history.clear();
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs->get_current_rendering_driver_name() != "metal" || !rs->get_rendering_device()) {
		diagnostics["last_error"] = "ARKit zero-copy camera requires the Metal rendering driver";
		set_state(UNSUPPORTED);
		return false;
	}
	ERR_FAIL_COND_V_MSG(!on_owner_thread(), false, "ARKit requires the main thread and Safe rendering thread mode.");
	camera_authorization(diagnostics);
	ERR_FAIL_COND_V_MSG(XRServer::get_singleton()->get_primary_interface().is_valid(), false, "Only one ARView may own the primary XR interface.");
	if (![ARWorldTrackingConfiguration isSupported]) {
		diagnostics["last_error"] = "ARWorldTrackingConfiguration is unsupported on this device";
		set_state(UNSUPPORTED);
		return false;
	}
	textures = arkit_texture_pool();
	if (textures.is_null()) { diagnostics["last_error"] = "Metal retain probe or CoreVideo texture cache failed"; set_state(UNSUPPORTED); return false; }
	for (const char *key : { "new_frame_count", "packets_published", "packets_presented", "repeated_updates", "dropped_frames", "texture_refusals", "timestamp_regressions", "projection_form_samples", "projection_aspect_mismatch_count", "display_transform_samples", "pause_count", "resume_count", "reset_count", "retired_tracker_count", "orientation_transition_drops" }) diagnostics[key] = int64_t(0);
	diagnostics["projection_form_valid"] = true;
	diagnostics["display_transform_valid"] = true;
	diagnostics["native_depth_zero_to_one"] = true;
	diagnostics["projection_near_far_error"] = 0.0;
	diagnostics["projection_core_error_px"] = 0.0;
	diagnostics["projection_half_pixel_error_px"] = 0.0;
	diagnostics["projection_no_half_pixel_error_px"] = 0.0;
	diagnostics["projection_aspect_mismatch"] = false;
	diagnostics["orientations_seen"] = Array();
	diagnostics["reset_frame_samples"] = Array();
	diagnostics["plane_extent_rotation_sign"] = "positive_Y_assumed_pending_device_overlay";
	UtilityFunctions::print("ARKIT plane_extent_rotation=positive_Y assumption_requires_device_overlay");
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
	if (state == CHECKING) call_deferred("emit_signal", "session_state_changed", int(CHECKING));
	set_state(CHECKING);
	call_deferred("emit_signal", "session_generation_changed", generation);
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
	current_packet.unref(); presented_packet.unref(); applied_packet.unref();
	clear_planes();
	textures->set_camera_material(RID());
	textures->clear_active_packets();
	textures.unref();
	XRServer::get_singleton()->remove_tracker(head);
	head.unref();
	set_primary(false);
	initialized = false;
	set_state(PAUSED);
	active_interfaces.erase(this);
}

void ARKitInterface::run_session(bool p_reset) {
	ERR_FAIL_COND(!on_owner_thread());
	if (suspended) return;
	const AVAuthorizationStatus authorization = camera_authorization(diagnostics);
	if (authorization == AVAuthorizationStatusDenied || authorization == AVAuthorizationStatusRestricted) {
		if (impl->running) [impl->session pause];
		impl->running = false;
		clear_packets();
		head->invalidate_pose("default");
		tracking_status = XR_NOT_TRACKING;
		set_reason(CAMERA_UNAVAILABLE);
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
	reset_relocalization();
	tracking_status = XR_UNKNOWN_TRACKING;
	impl->interrupted = false;
	diagnostics["interrupted"] = false;
	diagnostics["configured"] = true;
	diagnostics["session_id"] = String(impl->session.identifier.UUIDString.UTF8String);
	set_reason(NONE);
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
	clear_packets();
	reset_relocalization();
	impl->interrupted = false;
	diagnostics["interrupted"] = false;
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
	if (!impl->running || suspended) return;
	impl->interrupted = p_interrupted;
	diagnostics["interrupted"] = p_interrupted;
	set_reason(p_interrupted ? CAMERA_UNAVAILABLE : NONE);
	if (p_interrupted) {
		clear_packets();
		head->invalidate_pose("default");
		tracking_status = XR_NOT_TRACKING;
		set_state(PAUSED);
	} else {
		reset_relocalization();
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
	clear_packets();
	head->invalidate_pose("default");
	tracking_status = XR_NOT_TRACKING;
	set_reason(p_code == 103 ? CAMERA_UNAVAILABLE : BAD_STATE);
	set_state(p_code == 103 ? PERMISSION_DENIED : p_code == 100 ? UNSUPPORTED : FAILED);
}

void ARKitInterface::clear_packets() {
	current_packet.unref(); presented_packet.unref();
	if (textures.is_valid()) textures->set_active_packets(current_packet, presented_packet, applied_packet);
}

void ARKitInterface::reset_relocalization() {
	impl->relocalization_tick = 0;
	impl->relocalization_timeout_sent = false;
	diagnostics["relocalizing"] = false;
	diagnostics["relocalization_ms"] = 0.0;
}

void ARKitInterface::clear_planes() {
	const bool changed = !published_planes.empty();
	for (auto &entry : planes) { XRServer::get_singleton()->remove_tracker(entry.second); increment("retired_tracker_count"); }
	planes.clear();
	published_planes.clear();
	native_anchor_count = 0;
	if (changed) call_deferred("emit_signal", "planes_changed");
}

void ARKitInterface::reset() {
	ERR_FAIL_COND(!initialized || !on_owner_thread());
	generation = next_ar_id();
	clear_packets();
	clear_planes();
	head->invalidate_pose("default");
	tracking_status = XR_UNKNOWN_TRACKING;
	last_good_pose = Transform3D();
	impl->last_timestamp = 0;
	impl->reset_tick = tick_usec();
	impl->reset_pending = true;
	impl->reset_samples = 0;
	reset_relocalization();
	set_reason(NONE);
	geometry_dirty = true;
	increment("reset_count");
	call_deferred("emit_signal", "session_generation_changed", generation);
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

bool ARKitInterface::copy_planes(void *p_frame, ARFramePacket &r_packet) {
	ARFrame *frame = (__bridge ARFrame *)p_frame;
	native_anchor_count = int64_t(frame.anchors.count);
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
		const Transform3D local_frame = arkit_plane_local_frame(Vector3(plane.center.x, plane.center.y, plane.center.z), plane.planeExtent.rotationOnYAxis);
		const Transform3D anchor_to_plane = local_frame.affine_inverse();
		snapshot.transform = from_simd(plane.transform) * local_frame;
		snapshot.alignment = plane.alignment == ARPlaneAnchorAlignmentVertical ? ARPlaneTracker::VERTICAL :
			snapshot.transform.basis.get_column(1).y < 0 ? ARPlaneTracker::HORIZONTAL_DOWNWARD_FACING : ARPlaneTracker::HORIZONTAL_UPWARD_FACING;
		snapshot.tracking_state = frame.camera.trackingState == ARTrackingStateNormal ? ARPlaneTracker::TRACKING : ARPlaneTracker::PAUSED;
		ARPlaneGeometry *geometry = plane.geometry;
		snapshot.polygon.resize(geometry.boundaryVertexCount);
		for (NSInteger i = 0; i < geometry.boundaryVertexCount; ++i) {
			const auto &boundary = geometry.boundaryVertices[i];
			const Vector3 vertex = anchor_to_plane.xform(Vector3(boundary.x, boundary.y, boundary.z));
			snapshot.polygon[i] = Vector2(vertex.x, vertex.z);
		}
		snapshot.extents = Vector2(plane.planeExtent.width, plane.planeExtent.height);
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
	const bool changed = r_packet.planes != published_planes;
	published_planes = r_packet.planes;
	return changed;
}

void ARKitInterface::_process() {
	if (!initialized || suspended) return;
	ERR_FAIL_COND(!on_owner_thread());
	@autoreleasepool {
		const AVAuthorizationStatus authorization = camera_authorization(diagnostics);
		if (!impl->running || authorization != AVAuthorizationStatusAuthorized) {
			if (state != FAILED && state != UNSUPPORTED) run_session(false);
			if (!impl->running) return;
		}
		UIView *view = (__bridge UIView *)(void *)DisplayServer::get_singleton()->window_get_native_handle(DisplayServer::WINDOW_VIEW);
		const CGSize bounds = view.window.bounds.size;
		const Vector2 window_size(bounds.width, bounds.height);
		UIInterfaceOrientation orientation = view.window.windowScene.effectiveGeometry.interfaceOrientation;
		if (orientation == UIInterfaceOrientationUnknown) orientation = impl->orientation != UIInterfaceOrientationUnknown ? impl->orientation :
			window_size.x > window_size.y ? UIInterfaceOrientationLandscapeRight : UIInterfaceOrientationPortrait;
		const int turns = orientation_turns(orientation);
		if (impl->orientation_transition.wait_for_resize(turns, window_size, tick_usec())) {
			increment("orientation_transition_drops");
			return;
		}
		if (orientation != impl->orientation) geometry_dirty = true;
		ARFrame *frame = impl->session.currentFrame;
		if (frame && impl->reset_cutoff > 0 && impl->reset_samples < 10) {
			Dictionary sample;
			sample["timestamp"] = frame.timestamp;
			sample["reset_cutoff"] = impl->reset_cutoff;
			sample["session_generation"] = generation;
			sample["tracking_state"] = int(frame.camera.trackingState);
			sample["tracking_reason"] = int(frame.camera.trackingStateReason);
			sample["anchor_count"] = int64_t(frame.anchors.count);
			sample["accepted"] = frame.timestamp > impl->reset_cutoff && frame.timestamp >= impl->last_timestamp && (frame.timestamp != impl->last_timestamp || (geometry_dirty && !impl->interrupted));
			Array plane_uuids;
			for (ARAnchor *anchor in frame.anchors) if ([anchor isKindOfClass:[ARPlaneAnchor class]]) plane_uuids.append(String(anchor.identifier.UUIDString.UTF8String));
			sample["plane_uuids"] = plane_uuids;
			Array samples = diagnostics["reset_frame_samples"];
			if (samples.size() >= 100) samples.pop_front();
			samples.append(sample);
			++impl->reset_samples;
		}
		if (!frame || frame.timestamp <= impl->reset_cutoff) return;
		if (frame.timestamp < impl->last_timestamp) { increment("timestamp_regressions"); return; }
		const bool new_frame = frame.timestamp != impl->last_timestamp;
		if (!new_frame && (impl->interrupted || !geometry_dirty)) { increment("repeated_updates"); return; }
		if (new_frame) {
			increment("new_frame_count"); impl->last_timestamp = frame.timestamp;
			impl->interrupted = false;
			diagnostics["interrupted"] = false;
		}
		ARCamera *camera = frame.camera;
		Ref<ARFramePacket> packet;
		packet.instantiate();
		const int64_t timestamp = llround(frame.timestamp * 1e9);
		ARKitTexturePool::Import imported;
		if (current_packet.is_valid() && current_packet->timestamp_ns == timestamp) {
			packet->y_texture = current_packet->y_texture;
			packet->cbcr_texture = current_packet->cbcr_texture;
			packet->color_encoding = current_packet->color_encoding;
		} else {
			if (!textures->import_image((void *)frame.capturedImage, imported)) {
				increment("texture_refusals");
				if (!textures->get_import_error().is_empty()) {
					diagnostics["last_error"] = textures->get_import_error();
					[impl->session pause];
					impl->running = false;
					tracking_status = XR_NOT_TRACKING;
					head->invalidate_pose("default");
					clear_packets();
					set_reason(BAD_STATE);
					set_state(UNSUPPORTED);
					return;
				}
				if (current_packet.is_valid()) {
					packet->y_texture = current_packet->y_texture;
					packet->cbcr_texture = current_packet->cbcr_texture;
					packet->color_encoding = current_packet->color_encoding;
				}
			} else {
				packet->y_texture = imported.y;
				packet->cbcr_texture = imported.cbcr;
				packet->color_encoding = imported.color;
			}
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
		packet->tracking_valid = camera.trackingState == ARTrackingStateNormal || camera.trackingState == ARTrackingStateLimited;
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
		ARIntrinsics no_half_pixel = packet->intrinsics;
		no_half_pixel.cx -= 0.5; no_half_pixel.cy -= 0.5;
		const Projection alternate = arkit_intrinsics_projection(no_half_pixel, viewport_size, turns, near_plane, far_plane);
		const double projection_error = arkit_projection_residual_px(native, oracle, viewport_size);
		const double alternate_error = arkit_projection_residual_px(native, alternate, viewport_size);
		double pose_error = 0, uv_error = 0;
		const Transform3D apple_pose = from_simd(simd_inverse([camera viewMatrixForOrientation:orientation]));
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
		diagnostics["projection_half_pixel_error_px"] = std::max(double(diagnostics["projection_half_pixel_error_px"]), projection_error);
		diagnostics["projection_no_half_pixel_error_px"] = std::max(double(diagnostics["projection_no_half_pixel_error_px"]), alternate_error);
		diagnostics["pose_error"] = std::max(double(diagnostics["pose_error"]), pose_error);
		diagnostics["display_transform_error_px"] = std::max(double(diagnostics["display_transform_error_px"]), uv_error);
		diagnostics["host_minus_frame_ms"] = (CACurrentMediaTime() - frame.timestamp) * 1000.0;
		diagnostics["orientation_turns"] = turns;
		Array orientations_seen = diagnostics["orientations_seen"];
		if (!orientations_seen.has(turns)) orientations_seen.append(turns);
		diagnostics["committed_viewport"] = viewport_size;
		increment("projection_form_samples"); increment("display_transform_samples");
		TrackingReason next_reason = NONE;
		if (camera.trackingState == ARTrackingStateLimited) {
			switch (camera.trackingStateReason) {
				case ARTrackingStateReasonNone:
				case ARTrackingStateReasonInitializing:
				case ARTrackingStateReasonRelocalizing: next_reason = NONE; break;
				case ARTrackingStateReasonExcessiveMotion: next_reason = EXCESSIVE_MOTION; break;
				case ARTrackingStateReasonInsufficientFeatures: next_reason = INSUFFICIENT_FEATURES; break;
				default: next_reason = BAD_STATE; break;
			}
		} else if (camera.trackingState != ARTrackingStateNormal && camera.trackingState != ARTrackingStateNotAvailable) next_reason = BAD_STATE;
		set_reason(next_reason);
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
			reset_relocalization();
		}
		tracking_status = camera.trackingState == ARTrackingStateNormal ? XR_NORMAL_TRACKING :
			reason == EXCESSIVE_MOTION ? XR_EXCESSIVE_MOTION : reason == INSUFFICIENT_FEATURES ? XR_INSUFFICIENT_FEATURES :
			reason == NONE ? XR_UNKNOWN_TRACKING : XR_NOT_TRACKING;
		if (packet->tracking_valid) head->set_pose("default", packet->camera_transform, Vector3(), Vector3(), camera.trackingState == ARTrackingStateNormal ? XRPose::XR_TRACKING_CONFIDENCE_HIGH : XRPose::XR_TRACKING_CONFIDENCE_LOW);
		else head->invalidate_pose("default");
		const bool changed_planes = copy_planes((__bridge void *)frame, *packet.ptr());
		if (!impl->first_frame) { timings["first_frame_ms"] = (tick_usec() - impl->start_tick) / 1000.0; impl->first_frame = true; }
		if (camera.trackingState == ARTrackingStateNormal) {
			if (!impl->normal_tracking) { timings["normal_tracking_ms"] = (tick_usec() - impl->start_tick) / 1000.0; impl->normal_tracking = true; }
			if (impl->reset_tick) { timings["reset_to_tracking_ms"] = (tick_usec() - impl->reset_tick) / 1000.0; impl->reset_tick = 0; }
		}
		impl->orientation = orientation;
		geometry_dirty = false;
		current_packet = packet;
		textures->set_active_packets(current_packet, presented_packet, applied_packet);
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
		if (changed_planes) call_deferred("emit_signal", "planes_changed");
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
		if (std::abs(p_aspect - size.x / size.y) > 1e-3) {
			diagnostics["projection_aspect_mismatch"] = true;
			increment("projection_aspect_mismatch_count");
		}
		projection = arkit_projection_aspect(projection, current_packet->get_image_size(), size, current_packet->get_quarter_turns(), p_aspect);
	}
	for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) values[c * 4 + r] = projection[c][r];
	return values;
}

bool ARKitInterface::_pre_draw_viewport(const RID &) {
	if (applied_packet.is_null() || applied_packet->get_session_generation() != generation) return false;
	return (applied_packet->y_texture.is_null() && applied_packet->cbcr_texture.is_null()) || textures->check_sample(applied_packet);
}

void ARKitInterface::mark_applied(const Ref<ARFramePacket> &p_packet) {
	ERR_FAIL_COND(!initialized || !on_owner_thread());
	ERR_FAIL_COND(p_packet.is_valid() && p_packet->get_session_generation() != generation);
	ERR_FAIL_COND(p_packet.is_valid() && (p_packet->y_texture.is_valid() || p_packet->cbcr_texture.is_valid()) && !textures->check_sample(p_packet));
	applied_packet = p_packet;
	textures->set_active_packets(current_packet, presented_packet, applied_packet);
}

void ARKitInterface::mark_presented() {
	if (applied_packet.is_valid() && applied_packet->get_session_generation() == generation) {
		presented_packet = applied_packet;
		textures->set_active_packets(current_packet, presented_packet, applied_packet);
		increment("packets_presented");
	}
}

void ARKitInterface::debug_set_camera_material(const RID &p_material) {
	ERR_FAIL_COND(!initialized || !on_owner_thread());
	textures->set_camera_material(p_material);
}

bool ARKitInterface::debug_check_camera_material(const RID &p_material) {
	ERR_FAIL_COND_V(!initialized || !on_owner_thread(), false);
	return textures->check_material(p_material);
}

Array ARKitInterface::live_hit_test(const Vector2 &p_pixels) {
	Array result;
	ERR_FAIL_COND_V(!initialized || !on_owner_thread(), result);
	if (!impl->running || impl->interrupted || !p_pixels.is_finite() || current_packet.is_null()) return result;
	const Vector2 size = current_packet->get_viewport_size();
	if (p_pixels.x < 0 || p_pixels.y < 0 || p_pixels.x > size.x || p_pixels.y > size.y) return result;
	@autoreleasepool {
		ARFrame *frame = impl->session.currentFrame;
		if (!frame || frame.timestamp <= impl->reset_cutoff || frame.camera.trackingState != ARTrackingStateNormal) return result;
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

Dictionary ARKitInterface::get_native_plane_identifiers() const {
	Dictionary identifiers;
	for (const auto &entry : planes) identifiers[String::num_int64(entry.second->get_plane_id())] = String(entry.first.c_str());
	return identifiers;
}

Dictionary ARKitInterface::get_diagnostics() const {
	Dictionary result = diagnostics.duplicate(true);
	if (textures.is_valid()) result.merge(textures->get_diagnostics(), true);
	result["timings"] = timings.duplicate(true);
	result["state_history"] = state_history.duplicate();
	result["plane_count"] = int64_t(planes.size());
	result["native_plane_uuids"] = get_native_plane_identifiers();
	result["native_anchor_count"] = native_anchor_count;
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
