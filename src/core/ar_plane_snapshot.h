#pragma once

#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <cstdint>

namespace godot {

struct ARPlaneSnapshot {
	int64_t plane_id = 0;
	int alignment = 0;
	Vector2 extents;
	PackedVector2Array polygon;
	int64_t subsumed_by = 0;
	int tracking_state = 1;
	Transform3D transform;
	int64_t session_generation = 0;
#ifdef DEBUG_ENABLED
	bool debug = false;
#endif
	bool operator==(const ARPlaneSnapshot &p_other) const {
		return
#ifdef DEBUG_ENABLED
			debug == p_other.debug &&
#endif
			plane_id == p_other.plane_id && alignment == p_other.alignment && extents == p_other.extents &&
			polygon == p_other.polygon && subsumed_by == p_other.subsumed_by && tracking_state == p_other.tracking_state &&
			transform == p_other.transform && session_generation == p_other.session_generation;
	}
	bool operator!=(const ARPlaneSnapshot &p_other) const { return !(*this == p_other); }
};

}
