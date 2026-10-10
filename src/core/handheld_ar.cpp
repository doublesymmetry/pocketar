#include "handheld_ar.h"
#include <godot_cpp/core/class_db.hpp>

namespace godot {

void HandheldAR::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_version"), &HandheldAR::get_version);
	ClassDB::bind_method(D_METHOD("get_backend"), &HandheldAR::get_backend);
	ClassDB::bind_method(D_METHOD("screen_to_ray", "packet", "viewport_size", "screen_point"), &HandheldAR::screen_to_ray);
	ClassDB::bind_method(D_METHOD("raycast_planes", "packet", "ray"), &HandheldAR::raycast_planes);
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

TypedArray<ARRaycastHit> HandheldAR::raycast_planes(const Ref<ARFramePacket> &p_packet, const Dictionary &p_ray) const {
	if (p_packet.is_null() || !p_packet->get_tracking_valid()) {
		return {};
	}
	const Variant generation = p_ray.get("session_generation", Variant());
	const Variant frame = p_ray.get("frame_id", Variant());
	const Variant origin = p_ray.get("origin", Variant());
	const Variant direction = p_ray.get("direction", Variant());
	if (generation.get_type() != Variant::INT || frame.get_type() != Variant::INT || origin.get_type() != Variant::VECTOR3 || direction.get_type() != Variant::VECTOR3) {
		return {};
	}
	if (int64_t(generation) != p_packet->get_session_generation() || int64_t(frame) != p_packet->get_frame_id()) {
		return {};
	}
	return godot::raycast_planes(p_packet, { Vector3(origin), Vector3(direction) });
}

}
