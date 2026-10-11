#include "arcore_interface.h"
#include "yuv_repack.h"
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/xr_server.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <EGL/egl.h>
#include <algorithm>
#include <cmath>
#include <time.h>

namespace godot {

static int64_t clock_ns(clockid_t p_clock) {
	timespec value{};
	clock_gettime(p_clock, &value);
	return int64_t(value.tv_sec) * 1000000000 + value.tv_nsec;
}

void ARCoreInterface::_process() {
	if (!initialized) return;
	ERR_FAIL_COND(!on_owner_thread());
	bridge_changed();
	if (!session || !running || application_suspended || state == PocketAR::FAILED || state == PocketAR::UNSUPPORTED || state == PocketAR::PERMISSION_DENIED) return;
	const auto bridge = arcore_bridge_snapshot();
	if (!bridge.resumed || bridge.state != PocketAR::READY) return;
	if (frame_setup_swap != rd_swaps) {
		sample_timing(frame_setup_times, frame_setup_cursor, RenderingServer::get_singleton()->get_frame_setup_time_cpu() * 1000.0);
		frame_setup_swap = rd_swaps;
	}
	if (gpu_validation_pending && rd_swaps > gpu_validation_swap) finish_gpu_validation();
	if (geometry_dirty || geometry_revision != bridge.geometry_revision) {
		rotation = bridge.rotation;
		api.ArSession_setDisplayGeometry(session, rotation, viewport_size.x, viewport_size.y);
		geometry_revision = bridge.geometry_revision;
		geometry_dirty = false;
	}
	if (eglGetCurrentContext() != EGL_NO_CONTEXT) {
		increment("egl_context_violations");
		fail(AR_ERROR_FATAL, "ArSession_update thread has an EGL context");
		pause_session();
		return;
	}
	const uint64_t start = tick_usec();
	extras.before_update();
	const ArStatus status = api.ArSession_update(session, frame);
	diagnostics["update_us"] = int64_t(tick_usec() - start);
	const String key = String::num_int64(status);
	update_counts[key] = int64_t(update_counts.get(key, 0)) + 1;
	if (status != AR_SUCCESS) {
		UtilityFunctions::print("ARCORE update status=", int(status));
		if (status == AR_ERROR_SESSION_PAUSED) { running = false; set_state(PocketAR::PAUSED); }
		else if (status == AR_ERROR_CAMERA_NOT_AVAILABLE || status == AR_ERROR_FATAL) {
			set_reason(PocketAR::CAMERA_UNAVAILABLE);
			reset();
		} else { fail(status, "update"); pause_session(); }
		return;
	}
	increment("successful_updates");
	extras.on_tick(frame);
	int64_t timestamp = 0;
	api.ArFrame_getTimestamp(session, frame, &timestamp);
	if (timestamp == 0 || timestamp == last_timestamp) {
		if (timestamp > 0 && current_packet.is_valid() && current_packet->get_viewport_size() == Vector2(viewport_size) && current_packet->get_quarter_turns() == rotation) {
			std::map<int64_t, ArPlane *> native_planes;
			for (const auto &plane : planes) native_planes.emplace(plane.second.id, plane.first);
			extras.on_repeated_frame(frame, native_planes);
		}
		return;
	}
	if (last_timestamp && timestamp < last_timestamp) increment("timestamp_regressions");
	last_timestamp = timestamp;
	increment("new_frame_count");
	diagnostics["clock_boottime_offset_ns"] = timestamp - clock_ns(CLOCK_BOOTTIME);
	diagnostics["clock_monotonic_offset_ns"] = timestamp - clock_ns(CLOCK_MONOTONIC);
	diagnostics["clock_godot_offset_ns"] = timestamp - int64_t(tick_usec()) * 1000;
	build_packet(timestamp);
}

Transform3D ARCoreInterface::read_pose() const {
	return read_arcore_pose(api, session, scratch_pose);
}

void ARCoreInterface::inspect_projection(const ArCamera *p_camera, const float *p_projection) {
	float second[16], oracle[16];
	api.ArCamera_getProjectionMatrix(session, p_camera, 0.2f, 20.0f, second);
	api.ArCamera_getProjectionMatrix(session, p_camera, 0.13f, 57.0f, oracle);
	bool valid = p_projection[11] == -1 && p_projection[15] == 0;
	valid &= p_projection[0] > 0 && p_projection[5] > 0 && std::isfinite(p_projection[5] / p_projection[0]);
	for (int index : {0, 5, 8, 9}) valid &= p_projection[index] == second[index];
	for (int index : {1, 2, 3, 4, 6, 7, 12, 13}) valid &= std::abs(p_projection[index]) < 1e-6;
	for (int i = 0; i < 16; ++i) {
		double value = p_projection[i];
		if (i == 10) value = -(57.0 + 0.13) / (57.0 - 0.13);
		if (i == 14) value = -2 * 57.0 * 0.13 / (57.0 - 0.13);
		valid &= std::isfinite(value) && std::abs(value - oracle[i]) <= 1e-5;
	}
	diagnostics["projection_form_valid"] = bool(diagnostics["projection_form_valid"]) && valid;
	increment("projection_form_samples");
}

void ARCoreInterface::inspect_intrinsics(const ArCamera *p_camera, ARFramePacket &r_packet) {
	float focal[2], principal[2];
	int32_t dimensions[2];
	api.ArCamera_getImageIntrinsics(session, p_camera, camera_intrinsics);
	api.ArCameraIntrinsics_getFocalLength(session, camera_intrinsics, focal, focal + 1);
	api.ArCameraIntrinsics_getPrincipalPoint(session, camera_intrinsics, principal, principal + 1);
	api.ArCameraIntrinsics_getImageDimensions(session, camera_intrinsics, dimensions, dimensions + 1);
	r_packet.intrinsics = {focal[0], focal[1], principal[0], principal[1], Vector2(dimensions[0], dimensions[1])};
}

void ARCoreInterface::build_packet(int64_t p_timestamp) {
	if (gpu_validation_pending) increment("gpu_validation_skipped_submissions");
	else if (gpu_texture.is_valid() && !submit_gpu_camera()) return;
	ArCamera *camera = nullptr;
	api.ArFrame_acquireCamera(session, frame, &camera);
	if (!camera) { fail(AR_ERROR_FATAL, "acquire camera"); return; }
	ArTrackingState tracking;
	ArTrackingFailureReason failure;
	api.ArCamera_getTrackingState(session, camera, &tracking);
	api.ArCamera_getTrackingFailureReason(session, camera, &failure);
	set_reason(PocketAR::TrackingReason(failure));
	tracking_status = tracking == AR_TRACKING_STATE_TRACKING ? XR_NORMAL_TRACKING :
		tracking == AR_TRACKING_STATE_PAUSED && failure == AR_TRACKING_FAILURE_REASON_NONE ? XR_UNKNOWN_TRACKING :
		tracking == AR_TRACKING_STATE_PAUSED && failure == AR_TRACKING_FAILURE_REASON_EXCESSIVE_MOTION ? XR_EXCESSIVE_MOTION :
		tracking == AR_TRACKING_STATE_PAUSED && failure == AR_TRACKING_FAILURE_REASON_INSUFFICIENT_FEATURES ? XR_INSUFFICIENT_FEATURES : XR_NOT_TRACKING;
	set_state(tracking == AR_TRACKING_STATE_TRACKING ? PocketAR::TRACKING : PocketAR::INITIALIZING);
	if (tracking == AR_TRACKING_STATE_TRACKING && !first_tracking_tick) {
		first_tracking_tick = tick_usec();
		diagnostics["first_tracking_usec"] = int64_t(first_tracking_tick);
		if (reset_tick) timings["reset_to_tracking"] = (first_tracking_tick - reset_tick) / 1000.0;
	}
	Ref<ARFramePacket> packet;
	packet.instantiate();
	packet->session_generation = generation;
	packet->frame_id = ++sequence;
	packet->timestamp_ns = p_timestamp;
	packet->timestamp_usec = p_timestamp / 1000;
	packet->tracking_valid = tracking == AR_TRACKING_STATE_TRACKING;
	api.ArCamera_getDisplayOrientedPose(session, camera, scratch_pose);
	if (packet->tracking_valid) last_good_pose = read_pose();
	packet->camera_transform = last_good_pose;
	api.ArCamera_getPose(session, camera, scratch_pose);
	packet->physical_camera_transform = read_pose();
	float matrix[16];
	api.ArCamera_getProjectionMatrix(session, camera, 0.05f, 100.0f, matrix);
	for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) packet->projection[c][r] = matrix[c * 4 + r];
	inspect_projection(camera, matrix);
	inspect_intrinsics(camera, **packet);
	api.ArCamera_release(camera);
	const float corners[] = {0,0, 1,0, 0,1, 1,1};
	float mapped[8];
	api.ArFrame_transformCoordinates2d(session, frame, AR_COORDINATES_2D_VIEW_NORMALIZED, 4, corners, AR_COORDINATES_2D_IMAGE_NORMALIZED, mapped);
	const Transform2D cpu_display_transform(Vector2(mapped[2] - mapped[0], mapped[3] - mapped[1]), Vector2(mapped[4] - mapped[0], mapped[5] - mapped[1]), Vector2(mapped[0], mapped[1]));
	if (gpu_active) api.ArFrame_transformCoordinates2d(session, frame, AR_COORDINATES_2D_VIEW_NORMALIZED, 4, corners, AR_COORDINATES_2D_TEXTURE_NORMALIZED, mapped);
	const Vector2 origin(mapped[0], mapped[1]);
	const Vector2 x = Vector2(mapped[2], mapped[3]) - origin, y = Vector2(mapped[4], mapped[5]) - origin;
	bool affine = (origin + x + y).distance_to(Vector2(mapped[6], mapped[7])) <= 1e-4;
	bool inside = true;
	for (float value : mapped) inside &= std::isfinite(value) && value >= -1e-5 && value <= 1.00001;
	diagnostics["display_transform_valid"] = bool(diagnostics["display_transform_valid"]) && affine && inside;
	increment("display_transform_samples");
	packet->display_transform = Transform2D(x, y, origin);
	packet->quarter_turns = rotation;
	packet->viewport_size = viewport_size;
	int32_t width = 0, height = 0;
	uint64_t repack_usec = 0, stats_usec = 0;
	if (!gpu_active) {
		ArImage *image = nullptr;
		const uint64_t acquire_start = tick_usec();
		const ArStatus status = api.ArFrame_acquireCameraImage(session, frame, &image);
		sample_timing(acquire_times, acquire_cursor, double(tick_usec() - acquire_start));
		if (status != AR_SUCCESS) {
			if (status == AR_ERROR_NOT_YET_AVAILABLE) increment("image_not_yet_available");
			else if (status == AR_ERROR_RESOURCE_EXHAUSTED) increment("image_resource_exhausted");
			else if (status == AR_ERROR_DEADLINE_EXCEEDED) { increment("image_deadline_exceeded"); fail(status, "acquireCameraImage latest-frame invariant"); }
			else fail(status, "acquireCameraImage");
			return;
		}
		increment("camera_image_count");
		int64_t image_timestamp = 0;
		api.ArImage_getTimestamp(session, image, &image_timestamp);
		if (image_timestamp != p_timestamp) increment("image_timestamp_mismatches");
		const int64_t timestamp_delta = image_timestamp - p_timestamp;
		diagnostics["image_timestamp_delta_min_ns"] = std::min(int64_t(diagnostics.get("image_timestamp_delta_min_ns", timestamp_delta)), timestamp_delta);
		diagnostics["image_timestamp_delta_max_ns"] = std::max(int64_t(diagnostics.get("image_timestamp_delta_max_ns", timestamp_delta)), timestamp_delta);
		const double previous_mean = diagnostics.get("image_timestamp_delta_mean_ns", 0.0);
		diagnostics["image_timestamp_delta_mean_ns"] = previous_mean + (double(timestamp_delta) - previous_mean) / double(diagnostics["camera_image_count"]);
		if (fps_range.y > 0 && std::abs(double(timestamp_delta)) >= 500000000.0 / fps_range.y) increment("image_timestamp_far_count");
		int32_t count;
		ArImageFormat format;
		api.ArImage_getWidth(session, image, &width); api.ArImage_getHeight(session, image, &height);
		api.ArImage_getNumberOfPlanes(session, image, &count); api.ArImage_getFormat(session, image, &format);
		if (format != AR_IMAGE_FORMAT_YUV_420_888 || count != 3 || width <= 0 || height <= 0 || width > 8192 || height > 8192) {
			api.ArImage_release(image);
			fail(AR_ERROR_FATAL, "camera image format/dimensions");
			return;
		}
		const uint64_t copy_start = tick_usec();
		pocketar::ImagePlane image_planes[3];
		Array strides;
		for (int i = 0; i < 3; ++i) {
			int32_t length = 0;
			api.ArImage_getPlaneData(session, image, i, &image_planes[i].data, &length);
			image_planes[i].length = std::max(0, length);
			api.ArImage_getPlaneRowStride(session, image, i, &image_planes[i].row_stride);
			api.ArImage_getPlanePixelStride(session, image, i, &image_planes[i].pixel_stride);
			Dictionary plane;
			plane["row_stride"] = image_planes[i].row_stride; plane["pixel_stride"] = image_planes[i].pixel_stride; plane["data_length"] = length;
			strides.append(plane);
		}
		if (staging_size != Vector2i(width, height)) {
			luma.resize(width * height);
			chroma.resize(((width + 1) / 2) * ((height + 1) / 2) * 2);
			staging_size = Vector2i(width, height);
		}
		pocketar::YUVLayout layout;
		const bool repacked = pocketar::repack_yuv(width, height, image_planes, luma.ptrw(), chroma.ptrw(), layout);
		api.ArImage_release(image);
		if (!repacked) { fail(AR_ERROR_FATAL, "camera image plane bounds"); return; }
		repack_usec = tick_usec() - copy_start;
		const uint64_t stats_start = tick_usec();
		const String layout_name = layout == pocketar::YUVLayout::NV12 ? "NV12" : layout == pocketar::YUVLayout::NV21 ? "NV21" : "generic";
		if (!diagnostics.has("image_plane_layout") || diagnostics["image_plane_layout"] != Variant(strides)) {
			for (int i = 0; i < strides.size(); ++i) UtilityFunctions::print("ARCORE plane ", i, " ", strides[i], " layout=", layout_name);
		}
		diagnostics["image_plane_layout"] = strides;
		diagnostics["image_layout"] = layout_name;
		uint64_t sum = 0;
		const uint8_t *pixels = luma.ptr();
		for (int y = 0; y < height; y += 4) {
			const uint8_t *row = pixels + size_t(y) * width;
			for (int x = 0; x < width; x += 4) sum += row[x];
		}
		diagnostics["mean_y"] = double(sum) / (((width + 3) / 4) * ((height + 3) / 4));
		stats_usec = tick_usec() - stats_start;
	}
	const bool changed_planes = copy_planes(**packet);
	const Transform3D camera_from_world = packet->physical_camera_transform.affine_inverse();
	const Transform3D display_from_world = packet->camera_transform.affine_inverse();
	const Transform2D image_to_view = cpu_display_transform.affine_inverse();
	auto point_reprojection_error = [&](const Vector3 &p_world, double &r_error) {
		if (!packet->tracking_valid) return false;
		const Vector3 physical = camera_from_world.xform(p_world);
		const Vector3 oriented = display_from_world.xform(p_world);
		if (physical.z >= -0.05 || oriented.z >= -0.05) return false;
		const ARIntrinsics &k = packet->intrinsics;
		const Vector2 image_uv((k.fx * physical.x / -physical.z + k.cx) / k.image_size.x,
			(k.cy - k.fy * physical.y / -physical.z) / k.image_size.y);
		const Vector4 clip = packet->projection.xform(Vector4(oriented.x, oriented.y, oriented.z, 1));
		const Vector2 projected((clip.x / clip.w + 1) * 0.5, (1 - clip.y / clip.w) * 0.5);
		r_error = ((image_to_view.xform(image_uv) - projected) * Vector2(viewport_size)).length();
		return std::isfinite(r_error);
	};
	double plane_reprojection_error = 0;
	int plane_reprojection_samples = 0;
	for (const auto &plane : packet->planes) {
		if (!packet->tracking_valid || plane.tracking_state != AR_TRACKING_STATE_TRACKING || plane.subsumed_by) continue;
		for (int i = 0; i < plane.polygon.size(); ++i) {
			const Vector2 vertex = plane.polygon[i];
			const Vector3 world = plane.transform.xform(Vector3(vertex.x, 0, vertex.y));
			double error = 0;
			if (!point_reprojection_error(world, error)) continue;
			plane_reprojection_error = std::max(plane_reprojection_error, error);
			++plane_reprojection_samples;
		}
	}
	if (plane_reprojection_samples) {
		diagnostics["plane_reprojection_max_px"] = std::max(double(diagnostics.get("plane_reprojection_max_px", 0.0)), plane_reprojection_error);
		increment("plane_reprojection_samples", plane_reprojection_samples);
	}
	std::map<int64_t, ArPlane *> native_planes;
	for (const auto &plane : planes) native_planes.emplace(plane.second.id, plane.first);
	extras.on_frame(frame, packet->frame_id, packet, native_planes);
	double cloud_reprojection_error = 0;
	int cloud_reprojection_samples = 0;
	const PackedVector3Array points = extras.get_point_cloud().get("positions", PackedVector3Array());
	for (int64_t i = 0; i < points.size(); i++) {
		double error = 0;
		if (!point_reprojection_error(points[i], error)) continue;
		cloud_reprojection_error = std::max(cloud_reprojection_error, error);
		cloud_reprojection_samples++;
	}
	if (cloud_reprojection_samples) {
		diagnostics["point_cloud_reprojection_max_px"] = std::max(double(diagnostics.get("point_cloud_reprojection_max_px", 0.0)), cloud_reprojection_error);
		increment("point_cloud_reprojection_samples", cloud_reprojection_samples);
	}
	const int reprojection_samples = plane_reprojection_samples + cloud_reprojection_samples;
	if (reprojection_samples) {
		diagnostics["projection_reprojection_max_px"] = std::max(double(diagnostics.get("projection_reprojection_max_px", 0.0)), std::max(plane_reprojection_error, cloud_reprojection_error));
		increment("projection_reprojection_samples", reprojection_samples);
		Dictionary rotations = diagnostics.get("projection_reprojection_rotation_samples", Dictionary());
		const String key = String::num_int64(rotation);
		rotations[key] = int64_t(rotations.get(key, 0)) + reprojection_samples;
		diagnostics["projection_reprojection_rotation_samples"] = rotations;
	}
	if (gpu_active) {
		packet->rgb_texture = gpu_texture;
		packet->image_size = gpu_size;
		increment("textures_updated");
	} else if (TextureSlot *slot = find_texture_slot()) {
		const uint64_t upload_start = tick_usec();
		Ref<Image> y_image = Image::create_from_data(width, height, false, Image::FORMAT_R8, luma);
		Ref<Image> uv_image = Image::create_from_data((width + 1) / 2, (height + 1) / 2, false, Image::FORMAT_RG8, chroma);
		if (slot->y.is_null() || slot->y->get_width() != width || slot->y->get_height() != height) {
			slot->y = ImageTexture::create_from_image(y_image);
			slot->uv = ImageTexture::create_from_image(uv_image);
		} else {
			slot->y->update(y_image);
			slot->uv->update(uv_image);
		}
		// Safe rendering uploads synchronously; drop Image aliases before the next staging write.
		y_image.unref();
		uv_image.unref();
		const uint64_t upload_usec = tick_usec() - upload_start;
		packet->image_size = Vector2i(width, height);
		packet->y_texture = slot->y;
		packet->cbcr_texture = slot->uv;
		slot->last_use = rd_swaps;
		increment("textures_updated");
		sample_timing(repack_times, repack_cursor, double(repack_usec));
		sample_timing(stats_times, stats_cursor, double(stats_usec));
		sample_timing(upload_times, upload_cursor, double(upload_usec));
		sample_timing(repack_upload_times, repack_upload_cursor, double(repack_usec + stats_usec + upload_usec));
	} else {
		increment("texture_refusals");
		if (current_packet.is_valid()) {
			packet->y_texture = current_packet->y_texture;
			packet->cbcr_texture = current_packet->cbcr_texture;
			packet->image_size = current_packet->image_size;
		}
	}
	current_packet = packet;
	head->set_pose("default", last_good_pose, Vector3(), Vector3(), packet->tracking_valid ? XRPose::XR_TRACKING_CONFIDENCE_HIGH : XRPose::XR_TRACKING_CONFIDENCE_NONE);
	increment("packets_published");
	if (changed_planes) call_deferred("emit_signal", "planes_changed");
}

