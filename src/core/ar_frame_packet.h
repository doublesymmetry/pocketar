#pragma once

#include "projection.h"
#include "ar_plane_snapshot.h"
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <vector>

namespace godot {

class SyntheticARInterface;
class ARCoreInterface;

class ARFramePacket : public RefCounted {
	GDCLASS(ARFramePacket, RefCounted)
	friend class SyntheticARInterface;
	friend class ARCoreInterface;

public:
	enum ColorEncoding { BT601_FULL, BT601_VIDEO, BT709_FULL };

private:
	int64_t session_generation = 0;
	int64_t frame_id = 0;
	int64_t timestamp_usec = 0;
	int64_t timestamp_ns = 0;
	String clock_domain = "godot_monotonic_usec";
	Transform3D camera_transform;
	Transform3D physical_camera_transform;
	Projection projection;
	Vector2i image_size;
	Vector2i gpu_image_size;
	Vector2i camera_fps;
	Transform2D display_transform;
	Ref<Texture2D> y_texture;
	Ref<Texture2D> cbcr_texture;
	ColorEncoding color_encoding = BT601_FULL;
	bool tracking_valid = false;
	Vector2 viewport_size;
	ARIntrinsics intrinsics;
	int quarter_turns = 0;
	std::vector<ARPlaneSnapshot> planes;
	Dictionary point_cloud;

protected:
	static void _bind_methods();

public:
	int64_t get_session_generation() const { return session_generation; }
	int64_t get_frame_id() const { return frame_id; }
	int64_t get_timestamp_usec() const { return timestamp_usec; }
	int64_t get_timestamp_ns() const { return timestamp_ns; }
	String get_clock_domain() const { return clock_domain; }
	Transform3D get_camera_transform() const { return camera_transform; }
	Transform3D get_physical_camera_transform() const { return physical_camera_transform; }
	Projection get_projection() const { return projection; }
	Vector2i get_image_size() const { return image_size; }
	Vector2i get_gpu_image_size() const { return gpu_image_size; }
	Vector2i get_camera_fps() const { return camera_fps; }
	Transform2D get_display_transform() const { return display_transform; }
	Ref<Texture2D> get_y_texture() const { return y_texture; }
	Ref<Texture2D> get_cbcr_texture() const { return cbcr_texture; }
	ColorEncoding get_color_encoding() const { return color_encoding; }
	bool get_tracking_valid() const { return tracking_valid; }
	Vector2 get_viewport_size() const { return viewport_size; }
	int get_quarter_turns() const { return quarter_turns; }
	Dictionary get_intrinsics() const;
	TypedArray<Dictionary> get_planes() const;
	Dictionary get_point_cloud() const { return point_cloud.duplicate(true); }
	const std::vector<ARPlaneSnapshot> &get_plane_snapshots() const { return planes; }
};

ARRay ray_from_screen(const Ref<ARFramePacket> &p_packet, const Vector2 &p_viewport_size, const Vector2 &p_screen_point);

}
VARIANT_ENUM_CAST(godot::ARFramePacket::ColorEncoding)
