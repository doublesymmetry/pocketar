#include "arcore_interface.h"
#include "gpu_camera_validation.h"
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <android/hardware_buffer.h>
#include <limits>

namespace godot {

void ARCoreInterface::detect_gpu_camera() {
	gpu_texture.unref();
	gpu_active = false;
	diagnostics["camera_path"] = "cpu";
	diagnostics["gpu_color_override_reason"] = "default BT.601 full (camera JFIF); device validation pending";
	if (camera_path == "cpu") {
		diagnostics["camera_path_reason"] = "forced CPU";
	} else if (!ClassDB::class_has_method("ExternalTexture", "submit_hardware_buffer")) {
		diagnostics["camera_path_reason"] = "ExternalTexture.submit_hardware_buffer unavailable";
	} else {
		gpu_texture = ClassDB::instantiate("ExternalTexture");
		diagnostics["camera_path_reason"] = gpu_texture.is_valid() ? "awaiting first hardware buffer submission" : "ExternalTexture instantiation failed";
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
	if (!p_enabled) gpu_validation_pending = false;
}

Dictionary ARCoreInterface::begin_image_capacity_probe() {
	if (!gpu_active || gpu_validate) return extras.begin_image_capacity_probe();
	Dictionary result;
	result["ok"] = false;
	result["status"] = "CPU image probes on the GPU path require GPU validation";
	return result;
}

bool ARCoreInterface::submit_gpu_camera() {
	void *buffer = nullptr;
	const ArStatus status = api.ArFrame_getHardwareBuffer(session, frame, &buffer);
	if (status != AR_SUCCESS || !buffer) {
		increment("gpu_buffer_missing");
		if (status != AR_ERROR_NOT_YET_AVAILABLE) fail(status == AR_SUCCESS ? AR_ERROR_FATAL : status, "getHardwareBuffer");
		return false;
	}
	const uint64_t start = Time::get_singleton()->get_ticks_usec();
	// The dynamic call takes the engine's buffer reference before the next ArSession_update.
	const Variant result = gpu_texture->call("submit_hardware_buffer", int64_t(reinterpret_cast<uintptr_t>(buffer)), gpu_color_model, gpu_color_range);
	sample(gpu_submit_times, gpu_submit_cursor, double(Time::get_singleton()->get_ticks_usec() - start));
	const int error = result.get_type() == Variant::INT ? int(result) : int(ERR_INVALID_DATA);
	if (error != OK) {
		increment("gpu_submit_errors");
		diagnostics["camera_path"] = "cpu";
		diagnostics["camera_path_reason"] = error == ERR_UNAVAILABLE ? String("submit_hardware_buffer returned ERR_UNAVAILABLE") : String("submit_hardware_buffer error=") + String::num_int64(error);
		UtilityFunctions::print("ARCORE camera_path=cpu reason=", diagnostics["camera_path_reason"]);
		gpu_active = false;
		gpu_texture.unref();
		reset();
		return false;
	}
	if (!gpu_active) {
		gpu_active = true;
		diagnostics["camera_path"] = "gpu";
		diagnostics["camera_path_reason"] = "ExternalTexture hardware buffer submission accepted";
		UtilityFunctions::print("ARCORE camera_path=gpu color_model=", gpu_color_model, " color_range=", gpu_color_range);
	}
	increment("gpu_submissions");
	if (gpu_validate && int64_t(diagnostics["gpu_validation_samples"]) < 3 && int64_t(diagnostics["gpu_submissions"]) % 60 == 1) {
		AHardwareBuffer_Desc desc{};
		AHardwareBuffer_describe(static_cast<AHardwareBuffer *>(buffer), &desc);
		gpu_validation_size = Vector2i(desc.width, desc.height);
		capture_gpu_validation(last_timestamp);
	}
	return true;
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
	handheld_ar::ImagePlane planes[3];
	if (valid) {
		for (int i = 0; i < 3; ++i) {
			int32_t length = 0;
			api.ArImage_getPlaneData(session, image, i, &planes[i].data, &length);
			planes[i].length = std::max(0, length);
			api.ArImage_getPlaneRowStride(session, image, i, &planes[i].row_stride);
			api.ArImage_getPlanePixelStride(session, image, i, &planes[i].pixel_stride);
		}
		float texture_uv[handheld_ar::GPU_VALIDATION_PIXELS * 2], image_uv[handheld_ar::GPU_VALIDATION_PIXELS * 2];
		std::fill(std::begin(image_uv), std::end(image_uv), std::numeric_limits<float>::quiet_NaN());
		valid = handheld_ar::gpu_validation_coordinates(gpu_validation_size.x, gpu_validation_size.y, texture_uv);
		if (valid) {
			api.ArFrame_transformCoordinates2d(session, frame, AR_COORDINATES_2D_TEXTURE_NORMALIZED, handheld_ar::GPU_VALIDATION_PIXELS, texture_uv, AR_COORDINATES_2D_IMAGE_NORMALIZED, image_uv);
			valid = handheld_ar::cpu_camera_block_means(width, height, planes, image_uv, gpu_cpu_means);
		}
	}
	api.ArImage_release(image);
	if (!valid) {
		increment("gpu_validation_failures");
		return;
	}
	gpu_validation_pending = true;
	gpu_validation_swap = rd_swaps;
	gpu_validation_timestamp = p_timestamp;
	diagnostics["gpu_validation_cpu_timestamp_ns"] = timestamp;
	diagnostics["gpu_validation_timestamp_delta_ns"] = timestamp - p_timestamp;
}

void ARCoreInterface::finish_gpu_validation() {
	gpu_validation_pending = false;
	const Ref<Image> image = gpu_texture->get_image();
	if (image.is_null() || image->is_empty() || image->get_size() != gpu_validation_size) {
		increment("gpu_validation_failures");
		return;
	}
	std::array<handheld_ar::CameraRGB, 9> means{};
	for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col) {
		const int x = handheld_ar::gpu_validation_block_start(image->get_width(), col);
		const int y = handheld_ar::gpu_validation_block_start(image->get_height(), row);
		for (int dy = 0; dy < 16; ++dy) for (int dx = 0; dx < 16; ++dx) {
			const Color color = image->get_pixel(x + dx, y + dy);
			for (int channel = 0; channel < 3; ++channel) means[row * 3 + col][channel] += color[channel] * 255.0 / 256;
		}
	}
	increment("gpu_validation_samples");
	const int64_t samples = diagnostics["gpu_validation_samples"];
	const char *names[] = {"bt601_full", "bt601_narrow", "bt709_full", "bt709_narrow"};
	Dictionary candidates;
	int best = 0;
	for (int candidate = 0; candidate < 4; ++candidate) {
		Array block_errors;
		for (int block = 0; block < 9; ++block) {
			double worst = 0;
			for (int channel = 0; channel < 3; ++channel) {
				const double error = std::abs(means[block][channel] - gpu_cpu_means[candidate][block][channel]);
				worst = std::max(worst, error);
				gpu_validation_sum[candidate] += error;
			}
			block_errors.append(worst);
			gpu_validation_worst[candidate] = std::max(gpu_validation_worst[candidate], worst);
		}
		Dictionary result;
		result["worst_block_error_255"] = gpu_validation_worst[candidate];
		result["mean_error_255"] = gpu_validation_sum[candidate] / (samples * 27);
		result["last_block_errors_255"] = block_errors;
		candidates[names[candidate]] = result;
		if (gpu_validation_worst[candidate] < gpu_validation_worst[best]) best = candidate;
		UtilityFunctions::print("ARCORE gpu_validation candidate=", names[candidate], " samples=", samples, " worst_255=", result["worst_block_error_255"], " mean_255=", result["mean_error_255"]);
	}
	diagnostics["gpu_validation_candidates"] = candidates;
	diagnostics["gpu_validation_best"] = names[best];
	diagnostics["gpu_validation_worst_block_error_255"] = gpu_validation_worst[best];
	diagnostics["gpu_validation_frame_timestamp_ns"] = gpu_validation_timestamp;
	diagnostics["gpu_validation_readback_swaps"] = int64_t(rd_swaps - gpu_validation_swap);
	if (samples >= 3 && gpu_validation_worst[best] <= 6 && int64_t(diagnostics["gpu_validation_failures"]) == 0) {
		gpu_color_model = best < 2 ? 2 : 3;
		gpu_color_range = best % 2 ? 2 : 1;
		diagnostics["gpu_color_override_reason"] = String("validated ") + names[best];
		UtilityFunctions::print("ARCORE gpu_validation best=", names[best], " color_model=", gpu_color_model, " color_range=", gpu_color_range);
	}
}

}
