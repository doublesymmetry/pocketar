#pragma once

#include "simulated_ar_interface.h"
#include "pocketar.h"

namespace godot {

class SyntheticARInterface : public SimulatedARInterface {
	GDCLASS(SyntheticARInterface, SimulatedARInterface)

	bool frame_id_pose = false;
	int64_t sequence = 0;
	uint64_t last_tick = 0;
	double camera_rate = 30.0;
	Transform3D pose;
	void activity_changed() override;
	void make_packet(uint64_t p_tick);

protected:
	static void _bind_methods();

public:
	void reset();
	PocketAR::SessionState get_session_state() const override { return initialized && !is_paused() ? PocketAR::TRACKING : PocketAR::PAUSED; }
	PocketAR::TrackingReason get_tracking_reason() const { return PocketAR::NONE; }
	void set_intrinsics(double p_fx, double p_fy, double p_cx, double p_cy, const Vector2i &p_image_size);
	void set_pose(const Transform3D &p_pose);
	void set_camera_rate(double p_fps);
	void set_frame_id_pose(bool p_enabled);

	StringName _get_name() const override { return "PocketARSynthetic"; }
	bool _initialize() override;
	void _uninitialize() override;
	void _process() override;
};

}
