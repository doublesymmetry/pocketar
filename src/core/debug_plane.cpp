#include "debug_plane.h"

#ifdef DEBUG_ENABLED
#include "ar_ids.h"
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/xr_server.hpp>
#include <cmath>

namespace godot {

bool DebugPlane::valid_geometry(const Transform3D &p_transform, const Vector2 &p_extents) {
	if (!p_transform.is_finite() || !p_extents.is_finite() || p_extents.x <= 0 || p_extents.y <= 0) return false;
	const Basis &basis = p_transform.basis;
	for (int i = 0; i < 3; ++i) {
		if (std::abs(basis.get_column(i).length_squared() - 1) > 1e-5) return false;
		for (int j = i + 1; j < 3; ++j) if (std::abs(basis.get_column(i).dot(basis.get_column(j))) > 1e-5) return false;
	}
	return std::abs(basis.determinant() - 1) <= 1e-5 && basis.get_column(1).distance_to(Vector3(0, 1, 0)) <= 1e-5;
}

int64_t DebugPlane::set(const Transform3D &p_transform, const Vector2 &p_extents) {
	ERR_FAIL_COND_V(OS::get_singleton()->get_thread_caller_id() != OS::get_singleton()->get_main_thread_id(), 0);
	if (!valid_geometry(p_transform, p_extents)) return 0;
	plane = ARPlaneSnapshot();
	plane.plane_id = -next_ar_id();
	plane.debug = true;
	plane.transform = p_transform;
	plane.extents = p_extents;
	const Vector2 half = p_extents * 0.5;
	for (const Vector2 &corner : { Vector2(-half.x, -half.y), Vector2(half.x, -half.y), Vector2(half.x, half.y), Vector2(-half.x, half.y) }) plane.polygon.push_back(corner);
	return plane.plane_id;
}

void DebugPlane::request_clear() {
	ERR_FAIL_COND(OS::get_singleton()->get_thread_caller_id() != OS::get_singleton()->get_main_thread_id());
	plane = ARPlaneSnapshot();
}

bool DebugPlane::clear() {
	const bool published = tracker.is_valid();
	if (published) XRServer::get_singleton()->remove_tracker(tracker);
	tracker.unref();
	plane = ARPlaneSnapshot();
	return published;
}

void DebugPlane::pause() {
	paused = active();
	if (tracker.is_null()) return;
	ARPlaneSnapshot snapshot;
	snapshot.plane_id = tracker->get_plane_id();
	snapshot.session_generation = tracker->get_session_generation();
	snapshot.debug = true;
	snapshot.transform = tracker->get_transform();
	snapshot.extents = tracker->get_extents();
	snapshot.polygon = tracker->get_polygon();
	snapshot.tracking_state = ARPlaneTracker::PAUSED;
	tracker->update(snapshot);
}

void DebugPlane::merge(std::vector<ARPlaneSnapshot> &r_planes, int64_t p_generation, bool p_tracking) {
	paused = false;
	if (!plane.plane_id) { clear(); return; }
	plane.session_generation = p_generation;
	plane.tracking_state = p_tracking ? ARPlaneTracker::TRACKING : ARPlaneTracker::PAUSED;
	if (tracker.is_valid() && tracker->get_plane_id() != plane.plane_id) {
		XRServer::get_singleton()->remove_tracker(tracker);
		tracker.unref();
	}
	const bool added = tracker.is_null();
	if (added) tracker.instantiate();
	tracker->update(plane);
	if (added) XRServer::get_singleton()->add_tracker(tracker);
	r_planes.push_back(plane);
}

}
#endif
