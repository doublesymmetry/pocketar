#pragma once

#include <array>
#include <cstdint>
#include <map>

namespace pocketar {

template <class NativeAnchor, class Pose>
class ARKitAnchors {
public:
	using Key = std::array<uint8_t, 16>;
	enum { TRACKING, PAUSED, STOPPED };

private:
	struct Entry {
		int64_t id;
		NativeAnchor native;
		Pose pose;
		bool seen = false, present = false, stopped = false;
	};
	std::map<Key, Entry> entries;

public:
	template <class Add>
	void add(int64_t p_id, const Key &p_key, NativeAnchor p_native, const Pose &p_pose, Add p_add) {
		entries.emplace(p_key, Entry{ p_id, p_native, p_pose });
		p_add(p_native);
	}

	template <class Remove>
	void remove(int64_t p_id, Remove p_remove) {
		for (auto found = entries.begin(); found != entries.end(); ++found) {
			if (found->second.id != p_id) continue;
			NativeAnchor native = found->second.native;
			entries.erase(found);
			p_remove(native);
			return;
		}
	}

	int64_t changed(const Key &p_key, bool p_removed) {
		auto found = entries.find(p_key);
		if (found == entries.end() || !p_removed) return 0;
		found->second.stopped = true;
		return found->second.id;
	}

	template <class Frame, class Emit>
	void sample(bool p_tracking, Frame p_frame, Emit p_emit) {
		if (entries.empty()) return;
		for (auto &entry : entries) entry.second.present = false;
		p_frame([&](const Key &p_key, auto p_pose) {
			auto found = entries.find(p_key);
			if (found == entries.end() || found->second.stopped) return;
			Entry &entry = found->second;
			entry.seen = entry.present = true;
			if (p_tracking) entry.pose = p_pose();
		});
		for (auto &entry : entries) {
			Entry &anchor = entry.second;
			anchor.stopped = anchor.stopped || (anchor.seen && !anchor.present);
			p_emit(anchor.id, anchor.pose, anchor.stopped ? STOPPED : p_tracking && anchor.present ? TRACKING : PAUSED);
		}
	}

	bool empty() const { return entries.empty(); }
};

}
