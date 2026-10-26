#include "core/timing_samples.h"
#include <cassert>
#include <cstdio>

int main() {
	std::vector<double> values;
	size_t cursor = 0;
	assert(godot::timing_percentile(values, 0.5) == 0);
	for (int i = 0; i < 2048; ++i) godot::sample_timing(values, cursor, i);
	assert(values.size() == 512);
	const auto retained = values;
	assert(godot::timing_percentile(values, 0) == 1536);
	assert(godot::timing_percentile(values, 0.5) == 1791);
	assert(godot::timing_percentile(values, 0.95) == 2021);
	assert(godot::timing_percentile(values, 1) == 2047);
	assert(values == retained);
	godot::sample_timing(values, cursor, 2048);
	assert(godot::timing_percentile(values, 0) == 1537);
	std::puts("timing samples passed");
}
