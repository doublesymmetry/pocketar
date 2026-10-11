#pragma once

#include "ar_frame_packet.h"
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/xr_interface_extension.hpp>
#include <godot_cpp/classes/xr_positional_tracker.hpp>

namespace godot {

class SimulatedARInterface : public XRInterfaceExtension {
	GDCLASS(SimulatedARInterface, XRInterfaceExtension)

protected:
	bool initialized = false;
	bool geometry_dirty = true;
	int64_t generation = 0;
	ARIntrinsics intrinsics;
	Vector2 viewport_size = Vector2(960, 720);
	int quarter_turns = 0;
	double near_plane = 0.05;
	double far_plane = 100.0;
	Ref<XRPositionalTracker> head;
	Ref<ARFramePacket> current_packet;
	Ref<ARFramePacket> presented_packet;
	Ref<Image> y_image;
	Ref<Image> cbcr_image;

	static void _bind_methods();
	bool start(const String &p_description);
	void stop();
	void fill_packet(const Ref<ARFramePacket> &p_packet, const Transform3D &p_pose);

public:
	int64_t get_session_generation() const { return generation; }
	void set_display_geometry(const Vector2i &p_viewport_size, int p_quarter_turns, double p_near, double p_far);
	Ref<ARFramePacket> get_current_packet() const { return current_packet; }
	Ref<ARFramePacket> get_presented_packet() const { return presented_packet; }
	void mark_presented();

	uint32_t _get_capabilities() const override { return XR_MONO | XR_AR; }
	bool _is_initialized() const override { return initialized; }
	Vector2 _get_render_target_size() override;
	uint32_t _get_view_count() override { return 1; }
	Transform3D _get_camera_transform() override;
	Transform3D _get_transform_for_view(uint32_t p_view, const Transform3D &p_cam_transform) override;
	PackedFloat64Array _get_projection_for_view(uint32_t p_view, double p_aspect, double p_z_near, double p_z_far) override;
	bool _pre_draw_viewport(const RID &p_render_target) override { return current_packet.is_valid(); }
};

}
