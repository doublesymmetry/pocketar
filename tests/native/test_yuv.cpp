#include "platform/android/yuv_repack.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace handheld_ar;

static void test_layouts(int p_width, int p_height, int p_padding) {
	const int cw = (p_width + 1) / 2, ch = (p_height + 1) / 2;
	const int y_stride = p_width + p_padding, uv_stride = cw * 2 + p_padding;
	std::vector<uint8_t> y(size_t(p_height - 1) * y_stride + p_width, 99);
	std::vector<uint8_t> expected_y(p_width * p_height), expected_uv(cw * ch * 2);
	std::vector<uint8_t> u(cw * ch), v(cw * ch);
	std::vector<uint8_t> nv12(size_t(ch - 1) * uv_stride + cw * 2, 99), nv21(nv12.size(), 99);
	for (int row = 0; row < p_height; ++row) {
		for (int x = 0; x < p_width; ++x) {
			y[row * y_stride + x] = expected_y[row * p_width + x] = uint8_t(row * 7 + x);
		}
	}
	for (int row = 0; row < ch; ++row) {
		for (int x = 0; x < cw; ++x) {
			const int i = row * cw + x, offset = row * uv_stride + x * 2;
			u[i] = uint8_t(i * 3); v[i] = uint8_t(i * 5 + 17);
			expected_uv[i * 2] = nv12[offset] = nv21[offset + 1] = u[i];
			expected_uv[i * 2 + 1] = nv12[offset + 1] = nv21[offset] = v[i];
		}
	}
	std::vector<uint8_t> out_y(expected_y.size()), out_uv(expected_uv.size());
	ImagePlane planes[3] = {{y.data(), y.size(), y_stride, 1}, {}, {}};
	for (const auto expected_layout : {YUVLayout::NV12, YUVLayout::NV21, YUVLayout::GENERIC}) {
		if (expected_layout == YUVLayout::GENERIC) {
			planes[1] = {u.data(), u.size(), cw, 1};
			planes[2] = {v.data(), v.size(), cw, 1};
		} else {
			auto &interleaved = expected_layout == YUVLayout::NV12 ? nv12 : nv21;
			const int first = expected_layout == YUVLayout::NV12 ? 1 : 2, second = 3 - first;
			planes[first] = {interleaved.data(), interleaved.size() - 1, uv_stride, 2};
			planes[second] = {interleaved.data() + 1, interleaved.size() - 1, uv_stride, 2};
			if (p_width == 1920 && p_height == 1080 && p_padding == 0) {
				assert(planes[1].length == 1036799 && planes[2].length == 1036799);
				assert(planes[1].row_stride == 1920);
			}
		}
		YUVLayout layout;
		assert(repack_yuv(p_width, p_height, planes, out_y.data(), out_uv.data(), layout));
		assert(layout == expected_layout && out_y == expected_y && out_uv == expected_uv);
		for (int plane = 1; plane <= 2; ++plane) {
			--planes[plane].length;
			assert(!repack_yuv(p_width, p_height, planes, out_y.data(), out_uv.data(), layout));
			++planes[plane].length;
		}
	}
}

int main() {
	test_layouts(1920, 1080, 0);
	for (int width : {1, 3, 4, 7}) {
		for (int height : {1, 2, 3, 5}) {
			for (int padding : {0, 4}) test_layouts(width, height, padding);
		}
	}
	uint8_t y[] = {1,2,3,4,99,99,5,6,7,8};
	uint8_t uv[] = {20,30,21,31};
	uint8_t out_y[8], out_uv[4];
	ImagePlane planes[3] = {{y,10,6,1},{uv,3,4,2},{uv+1,3,4,2}};
	YUVLayout layout;
	assert(repack_yuv(4,2,planes,out_y,out_uv,layout));
	assert(layout == YUVLayout::NV12 && out_y[4] == 5 && out_y[7] == 8);
	assert(out_uv[0] == 20 && out_uv[1] == 30 && out_uv[2] == 21 && out_uv[3] == 31);
	planes[1] = {uv+1,3,4,2}; planes[2] = {uv,3,4,2};
	assert(repack_yuv(4,2,planes,out_y,out_uv,layout));
	assert(layout == YUVLayout::NV21 && out_uv[0] == 30 && out_uv[1] == 20 && out_uv[3] == 21);
	uint8_t u[] = {11,12}, v[] = {41,42};
	planes[1] = {u,2,2,1}; planes[2] = {v,2,2,1};
	assert(repack_yuv(4,2,planes,out_y,out_uv,layout));
	assert(layout == YUVLayout::GENERIC && out_uv[0] == 11 && out_uv[3] == 42);
	planes[1].length = 1;
	assert(!repack_yuv(4,2,planes,out_y,out_uv,layout));
	for (uint32_t q = 2; q <= 4; ++q) {
		for (uint64_t tick = 10; tick < 10 + q; ++tick) assert(!texture_slot_retired(tick,10,q,false));
		assert(texture_slot_retired(10+q,10,q,false));
		assert(!texture_slot_retired(100,10,q,true));
	}
	puts("YUV layouts, short chroma rows and Q=2..4 texture retirement PASS");
}
