#include "ar_anchor_tracker.h"
#include <godot_cpp/core/class_db.hpp>

namespace godot {

void ARAnchorTracker::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_anchor_id"), &ARAnchorTracker::get_anchor_id);
	ClassDB::bind_method(D_METHOD("get_session_generation"), &ARAnchorTracker::get_session_generation);
	ClassDB::bind_method(D_METHOD("get_source_plane_id"), &ARAnchorTracker::get_source_plane_id);
	ClassDB::bind_method(D_METHOD("get_transform"), &ARAnchorTracker::get_transform);
	ClassDB::bind_method(D_METHOD("get_tracking_state"), &ARAnchorTracker::get_tracking_state);
	ClassDB::bind_method(D_METHOD("_notify_state"), &ARAnchorTracker::notify_state);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "anchor_id"), "", "get_anchor_id");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "session_generation"), "", "get_session_generation");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "source_plane_id"), "", "get_source_plane_id");
	ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "transform"), "", "get_transform");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tracking_state"), "", "get_tracking_state");
	ADD_SIGNAL(MethodInfo("tracking_state_changed", PropertyInfo(Variant::INT, "state")));
	BIND_ENUM_CONSTANT(TRACKING);
	BIND_ENUM_CONSTANT(PAUSED);
	BIND_ENUM_CONSTANT(STOPPED);
}

ARAnchorTracker::ARAnchorTracker() {
	set_tracker_type(XRServer::TRACKER_ANCHOR);
	set_tracker_desc("PocketAR anchor");
}

void ARAnchorTracker::notify_state() {
	notification_pending = false;
	emit_signal("tracking_state_changed", tracking_state);
}

void ARAnchorTracker::update(const Transform3D &p_transform, int p_state) {
	if (tracking_state == STOPPED) return;
	const bool changed = tracking_state != p_state;
	const bool moved = p_state == TRACKING && transform != p_transform;
	tracking_state = p_state;
	if (p_state == TRACKING) transform = p_transform;
	if (changed && !notification_pending) {
		notification_pending = true;
		call_deferred("_notify_state");
	}
	if (p_state == TRACKING && (changed || moved)) {
		set_pose("default", transform, Vector3(), Vector3(), XRPose::XR_TRACKING_CONFIDENCE_HIGH);
	} else if (changed) invalidate_pose("default");
}

}
