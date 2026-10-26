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
	ClassDB::bind_method(D_METHOD("get_timestamp_ns"), &ARFramePacket::get_timestamp_ns);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "timestamp_ns"), "", "get_timestamp_ns");
	ClassDB::bind_method(D_METHOD("get_camera_transform"), &ARFramePacket::get_camera_transform);
	ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "camera_transform"), "", "get_camera_transform");
	ClassDB::bind_method(D_METHOD("get_physical_camera_transform"), &ARFramePacket::get_physical_camera_transform);
	ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "physical_camera_transform"), "", "get_physical_camera_transform");
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
	ClassDB::bind_method(D_METHOD("get_rgb_texture"), &ARFramePacket::get_rgb_texture);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "rgb_texture"), "", "get_rgb_texture");
	ClassDB::bind_method(D_METHOD("get_imported_chroma"), &ARFramePacket::get_imported_chroma);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "imported_chroma"), "", "get_imported_chroma");
	ClassDB::bind_method(D_METHOD("get_tracking_valid"), &ARFramePacket::get_tracking_valid);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "tracking_valid"), "", "get_tracking_valid");
	ClassDB::bind_method(D_METHOD("get_viewport_size"), &ARFramePacket::get_viewport_size);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "viewport_size"), "", "get_viewport_size");
	ClassDB::bind_method(D_METHOD("get_quarter_turns"), &ARFramePacket::get_quarter_turns);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "quarter_turns"), "", "get_quarter_turns");
	ClassDB::bind_method(D_METHOD("get_intrinsics"), &ARFramePacket::get_intrinsics);
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "intrinsics"), "", "get_intrinsics");
	ClassDB::bind_method(D_METHOD("get_planes"), &ARFramePacket::get_planes);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "planes"), "", "get_planes");
}

Dictionary ARFramePacket::get_intrinsics() const {
	Dictionary result;
	result["fx"] = intrinsics.fx;
	result["fy"] = intrinsics.fy;
	result["cx"] = intrinsics.cx;
	result["cy"] = intrinsics.cy;
	result["image_size"] = Vector2i(intrinsics.image_size);
	return result;
}

void ARFramePacket::reuse_plane_values(const Ref<ARFramePacket> &p_previous) {
	if (!planes.empty() && p_previous.is_valid() && planes == p_previous->planes) {
		if (!p_previous->plane_values) p_previous->plane_values = std::make_shared<TypedArray<Dictionary>>();
		plane_values = p_previous->plane_values;
	}
}

TypedArray<Dictionary> ARFramePacket::get_planes() const {
	if (planes.empty()) return TypedArray<Dictionary>();
	if (!plane_values) plane_values = std::make_shared<TypedArray<Dictionary>>();
	if (plane_values->is_empty()) {
		for (const ARPlaneSnapshot &plane : planes) {
			Dictionary value;
#ifdef DEBUG_ENABLED
			value["debug"] = plane.debug;
#endif
			value["plane_id"] = plane.plane_id;
			value["session_generation"] = plane.session_generation;
			value["alignment"] = plane.alignment;
			value["extents"] = plane.extents;
			value["subsumed_by"] = plane.subsumed_by;
			value["tracking_state"] = plane.tracking_state;
			value["transform"] = plane.transform;
			plane_values->push_back(value);
		}
	}
	TypedArray<Dictionary> result;
	for (size_t i = 0; i < planes.size(); ++i) {
		Dictionary value = Dictionary((*plane_values)[i]).duplicate();
		// A fresh Variant owns a copy-on-write polygon; sharing a cached Variant lets scripts mutate it.
		value["polygon"] = planes[i].polygon;
		result.push_back(value);
	}
	return result;
}

ARRay ray_from_screen(const Ref<ARFramePacket> &p_packet, const Vector2 &p_viewport_size, const Vector2 &p_screen_point) {
	return ray_from_screen(p_packet->get_projection(), p_packet->get_camera_transform(), p_viewport_size, p_screen_point);
}

}
