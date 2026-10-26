#include "ar_plane_tracker.h"
#include <godot_cpp/core/class_db.hpp>

namespace godot {

void ARPlaneTracker::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_plane_id"), &ARPlaneTracker::get_plane_id);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "plane_id"), "", "get_plane_id");
	ClassDB::bind_method(D_METHOD("get_session_generation"), &ARPlaneTracker::get_session_generation);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "session_generation"), "", "get_session_generation");
	ClassDB::bind_method(D_METHOD("get_alignment"), &ARPlaneTracker::get_alignment);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "alignment"), "", "get_alignment");
	ClassDB::bind_method(D_METHOD("get_extents"), &ARPlaneTracker::get_extents);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "extents"), "", "get_extents");
	ClassDB::bind_method(D_METHOD("get_polygon"), &ARPlaneTracker::get_polygon);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "polygon"), "", "get_polygon");
	ClassDB::bind_method(D_METHOD("get_subsumed_by"), &ARPlaneTracker::get_subsumed_by);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "subsumed_by"), "", "get_subsumed_by");
	ClassDB::bind_method(D_METHOD("get_tracking_state"), &ARPlaneTracker::get_tracking_state);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tracking_state"), "", "get_tracking_state");
	ClassDB::bind_method(D_METHOD("get_transform"), &ARPlaneTracker::get_transform);
	ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "transform"), "", "get_transform");
	BIND_ENUM_CONSTANT(TRACKING);
	BIND_ENUM_CONSTANT(PAUSED);
	BIND_ENUM_CONSTANT(STOPPED);
	BIND_ENUM_CONSTANT(HORIZONTAL_UPWARD_FACING);
	BIND_ENUM_CONSTANT(HORIZONTAL_DOWNWARD_FACING);
	BIND_ENUM_CONSTANT(VERTICAL);
}

ARPlaneTracker::ARPlaneTracker() {
	set_tracker_type(XRServer::TRACKER_ANCHOR);
	set_tracker_desc("PocketAR plane");
}

void ARPlaneTracker::update(const ARPlaneSnapshot &p_snapshot) {
	if (snapshot == p_snapshot) return;
	if (snapshot.plane_id != p_snapshot.plane_id || snapshot.session_generation != p_snapshot.session_generation) {
		set_tracker_name("pocketar/plane/" + String::num_int64(p_snapshot.session_generation) + "/" + String::num_int64(p_snapshot.plane_id));
	}
	snapshot = p_snapshot;
	if (snapshot.tracking_state == TRACKING) {
		set_pose("default", snapshot.transform, Vector3(), Vector3(), XRPose::XR_TRACKING_CONFIDENCE_HIGH);
	} else {
		invalidate_pose("default");
	}
}

}
