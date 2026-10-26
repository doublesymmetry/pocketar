#pragma once

#include "ar_plane_snapshot.h"
#include <godot_cpp/classes/xr_positional_tracker.hpp>

namespace godot {

class ARPlaneTracker : public XRPositionalTracker {
	GDCLASS(ARPlaneTracker, XRPositionalTracker)

public:
	enum TrackingState { TRACKING, PAUSED, STOPPED };
	enum Alignment { HORIZONTAL_UPWARD_FACING, HORIZONTAL_DOWNWARD_FACING, VERTICAL };

private:
	ARPlaneSnapshot snapshot;

protected:
	static void _bind_methods();

public:
	ARPlaneTracker();
	void update(const ARPlaneSnapshot &p_snapshot);
	const ARPlaneSnapshot &get_snapshot() const { return snapshot; }
	int64_t get_plane_id() const { return snapshot.plane_id; }
	int64_t get_session_generation() const { return snapshot.session_generation; }
	int get_alignment() const { return snapshot.alignment; }
	Vector2 get_extents() const { return snapshot.extents; }
	PackedVector2Array get_polygon() const { return snapshot.polygon; }
	int64_t get_subsumed_by() const { return snapshot.subsumed_by; }
	int get_tracking_state() const { return snapshot.tracking_state; }
	Transform3D get_transform() const { return snapshot.transform; }
};

}
VARIANT_ENUM_CAST(godot::ARPlaneTracker::TrackingState)
VARIANT_ENUM_CAST(godot::ARPlaneTracker::Alignment)
