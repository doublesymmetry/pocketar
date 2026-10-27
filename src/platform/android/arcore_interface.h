#pragma once

#ifdef DEBUG_ENABLED
#include "core/debug_plane.h"
#endif

#include "arcore_api.h"
#include "arcore_extras.h"
#include "arcore_bridge.h"
#include "gpu_camera_validation.h"
#include "core/ar_frame_packet.h"
#include "core/ar_plane_tracker.h"
#include "core/pocketar.h"
#include "core/timing_samples.h"
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/xr_interface_extension.hpp>
#include <atomic>
#include <map>
#include <memory>
#include <thread>
#include <vector>

namespace godot {

inline uint64_t tick_usec() { return Time::get_singleton()->get_ticks_usec(); }

class ARCoreInterface : public XRInterfaceExtension {
	GDCLASS(ARCoreInterface, XRInterfaceExtension)
private:
	struct TextureSlot {
		Ref<ImageTexture> y;
		Ref<ImageTexture> uv;
		uint64_t last_use = 0;
	};
	struct PlaneEntry {
		int64_t id = 0;
		Ref<ARPlaneTracker> tracker;
		bool dirty = false;
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
	ArConfig *config = nullptr;
	PocketAR::PlaneFindingMode plane_finding_mode = PocketAR::PLANE_FINDING_DISABLED;
	ArFrame *frame = nullptr;
	ArPose *scratch_pose = nullptr;
	ArCameraIntrinsics *camera_intrinsics = nullptr;
	ArTrackableList *trackable_list = nullptr;
	std::map<ArPlane *, PlaneEntry> planes;
	std::vector<ArPlane *> pending_planes;
	std::vector<float> plane_polygon;
	std::vector<TextureSlot> texture_slots;
	std::shared_ptr<DestroyStats> destroy_stats = std::make_shared<DestroyStats>();
	std::thread::id owner_thread;
#ifdef DEBUG_ENABLED
	DebugPlane debug_plane;
#endif
	bool initialized = false;
	bool running = false;
	bool application_suspended = false;
	bool suspended = false;
	PocketAR::CameraQuality camera_quality = PocketAR::HIGHEST;
	bool geometry_dirty = true;
	bool diagnostics_enabled = false;
	int camera_config_index = -1;
	String camera_path = "auto";
	Ref<Texture2D> gpu_texture;
	bool gpu_active = false;
	bool gpu_validate = false;
	bool gpu_validation_pending = false;
	int gpu_color_model = 0;
	int gpu_color_range = 0;
	int gpu_buffer_misses = 0;
	uint64_t gpu_validation_swap = 0;
	Vector2i gpu_validation_size;
	pocketar::CameraBlockMeans gpu_cpu_means{};
	double gpu_validation_luma_range = 0, gpu_validation_chroma_magnitude = 0;
	double gpu_validation_worst = 0;
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
	Transform3D last_good_pose;
	PocketAR::SessionState state = PocketAR::CHECKING;
	PocketAR::TrackingReason reason = PocketAR::NONE;
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
	std::vector<double> gpu_submit_times;
	size_t gpu_submit_cursor = 0;
	std::vector<double> frame_setup_times;
	size_t frame_setup_cursor = 0;
	uint64_t frame_setup_swap = 0;

	bool on_owner_thread() const;
	void set_state(PocketAR::SessionState p_state);
	void set_reason(PocketAR::TrackingReason p_reason);
	void increment(const char *p_key, int64_t p_amount = 1);
	void fail(ArStatus p_status, const String &p_operation);
	void create_session();
	bool choose_camera_config();
	void refresh_camera_config();
	void refresh_plane_finding_mode();
	void resume_session();
	void pause_session();
	void release_session();
	void clear_planes();
	void refresh_planes();
	bool copy_planes(ARFramePacket &r_packet);
	int64_t ensure_plane(ArPlane *p_plane);
	Transform3D read_pose() const;
	void build_packet(int64_t p_timestamp);
	TextureSlot *find_texture_slot();
	bool texture_slot_active(const TextureSlot &p_slot) const;
	void inspect_projection(const ArCamera *p_camera, const float *p_projection);
	void inspect_intrinsics(const ArCamera *p_camera, ARFramePacket &r_packet);
	void detect_gpu_camera();
	void fallback_gpu_camera(const String &p_reason);
	static void gpu_camera_completion(int p_status, uint64_t p_interface, int64_t p_generation);
	void gpu_camera_completed(int p_status, int64_t p_generation);
	bool submit_gpu_camera();
	void capture_gpu_validation(int64_t p_timestamp);
	void finish_gpu_validation();

protected:
	static void _bind_methods();

public:
#ifdef DEBUG_ENABLED
	int64_t set_debug_plane(const Transform3D &p_transform, const Vector2 &p_extents);
	void clear_debug_plane();
#endif
	~ARCoreInterface();
	PocketAR::SessionState get_session_state() const { return state; }
	PocketAR::TrackingReason get_tracking_reason() const { return reason; }
	int64_t get_session_generation() const { return generation; }
	Dictionary get_diagnostics() const;
	void set_diagnostics_enabled(bool p_enabled);
	void reset();
	void set_plane_finding_mode(PocketAR::PlaneFindingMode p_mode);
	PocketAR::PlaneFindingMode get_plane_finding_mode() const { return plane_finding_mode; }
	void set_suspended(bool p_suspended);
	void set_camera_quality(PocketAR::CameraQuality p_quality);
	void set_camera_config_index(int p_index);
	void set_camera_path(const String &p_path);
	void set_gpu_validation_enabled(bool p_enabled);
	void set_display_geometry(const Vector2i &p_size, int p_turns, double p_near, double p_far);
	Ref<ARFramePacket> get_current_packet() const { return current_packet; }
	Ref<ARFramePacket> get_presented_packet() const { return presented_packet; }
	void mark_applied(const Ref<ARFramePacket> &p_packet);
	void mark_presented();
	Dictionary get_dataset_diagnostics() const { return extras.diagnostics(); }
	Array live_hit_test(const Vector2 &p_pixels) { return extras.live_hit_test(p_pixels); }
	Array live_hit_test_ray(const Vector3 &p_origin, const Vector3 &p_direction) { return extras.live_hit_test_ray(p_origin, p_direction); }
	Dictionary start_recording(const String &p_uri) { return extras.start_recording(p_uri); }
	Dictionary stop_recording() { return extras.stop_recording(); }
	void start_playback(const String &p_uri);
	Dictionary record_input(const PackedByteArray &p_data) { return extras.record_input(p_data); }
	Array get_playback_inputs() { return extras.get_playback_inputs(); }
	void set_point_cloud_enabled(bool p_enabled) { extras.set_point_cloud_enabled(p_enabled); }
	void bridge_changed();
	void application_pause();
	void application_resume();

	StringName _get_name() const override { return "PocketARCore"; }
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
