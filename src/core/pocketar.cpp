#include "pocketar.h"
#include "ar_plane_geometry.h"
#include <godot_cpp/classes/xr_interface.hpp>
#include <godot_cpp/classes/xr_server.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

void PocketAR::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_version"), &PocketAR::get_version);
	ClassDB::bind_method(D_METHOD("get_backend"), &PocketAR::get_backend);
	ClassDB::bind_method(D_METHOD("screen_to_ray", "packet", "viewport_size", "screen_point"), &PocketAR::screen_to_ray);
	ClassDB::bind_method(D_METHOD("raycast_planes", "packet", "ray"), &PocketAR::raycast_planes);
	BIND_ENUM_CONSTANT(UNSUPPORTED);
	BIND_ENUM_CONSTANT(CHECKING);
	BIND_ENUM_CONSTANT(NEEDS_INSTALL);
	BIND_ENUM_CONSTANT(INSTALLING);
	BIND_ENUM_CONSTANT(PERMISSION_DENIED);
	BIND_ENUM_CONSTANT(READY);
	BIND_ENUM_CONSTANT(INITIALIZING);
	BIND_ENUM_CONSTANT(TRACKING);
	BIND_ENUM_CONSTANT(PAUSED);
	BIND_ENUM_CONSTANT(FAILED);
	BIND_ENUM_CONSTANT(NONE);
	BIND_ENUM_CONSTANT(BAD_STATE);
	BIND_ENUM_CONSTANT(INSUFFICIENT_LIGHT);
	BIND_ENUM_CONSTANT(EXCESSIVE_MOTION);
	BIND_ENUM_CONSTANT(INSUFFICIENT_FEATURES);
	BIND_ENUM_CONSTANT(CAMERA_UNAVAILABLE);
	BIND_ENUM_CONSTANT(HIGHEST);
	BIND_ENUM_CONSTANT(BALANCED);
	BIND_ENUM_CONSTANT(LOWEST);
	BIND_ENUM_CONSTANT(PLANE_FINDING_DISABLED);
	BIND_ENUM_CONSTANT(PLANE_FINDING_HORIZONTAL);
	BIND_ENUM_CONSTANT(PLANE_FINDING_HORIZONTAL_AND_VERTICAL);
	BIND_ENUM_CONSTANT(ALIGNMENT_MASK_HORIZONTAL_UPWARD);
	BIND_ENUM_CONSTANT(ALIGNMENT_MASK_HORIZONTAL_DOWNWARD);
	BIND_ENUM_CONSTANT(ALIGNMENT_MASK_VERTICAL);
	BIND_ENUM_CONSTANT(ALIGNMENT_MASK_ALL);
}

String PocketAR::get_backend() const {
	const Ref<XRInterface> interface = XRServer::get_singleton()->get_primary_interface();
	if (interface.is_null()) {
		return "none";
	}
	const StringName name = interface->get_name();
	if (name == StringName("PocketARSynthetic")) return "synthetic";
	if (name == StringName("PocketARScripted")) return "scripted";
	if (name == StringName("PocketARCore")) return "arcore";
	if (name == StringName("PocketARKit")) return "arkit";
	return "unknown";
}

Dictionary PocketAR::screen_to_ray(const Ref<ARFramePacket> &p_packet, const Vector2 &p_viewport_size, const Vector2 &p_screen_point) const {
	ERR_FAIL_COND_V(p_packet.is_null() || !p_packet->get_tracking_valid(), Dictionary());
	ERR_FAIL_COND_V(p_viewport_size.x <= 0 || p_viewport_size.y <= 0, Dictionary());
	const ARRay ray = ray_from_screen(p_packet->get_projection(), p_packet->get_camera_transform(), p_viewport_size, p_screen_point);
	Dictionary result;
	result["origin"] = ray.origin;
	result["direction"] = ray.direction;
	result["frame_id"] = p_packet->get_frame_id();
	result["session_generation"] = p_packet->get_session_generation();
	return result;
}

TypedArray<ARRaycastHit> PocketAR::raycast_planes(const Ref<ARFramePacket> &p_packet, const Dictionary &p_ray) const {
	const ARRay ray = { p_ray.get("origin", Vector3()), p_ray.get("direction", Vector3()) };
	TypedArray<ARRaycastHit> results;
	if (p_packet.is_null() || !p_packet->get_tracking_valid()) {
		return results;
	}
	std::vector<ARPlaneRaycastInput> planes;
	for (const ARPlaneSnapshot &plane : p_packet->get_plane_snapshots()) {
		planes.push_back({ plane.plane_id, plane.tracking_state, plane.subsumed_by, plane.transform, plane.polygon.ptr(), size_t(plane.polygon.size()) });
	}
	for (const ARPlaneIntersection &intersection : raycast_plane_polygons(planes, ray)) {
		Ref<ARRaycastHit> hit;
		hit.instantiate();
		hit->session_generation = p_packet->get_session_generation();
		hit->frame_id = p_packet->get_frame_id();
		hit->plane_id = intersection.plane_id;
		for (const ARPlaneSnapshot &plane : p_packet->get_plane_snapshots()) {
			if (plane.plane_id == intersection.plane_id) {
				hit->alignment = plane.alignment;
				break;
			}
		}
		hit->distance = intersection.distance;
		hit->pose = intersection.pose;
		results.push_back(hit);
	}
	return results;
}

}
