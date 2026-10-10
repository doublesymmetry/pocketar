#pragma once

#include "projection.h"
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/texture2d.hpp>

namespace godot {

class SyntheticARInterface;

class ARFramePacket : public RefCounted {
	GDCLASS(ARFramePacket, RefCounted)
	friend class SyntheticARInterface;

public:
	enum ColorEncoding { BT601_FULL, BT601_VIDEO, BT709_FULL };

private:
	int64_t session_generation = 0;
	int64_t frame_id = 0;
	int64_t timestamp_usec = 0;
	Transform3D camera_transform;
	Projection projection;
	Vector2i image_size;
	Transform2D display_transform;
	Ref<Texture2D> y_texture;
	Ref<Texture2D> cbcr_texture;
	ColorEncoding color_encoding = BT601_FULL;
	bool tracking_valid = false;
	Vector2 viewport_size;
	ARIntrinsics intrinsics;
	int quarter_turns = 0;

protected:
	static void _bind_methods();

public:
	int64_t get_session_generation() const { return session_generation; }
	int64_t get_frame_id() const { return frame_id; }
	int64_t get_timestamp_usec() const { return timestamp_usec; }
	Transform3D get_camera_transform() const { return camera_transform; }
	Projection get_projection() const { return projection; }
	Vector2i get_image_size() const { return image_size; }
	Transform2D get_display_transform() const { return display_transform; }
	Ref<Texture2D> get_y_texture() const { return y_texture; }
	Ref<Texture2D> get_cbcr_texture() const { return cbcr_texture; }
	ColorEncoding get_color_encoding() const { return color_encoding; }
	bool get_tracking_valid() const { return tracking_valid; }
	Vector2 get_viewport_size() const { return viewport_size; }
};

ARRay ray_from_screen(const Ref<ARFramePacket> &p_packet, const Vector2 &p_viewport_size, const Vector2 &p_screen_point);

}
VARIANT_ENUM_CAST(godot::ARFramePacket::ColorEncoding)
