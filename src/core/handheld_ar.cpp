#include "handheld_ar.h"
#include <godot_cpp/core/class_db.hpp>

namespace godot {

void HandheldAR::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_version"), &HandheldAR::get_version);
	ClassDB::bind_method(D_METHOD("get_backend"), &HandheldAR::get_backend);
	ClassDB::bind_method(D_METHOD("screen_to_ray", "packet", "viewport_size", "screen_point"), &HandheldAR::screen_to_ray);
}

Dictionary HandheldAR::screen_to_ray(const Ref<ARFramePacket> &p_packet, const Vector2 &p_viewport_size, const Vector2 &p_screen_point) const {
	ERR_FAIL_COND_V(p_packet.is_null() || !p_packet->get_tracking_valid(), Dictionary());
	ERR_FAIL_COND_V(p_viewport_size.x <= 0 || p_viewport_size.y <= 0, Dictionary());
	const ARRay ray = ray_from_screen(p_packet, p_viewport_size, p_screen_point);
	Dictionary result;
	result["origin"] = ray.origin;
	result["direction"] = ray.direction;
	result["frame_id"] = p_packet->get_frame_id();
	result["session_generation"] = p_packet->get_session_generation();
	return result;
}

}
