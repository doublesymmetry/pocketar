#include "platform/ios/arkit_anchors.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using Store = pocketar::ARKitAnchors<int, int>;

static void check(bool p_value, const char *p_name) {
	if (!p_value) { std::fprintf(stderr, "ARKIT ANCHORS FAIL %s\n", p_name); std::exit(1); }
}

int main() {
	Store anchors;
	Store::Key first{};
	Store::Key second{};
	first[0] = 1;
	second[0] = 2;
	int add_count = 0, remove_count = 0, scans = 0;
	struct Sample { int64_t id; int pose; int state; };
	std::vector<Sample> samples;
	auto sample = [&](bool p_tracking, const std::vector<std::pair<Store::Key, int>> &p_frame) {
		samples.clear();
		anchors.sample(p_tracking, [&](auto p_observe) {
			++scans;
			for (const auto &entry : p_frame) p_observe(entry.first, [&] { return entry.second; });
		}, [&](int64_t p_id, int p_pose, int p_state) { samples.push_back({ p_id, p_pose, p_state }); });
	};
	auto add = [&](int p_native) { check(p_native == 11 || p_native == 22, "native add receives retained owner"); ++add_count; };
	auto remove = [&](int p_native) { check(p_native == 11 || p_native == 22, "native remove receives original owner"); ++remove_count; };
	sample(true, { { first, 30 } });
	check(scans == 0 && samples.empty(), "zero ownership skips frame enumeration");
	anchors.add(1, first, 11, 10, add);
	check(add_count == 1, "accepted creation submits native anchor once");
	anchors.changed(first, false);
	sample(true, {});
	check(samples.size() == 1 && samples[0].pose == 10 && samples[0].state == Store::PAUSED, "pending addition survives missing frame membership");
	check(anchors.changed(second, true) == 0, "unknown callback cannot create ownership");
	sample(true, { { first, 30 }, { second, 900 } });
	check(samples.size() == 1 && samples[0].id == 1 && samples[0].pose == 30 && samples[0].state == Store::TRACKING, "frame UUID supplies its exact pose and ignores unrelated anchors");
	sample(false, { { first, 40 } });
	check(samples[0].pose == 30 && samples[0].state == Store::PAUSED, "limited camera freezes anchor pose");
	sample(true, { { first, 50 } });
	check(samples[0].pose == 50 && samples[0].state == Store::TRACKING, "fresh normal frame recovers same anchor");
	sample(true, {});
	check(samples[0].state == Store::STOPPED, "previously seen UUID missing from frame is terminal");
	sample(true, { { first, 60 } });
	check(samples[0].state == Store::STOPPED && samples[0].pose == 50, "late frame cannot revive terminal membership");
	anchors.remove(1, [&](int p_native) {
		check(anchors.empty(), "ownership erased before native removal callback");
		check(anchors.changed(first, true) == 0, "synchronous native removal callback sees no owner");
		remove(p_native);
	});
	anchors.remove(1, remove);
	check(remove_count == 1, "explicit removal is idempotent");
	anchors.add(1, second, 22, 70, add);
	check(anchors.changed(first, true) == 0, "old generation UUID cannot remove reused numeric ID");
	anchors.changed(first, false);
	anchors.changed(second, false);
	sample(true, { { first, 80 } });
	check(samples.size() == 1 && samples[0].state == Store::PAUSED && samples[0].pose == 70, "delayed old add and update frames cannot restore old ownership");
	check(anchors.changed(second, true) == 1, "native removal identifies even a pending owner");
	sample(true, { { second, 90 } });
	check(samples[0].state == Store::STOPPED, "removed pending anchor cannot become tracking");
	anchors.remove(1, remove);
	check(add_count == 2 && remove_count == 2 && anchors.empty(), "all accepted native owners are removed once");
	std::puts("ARKIT ANCHORS PASS pending, frame poses, tracking recovery, terminal UUIDs and native ownership");
}
