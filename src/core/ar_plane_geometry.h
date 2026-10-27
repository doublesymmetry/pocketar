#pragma once

#include "projection.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace godot {

struct ARPlaneRaycastInput {
	int64_t plane_id = 0;
	int tracking_state = 0;
	int64_t subsumed_by = 0;
	Transform3D transform;
	const Vector2 *polygon = nullptr;
	size_t polygon_size = 0;
};

struct ARPlaneIntersection {
	int64_t plane_id = 0;
	double distance = 0;
	Transform3D pose;
};

std::vector<ARPlaneIntersection> raycast_plane_polygons(const std::vector<ARPlaneRaycastInput> &p_planes, const ARRay &p_ray);

}
