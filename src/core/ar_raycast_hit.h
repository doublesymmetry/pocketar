#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include "projection.h"

namespace godot {

class ARFramePacket;

class ARRaycastHit : public RefCounted {
	GDCLASS(ARRaycastHit, RefCounted)
	friend TypedArray<ARRaycastHit> raycast_planes(const Ref<ARFramePacket> &p_packet, const ARRay &p_ray);

	int64_t session_generation = 0;
	int64_t frame_id = 0;
	int64_t plane_id = 0;
	double distance = 0;
	Transform3D pose;

protected:
	static void _bind_methods();

public:
	int64_t get_session_generation() const { return session_generation; }
	int64_t get_frame_id() const { return frame_id; }
	int64_t get_plane_id() const { return plane_id; }
	double get_distance() const { return distance; }
	Transform3D get_pose() const { return pose; }
};

TypedArray<ARRaycastHit> raycast_planes(const Ref<ARFramePacket> &p_packet, const ARRay &p_ray);

}
