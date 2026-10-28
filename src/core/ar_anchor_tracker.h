#pragma once

#include <godot_cpp/classes/xr_positional_tracker.hpp>

namespace godot {

struct ARAnchorSnapshot {
	int64_t anchor_id = 0;
	Transform3D transform;
	int tracking_state = 1;
	uint64_t lifecycle_revision = 0;
};

class ARAnchorRegistry;

class ARAnchorTracker : public XRPositionalTracker {
	GDCLASS(ARAnchorTracker, XRPositionalTracker)
	friend class ARAnchorRegistry;

public:
	enum TrackingState { TRACKING, PAUSED, STOPPED };

private:
	uint64_t owner_id = 0;
	uint64_t lifecycle_revision = 0;
	int64_t anchor_id = 0;
	int64_t session_generation = 0;
	int64_t source_plane_id = 0;
	Transform3D transform;
	int tracking_state = PAUSED;
	bool notification_pending = false;
	void update(const Transform3D &p_transform, int p_state);
	void notify_state();

protected:
	static void _bind_methods();

public:
	ARAnchorTracker();
	int64_t get_anchor_id() const { return anchor_id; }
	int64_t get_session_generation() const { return session_generation; }
	int64_t get_source_plane_id() const { return source_plane_id; }
	Transform3D get_transform() const { return transform; }
	int get_tracking_state() const { return tracking_state; }
};

}
VARIANT_ENUM_CAST(godot::ARAnchorTracker::TrackingState)
