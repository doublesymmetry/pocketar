#pragma once

#include "ar_frame_packet.h"
#include "ar_plane_tracker.h"
#include "pocketar.h"
#include "scripted_session.h"
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/xr_interface_extension.hpp>
#include <map>

namespace godot {

class ScriptedARInterface : public XRInterfaceExtension {
	GDCLASS(ScriptedARInterface, XRInterfaceExtension)

	bool initialized = false;
	bool externally_driven = false;
	bool geometry_dirty = true;
	int64_t generation = 0;
	int64_t playback_time = 0;
	uint64_t wall_start = 0;
	size_t cursor = 0;
	int64_t latest_frame_index = -1;
	PocketAR::SessionState state = PocketAR::INITIALIZING;
	PocketAR::TrackingReason reason = PocketAR::NONE;
	pocketar::ScriptedSession session;
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
	std::vector<ARPlaneSnapshot> planes;
	std::map<int64_t, Ref<ARPlaneTracker>> plane_trackers;
	TypedArray<Dictionary> pending_inputs;

	void set_state(PocketAR::SessionState p_state);
	void set_reason(PocketAR::TrackingReason p_reason);
	void clear_planes();
	void apply_planes(const std::vector<pocketar::FixturePlane> &p_planes);
	void advance_records(int64_t p_t_usec);
	void make_packet();

protected:
	static void _bind_methods();

public:
	Error load_fixture(const String &p_path);
	void advance_to(int64_t p_t_usec);
	TypedArray<Dictionary> get_pending_inputs();
	void reset();
	void application_pause();
	void application_resume();
	PocketAR::SessionState get_session_state() const { return state; }
	PocketAR::TrackingReason get_tracking_reason() const { return reason; }
	int64_t get_session_generation() const { return generation; }
	int64_t get_plane_count() const;
	String get_semantics() const { return String(session.semantics.c_str()); }
	void set_display_geometry(const Vector2i &p_viewport_size, int p_quarter_turns, double p_near, double p_far);
	Ref<ARFramePacket> get_current_packet() const { return current_packet; }
	Ref<ARFramePacket> get_presented_packet() const { return presented_packet; }
	void mark_presented();

	StringName _get_name() const override { return "PocketARScripted"; }
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
