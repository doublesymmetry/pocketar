#include "arcore_interface.h"
#include "gpu_camera_validation.h"
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/rect2i.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <android/hardware_buffer.h>
#include <limits>

namespace godot {

void ARCoreInterface::detect_gpu_camera() {
	gpu_texture.unref();
	gpu_active = false;
	gpu_buffer_misses = 0;
	gpu_validation_pending = false;
	gpu_validation_worst = 0;
	gpu_submit_times.clear(); gpu_submit_cursor = 0;
	frame_setup_times.clear(); frame_setup_cursor = 0;
	frame_setup_swap = rd_swaps;
	for (const char *key : {"gpu_submissions", "gpu_buffer_missing", "gpu_submit_errors", "gpu_completions", "gpu_completion_failures", "gpu_validation_samples", "gpu_validation_failures", "gpu_validation_image_missing", "gpu_validation_skipped_submissions"}) diagnostics[key] = int64_t(0);
	for (const char *key : {"gpu_validation_worst_error_255", "gpu_validation_luma_range_255", "gpu_validation_chroma_magnitude_255"}) diagnostics.erase(key);
	diagnostics["camera_path"] = "cpu";
	RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
	if (camera_path == "cpu") {
		diagnostics["camera_path_reason"] = "forced CPU";
	} else if (!ClassDB::class_has_method("ExternalTexture", "submit_hardware_buffer")) {
		diagnostics["camera_path_reason"] = "ExternalTexture.submit_hardware_buffer unavailable";
	} else if (!ClassDB::class_has_integer_constant("RenderingDevice", "SUPPORTS_ANDROID_HARDWARE_BUFFER") || !rd ||
		!rd->has_feature(RenderingDevice::Features(ClassDB::class_get_integer_constant("RenderingDevice", "SUPPORTS_ANDROID_HARDWARE_BUFFER")))) {
		diagnostics["camera_path_reason"] = "RenderingDevice lacks SUPPORTS_ANDROID_HARDWARE_BUFFER";
	} else {
		gpu_color_model = ClassDB::class_get_integer_constant("ExternalTexture", "COLOR_MODEL_BT601");
		gpu_color_range = ClassDB::class_get_integer_constant("ExternalTexture", "COLOR_RANGE_FULL");
		gpu_texture = ClassDB::instantiate("ExternalTexture");
		diagnostics["camera_path_reason"] = gpu_texture.is_valid() ? "awaiting first hardware buffer completion" : "ExternalTexture instantiation failed";
	}
}

void ARCoreInterface::fallback_gpu_camera(const String &p_reason) {
	diagnostics["camera_path"] = "cpu";
	diagnostics["camera_path_reason"] = p_reason;
	UtilityFunctions::print("ARCORE camera_path=cpu reason=", p_reason);
	gpu_active = false;
	gpu_texture.unref();
	reset();
}

void ARCoreInterface::gpu_camera_completion(int p_status, uint64_t p_interface, int64_t p_generation) {
	ARCoreInterface *interface = Object::cast_to<ARCoreInterface>(ObjectDB::get_instance(p_interface));
	if (interface) interface->gpu_camera_completed(p_status, p_generation);
}

void ARCoreInterface::gpu_camera_completed(int p_status, int64_t p_generation) {
	if (!initialized || p_generation != generation || gpu_texture.is_null()) return;
	ERR_FAIL_COND(!on_owner_thread());
	if (p_status == ClassDB::class_get_integer_constant("ExternalTexture", "COMPLETION_FAILED")) {
		increment("gpu_completion_failures");
		if (!gpu_active) fallback_gpu_camera("submit_hardware_buffer completion FAILED before GPU activation");
	} else if (p_status == ClassDB::class_get_integer_constant("ExternalTexture", "COMPLETION_COMPLETED")) {
		increment("gpu_completions");
		if (!gpu_active) {
			gpu_active = true;
			diagnostics["camera_path"] = "gpu";
			diagnostics["camera_path_reason"] = "ExternalTexture hardware buffer conversion completed";
			UtilityFunctions::print("ARCORE camera_path=gpu color_model=", gpu_color_model, " color_range=", gpu_color_range);
		}
	}
}

void ARCoreInterface::set_camera_path(const String &p_path) {
	ERR_FAIL_COND(p_path != "auto" && p_path != "cpu");
	ERR_FAIL_COND(initialized && !on_owner_thread());
	if (camera_path == p_path) return;
	camera_path = p_path;
	if (initialized) {
		detect_gpu_camera();
		reset();
	}
}

void ARCoreInterface::set_gpu_validation_enabled(bool p_enabled) {
	ERR_FAIL_COND(initialized && !on_owner_thread());
	gpu_validate = p_enabled;
	if (!p_enabled) {
		gpu_validation_pending = false;
	}
}

bool ARCoreInterface::submit_gpu_camera() {
	void *buffer = nullptr;
	const ArStatus status = api.ArFrame_getHardwareBuffer(session, frame, &buffer);
	if (status != AR_SUCCESS || !buffer) {
		increment("gpu_buffer_missing");
		++gpu_buffer_misses;
		if (!gpu_active && (status != AR_ERROR_NOT_YET_AVAILABLE || gpu_buffer_misses >= 30)) {
			fallback_gpu_camera(String("getHardwareBuffer status=") + String::num_int64(status) + " consecutive_misses=" + String::num_int64(gpu_buffer_misses));
		}
		return false;
	}
	gpu_buffer_misses = 0;
	AHardwareBuffer_Desc desc{};
	AHardwareBuffer_describe(static_cast<AHardwareBuffer *>(buffer), &desc);
	const bool rgb = desc.format == AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM || desc.format == AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM ||
		desc.format == AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM || desc.format == AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM ||
		desc.format == AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT || desc.format == AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM ||
		desc.format == AHARDWAREBUFFER_FORMAT_R10G10B10A10_UNORM;
	gpu_color_model = ClassDB::class_get_integer_constant("ExternalTexture", rgb ? "COLOR_MODEL_RGB" : "COLOR_MODEL_BT601");
	Rect2i crop;
	if (desc.width != uint32_t(gpu_size.x) || desc.height != uint32_t(gpu_size.y)) {
		if (gpu_size.x <= 0 || gpu_size.y <= 0 || desc.width < uint32_t(gpu_size.x) || desc.height < uint32_t(gpu_size.y)) {
			fallback_gpu_camera("hardware buffer smaller than ARCore texture dimensions");
			return false;
		}
		crop = Rect2i(Vector2i(), gpu_size);
	}
	const uint64_t start = tick_usec();
	// Completions can outlive this interface, and godot-cpp's callable_mp dereferences its object to check validity.
	const Callable completion = callable_mp_static(&ARCoreInterface::gpu_camera_completion).bind(get_instance_id(), generation);
	const Variant result = gpu_texture->call("submit_hardware_buffer", int64_t(reinterpret_cast<uintptr_t>(buffer)), gpu_color_model, gpu_color_range, crop, completion);
	sample_timing(gpu_submit_times, gpu_submit_cursor, double(tick_usec() - start));
	const int error = result.get_type() == Variant::INT ? int(result) : int(ERR_INVALID_DATA);
	if (error != OK) {
		increment("gpu_submit_errors");
		fallback_gpu_camera(error == ERR_UNAVAILABLE ? String("submit_hardware_buffer returned ERR_UNAVAILABLE") : String("submit_hardware_buffer error=") + String::num_int64(error));
		return false;
	}
	increment("gpu_submissions");
	if (gpu_active && gpu_validate && int64_t(diagnostics["gpu_validation_samples"]) < 3 && int64_t(diagnostics["gpu_submissions"]) % 60 == 1) {
		gpu_validation_size = gpu_size;
		capture_gpu_validation(last_timestamp);
	}
	return gpu_active;
}

void ARCoreInterface::capture_gpu_validation(int64_t p_timestamp) {
	ArImage *image = nullptr;
	const ArStatus status = api.ArFrame_acquireCameraImage(session, frame, &image);
	if (status != AR_SUCCESS || !image) {
		increment(status == AR_ERROR_NOT_YET_AVAILABLE ? "gpu_validation_image_missing" : "gpu_validation_failures");
		return;
	}
	int32_t width = 0, height = 0, count = 0;
	int64_t timestamp = 0;
	ArImageFormat format;
	api.ArImage_getWidth(session, image, &width);
	api.ArImage_getHeight(session, image, &height);
	api.ArImage_getNumberOfPlanes(session, image, &count);
	api.ArImage_getFormat(session, image, &format);
	api.ArImage_getTimestamp(session, image, &timestamp);
	bool valid = format == AR_IMAGE_FORMAT_YUV_420_888 && count == 3 && width > 0 && height > 0 && width <= 8192 && height <= 8192;
	valid &= fps_range.y > 0 && std::abs(double(timestamp - p_timestamp)) < 500000000.0 / fps_range.y;
	pocketar::ImagePlane planes[3];
	if (valid) {
		for (int i = 0; i < 3; ++i) {
			int32_t length = 0;
			api.ArImage_getPlaneData(session, image, i, &planes[i].data, &length);
			planes[i].length = std::max(0, length);
			api.ArImage_getPlaneRowStride(session, image, i, &planes[i].row_stride);
			api.ArImage_getPlanePixelStride(session, image, i, &planes[i].pixel_stride);
		}
		float texture_uv[pocketar::GPU_VALIDATION_PIXELS * 2], image_uv[pocketar::GPU_VALIDATION_PIXELS * 2];
		std::fill(std::begin(image_uv), std::end(image_uv), std::numeric_limits<float>::quiet_NaN());
		valid = pocketar::gpu_validation_coordinates(gpu_validation_size.x, gpu_validation_size.y, texture_uv);
		if (valid) {
			api.ArFrame_transformCoordinates2d(session, frame, AR_COORDINATES_2D_TEXTURE_NORMALIZED, pocketar::GPU_VALIDATION_PIXELS, texture_uv, AR_COORDINATES_2D_IMAGE_NORMALIZED, image_uv);
			valid = pocketar::cpu_camera_block_means(width, height, planes, image_uv, gpu_cpu_means, gpu_validation_luma_range, gpu_validation_chroma_magnitude);
		}
	}
	api.ArImage_release(image);
	if (!valid) {
		increment("gpu_validation_failures");
		return;
	}
	gpu_validation_pending = true;
	gpu_validation_swap = rd_swaps;
}

void ARCoreInterface::finish_gpu_validation() {
	gpu_validation_pending = false;
	const Ref<Image> image = gpu_texture->get_image();
	if (image.is_null() || image->is_empty() || image->get_size() != gpu_validation_size) {
		increment("gpu_validation_failures");
		return;
	}
	std::array<pocketar::CameraRGB, 9> means{};
	for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col) {
		const int x = pocketar::gpu_validation_block_start(image->get_width(), col);
		const int y = pocketar::gpu_validation_block_start(image->get_height(), row);
		for (int dy = 0; dy < 16; ++dy) for (int dx = 0; dx < 16; ++dx) {
			const Color color = image->get_pixel(x + dx, y + dy);
			for (int channel = 0; channel < 3; ++channel) means[row * 3 + col][channel] += color[channel] * 255.0 / 256;
		}
	}
	increment("gpu_validation_samples");
	for (int block = 0; block < 9; ++block) {
		for (int channel = 0; channel < 3; ++channel) {
			const double error = std::abs(means[block][channel] - gpu_cpu_means[block][channel]);
			gpu_validation_worst = std::max(gpu_validation_worst, error);
		}
	}
	diagnostics["gpu_validation_worst_error_255"] = gpu_validation_worst;
	diagnostics["gpu_validation_luma_range_255"] = std::min(double(diagnostics.get("gpu_validation_luma_range_255", gpu_validation_luma_range)), gpu_validation_luma_range);
	diagnostics["gpu_validation_chroma_magnitude_255"] = std::min(double(diagnostics.get("gpu_validation_chroma_magnitude_255", gpu_validation_chroma_magnitude)), gpu_validation_chroma_magnitude);
}

}
