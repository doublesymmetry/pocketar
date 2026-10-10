#pragma once

#include <atomic>
#include <cstdint>

namespace godot {

inline int64_t next_ar_id() {
	static std::atomic<int64_t> sequence{0};
	return sequence.fetch_add(1, std::memory_order_relaxed) + 1;
}

}
