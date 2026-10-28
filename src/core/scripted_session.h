#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pocketar {

using FixtureTransform = std::array<double, 16>;

struct FixturePlane {
	int64_t id = 0;
	int alignment = 0;
	FixtureTransform transform{};
	std::array<double, 2> extents{};
	std::vector<std::array<double, 2>> polygon;
	int tracking_state = 0;
	int64_t subsumed_by = 0;
};

struct FixtureAnchorUpdate {
	int64_t slot = 0;
	int tracking_state = 0;
};

struct FixtureRecord {
	enum Kind { FRAME, RESET, PAUSE, RESUME, INPUT };
	Kind kind = FRAME;
	int64_t t_usec = 0;
	int64_t frame_id = 0;
	FixtureTransform camera_transform{};
	bool tracking_valid = false;
	int session_state = 6;
	int tracking_reason = 0;
	bool has_map_transform = false;
	FixtureTransform map_transform{};
	std::vector<FixtureAnchorUpdate> anchor_updates;
	bool has_planes = false;
	std::vector<FixturePlane> planes;
	std::string input_kind;
	int finger = 0;
	std::array<double, 2> uv{};
	int64_t target_frame = 0;
};

struct ScriptedSession {
	int version = 1;
	std::string semantics;
	std::array<int, 2> image_size{};
	std::array<double, 4> intrinsics{};
	std::vector<FixtureRecord> records;
};

bool parse_scripted_session(const std::string &p_jsonl, ScriptedSession &r_session, std::string &r_error);

}
