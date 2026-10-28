#include "arcore_interface.h"
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

Ref<ARAnchorTracker> ARCoreInterface::create_anchor(const Transform3D &p_transform) {
	ERR_FAIL_COND_V(initialized && !on_owner_thread(), Ref<ARAnchorTracker>());
	return create_native_anchor(p_transform, 0);
}

Ref<ARAnchorTracker> ARCoreInterface::create_anchor_from_hit(const Ref<ARRaycastHit> &p_hit) {
	ERR_FAIL_COND_V(initialized && !on_owner_thread(), Ref<ARAnchorTracker>());
	if (!anchors.fresh_hit(p_hit, presented_packet, generation) || p_hit->get_plane_id() == 0) {
		UtilityFunctions::push_warning("Cannot create ARCore anchor: stale hit.");
		return Ref<ARAnchorTracker>();
	}
	return create_native_anchor(p_hit->get_pose(), p_hit->get_plane_id());
}

Ref<ARAnchorTracker> ARCoreInterface::create_native_anchor(const Transform3D &p_transform, int64_t p_plane_id) {
	if (!ARAnchorRegistry::valid_pose(p_transform)) {
		UtilityFunctions::push_warning("Cannot create ARCore anchor: invalid pose.");
		return Ref<ARAnchorTracker>();
	}
	if (state != PocketAR::TRACKING ||
			current_packet.is_null() || current_packet->session_generation != generation || !current_packet->tracking_valid || current_packet->anchor_epoch != anchors.get_epoch()) {
		UtilityFunctions::push_warning("Cannot create ARCore anchor: not tracking.");
		return Ref<ARAnchorTracker>();
	}
	ArPlane *plane = nullptr;
	ArPlane *parent = nullptr;
	if (p_plane_id > 0) {
		for (const auto &entry : planes) {
			if (entry.second.id == p_plane_id) { plane = entry.first; break; }
		}
		if (plane) {
			ArTrackingState tracking;
			api.ArTrackable_getTrackingState(session, reinterpret_cast<ArTrackable *>(plane), &tracking);
			if (tracking != AR_TRACKING_STATE_STOPPED) api.ArPlane_acquireSubsumedBy(session, plane, &parent);
			if (parent) {
				plane = parent;
				api.ArTrackable_getTrackingState(session, reinterpret_cast<ArTrackable *>(plane), &tracking);
			}
			if (tracking != AR_TRACKING_STATE_TRACKING) plane = nullptr;
		}
		if (!plane) {
			if (parent) api.ArTrackable_release(reinterpret_cast<ArTrackable *>(parent));
			UtilityFunctions::push_warning("Cannot create ARCore anchor: stale hit plane.");
			return Ref<ARAnchorTracker>();
		}
	} else if (p_plane_id < 0) {
#ifdef DEBUG_ENABLED
		if (debug_plane.get_plane_id() != p_plane_id || !debug_plane.is_tracking()) {
#endif
			UtilityFunctions::push_warning("Cannot create ARCore anchor: stale hit plane.");
			return Ref<ARAnchorTracker>();
#ifdef DEBUG_ENABLED
		}
#endif
	}
	const Quaternion rotation = p_transform.basis.get_rotation_quaternion();
	const float raw[] = { float(rotation.x), float(rotation.y), float(rotation.z), float(rotation.w),
		float(p_transform.origin.x), float(p_transform.origin.y), float(p_transform.origin.z) };
	ArPose *pose = nullptr;
	api.ArPose_create(session, raw, &pose);
	ArAnchor *native = nullptr;
	const ArStatus status = plane ? api.ArTrackable_acquireNewAnchor(session, reinterpret_cast<ArTrackable *>(plane), pose, &native) :
		api.ArSession_acquireNewAnchor(session, pose, &native);
	api.ArPose_destroy(pose);
	if (parent) api.ArTrackable_release(reinterpret_cast<ArTrackable *>(parent));
	if (status != AR_SUCCESS || !native) {
		UtilityFunctions::push_warning("Cannot create ARCore anchor: native refusal, status=" + String::num_int64(status) + ".");
		return Ref<ARAnchorTracker>();
	}
	native_anchors.emplace(anchors.next_id(), AnchorEntry{ native, AR_TRACKING_STATE_TRACKING });
	return anchors.add(generation, p_transform, p_plane_id);
}

void ARCoreInterface::remove_anchor(const Ref<ARAnchorTracker> &p_anchor) {
	ERR_FAIL_COND(initialized && !on_owner_thread());
	anchors.remove(p_anchor);
}

void ARCoreInterface::release_anchor(int64_t p_id) {
	const auto found = native_anchors.find(p_id);
	if (found == native_anchors.end()) return;
	ArAnchor *anchor = found->second.anchor;
	native_anchors.erase(found);
	api.ArAnchor_detach(session, anchor);
	api.ArAnchor_release(anchor);
}

void ARCoreInterface::refresh_anchor_states() {
	if (native_anchors.empty()) return;
	anchor_transitions.clear();
	for (auto &entry : native_anchors) {
		ArTrackingState tracking;
		api.ArAnchor_getTrackingState(session, entry.second.anchor, &tracking);
		if (tracking != entry.second.tracking) anchor_transitions.emplace_back(entry.first, tracking);
		entry.second.tracking = tracking;
	}
	const int64_t sampled_generation = generation;
	for (size_t i = 0; i < anchor_transitions.size(); ++i) {
		const auto transition = anchor_transitions[i];
		if (transition.second == AR_TRACKING_STATE_STOPPED) anchors.stop(transition.first);
		else if (transition.second == AR_TRACKING_STATE_PAUSED) anchors.pause(transition.first);
		if (generation != sampled_generation || !initialized || !session) return;
	}
}

void ARCoreInterface::copy_anchors(ARFramePacket &r_packet) {
	r_packet.anchor_epoch = anchors.get_epoch();
	if (native_anchors.empty()) return;
	r_packet.anchors.reserve(native_anchors.size());
	for (const auto &entry : native_anchors) {
		const Ref<ARAnchorTracker> tracker = anchors.get_trackers().at(entry.first);
		ARAnchorSnapshot snapshot;
		snapshot.anchor_id = entry.first;
		snapshot.transform = tracker->get_transform();
		snapshot.tracking_state = r_packet.tracking_valid ? int(entry.second.tracking) : int(ARAnchorTracker::PAUSED);
		if (snapshot.tracking_state == ARAnchorTracker::TRACKING) {
			api.ArAnchor_getPose(session, entry.second.anchor, scratch_pose);
			snapshot.transform = read_pose();
		}
		r_packet.anchors.push_back(snapshot);
	}
}

}
