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
	handheld_ar::ImportRetirement active;
	active.set_active(true, false, false);
	check(!active.retirement_ready(), "current image stays imported");
	active.set_active(false, true, false);
	check(!active.retirement_ready(), "presented image survives current replacement");
	active.set_active(false, false, true);
	check(!active.retirement_ready(), "applied image survives presentation reset");
	active.set_active(false, false, false);
	check(active.retirement_ready(), "import retires once all renderer owners move on");
	active.retire(7, 2);
	check(!active.retirement_ready(), "retired import cannot be freed twice");

	handheld_ar::ImportRetainCanary missing_drop;
	missing_drop.record_free(5);
	check(!missing_drop.observe(5, 32), "unchanged retain count cannot certify release");
	check(!missing_drop.observe(6, 33), "an unrelated retain cannot certify release");
	check(!missing_drop.drop_observed, "missing engine release remains detectable at s+Q");
	check(missing_drop.observe(4, 34), "first lower retain count records an engine release");
	check(missing_drop.drop_swap == 34, "canary records the observed swap");
	check(!missing_drop.observe(3, 35), "later drops cannot replace the first observation");
	check(missing_drop.drop_swap == 34, "first drop timing remains stable");
	handheld_ar::ImportRetainCanary no_import;
	check(!no_import.observe(0, 34), "a plane that was never imported has no release evidence");

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
	std::puts("RETIREMENT PASS Q=2,3,4, renderer ownership and observed retain drops");
}
