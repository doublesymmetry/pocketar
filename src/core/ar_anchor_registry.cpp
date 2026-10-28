#include "ar_anchor_registry.h"
#include <godot_cpp/classes/xr_server.hpp>
#include <cmath>

namespace godot {

void ARAnchorRegistry::configure(Object *p_owner, std::function<void(int64_t)> p_release) {
	owner = p_owner;
	release = std::move(p_release);
}

bool ARAnchorRegistry::valid_pose(const Transform3D &p_pose) {
	if (!p_pose.is_finite()) return false;
	for (int i = 0; i < 3; ++i) {
		if (std::abs(p_pose.basis.get_column(i).length_squared() - 1) > 1e-5) return false;
		for (int j = i + 1; j < 3; ++j) if (std::abs(p_pose.basis.get_column(i).dot(p_pose.basis.get_column(j))) > 1e-5) return false;
	}
	return std::abs(p_pose.basis.determinant() - 1) <= 1e-5;
}

void ARAnchorRegistry::changed() {
	owner->call_deferred("emit_signal", "anchors_changed");
}

Ref<ARAnchorTracker> ARAnchorRegistry::add(int64_t p_generation, const Transform3D &p_pose, int64_t p_source_plane_id) {
	if (clearing) { release(next_id()); WARN_PRINT("Cannot create anchor: not tracking during teardown."); return Ref<ARAnchorTracker>(); }
	Ref<ARAnchorTracker> anchor;
	anchor.instantiate();
	anchor->owner_id = owner->get_instance_id();
	anchor->anchor_id = ++sequence;
	anchor->session_generation = p_generation;
	anchor->source_plane_id = p_source_plane_id;
	anchor->transform = p_pose;
	anchor->set_tracker_name("pocketar/anchor/" + String::num_int64(p_generation) + "/" + String::num_int64(sequence));
	anchor->set_pose("default", p_pose, Vector3(), Vector3(), XRPose::XR_TRACKING_CONFIDENCE_NONE);
	anchor->invalidate_pose("default");
	trackers.emplace(sequence, anchor);
	changed();
	XRServer::get_singleton()->add_tracker(anchor);
	if (!owns(anchor)) { WARN_PRINT("Cannot create anchor: not tracking after removal during creation."); return Ref<ARAnchorTracker>(); }
	return anchor;
}

bool ARAnchorRegistry::owns(const Ref<ARAnchorTracker> &p_anchor) const {
	if (p_anchor.is_null() || p_anchor->owner_id != owner->get_instance_id()) return false;
	const auto entry = trackers.find(p_anchor->anchor_id);
	return entry != trackers.end() && entry->second == p_anchor;
}

void ARAnchorRegistry::remove(const Ref<ARAnchorTracker> &p_anchor) {
	if (p_anchor.is_valid() && p_anchor->owner_id == owner->get_instance_id() && (p_anchor->tracking_state == ARAnchorTracker::STOPPED || clearing)) return;
	ERR_FAIL_COND_MSG(!owns(p_anchor), "Cannot remove a foreign anchor.");
	stop(p_anchor->anchor_id);
}

void ARAnchorRegistry::stop(int64_t p_id) {
	const auto entry = trackers.find(p_id);
	if (entry == trackers.end()) return;
	const Ref<ARAnchorTracker> anchor = entry->second;
	trackers.erase(entry);
	release(p_id);
	changed();
	anchor->update(anchor->transform, ARAnchorTracker::STOPPED);
	XRServer::get_singleton()->remove_tracker(anchor);
}

void ARAnchorRegistry::clear(bool p_reset_ids) {
	++epoch;
	if (p_reset_ids) sequence = 0;
	if (trackers.empty()) return;
	clearing = true;
	auto previous = std::move(trackers);
	trackers.clear();
	for (const auto &entry : previous) release(entry.first);
	changed();
	for (const auto &entry : previous) {
		entry.second->update(entry.second->transform, ARAnchorTracker::STOPPED);
		XRServer::get_singleton()->remove_tracker(entry.second);
	}
	clearing = false;
}

void ARAnchorRegistry::pause() {
	++epoch;
	if (trackers.empty()) return;
	const auto active = trackers;
	for (const auto &entry : active) {
		if (owns(entry.second)) entry.second->update(entry.second->transform, ARAnchorTracker::PAUSED);
	}
}

void ARAnchorRegistry::pause(int64_t p_id) {
	const auto entry = trackers.find(p_id);
	if (entry == trackers.end()) return;
	const Ref<ARAnchorTracker> anchor = entry->second;
	++anchor->lifecycle_revision;
	anchor->update(anchor->transform, ARAnchorTracker::PAUSED);
}

void ARAnchorRegistry::apply(const Ref<ARFramePacket> &p_packet, int64_t p_generation) {
	const Ref<ARFramePacket> packet = p_packet;
	if (trackers.empty() || packet.is_null() || packet->session_generation != p_generation || packet->anchor_epoch != epoch) return;
	for (const ARAnchorSnapshot &sample : packet->anchors) {
		if (packet->anchor_epoch != epoch) break;
		const auto entry = trackers.find(sample.anchor_id);
		if (entry == trackers.end() || entry->second->session_generation != p_generation || entry->second->lifecycle_revision != sample.lifecycle_revision) continue;
		const Ref<ARAnchorTracker> anchor = entry->second;
		if (sample.tracking_state == ARAnchorTracker::STOPPED) stop(sample.anchor_id);
		else anchor->update(sample.transform, sample.tracking_state);
	}
}

bool ARAnchorRegistry::fresh_hit(const Ref<ARRaycastHit> &p_hit, const Ref<ARFramePacket> &p_presented, int64_t p_generation) const {
	return p_hit.is_valid() && p_presented.is_valid() && p_presented->session_generation == p_generation && p_presented->anchor_epoch == epoch && p_hit->get_session_generation() == p_generation && p_hit->get_frame_id() == p_presented->frame_id && p_presented->tracking_valid;
}

}
