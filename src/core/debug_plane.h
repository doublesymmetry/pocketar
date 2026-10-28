#pragma once

#ifdef DEBUG_ENABLED
#include "ar_plane_tracker.h"
#include <vector>

namespace godot {

class DebugPlane {
	ARPlaneSnapshot plane;
	Ref<ARPlaneTracker> tracker;
	bool paused = false;

public:
	static bool valid_geometry(const Transform3D &p_transform, const Vector2 &p_extents);
	int64_t set(const Transform3D &p_transform, const Vector2 &p_extents);
	void request_clear();
	bool clear();
	void pause();
	void merge(std::vector<ARPlaneSnapshot> &r_planes, int64_t p_generation, bool p_tracking);
	bool presentation_paused() const { return paused; }
	int64_t get_plane_id() const { return plane.plane_id; }
	bool is_tracking() const { return plane.plane_id != 0 && !paused && plane.tracking_state == ARPlaneTracker::TRACKING; }
	bool active() const { return plane.plane_id != 0 || tracker.is_valid(); }
};

}
#endif
