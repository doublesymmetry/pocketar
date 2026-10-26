#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace pocketar {

enum CameraQuality { HIGHEST, BALANCED, LOWEST };

struct CameraFormat {
	int width, height, texture_width, texture_height, fps;
};

inline int64_t quality_area(std::vector<int64_t> p_areas, CameraQuality p_quality) {
	std::sort(p_areas.begin(), p_areas.end());
	p_areas.erase(std::unique(p_areas.begin(), p_areas.end()), p_areas.end());
	return p_areas[p_quality == HIGHEST ? p_areas.size() - 1 : p_quality == BALANCED ? (p_areas.size() - 1) / 2 : 0];
}

inline int select_android_format(const std::vector<CameraFormat> &p_formats, CameraQuality p_quality, bool p_gpu) {
	std::vector<int64_t> areas;
	for (const auto &format : p_formats) {
		if (p_gpu || int64_t(format.width) * format.texture_height == int64_t(format.texture_width) * format.height) {
			areas.push_back(p_gpu ? int64_t(format.texture_width) * format.texture_height : int64_t(format.width) * format.height);
		}
	}
	if (areas.empty()) return -1;
	const int64_t area = quality_area(areas, p_quality);
	int selected = -1;
	int64_t secondary_area = INT64_MAX;
	for (size_t i = 0; i < p_formats.size(); ++i) {
		const auto &format = p_formats[i];
		if (!p_gpu && int64_t(format.width) * format.texture_height != int64_t(format.texture_width) * format.height) continue;
		const int64_t cpu = int64_t(format.width) * format.height, gpu = int64_t(format.texture_width) * format.texture_height;
		if ((p_gpu ? gpu : cpu) == area && (p_gpu ? cpu : gpu) < secondary_area) {
			selected = int(i);
			secondary_area = p_gpu ? cpu : gpu;
		}
	}
	return selected;
}

inline int select_ios_format(const std::vector<CameraFormat> &p_formats, CameraQuality p_quality, int p_fps) {
	if (p_formats.empty()) return -1;
	int fps = 0;
	if (p_fps) {
		fps = p_formats[0].fps;
		for (const auto &format : p_formats) {
			if (std::abs(format.fps - p_fps) < std::abs(fps - p_fps) ||
				(std::abs(format.fps - p_fps) == std::abs(fps - p_fps) && format.fps < fps)) fps = format.fps;
		}
	}
	std::vector<int64_t> areas;
	for (const auto &format : p_formats) if (!fps || format.fps == fps) areas.push_back(int64_t(format.width) * format.height);
	const int64_t area = quality_area(areas, p_quality);
	for (size_t i = 0; i < p_formats.size(); ++i) {
		const auto &format = p_formats[i];
		if ((!fps || format.fps == fps) && (p_quality == HIGHEST || int64_t(format.width) * format.height == area)) return int(i);
	}
	return -1;
}

}
