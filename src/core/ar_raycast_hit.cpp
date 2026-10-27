#include "ar_raycast_hit.h"
#include <godot_cpp/core/class_db.hpp>

namespace godot {

void ARRaycastHit::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_alignment"), &ARRaycastHit::get_alignment);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "alignment"), "", "get_alignment");
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

}
