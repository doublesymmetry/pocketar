#include "core/camera_quality.h"
#include <cassert>
#include <iostream>

int main() {
	using namespace pocketar;
	const std::vector<CameraFormat> android = {
		{ 1920, 1440, 1920, 1440, 30 }, { 640, 480, 1920, 1440, 30 },
		{ 1280, 960, 1280, 960, 30 }, { 640, 480, 640, 480, 30 },
		{ 3840, 2160, 640, 480, 30 }
	};
	assert(select_android_format(android, HIGHEST, false) == 0);
	assert(select_android_format(android, BALANCED, false) == 2);
	assert(select_android_format(android, LOWEST, false) == 3);
	assert(select_android_format(android, HIGHEST, true) == 1);
	assert(select_android_format(android, BALANCED, true) == 2);
	assert(select_android_format(android, LOWEST, true) == 3);
	assert(select_android_format({}, HIGHEST, false) == -1);
	assert(select_android_format({ android[4] }, HIGHEST, false) == -1);
	const std::vector<CameraFormat> ios = {
		{ 1920, 1440, 0, 0, 60 }, { 3840, 2160, 0, 0, 30 },
		{ 1920, 1440, 0, 0, 30 }, { 1280, 720, 0, 0, 60 },
		{ 1280, 720, 0, 0, 30 }
	};
	assert(select_ios_format(ios, HIGHEST, 0) == 0);
	assert(select_ios_format(ios, HIGHEST, 30) == 1);
	assert(select_ios_format(ios, HIGHEST, 60) == 0);
	assert(select_ios_format(ios, BALANCED, 0) == 0);
	assert(select_ios_format(ios, BALANCED, 30) == 2);
	assert(select_ios_format(ios, LOWEST, 0) == 3);
	assert(select_ios_format(ios, LOWEST, 30) == 4);
	assert(select_ios_format({ ios[1] }, HIGHEST, 60) == 0);
	assert(select_ios_format({}, LOWEST, 30) == -1);
	assert(select_ios_format({ ios[0], ios[1] }, HIGHEST, 45) == 1);
	std::cout << "camera quality PASS\n";
}
