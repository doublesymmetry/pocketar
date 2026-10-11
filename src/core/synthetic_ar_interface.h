#pragma once

#include "ar_frame_packet.h"
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/xr_interface_extension.hpp>
#include <godot_cpp/classes/xr_positional_tracker.hpp>

namespace godot {

class SyntheticARInterface : public XRInterfaceExtension {
	GDCLASS(SyntheticARInterface, XRInterfaceExtension)

	bool initialized = false;
	bool geometry_dirty = true;
	bool frame_id_pose = false;
	int64_t generation = 0;
	int64_t sequence = 0;
	uint64_t last_tick = 0;
	double camera_rate = 30.0;
	double near_plane = 0.05;
	double far_plane = 100.0;
	ARIntrinsics intrinsics;
	Vector2 viewport_size = Vector2(960, 720);
	int quarter_turns = 0;
	Transform3D pose;
	Ref<XRPositionalTracker> head;
	Ref<ARFramePacket> current_packet;
	Ref<ARFramePacket> presented_packet;
	Ref<Image> y_images[3];
	Ref<Image> cbcr_images[3];
	void make_packet(uint64_t p_tick);

protected:
	static void _bind_methods();

public:
	void set_intrinsics(double p_fx, double p_fy, double p_cx, double p_cy, const Vector2i &p_image_size);
	void set_pose(const Transform3D &p_pose);
	void set_camera_rate(double p_fps);
	void set_display_geometry(const Vector2i &p_viewport_size, int p_quarter_turns, double p_near, double p_far);
	void set_frame_id_pose(bool p_enabled);
	Ref<ARFramePacket> get_current_packet() const { return current_packet; }
	Ref<ARFramePacket> get_presented_packet() const { return presented_packet; }
	void mark_presented();

	StringName _get_name() const override { return "PocketARSynthetic"; }
	uint32_t _get_capabilities() const override { return XR_MONO | XR_AR; }
	bool _is_initialized() const override { return initialized; }
	bool _initialize() override;
	void _uninitialize() override;
	Vector2 _get_render_target_size() override;
	uint32_t _get_view_count() override { return 1; }
	Transform3D _get_camera_transform() override;
	Transform3D _get_transform_for_view(uint32_t p_view, const Transform3D &p_cam_transform) override;
	PackedFloat64Array _get_projection_for_view(uint32_t p_view, double p_aspect, double p_z_near, double p_z_far) override;
	void _process() override;
	bool _pre_draw_viewport(const RID &p_render_target) override { return current_packet.is_valid(); }
};

}
