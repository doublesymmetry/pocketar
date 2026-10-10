#include "synthetic_ar_interface.h"
#include "ar_ids.h"

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/xr_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <algorithm>
#include <cmath>

namespace godot {

void SyntheticARInterface::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_intrinsics", "fx", "fy", "cx", "cy", "image_size"), &SyntheticARInterface::set_intrinsics);
	ClassDB::bind_method(D_METHOD("set_pose", "pose"), &SyntheticARInterface::set_pose);
	ClassDB::bind_method(D_METHOD("set_camera_rate", "fps"), &SyntheticARInterface::set_camera_rate);
	ClassDB::bind_method(D_METHOD("set_display_geometry", "viewport_size", "quarter_turns", "near", "far"), &SyntheticARInterface::set_display_geometry);
	ClassDB::bind_method(D_METHOD("set_frame_id_pose", "enabled"), &SyntheticARInterface::set_frame_id_pose);
	ClassDB::bind_method(D_METHOD("get_current_packet"), &SyntheticARInterface::get_current_packet);
	ClassDB::bind_method(D_METHOD("get_presented_packet"), &SyntheticARInterface::get_presented_packet);
	ClassDB::bind_method(D_METHOD("mark_presented"), &SyntheticARInterface::mark_presented);
}

bool SyntheticARInterface::_initialize() {
	if (initialized) {
		return true;
	}
	ERR_FAIL_COND_V_MSG(OS::get_singleton()->get_thread_caller_id() != OS::get_singleton()->get_main_thread_id() || !RenderingServer::get_singleton()->is_on_render_thread(), false, "The synchronous synthetic backend requires Safe rendering thread mode on the main thread.");
	ERR_FAIL_COND_V_MSG(XRServer::get_singleton()->get_primary_interface().is_valid(), false, "Only one ARView may own the primary XR interface.");
	head.instantiate();
	head->set_tracker_name("head");
	head->set_tracker_type(XRServer::TRACKER_HEAD);
	head->set_tracker_desc("Synthetic handheld camera");
	XRServer::get_singleton()->add_tracker(head);
	initialized = true;
	generation = next_ar_id();
	sequence = 0;
	set_primary(true);
	make_packet(Time::get_singleton()->get_ticks_usec());
	return true;
}

void SyntheticARInterface::_uninitialize() {
	if (!initialized) {
		return;
	}
	set_primary(false);
	XRServer::get_singleton()->remove_tracker(head);
	head.unref();
	current_packet.unref();
	presented_packet.unref();
	initialized = false;
}

void SyntheticARInterface::set_intrinsics(double p_fx, double p_fy, double p_cx, double p_cy, const Vector2i &p_image_size) {
	ERR_FAIL_COND(!std::isfinite(p_fx) || !std::isfinite(p_fy) || !std::isfinite(p_cx) || !std::isfinite(p_cy));
	ERR_FAIL_COND(p_fx <= 0 || p_fy <= 0 || p_image_size.x < 32 || p_image_size.y < 32 || p_image_size.x > 4096 || p_image_size.y > 4096);
	intrinsics = { p_fx, p_fy, p_cx, p_cy, Vector2(p_image_size) };
	geometry_dirty = true;
}

void SyntheticARInterface::set_pose(const Transform3D &p_pose) {
	ERR_FAIL_COND(!p_pose.is_finite());
	pose = p_pose;
}

void SyntheticARInterface::set_camera_rate(double p_fps) {
	ERR_FAIL_COND(!std::isfinite(p_fps) || p_fps <= 0 || p_fps > 240);
	camera_rate = p_fps;
}

void SyntheticARInterface::set_display_geometry(const Vector2i &p_viewport_size, int p_quarter_turns, double p_near, double p_far) {
	ERR_FAIL_COND(p_viewport_size.x <= 0 || p_viewport_size.y <= 0 || !std::isfinite(p_near) || !std::isfinite(p_far) || p_near <= 0 || p_far <= p_near);
	const int turns = ((p_quarter_turns % 4) + 4) % 4;
	if (viewport_size == Vector2(p_viewport_size) && quarter_turns == turns && near_plane == p_near && far_plane == p_far) {
		return;
	}
	viewport_size = p_viewport_size;
	quarter_turns = turns;
	near_plane = p_near;
	far_plane = p_far;
	geometry_dirty = true;
}

void SyntheticARInterface::set_frame_id_pose(bool p_enabled) {
	frame_id_pose = p_enabled;
}

void SyntheticARInterface::_process() {
	if (!initialized) {
		return;
	}
	const uint64_t tick = Time::get_singleton()->get_ticks_usec();
	if (geometry_dirty || tick - last_tick >= uint64_t(1000000.0 / camera_rate)) {
		make_packet(tick);
	}
}