int64_t ARCoreInterface::ensure_plane(ArPlane *p_plane, bool p_acquired) {
	auto found = planes.find(p_plane);
	if (found != planes.end()) {
		if (p_acquired) api.ArTrackable_release(reinterpret_cast<ArTrackable *>(p_plane));
		return found->second.id;
	}
	PlaneEntry entry;
	entry.id = ++plane_sequence;
	entry.tracker.instantiate();
	ARPlaneSnapshot snapshot;
	snapshot.session_generation = generation;
	snapshot.plane_id = entry.id;
	snapshot.tracking_state = AR_TRACKING_STATE_PAUSED;
	entry.tracker->update(snapshot);
	XRServer::get_singleton()->add_tracker(entry.tracker);
	planes.emplace(p_plane, entry);
	return entry.id;
}

bool ARCoreInterface::copy_planes(ARFramePacket &r_packet) {
	api.ArSession_getAllTrackables(session, AR_TRACKABLE_PLANE, trackable_list);
	int32_t count = 0;
	api.ArTrackableList_getSize(session, trackable_list, &count);
	for (int i = 0; i < count; ++i) {
		ArTrackable *trackable = nullptr;
		api.ArTrackableList_acquireItem(session, trackable_list, i, &trackable);
		ArTrackingState state;
		api.ArTrackable_getTrackingState(session, trackable, &state);
		if (state == AR_TRACKING_STATE_STOPPED && planes.find(reinterpret_cast<ArPlane *>(trackable)) == planes.end()) {
			api.ArTrackable_release(trackable);
		} else ensure_plane(reinterpret_cast<ArPlane *>(trackable), true);
	}
	for (auto it = planes.begin(); it != planes.end();) {
		ArPlane *plane = it->first;
		ArTrackingState tracking;
		api.ArTrackable_getTrackingState(session, reinterpret_cast<ArTrackable *>(plane), &tracking);
		if (tracking == AR_TRACKING_STATE_STOPPED) {
			XRServer::get_singleton()->remove_tracker(it->second.tracker);
			api.ArTrackable_release(reinterpret_cast<ArTrackable *>(plane));
			it = planes.erase(it);
			continue;
		}
		ARPlaneSnapshot snapshot;
		snapshot.session_generation = generation; snapshot.plane_id = it->second.id; snapshot.tracking_state = tracking;
		ArPlaneType type;
		api.ArPlane_getType(session, plane, &type); snapshot.alignment = type;
		api.ArPlane_getCenterPose(session, plane, scratch_pose); snapshot.transform = read_pose();
		float extent_x, extent_z;
		api.ArPlane_getExtentX(session, plane, &extent_x); api.ArPlane_getExtentZ(session, plane, &extent_z);
		snapshot.extents = Vector2(extent_x, extent_z);
		int32_t polygon_size = 0;
		api.ArPlane_getPolygonSize(session, plane, &polygon_size);
		if (polygon_size >= 6 && polygon_size % 2 == 0) {
			std::vector<float> polygon(polygon_size);
			api.ArPlane_getPolygon(session, plane, polygon.data());
			snapshot.polygon.resize(polygon_size / 2);
			for (int v = 0; v < polygon_size / 2; ++v) snapshot.polygon[v] = Vector2(polygon[2*v], polygon[2*v+1]);
		}
		ArPlane *parent = nullptr;
		api.ArPlane_acquireSubsumedBy(session, plane, &parent);
		if (parent) snapshot.subsumed_by = ensure_plane(parent, true);
		it->second.tracker->update(snapshot);
		r_packet.planes.push_back(snapshot);
		++it;
	}
	return current_packet.is_valid() ? r_packet.planes != current_packet->planes : !r_packet.planes.empty();
}
}
