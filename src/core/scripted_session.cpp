#include "scripted_session.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace pocketar {
namespace {

struct JsonValue {
	enum Type { NIL, BOOLEAN, NUMBER, STRING, ARRAY, OBJECT } type = NIL;
	bool boolean = false;
	double number = 0;
	std::string number_text;
	std::string string;
	std::vector<JsonValue> array;
	std::map<std::string, JsonValue> object;
};

class JsonReader {
	const std::string &text;
	size_t offset = 0;
	std::string &error;

	bool fail(const char *p_message) {
		error = p_message;
		return false;
	}

	void whitespace() {
		while (offset < text.size() && (text[offset] == ' ' || text[offset] == '\t' || text[offset] == '\r')) ++offset;
	}

	bool take(char p_character) {
		whitespace();
		if (offset == text.size() || text[offset] != p_character) return false;
		++offset;
		return true;
	}

	bool read_string(std::string &r_value) {
		if (!take('"')) return fail("expected a JSON string");
		while (offset < text.size()) {
			const unsigned char c = text[offset++];
			if (c == '"') return true;
			if (c < 32) return fail("control character in string");
			if (c == '\\') return fail("string escapes are not supported");
			r_value += char(c);
		}
		return fail("unterminated string");
	}

	bool read_value(JsonValue &r_value, int p_depth) {
		if (p_depth > 32) return fail("JSON nesting exceeds 32 levels");
		whitespace();
		if (offset == text.size()) return fail("expected a JSON value");
		const char c = text[offset];
		if (c == '{') {
			++offset;
			r_value.type = JsonValue::OBJECT;
			if (take('}')) return true;
			do {
				std::string key;
				JsonValue value;
				if (!read_string(key) || !take(':')) return fail("expected object key and colon");
				if (!read_value(value, p_depth + 1)) return false;
				if (!r_value.object.emplace(key, std::move(value)).second) return fail("duplicate object key");
				if (take('}')) return true;
			} while (take(','));
			return fail("expected comma or closing brace");
		}
		if (c == '[') {
			++offset;
			r_value.type = JsonValue::ARRAY;
			if (take(']')) return true;
			do {
				JsonValue value;
				if (!read_value(value, p_depth + 1)) return false;
				r_value.array.push_back(std::move(value));
				if (take(']')) return true;
			} while (take(','));
			return fail("expected comma or closing bracket");
		}
		if (c == '"') {
			r_value.type = JsonValue::STRING;
			return read_string(r_value.string);
		}
		for (const auto &literal : { std::string("true"), std::string("false"), std::string("null") }) {
			if (text.compare(offset, literal.size(), literal) == 0) {
				offset += literal.size();
				r_value.type = literal == "null" ? JsonValue::NIL : JsonValue::BOOLEAN;
				r_value.boolean = literal == "true";
				return true;
			}
		}
		const size_t start = offset;
		if (text[offset] == '-') ++offset;
		if (offset == text.size()) return fail("incomplete number");
		if (text[offset] == '0') ++offset;
		else {
			if (text[offset] < '1' || text[offset] > '9') return fail("invalid JSON value");
			while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9') ++offset;
		}
		if (offset < text.size() && text[offset] == '.') {
			++offset;
			const size_t digits = offset;
			while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9') ++offset;
			if (digits == offset) return fail("fraction requires digits");
		}
		if (offset < text.size() && (text[offset] == 'e' || text[offset] == 'E')) {
			++offset;
			if (offset < text.size() && (text[offset] == '+' || text[offset] == '-')) ++offset;
			const size_t digits = offset;
			while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9') ++offset;
			if (digits == offset) return fail("exponent requires digits");
		}
		r_value.type = JsonValue::NUMBER;
		r_value.number_text = text.substr(start, offset - start);
		r_value.number = std::strtod(r_value.number_text.c_str(), nullptr);
		return std::isfinite(r_value.number) || fail("number must be finite");
	}

public:
	JsonReader(const std::string &p_text, std::string &r_error) : text(p_text), error(r_error) {}
	bool read(JsonValue &r_value) {
		if (!read_value(r_value, 0)) return false;
		whitespace();
		return offset == text.size() || fail("trailing text after JSON value");
	}
};

const JsonValue &field(const JsonValue &p_object, const char *p_name) {
	static const JsonValue missing;
	const auto found = p_object.object.find(p_name);
	return found == p_object.object.end() ? missing : found->second;
}

bool integer(const JsonValue &p_value, int64_t p_minimum, int64_t p_maximum, int64_t &r_value) {
	if (p_value.type != JsonValue::NUMBER) return false;
	const char *end = p_value.number_text.data() + p_value.number_text.size();
	const auto parsed = std::from_chars(p_value.number_text.data(), end, r_value);
	return parsed.ec == std::errc() && parsed.ptr == end && r_value >= p_minimum && r_value <= p_maximum;
}

int named(const JsonValue &p_value, const std::vector<std::string> &p_names) {
	if (p_value.type != JsonValue::STRING) return -1;
	for (size_t i = 0; i < p_names.size(); ++i) if (p_names[i] == p_value.string) return int(i);
	return -1;
}

template <size_t N>
bool numbers(const JsonValue &p_value, std::array<double, N> &r_values) {
	if (p_value.type != JsonValue::ARRAY || p_value.array.size() != N) return false;
	for (size_t i = 0; i < N; ++i) {
		if (p_value.array[i].type != JsonValue::NUMBER) return false;
		r_values[i] = p_value.array[i].number;
	}
	return true;
}

bool transform(const JsonValue &p_value, FixtureTransform &r_matrix) {
	if (!numbers(p_value, r_matrix)) return false;
	if (r_matrix[3] != 0 || r_matrix[7] != 0 || r_matrix[11] != 0 || r_matrix[15] != 1) return false;
	for (int a = 0; a < 3; ++a) {
		for (int b = a; b < 3; ++b) {
			double dot = 0;
			for (int row = 0; row < 3; ++row) dot += r_matrix[a * 4 + row] * r_matrix[b * 4 + row];
			if (std::abs(dot - (a == b ? 1 : 0)) > 0.0001) return false;
		}
	}
	const double determinant = r_matrix[0] * (r_matrix[5] * r_matrix[10] - r_matrix[9] * r_matrix[6]) - r_matrix[4] * (r_matrix[1] * r_matrix[10] - r_matrix[9] * r_matrix[2]) + r_matrix[8] * (r_matrix[1] * r_matrix[6] - r_matrix[5] * r_matrix[2]);
	return std::abs(determinant - 1) <= 0.0001;
}

bool parse_header(const JsonValue &p_header, ScriptedSession &r_session, std::string &r_error) {
	int64_t version;
	if (field(p_header, "format").string != "pocketar-session" || !integer(field(p_header, "version"), 1, 1, version)) {
		r_error = "expected pocketar-session version 1 header";
		return false;
	}
	r_session.semantics = field(p_header, "semantics").string;
	if (r_session.semantics != "arkit" && r_session.semantics != "arcore") {
		r_error = "semantics must be arkit or arcore";
		return false;
	}
	const JsonValue &size = field(p_header, "image_size");
	if (size.type != JsonValue::ARRAY || size.array.size() != 2) {
		r_error = "image_size requires two dimensions";
		return false;
	}
	for (int i = 0; i < 2; ++i) {
		int64_t dimension;
		if (!integer(size.array[i], 32, 4096, dimension)) {
			r_error = "image dimensions must be integers from 32 to 4096";
			return false;
		}
		r_session.image_size[i] = int(dimension);
	}
	int index = 0;
	for (const char *name : { "fx", "fy", "cx", "cy" }) {
		const JsonValue &value = field(field(p_header, "intrinsics"), name);
		if (value.type != JsonValue::NUMBER || (index < 2 && value.number <= 0)) {
			r_error = "intrinsics require finite fx, fy, cx, cy and positive focal lengths";
			return false;
		}
		r_session.intrinsics[index++] = value.number;
	}
	return true;
}

bool parse_planes(const JsonValue &p_planes, std::vector<FixturePlane> &r_planes) {
	if (p_planes.type != JsonValue::ARRAY) return false;
	std::set<int64_t> ids;
	for (const JsonValue &value : p_planes.array) {
		FixturePlane plane;
		if (!integer(field(value, "id"), 1, 9007199254740991LL, plane.id) || !ids.insert(plane.id).second || !transform(field(value, "transform"), plane.transform)) return false;
		plane.alignment = named(field(value, "alignment"), { "horizontal_upward_facing", "horizontal_downward_facing", "vertical" });
		plane.tracking_state = named(field(value, "tracking_state"), { "tracking", "paused", "stopped" });
		if (plane.alignment < 0 || plane.tracking_state < 0 || !numbers(field(value, "extents"), plane.extents) || plane.extents[0] < 0 || plane.extents[1] < 0) return false;
		if (!integer(field(value, "subsumed_by"), 0, 9007199254740991LL, plane.subsumed_by) || plane.subsumed_by == plane.id) return false;
		const JsonValue &polygon = field(value, "polygon");
		if (polygon.type != JsonValue::ARRAY || (!polygon.array.empty() && polygon.array.size() < 3)) return false;
		for (const JsonValue &vertex : polygon.array) {
			std::array<double, 2> point;
			if (!numbers(vertex, point)) return false;
			plane.polygon.push_back(point);
		}
		r_planes.push_back(std::move(plane));
	}
	for (const FixturePlane &plane : r_planes) if (plane.subsumed_by != 0 && ids.count(plane.subsumed_by) == 0) return false;
	return true;
}

bool parse_record(const JsonValue &p_value, FixtureRecord &r_record, std::string &r_error) {
	if (!integer(field(p_value, "t_usec"), 0, 9007199254740991LL, r_record.t_usec)) {
		r_error = "t_usec must be a nonnegative exact integer";
		return false;
	}
	const int discriminators = int(p_value.object.count("reset")) + int(p_value.object.count("lifecycle")) + int(p_value.object.count("kind")) + int(p_value.object.count("frame_id"));
	if (discriminators != 1) {
		r_error = "record requires exactly one of frame_id, reset, lifecycle or kind";
		return false;
	}
	if (p_value.object.count("reset")) {
		const JsonValue &reset = field(p_value, "reset");
		if (reset.type != JsonValue::BOOLEAN || !reset.boolean) {
			r_error = "reset must be true";
			return false;
		}
		r_record.kind = FixtureRecord::RESET;
		return true;
	}
	if (p_value.object.count("lifecycle")) {
		const int lifecycle = named(field(p_value, "lifecycle"), { "pause", "resume" });
		if (lifecycle < 0) {
			r_error = "lifecycle must be pause or resume";
			return false;
		}
		r_record.kind = lifecycle == 0 ? FixtureRecord::PAUSE : FixtureRecord::RESUME;
		return true;
	}
	if (p_value.object.count("kind")) {
		r_record.kind = FixtureRecord::INPUT;
		int64_t finger;
		if (named(field(p_value, "kind"), { "down", "move", "up", "cancel" }) < 0 || !integer(field(p_value, "finger"), 0, 2147483647, finger) || !integer(field(p_value, "target_frame"), 1, 9007199254740991LL, r_record.target_frame) || !numbers(field(p_value, "uv"), r_record.uv) || r_record.uv[0] < 0 || r_record.uv[0] > 1 || r_record.uv[1] < 0 || r_record.uv[1] > 1) {
			r_error = "input requires kind, nonnegative finger, normalized uv and positive target_frame";
			return false;
		}
		r_record.finger = int(finger);
		r_record.input_kind = field(p_value, "kind").string;
		return true;
	}
	if (!integer(field(p_value, "frame_id"), 1, 9007199254740991LL, r_record.frame_id) || !transform(field(p_value, "camera_transform"), r_record.camera_transform) || field(p_value, "tracking_valid").type != JsonValue::BOOLEAN) {
		r_error = "frame requires positive frame_id, rigid physical camera_transform and tracking_valid boolean";
		return false;
	}
	r_record.tracking_valid = field(p_value, "tracking_valid").boolean;
	r_record.session_state = named(field(p_value, "session_state"), { "unsupported", "checking", "needs_install", "installing", "permission_denied", "ready", "initializing", "tracking", "paused", "failed" });
	r_record.tracking_reason = named(field(p_value, "tracking_reason"), { "none", "bad_state", "insufficient_light", "excessive_motion", "insufficient_features", "camera_unavailable" });
	if (r_record.session_state < 0 || r_record.tracking_reason < 0) {
		r_error = "unknown session_state or tracking_reason";
		return false;
	}
	r_record.has_planes = p_value.object.count("planes") != 0;
	if (r_record.has_planes && !parse_planes(field(p_value, "planes"), r_record.planes)) {
		r_error = "invalid full plane list";
		return false;
	}
	return true;
}

}

