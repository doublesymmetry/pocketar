#pragma once

#include <algorithm>
#include <vector>

namespace godot {
inline void sample_timing(std::vector<double> &r_values, size_t &r_cursor, double p_value) {
	if (r_values.size() < 512) r_values.push_back(p_value);
	else { r_values[r_cursor] = p_value; r_cursor = (r_cursor + 1) % r_values.size(); }
}

inline double timing_percentile(std::vector<double> p_values, double p_fraction) {
	if (p_values.empty()) return 0;
	const size_t index = size_t((p_values.size() - 1) * p_fraction);
	std::nth_element(p_values.begin(), p_values.begin() + index, p_values.end());
	return p_values[index];
}
}
