#include "platform/ios/import_retirement.h"
#include <cstdio>
#include <cstdlib>

static void check(bool p_value, const char *p_name) {
	if (!p_value) {
		std::fprintf(stderr, "RETIREMENT FAIL %s\n", p_name);
		std::exit(1);
	}
}

int main() {
	for (unsigned q : { 2u, 3u, 4u }) {
		handheld_ar::ImportRetirement lifetime;
		check(lifetime.can_sample(), "live import can be sampled");
		lifetime.retire(31, q);
		check(!lifetime.can_sample(), "free invalidates sampling immediately");
		for (uint64_t swap = 31; swap < 31 + q; ++swap) {
			check(!lifetime.release_ready(swap), "native owners survive every pre-fence swap");
		}
		check(lifetime.release_ready(31 + q), "native owners retire at s+Q");
		lifetime.defer_after_stall(31 + q, q);
		check(!lifetime.release_ready(31 + q), "stall extends native ownership");
		check(!lifetime.release_ready(31 + 2 * q - 1), "stall waits another full cycle");
		check(lifetime.release_ready(31 + 2 * q), "stall cycle eventually drains");
	}
	std::puts("RETIREMENT PASS Q=2,3,4 and stall/canary boundaries");
}
