#include "ar_raycast_hit.h"
#include "ar_frame_packet.h"
#include "ar_plane_geometry.h"
#include <godot_cpp/core/class_db.hpp>

namespace godot {

void ARRaycastHit::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_session_generation"), &ARRaycastHit::get_session_generation);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "session_generation"), "", "get_session_generation");
	ClassDB::bind_method(D_METHOD("get_frame_id"), &ARRaycastHit::get_frame_id);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "frame_id"), "", "get_frame_id");
	ClassDB::bind_method(D_METHOD("get_plane_id"), &ARRaycastHit::get_plane_id);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "plane_id"), "", "get_plane_id");
	ClassDB::bind_method(D_METHOD("get_distance"), &ARRaycastHit::get_distance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "distance"), "", "get_distance");
	ClassDB::bind_method(D_METHOD("get_pose"), &ARRaycastHit::get_pose);
	ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "pose"), "", "get_pose");
}

TypedArray<ARRaycastHit> raycast_planes(const Ref<ARFramePacket> &p_packet, const ARRay &p_ray) {
	TypedArray<ARRaycastHit> results;
	if (p_packet.is_null() || !p_packet->get_tracking_valid()) {
		return results;
	}
	std::vector<ARPlaneRaycastInput> planes;
	for (const ARPlaneSnapshot &plane : p_packet->get_plane_snapshots()) {
		if (plane.session_generation != p_packet->get_session_generation()) {
			continue;
		}
		planes.push_back({ plane.plane_id, plane.tracking_state, plane.subsumed_by, plane.transform, plane.polygon.ptr(), size_t(plane.polygon.size()) });
	}
	for (const ARPlaneIntersection &intersection : raycast_plane_polygons(planes, p_ray)) {
		Ref<ARRaycastHit> hit;
		hit.instantiate();
		hit->session_generation = p_packet->get_session_generation();
		hit->frame_id = p_packet->get_frame_id();
		hit->plane_id = intersection.plane_id;
		hit->distance = intersection.distance;
		hit->pose = intersection.pose;
		results.push_back(hit);
	}
	return results;
}

}