bool parse_scripted_session(const std::string &p_jsonl, ScriptedSession &r_session, std::string &r_error) {
	ScriptedSession session;
	std::istringstream stream(p_jsonl);
	std::string line;
	size_t line_number = 0;
	int64_t last_time = -1;
	int64_t last_frame = 0;
	std::set<int64_t> frame_ids;
	std::vector<std::pair<int64_t, size_t>> input_frames;
	r_error.clear();
	while (std::getline(stream, line)) {
		++line_number;
		JsonValue value;
		JsonReader reader(line, r_error);
		bool valid = reader.read(value);
		if (valid && value.type != JsonValue::OBJECT) {
			r_error = "line must contain a JSON object";
			valid = false;
		}
		if (valid && line_number == 1) valid = parse_header(value, session, r_error);
		else if (valid) {
			FixtureRecord record;
			valid = parse_record(value, record, r_error);
			if (valid && record.t_usec < last_time) {
				r_error = "t_usec must be nondecreasing";
				valid = false;
			}
			if (valid && record.kind == FixtureRecord::FRAME) {
				if (record.frame_id <= last_frame) {
					r_error = "frame_id must increase across the entire fixture";
					valid = false;
				}
				last_frame = record.frame_id;
				frame_ids.insert(last_frame);
			} else if (valid && record.kind == FixtureRecord::INPUT) input_frames.emplace_back(record.target_frame, line_number);
			last_time = record.t_usec;
			if (valid) session.records.push_back(std::move(record));
		}
		if (!valid) {
			r_error = "line " + std::to_string(line_number) + ": " + r_error;
			return false;
		}
	}
	if (line_number == 0 || frame_ids.empty()) {
		r_error = "line " + std::to_string(line_number == 0 ? 1 : line_number) + ": fixture requires a header and at least one frame";
		return false;
	}
	for (const auto &input : input_frames) {
		if (frame_ids.count(input.first) == 0) {
			r_error = "line " + std::to_string(input.second) + ": target_frame does not exist";
			return false;
		}
	}
	r_session = std::move(session);
	return true;
}

}
