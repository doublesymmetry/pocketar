#include "platform/android/yuv_repack.h"
#include <cassert>
#include <cstdio>
using namespace handheld_ar;
int main() {
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