void SyntheticARInterface::make_packet(uint64_t p_tick) {
	last_tick = p_tick;
	geometry_dirty = false;
	sequence++;
	const int width = int(intrinsics.image_size.x);
	const int height = int(intrinsics.image_size.y);
	PackedByteArray y_data;
	PackedByteArray cbcr_data;
	y_data.resize(width * height);
	cbcr_data.resize(width * height * 2);
	uint8_t *y_bytes = y_data.ptrw();
	uint8_t *chroma = cbcr_data.ptrw();
	const Color bars[] = { Color(1, 0, 0), Color(0, 0, 1), Color(0, 1, 0), Color(1, 1, 0), Color(0, 1, 1), Color(1, 0, 1), Color(1, 1, 1), Color(0, 0, 0) };
	auto quantize = [](double p_value) { return uint8_t(std::clamp(std::lround(p_value), 0L, 255L)); };
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			Color rgb = bars[std::min(7, x * 8 / width)];
			if (y < 24) {
				const bool bit = (sequence >> std::min(15, x * 16 / width)) & 1;
				rgb = bit ? Color(1, 1, 1) : Color(0, 0, 0);
			} else if (x % 32 == 0 || y % 32 == 0) {
				rgb = Color(0.25, 0.25, 0.25);
			}
			const int i = y * width + x;
			y_bytes[i] = quantize(255 * (0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b));
			chroma[2 * i] = quantize(128 + 255 * (-0.168736 * rgb.r - 0.331264 * rgb.g + 0.5 * rgb.b));
			chroma[2 * i + 1] = quantize(128 + 255 * (0.5 * rgb.r - 0.418688 * rgb.g - 0.081312 * rgb.b));
		}
	}
	const int slot = sequence % 3;
	if (y_images[slot].is_null()) {
		y_images[slot].instantiate();
		cbcr_images[slot].instantiate();
	}
	y_images[slot]->set_data(width, height, false, Image::FORMAT_R8, y_data);
	cbcr_images[slot]->set_data(width, height, false, Image::FORMAT_RG8, cbcr_data);
	Ref<ARFramePacket> packet;
	packet.instantiate();
	packet->session_generation = generation;
	packet->frame_id = sequence;
	packet->timestamp_usec = p_tick;
	packet->timestamp_ns = p_tick * 1000;
	packet->intrinsics = intrinsics;
	packet->viewport_size = viewport_size;
	packet->quarter_turns = quarter_turns;
	packet->projection = projection_from_intrinsics(intrinsics, viewport_size, quarter_turns, near_plane, far_plane);
	packet->display_transform = display_transform_for_view(intrinsics.image_size, viewport_size, quarter_turns);
	packet->physical_camera_transform = pose;
	packet->camera_transform = display_oriented_pose(pose, quarter_turns);
	if (frame_id_pose) {
		const Vector2 pixel((double(sequence % 16) + 0.5) * viewport_size.x / 16.0, viewport_size.y * 0.5);
		const ARRay ray = ray_from_screen(packet->projection, display_oriented_pose(Transform3D(), quarter_turns), viewport_size, pixel);
		const Vector3 point = ray.direction * (-2.0 / ray.direction.z);
		packet->physical_camera_transform = Transform3D(Basis(), Vector3(0, 0, -2) - point);
		packet->camera_transform = display_oriented_pose(packet->physical_camera_transform, quarter_turns);
	}
	packet->image_size = Vector2i(width, height);
	packet->gpu_image_size = packet->image_size;
	packet->camera_fps = Vector2i(int(std::lround(camera_rate)), int(std::lround(camera_rate)));
	// Reuse CPU storage only; packet textures are immutable and retire with Godot's RID fences.
	packet->y_texture = ImageTexture::create_from_image(y_images[slot]);
	packet->cbcr_texture = ImageTexture::create_from_image(cbcr_images[slot]);
	packet->tracking_valid = true;
	current_packet = packet;
	head->set_pose("default", packet->camera_transform, Vector3(), Vector3(), XRPose::XR_TRACKING_CONFIDENCE_HIGH);
}

Vector2 SyntheticARInterface::_get_render_target_size() {
	return current_packet.is_valid() ? current_packet->viewport_size : viewport_size;
}

Transform3D SyntheticARInterface::_get_camera_transform() {
	return current_packet.is_valid() ? current_packet->camera_transform : Transform3D();
}

Transform3D SyntheticARInterface::_get_transform_for_view(uint32_t p_view, const Transform3D &p_cam_transform) {
	return p_cam_transform * _get_camera_transform();
}

PackedFloat64Array SyntheticARInterface::_get_projection_for_view(uint32_t p_view, double p_aspect, double p_z_near, double p_z_far) {
	PackedFloat64Array values;
	values.resize(16);
	const ARIntrinsics &calibration = current_packet.is_valid() ? current_packet->intrinsics : intrinsics;
	const Vector2 size = current_packet.is_valid() ? current_packet->viewport_size : viewport_size;
	const int turns = current_packet.is_valid() ? current_packet->quarter_turns : quarter_turns;
	const Projection matrix = projection_from_intrinsics(calibration, Vector2(size.y * p_aspect, size.y), turns, p_z_near, p_z_far);
	for (int col = 0; col < 4; col++) {
		for (int row = 0; row < 4; row++) {
			values[col * 4 + row] = matrix[col][row];
		}
	}
	return values;
}

void SyntheticARInterface::mark_presented() {
	presented_packet = current_packet;
}

}
