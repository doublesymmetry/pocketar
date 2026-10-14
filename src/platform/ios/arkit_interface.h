#pragma once

#ifdef DEBUG_ENABLED
#include "core/debug_plane.h"
#endif

#include "core/ar_frame_packet.h"
#include "core/ar_plane_tracker.h"
#include "core/pocketar.h"
#include "arkit_texture_pool.h"
#include <godot_cpp/classes/xr_interface_extension.hpp>
#include <map>
#include <memory>
#include <string>

namespace godot {

class ARKitInterface : public XRInterfaceExtension {
	GDCLASS(ARKitInterface, XRInterfaceExtension)

	struct Impl;
	std::unique_ptr<Impl> impl;
	Ref<ARKitTexturePool> textures;
	Ref<XRPositionalTracker> head;
	Ref<ARFramePacket> current_packet, presented_packet, applied_packet;
	std::map<std::string, Ref<ARPlaneTracker>> planes;
	std::vector<ARPlaneSnapshot> published_planes;
	int64_t generation = 0, sequence = 0;
	int64_t native_anchor_count = 0;
#ifdef DEBUG_ENABLED
	DebugPlane debug_plane;
#endif
	bool initialized = false;
	bool suspended = false;
	bool geometry_dirty = true;
	Vector2i viewport_size = Vector2i(1, 1);
	double near_plane = 0.05, far_plane = 100.0;
	Transform3D last_good_pose;
	PocketAR::SessionState state = PocketAR::CHECKING;
	PocketAR::TrackingReason reason = PocketAR::NONE;
	PocketAR::PlaneFindingMode plane_finding_mode = PocketAR::PLANE_FINDING_HORIZONTAL_AND_VERTICAL;
	TrackingStatus tracking_status = XR_UNKNOWN_TRACKING;
	Dictionary diagnostics, timings;
	Array state_history;
	void set_state(PocketAR::SessionState p_state);
	void set_reason(PocketAR::TrackingReason p_reason);
	void increment(const char *p_key);
	void run_session(bool p_reset);
	void clear_packets();
	void clear_planes();
	bool copy_planes(void *p_frame, ARFramePacket &r_packet);
	bool on_owner_thread() const;

protected:
	static void _bind_methods();

public:
#ifdef DEBUG_ENABLED
	int64_t set_debug_plane(const Transform3D &p_transform, const Vector2 &p_extents);
	void clear_debug_plane();
#endif
	ARKitInterface();
	~ARKitInterface();
	PocketAR::SessionState get_session_state() const { return state; }
	PocketAR::TrackingReason get_tracking_reason() const { return reason; }
	int64_t get_session_generation() const { return generation; }
	Dictionary get_diagnostics() const;
	Dictionary get_native_plane_identifiers() const;
	void reset();
	void set_plane_finding_mode(PocketAR::PlaneFindingMode p_mode);
	PocketAR::PlaneFindingMode get_plane_finding_mode() const { return plane_finding_mode; }
	void set_display_geometry(const Vector2i &p_size, int p_turns, double p_near, double p_far);
	Ref<ARFramePacket> get_current_packet() const { return current_packet; }
	Ref<ARFramePacket> get_presented_packet() const { return presented_packet; }
	void mark_applied(const Ref<ARFramePacket> &p_packet);
	void mark_presented();
	bool debug_check_camera_material(const RID &p_material);
	Array live_hit_test(const Vector2 &p_pixels);
	void application_pause();
	void application_resume();
	void session_interrupted(bool p_interrupted);
	void session_failed(int p_code, const String &p_message);

	StringName _get_name() const override { return "PocketARKit"; }
	uint32_t _get_capabilities() const override { return XR_MONO | XR_AR; }
	bool _is_initialized() const override { return initialized; }
	bool _initialize() override;
	void _uninitialize() override;
	Vector2 _get_render_target_size() override;
	uint32_t _get_view_count() override { return 1; }
	Transform3D _get_camera_transform() override;
	Transform3D _get_transform_for_view(uint32_t p_view, const Transform3D &p_transform) override;
	PackedFloat64Array _get_projection_for_view(uint32_t p_view, double p_aspect, double p_near, double p_far) override;
	TrackingStatus _get_tracking_status() const override { return tracking_status; }
	void _process() override;
	bool _pre_draw_viewport(const RID &p_target) override;
};

void shutdown_arkit_interfaces();

}
