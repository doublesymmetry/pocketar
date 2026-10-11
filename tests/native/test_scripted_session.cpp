#include "core/scripted_session.h"
#include <cstdlib>
#include <fstream>
#include <iostream>

using namespace pocketar;

static const std::string header = R"({"format":"pocketar-session","version":1,"semantics":"arkit","image_size":[640,480],"intrinsics":{"fx":520,"fy":510,"cx":320,"cy":240}})";
static const std::string pose = "[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]";
static const std::string frame = "{\"t_usec\":0,\"frame_id\":1,\"camera_transform\":" + pose + ",\"tracking_valid\":true,\"session_state\":\"tracking\",\"tracking_reason\":\"none\"}";

static void require(bool p_value, const std::string &p_message) {
	if (!p_value) {
		std::cerr << "FAIL: " << p_message << '\n';
		std::exit(1);
	}
}

static void reject(const std::string &p_data, int p_line) {
	ScriptedSession session;
	session.semantics = "unchanged";
	std::string error;
	require(!parse_scripted_session(p_data, session, error), "malformed fixture accepted: " + p_data);
	require(error.find("line " + std::to_string(p_line) + ":") == 0, "missing line location: " + error);
	require(session.semantics == "unchanged", "failed load replaced the previous fixture");
}

int main(int p_argc, char **p_argv) {
	ScriptedSession session;
	std::string error;
	if (p_argc == 2) {
		std::ifstream file(p_argv[1]);
		std::string text((std::istreambuf_iterator<char>(file)), {});
		require(parse_scripted_session(text, session, error), error);
		std::cout << "Scripted fixture accepted: " << session.records.size() << " records\n";
		return 0;
	}
	require(parse_scripted_session(header + '\n' + frame + '\n', session, error), "valid fixture rejected: " + error);
	require(session.image_size[0] == 640 && session.intrinsics[1] == 510, "calibration lost");
	require(session.records[0].frame_id == 1 && session.records[0].camera_transform[15] == 1, "frame pose lost");
	require(!session.records[0].has_planes && session.records[0].session_state == 7 && session.records[0].tracking_reason == 0, "frame state lost");
	const std::string plane = "{\"id\":7,\"alignment\":\"horizontal_upward_facing\",\"transform\":" + pose + ",\"extents\":[2,2],\"polygon\":[[-1,-1],[1,-1],[1,1],[-1,1]],\"tracking_state\":\"tracking\",\"subsumed_by\":0}";
	std::string with_planes = frame;
	with_planes.insert(with_planes.size() - 1, ",\"planes\":[" + plane + "]");
	const std::string events = "\n{\"t_usec\":1,\"reset\":true}\n{\"t_usec\":2,\"lifecycle\":\"pause\"}\n{\"t_usec\":2,\"lifecycle\":\"resume\"}\n{\"t_usec\":2,\"kind\":\"down\",\"finger\":0,\"uv\":[0.5,0.7],\"target_frame\":1}";
	require(parse_scripted_session(header + '\n' + with_planes + events, session, error), error);
	require(session.records.size() == 5 && session.records[0].has_planes, "full plane replacement lost");
	require(session.records[0].planes[0].polygon[2][1] == 1 && session.records[0].planes[0].id == 7, "plane geometry lost");
	require(session.records[1].kind == FixtureRecord::RESET && session.records[2].kind == FixtureRecord::PAUSE && session.records[3].kind == FixtureRecord::RESUME, "event order changed");
	require(session.records[4].input_kind == "down" && session.records[4].target_frame == 1, "input association lost");
	reject("", 1);
	reject(header, 1);
	reject(header + '\n' + "{broken}", 2);
	reject(header + '\n' + "[]", 2);
	reject(header + '\n' + "{}", 2);
	reject(header + '\n' + frame + '\n' + "\n", 3);
	reject(header + '\n' + frame + "\n{\"t_usec\":1,\"reset\":true,\"lifecycle\":\"pause\"}", 3);
	reject(header + '\n' + frame + "\n{\"t_usec\":1,\"kind\":\"cancel\",\"finger\":0,\"uv\":[0,0],\"target_frame\":99}", 3);
	reject(header + '\n' + frame + "garbage", 2);
	reject(header + '\n' + "{\"t_usec\":0,\"reset\":false}", 2);
	reject(header + '\n' + "{\"t_usec\":0,\"lifecycle\":\"invalid\"}", 2);
	reject(header + '\n' + "{\"t_usec\":0,\"kind\":\"down\",\"finger\":0,\"uv\":[1.1,0],\"target_frame\":1}", 2);
	reject(header + '\n' + frame + "\n{\"t_usec\":-1,\"reset\":true}", 3);
	reject(header + "\n{\"t_usec\":2,\"reset\":true}\n" + frame, 3);
	reject(header + '\n' + frame + '\n' + frame, 3);
	for (const std::string &fractional_time : { "9007199254740990.5", "1.0000000000000001" }) {
		std::string invalid = frame;
		invalid.replace(invalid.find("\"t_usec\":0"), 10, "\"t_usec\":" + fractional_time);
		reject(header + '\n' + invalid, 2);
	}
	for (const auto &change : std::vector<std::pair<std::string, std::string>>{{"\"version\":1", "\"version\":2"}, {"\"arkit\"", "\"unknown\""}, {"520", "0"}, {"640", "640.5"}, {"520", "1e309"}, {"\"version\":1", "\"version\":1,\"version\":1"}}) {
		std::string invalid = header;
		invalid.replace(invalid.find(change.first), change.first.size(), change.second);
		reject(invalid + '\n' + frame, 1);
	}
	for (const auto &change : std::vector<std::pair<std::string, std::string>>{{"true", "1"}, {"\"tracking\"", "\"banana\""}, {"\"none\"", "\"banana\""}, {"\"frame_id\":1", "\"frame_id\":0"}, {pose, "[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0]"}, {pose, "[2,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]"}}) {
		std::string invalid = frame;
		invalid.replace(invalid.find(change.first), change.first.size(), change.second);
		reject(header + '\n' + invalid, 2);
	}
	for (const auto &change : std::vector<std::pair<std::string, std::string>>{{"horizontal_upward_facing", "diagonal"}, {"\"id\":7", "\"id\":0"}, {"\"subsumed_by\":0", "\"subsumed_by\":99"}, {"\"extents\":[2,2]", "\"extents\":[-2,2]"}, {"[[-1,-1],[1,-1],[1,1],[-1,1]]", "[[0,0],[1,1]]"}}) {
		std::string invalid = with_planes;
		invalid.replace(invalid.find(change.first), change.first.size(), change.second);
		reject(header + '\n' + invalid, 2);
	}
	std::string duplicates = frame;
	duplicates.insert(duplicates.size() - 1, ",\"planes\":[" + plane + "," + plane + "]");
	reject(header + '\n' + duplicates, 2);
	std::string empty_planes = frame;
	empty_planes.insert(empty_planes.size() - 1, ",\"planes\":[]");
	require(parse_scripted_session(header + '\n' + empty_planes, session, error), error);
	require(session.records[0].has_planes && session.records[0].planes.empty(), "empty plane replacement confused with omitted planes");
	for (const std::string &kind : { "down", "move", "up", "cancel" }) {
		const std::string input = "{\"t_usec\":1,\"kind\":\"" + kind + "\",\"finger\":2,\"uv\":[0,1],\"target_frame\":1}";
		require(parse_scripted_session(header + '\n' + frame + '\n' + input, session, error), error);
		require(session.records[1].input_kind == kind && session.records[1].finger == 2, "input phase or finger lost");
	}
	std::cout << "Scripted session parser tests passed\n";
}
