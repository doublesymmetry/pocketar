#pragma once

#include "ar_anchor_tracker.h"
#include "ar_frame_packet.h"
#include "ar_raycast_hit.h"
#include <functional>
#include <map>

namespace godot {

class ARAnchorRegistry {
	Object *owner = nullptr;
	std::function<void(int64_t)> release;
	std::map<int64_t, Ref<ARAnchorTracker>> trackers;
	int64_t sequence = 0;
	uint64_t epoch = 0;
	bool clearing = false;
	void changed();

public:
	void configure(Object *p_owner, std::function<void(int64_t)> p_release);
	int64_t next_id() const { return sequence + 1; }
	Ref<ARAnchorTracker> add(int64_t p_generation, const Transform3D &p_pose, int64_t p_source_plane_id = 0);
	bool owns(const Ref<ARAnchorTracker> &p_anchor) const;
	void remove(const Ref<ARAnchorTracker> &p_anchor);
	void stop(int64_t p_id);
	void clear(bool p_reset_ids = true);
	void pause();
	void pause(int64_t p_id);
	void invalidate_samples() { ++epoch; }
	uint64_t get_revision(int64_t p_id) const { return trackers.at(p_id)->lifecycle_revision; }
	uint64_t get_epoch() const { return epoch; }
	const std::map<int64_t, Ref<ARAnchorTracker>> &get_trackers() const { return trackers; }
	bool empty() const { return trackers.empty(); }
	void apply(const Ref<ARFramePacket> &p_packet, int64_t p_generation);
	bool fresh_hit(const Ref<ARRaycastHit> &p_hit, const Ref<ARFramePacket> &p_presented, int64_t p_generation) const;
	static bool valid_pose(const Transform3D &p_pose);
};

}
