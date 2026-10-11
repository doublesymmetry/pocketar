#pragma once

#include <algorithm>
#include <cstdint>

namespace pocketar {

struct ImportRetirement {
	bool freed = false;
	bool active = false;
	uint64_t free_swap = 0;
	uint64_t release_swap = 0;

	void set_active(bool p_current, bool p_presented, bool p_applied) {
		active = p_current || p_presented || p_applied;
	}
	bool retirement_ready() const { return !freed && !active; }
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

struct ImportRetainCanary {
	bool armed = false;
	bool drop_observed = false;
	int64_t free_retain_count = 0;
	uint64_t drop_swap = 0;

	void record_free(int64_t p_retain_count) {
		armed = true;
		free_retain_count = p_retain_count;
	}
	bool observe(int64_t p_retain_count, uint64_t p_swap) {
		if (!armed || drop_observed || p_retain_count >= free_retain_count) return false;
		drop_observed = true;
		drop_swap = p_swap;
		return true;
	}
};

}
