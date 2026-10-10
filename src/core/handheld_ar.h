#pragma once

#include "ar_frame_packet.h"
#include "ar_raycast_hit.h"
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot {

class HandheldAR : public Object {
	GDCLASS(HandheldAR, Object)

protected:
	static void _bind_methods();

public:
	String get_version() const { return "0.0.1-dev"; }
	String get_backend() const { return "synthetic"; }
	Dictionary screen_to_ray(const Ref<ARFramePacket> &p_packet, const Vector2 &p_viewport_size, const Vector2 &p_screen_point) const;
	TypedArray<ARRaycastHit> raycast_planes(const Ref<ARFramePacket> &p_packet, const Dictionary &p_ray) const;
};

}
