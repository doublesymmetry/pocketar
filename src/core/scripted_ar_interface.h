#pragma once

#include "simulated_ar_interface.h"
#include "ar_plane_tracker.h"
#include "pocketar.h"
#include "scripted_session.h"
#include <map>

namespace godot {

class ScriptedARInterface : public SimulatedARInterface {
	GDCLASS(ScriptedARInterface, SimulatedARInterface)

	bool replay_failed = false;
	uint64_t frame_anchor_epoch = 0;
	int64_t playback_time = 0;
	size_t cursor = 0;
	int64_t latest_frame_index = -1;
	int64_t latest_planes_index = -1;
	int64_t applied_planes_index = -1;
	Transform3D applied_plane_map;
	PocketAR::PlaneFindingMode applied_plane_mode = PocketAR::PLANE_FINDING_DISABLED;
	PocketAR::SessionState state = PocketAR::INITIALIZING;
	PocketAR::TrackingReason reason = PocketAR::NONE;
	pocketar::ScriptedSession session;
	std::vector<ARPlaneSnapshot> planes;
	std::map<int64_t, Ref<ARPlaneTracker>> plane_trackers;
	TypedArray<Dictionary> pending_inputs;

	void set_state(PocketAR::SessionState p_state);
	void set_reason(PocketAR::TrackingReason p_reason);
	void clear_planes();
	void apply_planes(const std::vector<pocketar::FixturePlane> &p_planes);
	void advance_records(int64_t p_t_usec);
	void make_packet();
	void activity_changed() override;
	bool anchor_plane_tracking(int64_t p_id) const override;
	int64_t plane_geometry_rebuild_count = 0;

protected:
	static void _bind_methods();

public:
	Error load_fixture(const String &p_path);
	void advance_to(int64_t p_t_usec);
	TypedArray<Dictionary> get_pending_inputs();
	void reset();
	PocketAR::SessionState get_session_state() const override { return state; }
	PocketAR::TrackingReason get_tracking_reason() const { return reason; }
	int64_t get_plane_count() const;
	int64_t get_plane_geometry_rebuild_count() const { return plane_geometry_rebuild_count; }
	String get_semantics() const { return String(session.semantics.c_str()); }

	StringName _get_name() const override { return "PocketARScripted"; }
	bool _initialize() override;
	void _uninitialize() override;
	void _process() override;
};

}
