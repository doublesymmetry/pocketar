#include "synthetic_ar_interface.h"
#include "ar_ids.h"

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <cmath>

namespace godot {

void SyntheticARInterface::_bind_methods() {
	ClassDB::bind_method(D_METHOD("reset"), &SyntheticARInterface::reset);
	ClassDB::bind_method(D_METHOD("get_session_state"), &SyntheticARInterface::get_session_state);
	ClassDB::bind_method(D_METHOD("get_tracking_reason"), &SyntheticARInterface::get_tracking_reason);
	ClassDB::bind_method(D_METHOD("set_intrinsics", "fx", "fy", "cx", "cy", "image_size"), &SyntheticARInterface::set_intrinsics);
	ClassDB::bind_method(D_METHOD("set_pose", "pose"), &SyntheticARInterface::set_pose);
	ClassDB::bind_method(D_METHOD("set_camera_rate", "fps"), &SyntheticARInterface::set_camera_rate);
	ClassDB::bind_method(D_METHOD("set_frame_id_pose", "enabled"), &SyntheticARInterface::set_frame_id_pose);
}

bool SyntheticARInterface::_initialize() {
	if (initialized) {
		return true;
	}
	if (!start("Synthetic handheld camera")) return false;
#ifdef DEBUG_ENABLED
	reset_debug_plane();
#endif
	generation = next_ar_id();
	anchors.clear();
	sequence = 0;
	if (!is_paused()) make_packet(Time::get_singleton()->get_ticks_usec());
	call_deferred("emit_signal", "session_generation_changed", generation);
	call_deferred("emit_signal", "session_state_changed", get_session_state());
	return true;
}

void SyntheticARInterface::reset() {
	ERR_FAIL_COND(!initialized);
#ifdef DEBUG_ENABLED
	reset_debug_plane();
#endif
	generation = next_ar_id();
	anchors.clear();
	sequence = 0;
	current_packet.unref();
	presented_packet.unref();
	if (!is_paused()) make_packet(Time::get_singleton()->get_ticks_usec());
	call_deferred("emit_signal", "session_generation_changed", generation);
}

void SyntheticARInterface::_uninitialize() {
	if (!initialized) {
		return;
	}
	stop();
	call_deferred("emit_signal", "session_state_changed", PocketAR::PAUSED);
}

void SyntheticARInterface::set_intrinsics(double p_fx, double p_fy, double p_cx, double p_cy, const Vector2i &p_image_size) {
	ERR_FAIL_COND(!std::isfinite(p_fx) || !std::isfinite(p_fy) || !std::isfinite(p_cx) || !std::isfinite(p_cy));
	ERR_FAIL_COND(p_fx <= 0 || p_fy <= 0 || p_image_size.x < 32 || p_image_size.y < 32 || p_image_size.x > 4096 || p_image_size.y > 4096);
	intrinsics = { p_fx, p_fy, p_cx, p_cy, Vector2(p_image_size) };
	geometry_dirty = true;
}

void SyntheticARInterface::set_pose(const Transform3D &p_pose) {
	ERR_FAIL_COND(!p_pose.is_finite());
	pose = p_pose;
}

void SyntheticARInterface::set_camera_rate(double p_fps) {
	ERR_FAIL_COND(!std::isfinite(p_fps) || p_fps <= 0 || p_fps > 240);
	camera_rate = p_fps;
}

void SyntheticARInterface::set_frame_id_pose(bool p_enabled) {
	frame_id_pose = p_enabled;
}

void SyntheticARInterface::activity_changed() {
	if (is_paused()) {
		anchors.pause();
#ifdef DEBUG_ENABLED
		debug_plane.pause();
#endif
		current_packet.unref(); presented_packet.unref();
		head->invalidate_pose("default");
	} else geometry_dirty = true;
	call_deferred("emit_signal", "session_state_changed", get_session_state());
}

void SyntheticARInterface::_process() {
	if (!initialized || is_paused()) return;
	const uint64_t tick = Time::get_singleton()->get_ticks_usec();
	if (geometry_dirty || tick - last_tick >= uint64_t(1000000.0 / camera_rate)) {
		make_packet(tick);
	}
}

void SyntheticARInterface::make_packet(uint64_t p_tick) {
	last_tick = p_tick;
	sequence++;
	Ref<ARFramePacket> packet;
	packet.instantiate();
	packet->session_generation = generation;
	packet->frame_id = sequence;
	packet->timestamp_usec = p_tick;
	packet->timestamp_ns = p_tick * 1000;
	fill_packet(packet, pose);
	if (frame_id_pose) {
		const Vector2 pixel((double(sequence % 16) + 0.5) * viewport_size.x / 16.0, viewport_size.y * 0.5);
		const ARRay ray = ray_from_screen(packet->projection, display_oriented_pose(Transform3D(), quarter_turns), viewport_size, pixel);
		const Vector3 point = ray.direction * (-2.0 / ray.direction.z);
		packet->physical_camera_transform = Transform3D(Basis(), Vector3(0, 0, -2) - point);
		packet->camera_transform = display_oriented_pose(packet->physical_camera_transform, quarter_turns);
	}
	packet->tracking_valid = true;
	sample_anchors(packet);
#ifdef DEBUG_ENABLED
	publish_debug_plane(packet, true);
#endif
	packet->reuse_plane_values(current_packet);
	current_packet = packet;
	head->set_pose("default", packet->camera_transform, Vector3(), Vector3(), XRPose::XR_TRACKING_CONFIDENCE_HIGH);
}

}
