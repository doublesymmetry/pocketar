#include "arkit_interface.h"
#include "arkit_math.h"
#include "arkit_anchors.h"
#include "core/ar_ids.h"
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/xr_server.hpp>
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
- (void)session:(ARSession *)session didAddAnchors:(NSArray<ARAnchor *> *)anchors {
	if (self.owner) self.owner->anchors_changed((__bridge void *)session, (__bridge void *)anchors, false);
}
- (void)session:(ARSession *)session didUpdateAnchors:(NSArray<ARAnchor *> *)anchors {
	if (self.owner) self.owner->anchors_changed((__bridge void *)session, (__bridge void *)anchors, false);
}
- (void)session:(ARSession *)session didRemoveAnchors:(NSArray<ARAnchor *> *)anchors {
	if (self.owner) self.owner->anchors_changed((__bridge void *)session, (__bridge void *)anchors, true);
}
- (BOOL)sessionShouldAttemptRelocalization:(ARSession *)session {
	return YES;
}
@end

namespace godot {

struct ARKitInterface::Impl {
	ARSession *session = nil;
	ARWorldTrackingConfiguration *config = nil;
	HARSessionDelegate *delegate = nil;
	bool running = false, interrupted = false, permission_requested = false;
	bool reset_pending = false;
	double last_timestamp = 0, reset_cutoff = 0;
	uint64_t start_tick = 0;
	bool first_frame = false;
	UIInterfaceOrientation orientation = UIInterfaceOrientationUnknown;
	ARKitOrientationTransition orientation_transition;
	pocketar::ARKitAnchors<ARAnchor *, Transform3D> anchors;
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

static simd_float4x4 to_simd(const Transform3D &p_transform) {
	simd_float4x4 result = matrix_identity_float4x4;
	for (int column = 0; column < 3; ++column) {
		const Vector3 axis = p_transform.basis.get_column(column);
		result.columns[column] = simd_make_float4(axis.x, axis.y, axis.z, 0);
	}
	result.columns[3] = simd_make_float4(p_transform.origin.x, p_transform.origin.y, p_transform.origin.z, 1);
	return result;
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

static std::array<uint8_t, 16> anchor_key(NSUUID *p_identifier) {
	std::array<uint8_t, 16> key;
	[p_identifier getUUIDBytes:key.data()];
	return key;
}

static AVAuthorizationStatus camera_authorization(Dictionary &r_diagnostics) {
	const AVAuthorizationStatus authorization = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
	static const char *names[] = { "not_determined", "restricted", "denied", "authorized" };
	r_diagnostics["camera_authorization_name"] = authorization >= 0 && authorization < 4 ? names[authorization] : "unknown";
	return authorization;
}

ARKitInterface::ARKitInterface() : impl(new Impl) {
	anchors.configure(this, [this](int64_t p_id) {
		if (ARAnchor *anchor = impl->anchors.remove(p_id)) [impl->session removeAnchor:anchor];
	});
}
ARKitInterface::~ARKitInterface() { if (initialized) _uninitialize(); }

#ifdef DEBUG_ENABLED
int64_t ARKitInterface::set_debug_plane(const Transform3D &p_transform, const Vector2 &p_extents) {
	ERR_FAIL_COND_V(initialized && !on_owner_thread(), 0);
	return debug_plane.set(p_transform, p_extents);
}

void ARKitInterface::clear_debug_plane() {
	ERR_FAIL_COND(initialized && !on_owner_thread());
	debug_plane.request_clear();
}
#endif

void ARKitInterface::_bind_methods() {
#ifdef DEBUG_ENABLED
	ClassDB::bind_method(D_METHOD("set_debug_plane", "transform", "extents"), &ARKitInterface::set_debug_plane);
	ClassDB::bind_method(D_METHOD("clear_debug_plane"), &ARKitInterface::clear_debug_plane);
#endif
	ClassDB::bind_method(D_METHOD("get_session_state"), &ARKitInterface::get_session_state);
	ClassDB::bind_method(D_METHOD("get_tracking_reason"), &ARKitInterface::get_tracking_reason);
	ClassDB::bind_method(D_METHOD("get_session_generation"), &ARKitInterface::get_session_generation);
	ClassDB::bind_method(D_METHOD("get_diagnostics"), &ARKitInterface::get_diagnostics);
	ClassDB::bind_method(D_METHOD("set_diagnostics_enabled", "enabled"), &ARKitInterface::set_diagnostics_enabled);
	ClassDB::bind_method(D_METHOD("get_native_plane_identifiers"), &ARKitInterface::get_native_plane_identifiers);
	ClassDB::bind_method(D_METHOD("reset"), &ARKitInterface::reset);
	ClassDB::bind_method(D_METHOD("create_anchor", "pose"), &ARKitInterface::create_anchor);
	ClassDB::bind_method(D_METHOD("create_anchor_from_hit", "hit"), &ARKitInterface::create_anchor_from_hit);
	ClassDB::bind_method(D_METHOD("remove_anchor", "anchor"), &ARKitInterface::remove_anchor);
	ClassDB::bind_method(D_METHOD("owns_anchor", "anchor"), &ARKitInterface::owns_anchor);
	ClassDB::bind_method(D_METHOD("set_plane_finding_mode", "mode"), &ARKitInterface::set_plane_finding_mode);
	ClassDB::bind_method(D_METHOD("get_plane_finding_mode"), &ARKitInterface::get_plane_finding_mode);
	ClassDB::bind_method(D_METHOD("set_suspended", "suspended"), &ARKitInterface::set_suspended);
	ClassDB::bind_method(D_METHOD("set_camera_quality", "quality"), &ARKitInterface::set_camera_quality);
	ClassDB::bind_method(D_METHOD("set_camera_frame_rate", "fps"), &ARKitInterface::set_camera_frame_rate);
	ClassDB::bind_method(D_METHOD("set_display_geometry", "size", "turns", "near", "far"), &ARKitInterface::set_display_geometry);
	ClassDB::bind_method(D_METHOD("get_current_packet"), &ARKitInterface::get_current_packet);
	ClassDB::bind_method(D_METHOD("get_presented_packet"), &ARKitInterface::get_presented_packet);
	ClassDB::bind_method(D_METHOD("mark_applied", "packet"), &ARKitInterface::mark_applied);
	ClassDB::bind_method(D_METHOD("mark_presented"), &ARKitInterface::mark_presented);
	ClassDB::bind_method(D_METHOD("debug_check_camera_material", "material"), &ARKitInterface::debug_check_camera_material);
	ClassDB::bind_method(D_METHOD("application_pause"), &ARKitInterface::application_pause);
	ClassDB::bind_method(D_METHOD("application_resume"), &ARKitInterface::application_resume);
	ClassDB::bind_method(D_METHOD("live_hit_test", "view_pixels"), &ARKitInterface::live_hit_test);
	ADD_SIGNAL(MethodInfo("session_state_changed", PropertyInfo(Variant::INT, "state")));
	ADD_SIGNAL(MethodInfo("tracking_reason_changed", PropertyInfo(Variant::INT, "reason")));
	ADD_SIGNAL(MethodInfo("session_generation_changed", PropertyInfo(Variant::INT, "generation")));
	ADD_SIGNAL(MethodInfo("planes_changed"));
	ADD_SIGNAL(MethodInfo("anchors_changed"));
}

bool ARKitInterface::on_owner_thread() const { return [NSThread isMainThread] && RenderingServer::get_singleton()->is_on_render_thread(); }
void ARKitInterface::increment(const char *p_key) { diagnostics[p_key] = int64_t(diagnostics.get(p_key, 0)) + 1; }
void ARKitInterface::set_state(PocketAR::SessionState p_state) {
	const bool changed = state != p_state;
	if (!changed && !state_history.is_empty()) return;
	const int64_t state_generation = generation;
	state = p_state;
	state_history.append(int(state));
	if (changed) call_deferred("emit_signal", "session_state_changed", int(state));
#ifdef DEBUG_ENABLED
	if (p_state != PocketAR::TRACKING) {
		debug_plane.pause();
		if (debug_plane.active()) presented_packet.unref();
	}
#endif
	if (generation != state_generation || state != p_state) return;
	if (changed && state != PocketAR::TRACKING) {
		anchors.pause();
	}
}
void ARKitInterface::set_reason(PocketAR::TrackingReason p_reason) {
	if (reason == p_reason) return;
	reason = p_reason;
	call_deferred("emit_signal", "tracking_reason_changed", int(reason));
}

bool ARKitInterface::_initialize() {
	if (uninitializing) return false;
	if (initialized) return true;
	impl.reset(new Impl);
	geometry_dirty = true;
	last_good_pose = Transform3D();
	tracking_status = XR_UNKNOWN_TRACKING;
	set_reason(PocketAR::NONE);
	diagnostics.clear(); timings.clear(); state_history.clear();
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs->get_current_rendering_driver_name() != "metal" || !rs->get_rendering_device()) {
		diagnostics["last_error"] = "ARKit zero-copy camera requires the Metal rendering driver";
		set_state(PocketAR::UNSUPPORTED);
		return false;
	}
	ERR_FAIL_COND_V_MSG(!on_owner_thread(), false, "ARKit requires the main thread and Safe rendering thread mode.");
	camera_authorization(diagnostics);
	ERR_FAIL_COND_V_MSG(XRServer::get_singleton()->get_primary_interface().is_valid(), false, "Only one ARView may own the primary XR interface.");
	if (![ARWorldTrackingConfiguration isSupported]) {
		diagnostics["last_error"] = "ARWorldTrackingConfiguration is unsupported on this device";
		set_state(PocketAR::UNSUPPORTED);
		return false;
	}
	textures = arkit_texture_pool();
	if (textures.is_null()) { diagnostics["last_error"] = "Metal retain probe or CoreVideo texture cache failed"; set_state(PocketAR::UNSUPPORTED); return false; }
	for (const char *key : { "plane_updates", "plane_geometry_rebuilds", "new_frame_count", "packets_published", "packets_presented", "repeated_updates", "texture_refusals", "timestamp_regressions", "projection_aspect_mismatch_count", "pause_count", "resume_count", "reset_count", "retired_tracker_count", "orientation_transition_drops" }) diagnostics[key] = int64_t(0);
	diagnostics["diagnostic_samples"] = int64_t(0);
	diagnostics["projection_form_valid"] = true;
	diagnostics["display_transform_valid"] = true;
	diagnostics["native_depth_zero_to_one"] = true;
	diagnostics["projection_near_far_error"] = 0.0;
	diagnostics["projection_core_error_px"] = 0.0;
	diagnostics["orientations_seen"] = Array();
	diagnostics["display_transform_error_px"] = 0.0;
	diagnostics["pose_error"] = 0.0;
	diagnostics["configured"] = false;
	diagnostics["renderer"] = rs->get_current_rendering_method();
	diagnostics["driver"] = rs->get_current_rendering_driver_name();
	NSBundle *framework = [NSBundle bundleForClass:[ARSession class]];
	NSString *version = [framework objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
	diagnostics["arkit_version"] = version ? String(version.UTF8String) : String("OS-bundled");
	diagnostics["ios_version"] = String(NSProcessInfo.processInfo.operatingSystemVersionString.UTF8String);
	struct utsname hardware;
	diagnostics["device_model"] = uname(&hardware) == 0 ? String(hardware.machine) : String("unknown");
	head.instantiate();
	head->set_tracker_name("head");
	head->set_tracker_desc("ARKit display-oriented camera");
	head->set_tracker_type(XRServer::TRACKER_HEAD);
	XRServer::get_singleton()->add_tracker(head);
#ifdef DEBUG_ENABLED
	debug_plane.clear();
#endif
	generation = next_ar_id();
	initialized = true;
	active_interfaces.insert(this);
	impl->start_tick = tick_usec();
	set_primary(true);
	const auto initial_state = suspended || application_suspended ? PocketAR::PAUSED : PocketAR::CHECKING;
	if (state == initial_state) call_deferred("emit_signal", "session_state_changed", int(initial_state));
	set_state(initial_state);
	call_deferred("emit_signal", "session_generation_changed", generation);
	return true;
}

void ARKitInterface::_uninitialize() {
	if (!initialized) return;
	ERR_FAIL_COND(!on_owner_thread());
	uninitializing = true;
	initialized = false;
	impl->delegate.owner = nullptr;
	impl->session.delegate = nil;
	[impl->session pause];
	impl->running = false;
	anchors.clear();
	impl->session = nil;
	impl->config = nil;
	impl->delegate = nil;
	tracking_status = XR_NOT_TRACKING;
	current_packet.unref(); presented_packet.unref(); applied_packet.unref();
	clear_planes();
	textures->set_active_packets(current_packet, presented_packet, applied_packet);
	textures.unref();
	XRServer::get_singleton()->remove_tracker(head);
	head.unref();
	set_primary(false);
	set_state(PocketAR::PAUSED);
	active_interfaces.erase(this);
	uninitializing = false;
}

void ARKitInterface::run_session() {
	if (suspended || application_suspended) { set_state(PocketAR::PAUSED); return; }
	const AVAuthorizationStatus authorization = camera_authorization(diagnostics);
	if (authorization == AVAuthorizationStatusDenied || authorization == AVAuthorizationStatusRestricted) {
		if (impl->running) [impl->session pause];
		impl->running = false;
		clear_packets();
		head->invalidate_pose("default");
		tracking_status = XR_NOT_TRACKING;
		set_reason(PocketAR::CAMERA_UNAVAILABLE);
		set_state(PocketAR::PERMISSION_DENIED);
		return;
	}
	if (authorization == AVAuthorizationStatusNotDetermined) {
		if (!impl->permission_requested) {
			impl->permission_requested = true;
			// Completion is off-thread; _process observes authorization again on the main thread.
			[AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL granted) {}];
		}
		set_state(PocketAR::CHECKING);
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
		impl->config.lightEstimationEnabled = NO;
	}
	choose_camera_format();
	impl->config.planeDetection = ARPlaneDetection(plane_finding_mode);
	Dictionary config;
	config["width"] = int(impl->config.videoFormat.imageResolution.width);
	config["height"] = int(impl->config.videoFormat.imageResolution.height);
	config["fps"] = int(impl->config.videoFormat.framesPerSecond);
	diagnostics["config"] = config;
	const int64_t run_generation = generation;
	set_state(PocketAR::READY);
	if (!initialized || generation != run_generation || suspended || application_suspended) return;
	const uint64_t started = tick_usec();
	if (!bool(diagnostics["configured"])) impl->start_tick = started;
	if (impl->reset_pending) impl->reset_cutoff = CACurrentMediaTime();
	[impl->session runWithConfiguration:impl->config options:(impl->reset_pending) ? ARSessionRunOptionResetTracking | ARSessionRunOptionRemoveExistingAnchors : 0];
	timings[impl->reset_pending ? "reset_ms" : "resume_ms"] = (tick_usec() - started) / 1000.0;
	impl->reset_pending = false;
	impl->running = true;
	tracking_status = XR_UNKNOWN_TRACKING;
	impl->interrupted = false;
	diagnostics["configured"] = true;
	set_reason(PocketAR::NONE);
	increment("resume_count");
	set_state(PocketAR::INITIALIZING);
}

void ARKitInterface::pause_session() {
	const int64_t paused_generation = generation;
	const uint64_t started = tick_usec();
	[impl->session pause];
	timings["pause_ms"] = (tick_usec() - started) / 1000.0;
	impl->running = false;
	clear_packets();
	impl->interrupted = false;
	tracking_status = XR_NOT_TRACKING;
	increment("pause_count");
	set_state(PocketAR::PAUSED);
	if (!initialized || generation != paused_generation) return;
	head->invalidate_pose("default");
}

void ARKitInterface::set_suspended(bool p_suspended) {
	ERR_FAIL_COND(initialized && !on_owner_thread());
	if (suspended == p_suspended) return;
	suspended = p_suspended;
	if (!initialized) return;
	if (suspended) pause_session();
	else if (!application_suspended) run_session();
}

void ARKitInterface::application_pause() {
	if (!initialized) return;
	ERR_FAIL_COND(!on_owner_thread());
	if (application_suspended) return;
	application_suspended = true;
	if (!suspended) pause_session();
}

void ARKitInterface::application_resume() {
	if (!initialized) return;
	ERR_FAIL_COND(!on_owner_thread());
	const bool was_suspended = application_suspended;
	application_suspended = false;
	if (!suspended && (was_suspended || !impl->running)) run_session();
}

void ARKitInterface::choose_camera_format() {
	NSArray<ARVideoFormat *> *native_formats = ARWorldTrackingConfiguration.supportedVideoFormats;
	std::vector<pocketar::CameraFormat> formats;
	Array available;
	for (ARVideoFormat *format in native_formats) {
		const int width = int(format.imageResolution.width), height = int(format.imageResolution.height), fps = int(format.framesPerSecond);
		formats.push_back({ width, height, 0, 0, fps });
		Dictionary value;
		value["width"] = width; value["height"] = height; value["fps"] = fps;
		available.push_back(value);
	}
	const int selected = pocketar::select_ios_format(formats, pocketar::CameraQuality(camera_quality), camera_frame_rate);
	if (selected >= 0) impl->config.videoFormat = native_formats[selected];
	diagnostics["config_list"] = available;
	diagnostics["config_index"] = selected;
}

void ARKitInterface::set_camera_quality(PocketAR::CameraQuality p_quality) {
	ERR_FAIL_COND(p_quality < PocketAR::HIGHEST || p_quality > PocketAR::LOWEST);
	ERR_FAIL_COND(initialized && !on_owner_thread());
	if (camera_quality == p_quality) return;
	camera_quality = p_quality;
	if (impl->running) run_session();
}

void ARKitInterface::set_camera_frame_rate(int p_fps) {
	ERR_FAIL_COND(p_fps != 0 && p_fps != 30 && p_fps != 60);
	ERR_FAIL_COND(initialized && !on_owner_thread());
	if (camera_frame_rate == p_fps) return;
	camera_frame_rate = p_fps;
	if (impl->running) run_session();
}

void ARKitInterface::session_interrupted(bool p_interrupted) {
	if (!impl->running || suspended || application_suspended) return;
	impl->interrupted = p_interrupted;
	set_reason(p_interrupted ? PocketAR::CAMERA_UNAVAILABLE : PocketAR::NONE);
	if (p_interrupted) {
		clear_packets();
		head->invalidate_pose("default");
		tracking_status = XR_NOT_TRACKING;
		set_state(PocketAR::PAUSED);
	} else {
		tracking_status = XR_UNKNOWN_TRACKING;
		set_state(PocketAR::INITIALIZING);
	}
}

void ARKitInterface::session_failed(int p_code, const String &p_message) {
	const int64_t failed_generation = generation;
	[impl->session pause];
	impl->running = false;
	diagnostics["last_error"] = p_message;
	diagnostics["arkit_error_code"] = p_code;
	clear_packets();
	if (p_code != 103) anchors.clear(false);
	if (!initialized || generation != failed_generation) return;
	head->invalidate_pose("default");
	if (!initialized || generation != failed_generation) return;
	tracking_status = XR_NOT_TRACKING;
	set_reason(p_code == 103 ? PocketAR::CAMERA_UNAVAILABLE : PocketAR::BAD_STATE);
	set_state(p_code == 103 ? PocketAR::PERMISSION_DENIED : p_code == 100 ? PocketAR::UNSUPPORTED : PocketAR::FAILED);
}

void ARKitInterface::clear_packets() {
	current_packet.unref(); presented_packet.unref();
	textures->set_active_packets(current_packet, presented_packet, applied_packet);
}

void ARKitInterface::clear_planes() {
	const bool changed = !published_planes.empty();
	auto retired = std::move(planes);
	planes.clear();
	published_planes.clear();
	native_anchor_count = 0;
#ifdef DEBUG_ENABLED
	debug_plane.clear();
#endif
	for (auto &entry : retired) { XRServer::get_singleton()->remove_tracker(entry.second); increment("retired_tracker_count"); }
	if (changed) call_deferred("emit_signal", "planes_changed");
}

void ARKitInterface::reset() {
	ERR_FAIL_COND(!initialized || !on_owner_thread());
	[impl->session pause];
	impl->running = false;
	generation = next_ar_id();
	const int64_t reset_generation = generation;
	impl->reset_pending = true;
	clear_packets();
	anchors.clear();
	if (!initialized || generation != reset_generation) return;
	clear_planes();
	if (!initialized || generation != reset_generation) return;
	head->invalidate_pose("default");
	if (!initialized || generation != reset_generation) return;
	tracking_status = XR_UNKNOWN_TRACKING;
	last_good_pose = Transform3D();
	impl->last_timestamp = 0;
	set_reason(PocketAR::NONE);
	geometry_dirty = true;
	increment("reset_count");
	call_deferred("emit_signal", "session_generation_changed", generation);
	run_session();
}

bool ARKitInterface::can_create_anchor(void *p_frame) {
	if (state != PocketAR::TRACKING || current_packet.is_null() || current_packet->session_generation != generation || current_packet->anchor_epoch != anchors.get_epoch()) return false;
	ARFrame *frame = (__bridge ARFrame *)p_frame;
	if (!frame || frame.timestamp <= impl->reset_cutoff) return false;
	return frame.camera.trackingState == ARTrackingStateNormal;
}

Ref<ARAnchorTracker> ARKitInterface::create_native_anchor(const Transform3D &p_pose, int64_t p_source_plane_id) {
	ARAnchor *native = [[ARAnchor alloc] initWithTransform:to_simd(p_pose)];
	impl->anchors.add(anchors.next_id(), anchor_key(native.identifier), native, p_pose);
	[impl->session addAnchor:native];
	return anchors.add(generation, p_pose, p_source_plane_id);
}

Ref<ARAnchorTracker> ARKitInterface::create_anchor(const Transform3D &p_pose) {
	ERR_FAIL_COND_V(initialized && !on_owner_thread(), {});
	@autoreleasepool {
		ARFrame *frame = impl->session.currentFrame;
		if (!ARAnchorRegistry::valid_pose(p_pose)) { WARN_PRINT("Cannot create anchor: invalid pose."); return {}; }
		if (!can_create_anchor((__bridge void *)frame)) { WARN_PRINT("Cannot create anchor: not tracking."); return {}; }
		return create_native_anchor(p_pose, 0);
	}
}

Ref<ARAnchorTracker> ARKitInterface::create_anchor_from_hit(const Ref<ARRaycastHit> &p_hit) {
	ERR_FAIL_COND_V(initialized && !on_owner_thread(), {});
	@autoreleasepool {
		ARFrame *frame = impl->session.currentFrame;
		if (!anchors.fresh_hit(p_hit, presented_packet, generation)) { WARN_PRINT("Cannot create anchor: stale hit."); return {}; }
		if (!can_create_anchor((__bridge void *)frame)) { WARN_PRINT("Cannot create anchor: not tracking."); return {}; }
		const int64_t plane_id = p_hit->get_plane_id();
		bool tracking_plane = false;
#ifdef DEBUG_ENABLED
		if (plane_id < 0) tracking_plane = debug_plane.tracks(plane_id);
#endif
		if (plane_id > 0) {
			for (const auto &entry : planes) {
				if (entry.second->get_plane_id() != plane_id || entry.second->get_tracking_state() != ARPlaneTracker::TRACKING) continue;
				for (ARAnchor *native in frame.anchors) {
					if (anchor_key(native.identifier) == entry.first) { tracking_plane = true; break; }
				}
				break;
			}
		}
		if (!tracking_plane) { WARN_PRINT("Cannot create anchor: stale hit."); return {}; }
		if (!ARAnchorRegistry::valid_pose(p_hit->get_pose())) { WARN_PRINT("Cannot create anchor: invalid pose."); return {}; }
		return create_native_anchor(p_hit->get_pose(), plane_id);
	}
}

void ARKitInterface::remove_anchor(const Ref<ARAnchorTracker> &p_anchor) {
	ERR_FAIL_COND(initialized && !on_owner_thread());
	anchors.remove(p_anchor);
}

static_assert(NSUInteger(PocketAR::PLANE_FINDING_DISABLED) == ARPlaneDetectionNone && NSUInteger(PocketAR::PLANE_FINDING_HORIZONTAL) == ARPlaneDetectionHorizontal &&
		NSUInteger(PocketAR::PLANE_FINDING_HORIZONTAL_AND_VERTICAL) == (ARPlaneDetectionHorizontal | ARPlaneDetectionVertical), "PlaneFindingMode is passed to ARKit unchanged");

void ARKitInterface::set_plane_finding_mode(PocketAR::PlaneFindingMode p_mode) {
	ERR_FAIL_COND(p_mode != PocketAR::PLANE_FINDING_DISABLED && p_mode != PocketAR::PLANE_FINDING_HORIZONTAL && p_mode != PocketAR::PLANE_FINDING_HORIZONTAL_AND_VERTICAL);
	ERR_FAIL_COND(initialized && !on_owner_thread());
	if (plane_finding_mode == p_mode) return;
	plane_finding_mode = p_mode;
	if (impl->config) {
		impl->config.planeDetection = ARPlaneDetection(p_mode);
		if (impl->running) [impl->session runWithConfiguration:impl->config options:0];
	}
}

void ARKitInterface::set_display_geometry(const Vector2i &p_size, int, double p_near, double p_far) {
	ERR_FAIL_COND(p_size.x <= 0 || p_size.y <= 0 || !std::isfinite(p_near) || !std::isfinite(p_far) || p_near <= 0 || p_far <= p_near);
	if (p_size != viewport_size || p_near != near_plane || p_far != far_plane) geometry_dirty = true;
	viewport_size = p_size;
	near_plane = p_near;
	far_plane = p_far;
}

void ARKitInterface::anchors_changed(void *p_session, void *p_anchors, bool p_removed) {
	if (!initialized || (__bridge ARSession *)p_session != impl->session || impl->reset_pending || (impl->reset_cutoff > 0 && impl->session.currentFrame.timestamp <= impl->reset_cutoff)) return;
	ERR_FAIL_COND(!on_owner_thread());
	const int64_t callback_generation = generation;
	std::vector<int64_t> stopped;
	for (ARAnchor *anchor in (__bridge NSArray<ARAnchor *> *)p_anchors) {
		const auto key = anchor_key(anchor.identifier);
		if (![anchor isKindOfClass:[ARPlaneAnchor class]]) {
			if (p_removed) {
				const int64_t id = impl->anchors.removed(key);
				if (id) stopped.push_back(id);
			}
			continue;
		}
		auto found = planes.find(key);
		if (p_removed) {
			if (found != planes.end()) {
				const Ref<ARPlaneTracker> tracker = found->second;
				planes.erase(found);
				XRServer::get_singleton()->remove_tracker(tracker);
				if (!initialized || generation != callback_generation) return;
				increment("retired_tracker_count");
			}
			continue;
		}
		ARPlaneAnchor *plane = (ARPlaneAnchor *)anchor;
		ARPlaneSnapshot snapshot;
		const bool added = found == planes.end();
		if (added) {
			Ref<ARPlaneTracker> tracker;
			tracker.instantiate();
			snapshot.plane_id = next_ar_id();
			snapshot.session_generation = generation;
			found = planes.emplace(key, tracker).first;
		} else snapshot = found->second->get_snapshot();
		const Transform3D local_frame = arkit_plane_local_frame(Vector3(plane.center.x, plane.center.y, plane.center.z), plane.planeExtent.rotationOnYAxis);
		const Transform3D anchor_to_plane = local_frame.affine_inverse();
		snapshot.transform = from_simd(plane.transform) * local_frame;
		snapshot.alignment = plane.alignment == ARPlaneAnchorAlignmentVertical ? ARPlaneTracker::VERTICAL :
			snapshot.transform.basis.get_column(1).y < 0 ? ARPlaneTracker::HORIZONTAL_DOWNWARD_FACING : ARPlaneTracker::HORIZONTAL_UPWARD_FACING;
		snapshot.tracking_state = impl->session.currentFrame.camera.trackingState == ARTrackingStateNormal ? ARPlaneTracker::TRACKING : ARPlaneTracker::PAUSED;
		ARPlaneGeometry *geometry = plane.geometry;
		plane_polygon.resize(geometry.boundaryVertexCount);
		bool same_polygon = snapshot.polygon.size() == geometry.boundaryVertexCount;
		for (NSInteger i = 0; i < geometry.boundaryVertexCount; ++i) {
			const auto &boundary = geometry.boundaryVertices[i];
			const Vector3 vertex = anchor_to_plane.xform(Vector3(boundary.x, boundary.y, boundary.z));
			plane_polygon[i] = Vector2(vertex.x, vertex.z);
			if (same_polygon && snapshot.polygon.get(i) != plane_polygon[i]) same_polygon = false;
		}
		if (!same_polygon) {
			snapshot.polygon = PackedVector2Array();
			snapshot.polygon.resize(plane_polygon.size());
			for (size_t i = 0; i < plane_polygon.size(); ++i) snapshot.polygon[i] = plane_polygon[i];
			increment("plane_geometry_rebuilds");
		}
		snapshot.extents = Vector2(plane.planeExtent.width, plane.planeExtent.height);
		const Ref<ARPlaneTracker> tracker = found->second;
		tracker->update(snapshot);
		if (!initialized || generation != callback_generation) return;
		if (added) XRServer::get_singleton()->add_tracker(tracker);
		if (!initialized || generation != callback_generation) return;
		increment("plane_updates");
	}
	for (int64_t id : stopped) {
		anchors.stop(id);
		if (!initialized || generation != callback_generation) return;
	}
}

bool ARKitInterface::copy_planes(bool p_tracking, ARFramePacket &r_packet) {
	const int tracking = p_tracking ? ARPlaneTracker::TRACKING : ARPlaneTracker::PAUSED;
	for (const auto &entry : planes) {
		ARPlaneSnapshot snapshot = entry.second->get_snapshot();
		if (snapshot.tracking_state != tracking) {
			snapshot.tracking_state = tracking;
			entry.second->update(snapshot);
		}
		r_packet.planes.push_back(snapshot);
	}
#ifdef DEBUG_ENABLED
	debug_plane.merge(r_packet.planes, generation, tracking == ARPlaneTracker::TRACKING);
#endif
	const bool changed = r_packet.planes != published_planes;
	published_planes = r_packet.planes;
	return changed;
}

void ARKitInterface::copy_anchors(void *p_frame, bool p_tracking, ARFramePacket &r_packet) {
	ARFrame *frame = (__bridge ARFrame *)p_frame;
	impl->anchors.sample(p_tracking, [&](auto p_observe) {
		for (ARAnchor *native in frame.anchors) {
			p_observe(anchor_key(native.identifier), [&] { return from_simd(native.transform); });
		}
	}, [&](int64_t p_id, const Transform3D &p_pose, int p_state) {
		r_packet.anchors.push_back({ p_id, p_pose, p_state });
	});
}

void ARKitInterface::_process() {
	if (!initialized || suspended || application_suspended) return;
	ERR_FAIL_COND(!on_owner_thread());
	@autoreleasepool {
		const AVAuthorizationStatus authorization = camera_authorization(diagnostics);
		if (!impl->running || authorization != AVAuthorizationStatusAuthorized) {
			if (state != PocketAR::FAILED && state != PocketAR::UNSUPPORTED) run_session();
			if (!impl->running) return;
		}
		const int64_t frame_generation = generation;
		ARFrame *frame = impl->session.currentFrame;
		if (!frame || frame.timestamp <= impl->reset_cutoff) return;
		const bool camera_tracking = frame.camera.trackingState == ARTrackingStateNormal;
		if (!camera_tracking && state == PocketAR::TRACKING) {
			anchors.pause();
			if (!initialized || generation != frame_generation || !impl->running || suspended || application_suspended) return;
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
		if (frame.timestamp < impl->last_timestamp) { increment("timestamp_regressions"); return; }
		const bool new_frame = frame.timestamp != impl->last_timestamp;
		if (!new_frame && (impl->interrupted || !geometry_dirty)) { increment("repeated_updates"); return; }
		if (new_frame) {
			increment("new_frame_count"); impl->last_timestamp = frame.timestamp;
			impl->interrupted = false;
		}
		ARCamera *camera = frame.camera;
		Ref<ARFramePacket> packet;
		packet.instantiate();
		const int64_t timestamp = llround(frame.timestamp * 1e9);
		ARKitTexturePool::Import imported;
		const bool repeated = current_packet.is_valid() && current_packet->timestamp_ns == timestamp;
		bool camera_acquired = repeated;
		const uint64_t acquisition_epoch = anchors.get_epoch();
		if (!repeated && textures->import_image((void *)frame.capturedImage, imported)) {
			camera_acquired = true;
			packet->y_texture = imported.y;
			packet->cbcr_texture = imported.cbcr;
		} else {
			if (!repeated) {
				increment("texture_refusals");
				if (!textures->get_import_error().is_empty()) {
					diagnostics["last_error"] = textures->get_import_error();
					[impl->session pause];
					impl->running = false;
					tracking_status = XR_NOT_TRACKING;
					clear_packets();
					anchors.clear(false);
					if (!initialized || generation != frame_generation) return;
					head->invalidate_pose("default");
					if (!initialized || generation != frame_generation) return;
					set_reason(PocketAR::BAD_STATE);
					set_state(PocketAR::UNSUPPORTED);
					return;
				}
				anchors.invalidate_samples();
				if (!anchors.empty()) return;
			}
			if (current_packet.is_valid()) {
				packet->y_texture = current_packet->y_texture;
				packet->cbcr_texture = current_packet->cbcr_texture;
			}
		}
		packet->imported_chroma = true;
		packet->session_generation = generation;
		packet->frame_id = ++sequence;
		packet->timestamp_ns = timestamp;
		packet->timestamp_usec = llround(frame.timestamp * 1e6);
		packet->viewport_size = viewport_size;
		packet->image_size = Vector2i(camera.imageResolution.width, camera.imageResolution.height);
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
		bool uv_valid = packet->display_transform.is_finite() && std::abs(packet->display_transform.determinant()) > 1e-9;
		for (const Vector2 &corner : { Vector2(), Vector2(1, 0), Vector2(0, 1), Vector2(1, 1) }) {
			const Vector2 image_uv = packet->display_transform.xform(corner);
			uv_valid = uv_valid && image_uv.x >= -1e-5 && image_uv.x <= 1.00001 && image_uv.y >= -1e-5 && image_uv.y <= 1.00001;
		}
		const bool projection_valid = native[0][0] > 0 && native[1][1] > 0 && std::isfinite(native[1][1] / native[0][0]) && std::abs(native[0][1]) < 1e-5 && std::abs(native[1][0]) < 1e-5 && std::abs(native[2][3] + 1) < 1e-5 && std::abs(native[3][3]) < 1e-5;
		diagnostics["projection_form_valid"] = bool(diagnostics["projection_form_valid"]) && projection_valid;
		diagnostics["display_transform_valid"] = bool(diagnostics["display_transform_valid"]) && uv_valid;
		if (diagnostics_enabled) {
			const Projection oracle = arkit_intrinsics_projection(packet->intrinsics, viewport_size, turns, near_plane, far_plane);
			const double projection_error = arkit_projection_residual_px(native, oracle, viewport_size);
			double pose_error = 0, uv_error = 0;
			const Transform3D apple_pose = from_simd(simd_inverse([camera viewMatrixForOrientation:orientation]));
			if (packet->tracking_valid) {
				pose_error = apple_pose.origin.distance_to(packet->camera_transform.origin);
				for (int c = 0; c < 3; ++c) pose_error = std::max(pose_error, double(apple_pose.basis.get_column(c).distance_to(packet->camera_transform.basis.get_column(c))));
			}
			const Transform2D uv_oracle = display_transform_for_view(packet->image_size, viewport_size, turns);
			for (const Vector2 &corner : { Vector2(), Vector2(1, 0), Vector2(0, 1), Vector2(1, 1) }) {
				uv_error = std::max(uv_error, double(((packet->display_transform.xform(corner) - uv_oracle.xform(corner)) * Vector2(packet->image_size)).length()));
			}
			diagnostics["native_depth_zero_to_one"] = bool(diagnostics["native_depth_zero_to_one"]) && std::abs(native[2][2] - 100.0 / (0.1 - 100)) < 1e-5 && std::abs(native[3][2] - 10.0 / (0.1 - 100)) < 1e-5;
			diagnostics["projection_core_error_px"] = std::max(double(diagnostics["projection_core_error_px"]), projection_error);
			diagnostics["pose_error"] = std::max(double(diagnostics["pose_error"]), pose_error);
			diagnostics["display_transform_error_px"] = std::max(double(diagnostics["display_transform_error_px"]), uv_error);
		}
		Array orientations_seen = diagnostics["orientations_seen"];
		if (!orientations_seen.has(turns)) orientations_seen.append(turns);
		PocketAR::TrackingReason next_reason = PocketAR::NONE;
		if (camera.trackingState == ARTrackingStateLimited) {
			switch (camera.trackingStateReason) {
				case ARTrackingStateReasonNone:
				case ARTrackingStateReasonInitializing:
				case ARTrackingStateReasonRelocalizing: next_reason = PocketAR::NONE; break;
				case ARTrackingStateReasonExcessiveMotion: next_reason = PocketAR::EXCESSIVE_MOTION; break;
				case ARTrackingStateReasonInsufficientFeatures: next_reason = PocketAR::INSUFFICIENT_FEATURES; break;
				default: next_reason = PocketAR::BAD_STATE; break;
			}
		} else if (camera.trackingState != ARTrackingStateNormal && camera.trackingState != ARTrackingStateNotAvailable) next_reason = PocketAR::BAD_STATE;
		set_reason(next_reason);
		tracking_status = camera.trackingState == ARTrackingStateNormal ? XR_NORMAL_TRACKING :
			reason == PocketAR::EXCESSIVE_MOTION ? XR_EXCESSIVE_MOTION : reason == PocketAR::INSUFFICIENT_FEATURES ? XR_INSUFFICIENT_FEATURES :
			reason == PocketAR::NONE ? XR_UNKNOWN_TRACKING : XR_NOT_TRACKING;
		native_anchor_count = int64_t(frame.anchors.count);
		copy_anchors((__bridge void *)frame, camera_tracking && (new_frame || repeated), *packet.ptr());
		camera = nil;
		frame = nil;
		const uint64_t frame_epoch = anchors.get_epoch();
		for (const ARAnchorSnapshot &anchor : packet->anchors) {
			if (anchor.tracking_state == ARAnchorTracker::STOPPED) anchors.stop(anchor.anchor_id);
			if (!initialized || generation != frame_generation || anchors.get_epoch() != frame_epoch) return;
		}
		if (packet->tracking_valid) head->set_pose("default", packet->camera_transform, Vector3(), Vector3(), camera_tracking ? XRPose::XR_TRACKING_CONFIDENCE_HIGH : XRPose::XR_TRACKING_CONFIDENCE_LOW);
		else head->invalidate_pose("default");
		if (!initialized || generation != frame_generation || anchors.get_epoch() != frame_epoch) return;
		set_state(camera_tracking ? PocketAR::TRACKING : PocketAR::INITIALIZING);
		if (!initialized || generation != frame_generation || !impl->running || suspended || application_suspended) return;
		const uint64_t publish_epoch = anchors.get_epoch();
		packet->anchor_epoch = camera_acquired ? publish_epoch : acquisition_epoch;
		const bool changed_planes = copy_planes(camera_tracking, *packet.ptr());
		if (!initialized || generation != frame_generation || anchors.get_epoch() != publish_epoch) return;
		if (!impl->first_frame) { timings["first_frame_ms"] = (tick_usec() - impl->start_tick) / 1000.0; impl->first_frame = true; }
		impl->orientation = orientation;
		geometry_dirty = false;
		packet->reuse_plane_values(current_packet);
		current_packet = packet;
		textures->set_active_packets(current_packet, presented_packet, applied_packet);
		if (diagnostics_enabled) {
			for (const Vector2 &clip_range : { Vector2(0.07, 17), Vector2(0.23, 131) }) {
				const PackedFloat64Array values = _get_projection_for_view(0, double(viewport_size.x) / viewport_size.y, clip_range.x, clip_range.y);
				Projection clipped;
				for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) clipped[c][r] = values[c * 4 + r];
				const Vector4 n = clipped.xform(Vector4(0, 0, -clip_range.x, 1));
				const Vector4 f = clipped.xform(Vector4(0, 0, -clip_range.y, 1));
				const double error = std::max(std::abs(double(n.z / n.w + 1)), std::abs(double(f.z / f.w - 1)));
				diagnostics["projection_near_far_error"] = std::max(double(diagnostics["projection_near_far_error"]), error);
			}
			increment("diagnostic_samples");
		}
		increment("packets_published");
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
		if (std::abs(p_aspect - size.x / size.y) > 1e-3) increment("projection_aspect_mismatch_count");
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
	anchors.apply(p_packet, generation);
}

void ARKitInterface::mark_presented() {
	if (suspended || application_suspended) return;
#ifdef DEBUG_ENABLED
	if (debug_plane.presentation_paused()) { presented_packet.unref(); return; }
#endif
	if (applied_packet.is_valid() && applied_packet->get_session_generation() == generation) {
		presented_packet = applied_packet;
		textures->set_active_packets(current_packet, presented_packet, applied_packet);
		increment("packets_presented");
	}
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
			const auto plane = hit.anchor ? planes.find(anchor_key(hit.anchor.identifier)) : planes.end();
			entry["plane_id"] = plane != planes.end() ? plane->second->get_plane_id() : int64_t(0);
			result.append(entry);
		}
	}
	return result;
}

Dictionary ARKitInterface::get_native_plane_identifiers() const {
	Dictionary identifiers;
	for (const auto &entry : planes) {
		NSUUID *identifier = [[NSUUID alloc] initWithUUIDBytes:entry.first.data()];
		identifiers[String::num_int64(entry.second->get_plane_id())] = String(identifier.UUIDString.UTF8String);
	}
	return identifiers;
}

void ARKitInterface::set_diagnostics_enabled(bool p_enabled) {
	ERR_FAIL_COND(initialized && !on_owner_thread());
	diagnostics_enabled = p_enabled;
}

Dictionary ARKitInterface::get_diagnostics() const {
	Dictionary result = diagnostics.duplicate(true);
	result["diagnostics_enabled"] = diagnostics_enabled;
	result["plane_finding_mode"] = int(plane_finding_mode);
	result["camera_quality"] = int(camera_quality);
	result["camera_frame_rate"] = camera_frame_rate;
	result["suspended"] = suspended;
	ARWorldTrackingConfiguration *config = (ARWorldTrackingConfiguration *)impl->session.configuration;
	if (config) result["effective_plane_finding_mode"] = int(config.planeDetection);
	if (textures.is_valid()) result.merge(textures->get_diagnostics(), true);
	result["timings"] = timings.duplicate(true);
	result["state_history"] = state_history.duplicate();
	result["plane_count"] = int64_t(planes.size());
	result["native_anchor_count"] = native_anchor_count;
	result["session_generation"] = generation;
	result["state"] = int(state);
	result["reason"] = int(reason);
	result["thermal_state"] = int64_t([NSProcessInfo processInfo].thermalState);
	return result;
}

}
