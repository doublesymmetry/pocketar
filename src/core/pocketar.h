#pragma once

#include "ar_frame_packet.h"
#include "ar_raycast_hit.h"
#include "camera_quality.h"
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot {

class PocketAR : public Object {
	GDCLASS(PocketAR, Object)

protected:
	static void _bind_methods();

public:
	enum SessionState { UNSUPPORTED, CHECKING, NEEDS_INSTALL, INSTALLING, PERMISSION_DENIED, READY, INITIALIZING, TRACKING, PAUSED, FAILED };
	enum TrackingReason { NONE, BAD_STATE, INSUFFICIENT_LIGHT, EXCESSIVE_MOTION, INSUFFICIENT_FEATURES, CAMERA_UNAVAILABLE };
	enum PlaneFindingMode { PLANE_FINDING_DISABLED = 0, PLANE_FINDING_HORIZONTAL = 1, PLANE_FINDING_HORIZONTAL_AND_VERTICAL = 3 };
	enum CameraQuality { HIGHEST = pocketar::HIGHEST, BALANCED = pocketar::BALANCED, LOWEST = pocketar::LOWEST };
	enum AlignmentMask { ALIGNMENT_MASK_HORIZONTAL_UPWARD = 1, ALIGNMENT_MASK_HORIZONTAL_DOWNWARD = 2, ALIGNMENT_MASK_VERTICAL = 4, ALIGNMENT_MASK_ALL = 7 };
	String get_version() const { return "0.0.1"; }
	String get_backend() const;
	Dictionary screen_to_ray(const Ref<ARFramePacket> &p_packet, const Vector2 &p_viewport_size, const Vector2 &p_screen_point) const;
	TypedArray<ARRaycastHit> raycast_planes(const Ref<ARFramePacket> &p_packet, const Dictionary &p_ray) const;
};

}
VARIANT_ENUM_CAST(godot::PocketAR::SessionState)
VARIANT_ENUM_CAST(godot::PocketAR::TrackingReason)
VARIANT_ENUM_CAST(godot::PocketAR::PlaneFindingMode)
VARIANT_ENUM_CAST(godot::PocketAR::AlignmentMask)
VARIANT_ENUM_CAST(godot::PocketAR::CameraQuality)
