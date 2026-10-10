#pragma once

#include <algorithm>
#include <cstdint>

namespace handheld_ar {

struct ImportRetirement {
	bool freed = false;
	uint64_t free_swap = 0;
	uint64_t release_swap = 0;

	void retire(uint64_t p_swap, uint32_t p_delay) {
		freed = true;
		free_swap = p_swap;
		release_swap = p_swap + p_delay;
	}
	bool can_sample() const { return !freed; }
	bool release_ready(uint64_t p_swap) const { return freed && p_swap >= release_swap; }
	void defer_after_stall(uint64_t p_swap, uint32_t p_delay) {
		if (freed) release_swap = std::max(release_swap, p_swap + p_delay);
	}
};

}
