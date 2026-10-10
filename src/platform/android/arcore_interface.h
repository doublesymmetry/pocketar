#pragma once

#include "arcore_api.h"
#include "arcore_extras.h"
#include "arcore_bridge.h"
#include "core/ar_frame_packet.h"
#include "core/ar_plane_tracker.h"
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/xr_interface_extension.hpp>
#include <atomic>
#include <map>
#include <memory>
#include <thread>
#include <vector>

namespace godot {

class ARCoreInterface : public XRInterfaceExtension {
	GDCLASS(ARCoreInterface, XRInterfaceExtension)
public:
	enum SessionState { UNSUPPORTED, CHECKING, NEEDS_INSTALL, INSTALLING, PERMISSION_DENIED, READY, INITIALIZING, TRACKING, PAUSED, FAILED };
	enum TrackingReason { NONE, BAD_STATE, INSUFFICIENT_LIGHT, EXCESSIVE_MOTION, INSUFFICIENT_FEATURES, CAMERA_UNAVAILABLE };

private:
	struct TextureSlot {
		Ref<ImageTexture> y;
		Ref<ImageTexture> uv;
		uint64_t last_use = 0;
	};
	struct PlaneEntry {
		int64_t id = 0;
		Ref<ARPlaneTracker> tracker;
	};
	struct DestroyStats {
		std::atomic<int64_t> count{0};
		std::atomic<int64_t> last_usec{0};
		std::atomic<int64_t> active{0};
	};
	ARCoreApi &api = arcore_api();
	ARCoreExtras extras;
	String playback_dataset;
	ArSession *session = nullptr;
	ArFrame *frame = nullptr;
	ArPose *scratch_pose = nullptr;
	ArCameraIntrinsics *camera_intrinsics = nullptr;
	ArTrackableList *trackable_list = nullptr;
	std::map<ArPlane *, PlaneEntry> planes;
	std::vector<TextureSlot> texture_slots;
	std::shared_ptr<DestroyStats> destroy_stats = std::make_shared<DestroyStats>();
	std::thread::id owner_thread;
	bool initialized = false;
	bool running = false;
	bool application_suspended = false;
	bool geometry_dirty = true;
	bool focus_auto = true;
	int camera_config_index = -1;
	int64_t generation = 0;
	int64_t sequence = 0;
	int64_t plane_sequence = 0;
	int64_t last_timestamp = 0;
	uint64_t geometry_revision = 0;
	uint64_t rd_swaps = 0;
	uint32_t frame_delay = 2;
	uint64_t retry_at = 0;
	uint64_t retry_delay = 100000;
	uint64_t reset_tick = 0;
	uint64_t first_tracking_tick = 0;
	int rotation = 0;
	Vector2i viewport_size = Vector2i(1, 1);
	Vector2i cpu_size, gpu_size, fps_range;
	Vector2i staging_size;
	PackedByteArray luma, chroma;
	double near_plane = 0.05, far_plane = 100.0;
	Transform3D last_good_pose;
	SessionState state = CHECKING;
	TrackingReason reason = NONE;
	TrackingStatus tracking_status = XR_UNKNOWN_TRACKING;
	Ref<XRPositionalTracker> head;
	Ref<ARFramePacket> current_packet, presented_packet, applied_packet;
	Dictionary diagnostics;
	Dictionary timings;
	Dictionary update_counts;
	Array state_history;
	std::vector<double> acquire_times, repack_times, stats_times, upload_times, repack_upload_times;
	size_t acquire_cursor = 0, repack_cursor = 0, stats_cursor = 0, upload_cursor = 0, repack_upload_cursor = 0;
	size_t bridge_history_size = 0;

	bool on_owner_thread() const;
	void set_state(SessionState p_state);
	void increment(const char *p_key, int64_t p_amount = 1);
	void fail(ArStatus p_status, const String &p_operation);
	bool create_session();
	bool choose_camera_config();
	void refresh_camera_config();
	void resume_session();
	void pause_session();
	void release_session();
	void clear_planes();
	void copy_planes(ARFramePacket &r_packet);
	int64_t ensure_plane(ArPlane *p_plane, bool p_acquired);
	Transform3D read_pose() const;
	void build_packet(int64_t p_timestamp);
	TextureSlot *find_texture_slot();
	bool texture_slot_active(const TextureSlot &p_slot) const;
	void inspect_projection(const ArCamera *p_camera, const float *p_projection);
	void inspect_intrinsics(const ArCamera *p_camera, ARFramePacket &r_packet);
	void inspect_hardware_buffer();
	static void sample(std::vector<double> &r_values, size_t &r_cursor, double p_value);
	static double percentile(const std::vector<double> &p_values, double p_fraction);

protected:
	static void _bind_methods();

public:
	~ARCoreInterface();
	SessionState get_session_state() const { return state; }
	TrackingReason get_tracking_reason() const { return reason; }
	int64_t get_session_generation() const { return generation; }
	Dictionary get_diagnostics() const;
	void reset();
	void set_focus_auto(bool p_auto);
	void set_camera_config_index(int p_index);
	void set_display_geometry(const Vector2i &p_size, int p_turns, double p_near, double p_far);
	Ref<ARFramePacket> get_current_packet() const { return current_packet; }
	Ref<ARFramePacket> get_presented_packet() const { return presented_packet; }
	void mark_applied(const Ref<ARFramePacket> &p_packet);
	void mark_presented();
	Dictionary get_dataset_diagnostics() const { return extras.diagnostics(); }
	Array live_hit_test(const Vector2 &p_pixels) { return extras.live_hit_test(p_pixels); }
	Array live_hit_test_ray(const Vector3 &p_origin, const Vector3 &p_direction) { return extras.live_hit_test_ray(p_origin, p_direction); }
	Dictionary create_live_anchor(int64_t p_query_id) { return extras.create_live_anchor(p_query_id); }
	Dictionary create_anchor(const Ref<ARFramePacket> &p_packet, const Ref<ARRaycastHit> &p_hit, bool p_fallback) { return extras.create_anchor(p_packet, p_hit, p_fallback); }
	Array get_anchors() { return extras.get_anchors(); }
	bool remove_anchor(int64_t p_id) { return extras.remove_anchor(p_id); }
	Dictionary start_recording(const String &p_uri) { return extras.start_recording(p_uri); }
	Dictionary stop_recording() { return extras.stop_recording(); }
	void start_playback(const String &p_uri);
	void stop_playback();
	Dictionary record_input(const PackedByteArray &p_data) { return extras.record_input(p_data); }
	Array get_playback_inputs() { return extras.get_playback_inputs(); }
	void set_point_cloud_enabled(bool p_enabled) { extras.set_point_cloud_enabled(p_enabled); }
	Dictionary begin_image_capacity_probe() { return extras.begin_image_capacity_probe(); }
	void cancel_image_capacity_probe() { extras.cancel_image_capacity_probe(); }
	void begin_old_frame_probe();
	void set_frame_probes_enabled(bool p_enabled) { extras.set_frame_probes_enabled(p_enabled); }
	void bridge_changed();
	void application_pause();
	void application_resume();
	void harness_pause() { application_pause(); }
	void harness_resume() { application_resume(); }

	StringName _get_name() const override { return "HandheldARCore"; }
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
	void _end_frame() override;
	bool _pre_draw_viewport(const RID &p_target) override { return current_packet.is_valid() && current_packet->get_session_generation() == generation; }
};
}
VARIANT_ENUM_CAST(godot::ARCoreInterface::SessionState)
VARIANT_ENUM_CAST(godot::ARCoreInterface::TrackingReason)
