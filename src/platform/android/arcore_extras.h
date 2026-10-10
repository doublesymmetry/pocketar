#pragma once

#include "arcore_api.h"
#include "core/ar_frame_packet.h"
#include "core/ar_raycast_hit.h"
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <deque>
#include <map>
#include <thread>

namespace godot {

class ARCoreExtras {
	struct PendingInput {
		PackedByteArray payload;
		int attempts = 0;
	};

	ARCoreApi &api = arcore_api();
	ArSession *session = nullptr;
	ArFrame *live_frame = nullptr;
	ArFrame *recording_frame = nullptr;
	ArPose *scratch_pose = nullptr;
	ArTrackDataList *track_data_list = nullptr;
	ArFrame *old_probe_frame = nullptr;
	std::thread::id owner_thread;
	int64_t generation = 0;
	int64_t frame_id = 0;
	int64_t last_track_timestamp = 0;
	ArRecordingStatus recording_status = AR_RECORDING_NONE;
	ArPlaybackStatus playback_status = AR_PLAYBACK_NONE;
	bool point_cloud_enabled = false;
	bool frame_probes_enabled = false;
	bool image_capacity_probe = false;
	int image_probe_ticks = 0;
	int64_t image_probe_timestamp = 0;
	int64_t old_probe_timestamp = 0;
	ARRay old_probe_ray;
	Vector2 old_probe_pixel;
	Ref<ARFramePacket> latest_packet;
	std::map<int64_t, ArPlane *> native_planes;
	std::map<int64_t, ArHitResult *> live_hits;
	std::map<int64_t, ArAnchor *> anchors;
	std::deque<PendingInput> pending_inputs;
	std::vector<ArImage *> probe_images_held;
	Array playback_inputs;
	Dictionary point_cloud;
	Dictionary counters;

	bool usable() const;
	bool live_ready() const;
	void increment(const char *p_key, int64_t p_amount = 1);
	Dictionary result(const String &p_status, ArStatus p_code = AR_SUCCESS) const;
	Transform3D read_pose() const;
	Array collect_hits(ArHitResultList *p_list, const Vector3 &p_origin);
	Dictionary keep_anchor(ArAnchor *p_anchor, ArStatus p_status, bool p_fallback);
	Dictionary anchor_snapshot(int64_t p_id, ArAnchor *p_anchor);
	void read_playback_inputs(int64_t p_timestamp);
	void retry_inputs();
	void copy_point_cloud();
	void probe_old_frame(ArFrame *p_current);
	void probe_repeated_frame(ArFrame *p_frame, int64_t p_timestamp);
	static bool valid_dataset_uri(const String &p_uri);

public:
	ARCoreExtras() = default;
	ARCoreExtras(const ARCoreExtras &) = delete;
	ARCoreExtras &operator=(const ARCoreExtras &) = delete;
	void setup(ArSession *p_session, int64_t p_generation);
	void close();
	void before_update();
	void on_tick(ArFrame *p_frame);
	void on_frame(ArFrame *p_frame, int64_t p_frame_id, const Ref<ARFramePacket> &p_latest, const std::map<int64_t, ArPlane *> &p_planes);
	void on_repeated_frame(ArFrame *p_frame, const std::map<int64_t, ArPlane *> &p_planes);
	Dictionary diagnostics() const;
	Array live_hit_test(const Vector2 &p_view_pixels);
	Array live_hit_test_ray(const Vector3 &p_origin, const Vector3 &p_direction);
	Dictionary create_live_anchor(int64_t p_query_id);
	Dictionary create_anchor(const Ref<ARFramePacket> &p_old_packet, const Ref<ARRaycastHit> &p_hit, bool p_allow_fallback);
	Array get_anchors();
	bool remove_anchor(int64_t p_anchor_id);
	Dictionary start_recording(const String &p_file_uri);
	Dictionary stop_recording();
	Dictionary set_playback_dataset(const String &p_file_uri);
	Dictionary record_input(const PackedByteArray &p_payload);
	Array get_playback_inputs();
	void set_point_cloud_enabled(bool p_enabled);
	Dictionary get_point_cloud() const { return point_cloud.duplicate(true); }
	Dictionary begin_image_capacity_probe();
	void cancel_image_capacity_probe();
	bool probe_images(ArFrame *p_frame);
	ArFrame *begin_old_frame_probe(ArFrame *p_current);
	void set_frame_probes_enabled(bool p_enabled) { frame_probes_enabled = p_enabled; }
};

}
