#include "ar_frame_packet.h"
#include <godot_cpp/core/class_db.hpp>

namespace godot {

void ARFramePacket::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_session_generation"), &ARFramePacket::get_session_generation);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "session_generation"), "", "get_session_generation");
	ClassDB::bind_method(D_METHOD("get_frame_id"), &ARFramePacket::get_frame_id);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "frame_id"), "", "get_frame_id");
	ClassDB::bind_method(D_METHOD("get_timestamp_usec"), &ARFramePacket::get_timestamp_usec);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "timestamp_usec"), "", "get_timestamp_usec");
	ClassDB::bind_method(D_METHOD("get_camera_transform"), &ARFramePacket::get_camera_transform);
	ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "camera_transform"), "", "get_camera_transform");
	ClassDB::bind_method(D_METHOD("get_projection"), &ARFramePacket::get_projection);
	ADD_PROPERTY(PropertyInfo(Variant::PROJECTION, "projection"), "", "get_projection");
	ClassDB::bind_method(D_METHOD("get_image_size"), &ARFramePacket::get_image_size);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "image_size"), "", "get_image_size");
	ClassDB::bind_method(D_METHOD("get_display_transform"), &ARFramePacket::get_display_transform);
	ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM2D, "display_transform"), "", "get_display_transform");
	ClassDB::bind_method(D_METHOD("get_y_texture"), &ARFramePacket::get_y_texture);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "y_texture"), "", "get_y_texture");
	ClassDB::bind_method(D_METHOD("get_cbcr_texture"), &ARFramePacket::get_cbcr_texture);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "cbcr_texture"), "", "get_cbcr_texture");
	ClassDB::bind_method(D_METHOD("get_color_encoding"), &ARFramePacket::get_color_encoding);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "color_encoding"), "", "get_color_encoding");
	ClassDB::bind_method(D_METHOD("get_tracking_valid"), &ARFramePacket::get_tracking_valid);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "tracking_valid"), "", "get_tracking_valid");
	ClassDB::bind_method(D_METHOD("get_viewport_size"), &ARFramePacket::get_viewport_size);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "viewport_size"), "", "get_viewport_size");
	BIND_ENUM_CONSTANT(BT601_FULL);
	BIND_ENUM_CONSTANT(BT601_VIDEO);
	BIND_ENUM_CONSTANT(BT709_FULL);
}

ARRay ray_from_screen(const Ref<ARFramePacket> &p_packet, const Vector2 &p_viewport_size, const Vector2 &p_screen_point) {
	return ray_from_screen(p_packet->get_projection(), p_packet->get_camera_transform(), p_viewport_size, p_screen_point);
}

}
