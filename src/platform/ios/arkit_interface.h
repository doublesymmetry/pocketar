#pragma once

#include "core/ar_frame_packet.h"
#include "core/ar_plane_tracker.h"
#include "arkit_texture_pool.h"
#include <godot_cpp/classes/xr_interface_extension.hpp>
#include <map>
#include <memory>
#include <string>

namespace godot {

class ARKitInterface : public XRInterfaceExtension {
	GDCLASS(ARKitInterface, XRInterfaceExtension)

public:
	enum SessionState { UNSUPPORTED, CHECKING, NEEDS_INSTALL, INSTALLING, PERMISSION_DENIED, READY, INITIALIZING, TRACKING, PAUSED, FAILED };
	enum TrackingReason { NONE, BAD_STATE, INSUFFICIENT_LIGHT, EXCESSIVE_MOTION, INSUFFICIENT_FEATURES, CAMERA_UNAVAILABLE };

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
	Ref<ARKitTexturePool> textures;
	Ref<XRPositionalTracker> head;
	Ref<ARFramePacket> current_packet, presented_packet;
	std::map<std::string, Ref<ARPlaneTracker>> planes;
	int64_t generation = 0, sequence = 0;
	bool initialized = false;
	bool suspended = false;
	bool geometry_dirty = true;
	bool focus_auto = true;
	bool light_estimation = false;
	bool relocalization_enabled = true;
	int plane_detection = 3;
	Vector2i viewport_size = Vector2i(1, 1);
	double near_plane = 0.05, far_plane = 100.0;
	Transform3D last_good_pose;
	SessionState state = CHECKING;
	TrackingReason reason = NONE;
	TrackingStatus tracking_status = XR_UNKNOWN_TRACKING;
	Dictionary diagnostics, timings;
	Array state_history;
	void set_state(SessionState p_state);
	void increment(const char *p_key);
	void run_session(bool p_reset);
	void clear_planes();
	void copy_planes(void *p_frame, ARFramePacket &r_packet);
	bool on_owner_thread() const;

protected:
	static void _bind_methods();

public:
	ARKitInterface();
	~ARKitInterface();
	SessionState get_session_state() const { return state; }
	TrackingReason get_tracking_reason() const { return reason; }
	int64_t get_session_generation() const { return generation; }
	Dictionary get_diagnostics() const;
	void reset();
	void set_focus_auto(bool p_enabled);
	void set_light_estimation_enabled(bool p_enabled);
	void set_relocalization_enabled(bool p_enabled) { relocalization_enabled = p_enabled; }
	bool get_relocalization_enabled() const { return relocalization_enabled; }
	void set_plane_detection(int p_mask);
	void set_display_geometry(const Vector2i &p_size, int p_turns, double p_near, double p_far);
	Ref<ARFramePacket> get_current_packet() const { return current_packet; }
	Ref<ARFramePacket> get_presented_packet() const { return presented_packet; }
	void mark_presented();
	Array live_hit_test(const Vector2 &p_pixels);
	void application_pause();
	void application_resume();
	void session_interrupted(bool p_interrupted);
	void session_failed(int p_code, const String &p_message);

	StringName _get_name() const override { return "HandheldARKit"; }
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
VARIANT_ENUM_CAST(godot::ARKitInterface::SessionState)
VARIANT_ENUM_CAST(godot::ARKitInterface::TrackingReason)
