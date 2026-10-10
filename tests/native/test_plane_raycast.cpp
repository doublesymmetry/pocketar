#include "core/ar_plane_geometry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace godot;

static int checks = 0;

static void check(bool p_condition, const char *p_name) {
	checks++;
	if (!p_condition) {
		std::fprintf(stderr, "PLANE FAIL %s\n", p_name);
		std::exit(1);
	}
}

static bool close(const Vector3 &p_actual, const Vector3 &p_expected) {
	return p_actual.distance_to(p_expected) < 1e-5;
}

int main() {
	std::vector<Vector2> square = { Vector2(-1, -1), Vector2(1, -1), Vector2(1, 1), Vector2(-1, 1) };
	ARPlaneRaycastInput plane = { 7, 0, 0, Transform3D(), square.data(), square.size() };
	const ARRay down = { Vector3(0, 2, 0), Vector3(0, -1, 0) };
	auto hits = raycast_plane_polygons({ plane }, down);
	check(hits.size() == 1, "front-side polygon hit");
	check(hits[0].plane_id == 7 && std::abs(hits[0].distance - 2.0) < 1e-6, "plane identity and metric distance");
	check(close(hits[0].pose.origin, Vector3()), "hit lies on plane");
	check(close(hits[0].pose.basis.get_column(0), Vector3(1, 0, 0)), "normal-incidence fallback X");
	check(close(hits[0].pose.basis.get_column(1), Vector3(0, 1, 0)), "plane normal Y");
	check(close(hits[0].pose.basis.get_column(2), Vector3(0, 0, 1)), "normal-incidence fallback Z");
	std::reverse(square.begin(), square.end());
	check(raycast_plane_polygons({ plane }, down).size() == 1, "clockwise winding accepted");
	for (const Vector3 &point : { Vector3(1, 2, 0), Vector3(-1, 2, 1), Vector3(1, 2, -1) }) {
		check(raycast_plane_polygons({ plane }, { point, Vector3(0, -1, 0) }).size() == 1, "edge and vertex included");
	}
	check(raycast_plane_polygons({ plane }, { Vector3(1.01, 2, 0), Vector3(0, -1, 0) }).empty(), "outside polygon rejected");
	check(raycast_plane_polygons({ plane }, { Vector3(0, -2, 0), Vector3(0, 1, 0) }).empty(), "back side rejected");
	check(raycast_plane_polygons({ plane }, { Vector3(), Vector3(0, -1, 0) }).empty(), "origin on surface rejected");
	check(raycast_plane_polygons({ plane }, { Vector3(0, 2, 0), Vector3(0, 1, 0) }).empty(), "ray moving away rejected");
	check(raycast_plane_polygons({ plane }, { Vector3(0, 2, 0), Vector3(1, 0, 0) }).empty(), "parallel ray rejected");
	check(raycast_plane_polygons({ plane }, { Vector3(0, 2, 0), Vector3(1, -1e-8, 0) }).empty(), "near-parallel ray rejected");
	check(raycast_plane_polygons({ plane }, { Vector3(0, 2, 0), Vector3() }).empty(), "zero direction rejected");
	hits = raycast_plane_polygons({ plane }, { Vector3(0, 2, 0), Vector3(0, -3, 0) });
	check(hits.size() == 1 && std::abs(hits[0].distance - 2.0) < 1e-6, "non-unit input gives metric distance");
	hits = raycast_plane_polygons({ plane }, { Vector3(0.5, 1, 0), Vector3(-0.5, -1, 0) });
	check(hits.size() == 1, "oblique hit");
	check(close(hits[0].pose.basis.get_column(2), Vector3(1, 0, 0)), "hit Z faces projected ray origin");
	check(close(hits[0].pose.basis.get_column(0), Vector3(0, 0, -1)), "hit X is Y cross Z");
	plane.subsumed_by = 9;
	check(raycast_plane_polygons({ plane }, down).empty(), "subsumed plane hidden");
	plane.subsumed_by = 0;
	for (int state : { 1, 2 }) {
		plane.tracking_state = state;
		check(raycast_plane_polygons({ plane }, down).empty(), "paused and stopped planes hidden");
	}
	plane.tracking_state = 0;
	ARPlaneRaycastInput near_plane = plane;
	near_plane.plane_id = 8;
	near_plane.transform.origin.y = 1;
	ARPlaneRaycastInput far_plane = plane;
	far_plane.plane_id = 9;
	far_plane.transform.origin.y = -1;
	hits = raycast_plane_polygons({ far_plane, plane, near_plane }, down);
	check(hits.size() == 3, "all intersected planes returned");
	check(hits[0].plane_id == 8 && hits[1].plane_id == 7 && hits[2].plane_id == 9, "hits sorted nearest first");
	check(std::abs(hits[0].distance - 1) < 1e-6 && std::abs(hits[2].distance - 3) < 1e-6, "translated plane distances");
	plane.transform = Transform3D(Basis(Vector3(0, 0, 1), 1.5707963267948966), Vector3(1, 2, 3));
	hits = raycast_plane_polygons({ plane }, { Vector3(-1, 2, 3), Vector3(1, 0, 0) });
	check(hits.size() == 1 && std::abs(hits[0].distance - 2) < 1e-5, "rotated translated plane hit");
	check(close(hits[0].pose.origin, Vector3(1, 2, 3)), "rotated plane world position");
	check(close(hits[0].pose.basis.get_column(1), Vector3(-1, 0, 0)), "rotated normal retained");
	check(close(hits[0].pose.basis.get_column(0), Vector3(0, 1, 0)), "rotated fallback basis");
	check(raycast_plane_polygons({ plane }, { Vector3(-1, 2, 4.1), Vector3(1, 0, 0) }).empty(), "rotated polygon boundary");
	plane.transform = Transform3D();
	const std::vector<std::vector<Vector2>> degenerate = {
		{}, { Vector2() }, { Vector2(-1, 0), Vector2(1, 0) },
		{ Vector2(-1, 0), Vector2(), Vector2(1, 0) },
		{ Vector2(), Vector2(), Vector2() }
	};
	for (const auto &polygon : degenerate) {
		plane.polygon = polygon.data();
		plane.polygon_size = polygon.size();
		check(raycast_plane_polygons({ plane }, down).empty(), "degenerate polygon rejected");
	}
	square[0].x = std::numeric_limits<float>::quiet_NaN();
	plane.polygon = square.data();
	plane.polygon_size = square.size();
	check(raycast_plane_polygons({ plane }, down).empty(), "non-finite polygon rejected");
	square[0] = Vector2(-1, 1);
	check(raycast_plane_polygons({ plane }, { Vector3(0, std::numeric_limits<float>::infinity(), 0), down.direction }).empty(), "non-finite ray rejected");
	plane.transform.basis = Basis(Vector3(), Vector3(), Vector3());
	check(raycast_plane_polygons({ plane }, down).empty(), "singular plane basis rejected");
	std::printf("PLANE PASS %d checks\n", checks);
	return 0;
}
