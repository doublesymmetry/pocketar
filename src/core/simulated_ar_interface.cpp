#include "simulated_ar_interface.h"

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/xr_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <algorithm>
#include <cmath>

namespace godot {

static void fill_synthetic_camera_image(int p_width, int p_height, int64_t p_sequence, Ref<Image> &r_y_image, Ref<Image> &r_cbcr_image) {
	PackedByteArray y_data;
	PackedByteArray cbcr_data;
	y_data.resize(p_width * p_height);
	cbcr_data.resize(p_width * p_height * 2);
	uint8_t *y_bytes = y_data.ptrw();
	uint8_t *chroma = cbcr_data.ptrw();
	const Color bars[] = { Color(1, 0, 0), Color(0, 0, 1), Color(0, 1, 0), Color(1, 1, 0), Color(0, 1, 1), Color(1, 0, 1), Color(1, 1, 1), Color(0, 0, 0) };
	auto quantize = [](double p_value) { return uint8_t(std::clamp(std::lround(p_value), 0L, 255L)); };
	for (int y = 0; y < p_height; y++) {
		for (int x = 0; x < p_width; x++) {
			Color rgb = bars[std::min(7, x * 8 / p_width)];
			if (y < 24) {
				const bool bit = (p_sequence >> std::min(15, x * 16 / p_width)) & 1;
				rgb = bit ? Color(1, 1, 1) : Color(0, 0, 0);
			} else if (x % 32 == 0 || y % 32 == 0) {
				rgb = Color(0.25, 0.25, 0.25);
			}
			const int i = y * p_width + x;
			y_bytes[i] = quantize(255 * (0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b));
			chroma[2 * i] = quantize(128 + 255 * (-0.168736 * rgb.r - 0.331264 * rgb.g + 0.5 * rgb.b));
			chroma[2 * i + 1] = quantize(128 + 255 * (0.5 * rgb.r - 0.418688 * rgb.g - 0.081312 * rgb.b));
		}
	}
	if (r_y_image.is_null()) {
		r_y_image.instantiate();
		r_cbcr_image.instantiate();
	}
	r_y_image->set_data(p_width, p_height, false, Image::FORMAT_R8, y_data);
	r_cbcr_image->set_data(p_width, p_height, false, Image::FORMAT_RG8, cbcr_data);
}

void SimulatedARInterface::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_session_generation"), &SimulatedARInterface::get_session_generation);
	ClassDB::bind_method(D_METHOD("set_display_geometry", "viewport_size", "quarter_turns", "near", "far"), &SimulatedARInterface::set_display_geometry);
	ClassDB::bind_method(D_METHOD("get_current_packet"), &SimulatedARInterface::get_current_packet);
	ClassDB::bind_method(D_METHOD("get_presented_packet"), &SimulatedARInterface::get_presented_packet);
	ClassDB::bind_method(D_METHOD("mark_presented"), &SimulatedARInterface::mark_presented);
	ADD_SIGNAL(MethodInfo("session_state_changed", PropertyInfo(Variant::INT, "state")));
	ADD_SIGNAL(MethodInfo("tracking_reason_changed", PropertyInfo(Variant::INT, "reason")));
	ADD_SIGNAL(MethodInfo("session_generation_changed", PropertyInfo(Variant::INT, "generation")));
	ADD_SIGNAL(MethodInfo("planes_changed"));
}

bool SimulatedARInterface::start(const String &p_description) {
	ERR_FAIL_COND_V_MSG(OS::get_singleton()->get_thread_caller_id() != OS::get_singleton()->get_main_thread_id() || !RenderingServer::get_singleton()->is_on_render_thread(), false, "The simulated backend requires Safe rendering thread mode on the main thread.");
	ERR_FAIL_COND_V_MSG(XRServer::get_singleton()->get_primary_interface().is_valid(), false, "Only one ARView may own the primary XR interface.");
	head.instantiate();
	head->set_tracker_name("head");
	head->set_tracker_type(XRServer::TRACKER_HEAD);
	head->set_tracker_desc(p_description);
	XRServer::get_singleton()->add_tracker(head);
	initialized = true;
	set_primary(true);
	return true;
}

void SimulatedARInterface::stop() {
	set_primary(false);
	XRServer::get_singleton()->remove_tracker(head);
	head.unref();
	current_packet.unref();
	presented_packet.unref();
	initialized = false;
}

void SimulatedARInterface::fill_packet(const Ref<ARFramePacket> &p_packet, const Transform3D &p_pose) {
	p_packet->intrinsics = intrinsics;
	p_packet->viewport_size = viewport_size;
	p_packet->quarter_turns = quarter_turns;
	p_packet->projection = projection_from_intrinsics(intrinsics, viewport_size, quarter_turns, near_plane, far_plane);
	p_packet->display_transform = display_transform_for_view(intrinsics.image_size, viewport_size, quarter_turns);
	p_packet->physical_camera_transform = p_pose;
	p_packet->camera_transform = display_oriented_pose(p_pose, quarter_turns);
	p_packet->image_size = Vector2i(intrinsics.image_size);
	fill_synthetic_camera_image(p_packet->image_size.x, p_packet->image_size.y, p_packet->frame_id, y_image, cbcr_image);
	p_packet->y_texture = ImageTexture::create_from_image(y_image);
	p_packet->cbcr_texture = ImageTexture::create_from_image(cbcr_image);
	geometry_dirty = false;
}

void SimulatedARInterface::set_display_geometry(const Vector2i &p_viewport_size, int p_quarter_turns, double p_near, double p_far) {
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

Vector2 SimulatedARInterface::_get_render_target_size() {
	return current_packet.is_valid() ? current_packet->viewport_size : viewport_size;
}

Transform3D SimulatedARInterface::_get_camera_transform() {
	return current_packet.is_valid() ? current_packet->camera_transform : Transform3D();
}

Transform3D SimulatedARInterface::_get_transform_for_view(uint32_t p_view, const Transform3D &p_cam_transform) {
	return p_cam_transform * _get_camera_transform();
}

PackedFloat64Array SimulatedARInterface::_get_projection_for_view(uint32_t p_view, double p_aspect, double p_z_near, double p_z_far) {
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

void SimulatedARInterface::mark_presented() {
	presented_packet = current_packet;
}

}
