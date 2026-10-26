#pragma once

#include "projection.h"
#include "ar_plane_snapshot.h"
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <memory>
#include <vector>

namespace godot {

class SimulatedARInterface;
class SyntheticARInterface;
class ScriptedARInterface;
class ARCoreInterface;
class ARKitInterface;

// Values are copied; native texture Refs do not archive images, and the GPU texture can change before presentation.
class ARFramePacket : public RefCounted {
	GDCLASS(ARFramePacket, RefCounted)
	friend class SimulatedARInterface;
	friend class SyntheticARInterface;
	friend class ScriptedARInterface;
	friend class ARCoreInterface;
	friend class ARKitInterface;

	int64_t session_generation = 0;
	int64_t frame_id = 0;
	int64_t timestamp_usec = 0;
	int64_t timestamp_ns = 0;
	Transform3D camera_transform;
	Transform3D physical_camera_transform;
	Projection projection;
	Vector2i image_size;
	Transform2D display_transform;
	Ref<Texture2D> y_texture;
	Ref<Texture2D> cbcr_texture;
	Ref<Texture2D> rgb_texture;
	bool imported_chroma = false;
	bool tracking_valid = false;
	Vector2 viewport_size;
	ARIntrinsics intrinsics;
	int quarter_turns = 0;
	std::vector<ARPlaneSnapshot> planes;
	mutable std::shared_ptr<TypedArray<Dictionary>> plane_values;
	void reuse_plane_values(const Ref<ARFramePacket> &p_previous);

protected:
	static void _bind_methods();

public:
	int64_t get_session_generation() const { return session_generation; }
	int64_t get_frame_id() const { return frame_id; }
	int64_t get_timestamp_usec() const { return timestamp_usec; }
	int64_t get_timestamp_ns() const { return timestamp_ns; }
	Transform3D get_camera_transform() const { return camera_transform; }
	Transform3D get_physical_camera_transform() const { return physical_camera_transform; }
	Projection get_projection() const { return projection; }
	Vector2i get_image_size() const { return image_size; }
	Transform2D get_display_transform() const { return display_transform; }
	Ref<Texture2D> get_y_texture() const { return y_texture; }
	Ref<Texture2D> get_cbcr_texture() const { return cbcr_texture; }
	Ref<Texture2D> get_rgb_texture() const { return rgb_texture; }
	bool get_imported_chroma() const { return imported_chroma; }
	bool get_tracking_valid() const { return tracking_valid; }
	Vector2 get_viewport_size() const { return viewport_size; }
	int get_quarter_turns() const { return quarter_turns; }
	Dictionary get_intrinsics() const;
	TypedArray<Dictionary> get_planes() const;
	const std::vector<ARPlaneSnapshot> &get_plane_snapshots() const { return planes; }
};

ARRay ray_from_screen(const Ref<ARFramePacket> &p_packet, const Vector2 &p_viewport_size, const Vector2 &p_screen_point);

}
