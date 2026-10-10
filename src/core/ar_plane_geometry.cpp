#include "ar_plane_geometry.h"
#include <algorithm>
#include <cmath>

namespace godot {

static constexpr double RAY_EPSILON = 1e-6;

static bool contains_polygon_point(const ARPlaneRaycastInput &p_plane, const Vector2 &p_point) {
	if (p_plane.polygon == nullptr || p_plane.polygon_size < 3) {
		return false;
	}
	double area = 0;
	for (size_t i = 0; i < p_plane.polygon_size; i++) {
		const Vector2 &a = p_plane.polygon[i];
		const Vector2 &b = p_plane.polygon[(i + 1) % p_plane.polygon_size];
		if (!a.is_finite() || !b.is_finite()) {
			return false;
		}
		area += double(a.x) * b.y - double(a.y) * b.x;
	}
	if (!std::isfinite(area) || std::abs(area) <= RAY_EPSILON * RAY_EPSILON) {
		return false;
	}
	bool inside = false;
	for (size_t i = 0; i < p_plane.polygon_size; i++) {
		const Vector2 &a = p_plane.polygon[i];
		const Vector2 &b = p_plane.polygon[(i + 1) % p_plane.polygon_size];
		const Vector2 edge = b - a;
		const double edge_length = edge.length();
		if (edge_length <= RAY_EPSILON) {
			continue;
		}
		const double cross = double(edge.x) * (p_point.y - a.y) - double(edge.y) * (p_point.x - a.x);
		const double along = double(p_point.x - a.x) * edge.x + double(p_point.y - a.y) * edge.y;
		const double tolerance = RAY_EPSILON * edge_length;
		if (std::abs(cross) <= tolerance && along >= -tolerance && along <= edge_length * edge_length + tolerance) {
			return true;
		}
		if ((a.y > p_point.y) != (b.y > p_point.y)) {
			const double intersection_x = double(a.x) + double(b.x - a.x) * (p_point.y - a.y) / double(b.y - a.y);
			if (p_point.x < intersection_x) {
				inside = !inside;
			}
		}
	}
	return inside;
}

std::vector<ARPlaneIntersection> raycast_plane_polygons(const std::vector<ARPlaneRaycastInput> &p_planes, const ARRay &p_ray) {
	std::vector<ARPlaneIntersection> hits;
	if (!p_ray.origin.is_finite() || !p_ray.direction.is_finite() || p_ray.direction.length_squared() < RAY_EPSILON * RAY_EPSILON) {
		return hits;
	}
	const Vector3 direction = p_ray.direction.normalized();
	for (const ARPlaneRaycastInput &plane : p_planes) {
		if (plane.tracking_state != 0 || plane.subsumed_by != 0 || !plane.transform.is_finite() || std::abs(plane.transform.basis.determinant()) < RAY_EPSILON) {
			continue;
		}
		const Transform3D inverse = plane.transform.affine_inverse();
		const Vector3 local_origin = inverse.xform(p_ray.origin);
		const Vector3 local_direction = inverse.basis.xform(direction);
		if (local_origin.y <= 0 || local_direction.y >= -RAY_EPSILON) {
			continue;
		}
		const double distance = -double(local_origin.y) / local_direction.y;
		const Vector3 local_hit = local_origin + local_direction * distance;
		if (!std::isfinite(distance) || !local_hit.is_finite() || !contains_polygon_point(plane, Vector2(local_hit.x, local_hit.z))) {
			continue;
		}
		const Vector3 y = plane.transform.basis.get_column(1).normalized();
		Vector3 z = -direction + direction.dot(y) * y;
		if (z.length_squared() < RAY_EPSILON * RAY_EPSILON) {
			z = plane.transform.basis.get_column(2);
		}
		z.normalize();
		const Vector3 x = y.cross(z).normalized();
		hits.push_back({ plane.plane_id, distance, Transform3D(Basis(x, y, z), plane.transform.xform(local_hit)) });
	}
	std::stable_sort(hits.begin(), hits.end(), [](const ARPlaneIntersection &p_a, const ARPlaneIntersection &p_b) {
		return p_a.distance < p_b.distance;
	});
	return hits;
}

}
