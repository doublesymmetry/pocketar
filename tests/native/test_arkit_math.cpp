#include "platform/ios/arkit_math.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace godot;

static void check(bool p_value, const char *p_name) {
	if (!p_value) { std::fprintf(stderr, "ARKIT MATH FAIL %s\n", p_name); std::exit(1); }
}

int main() {
	const ARIntrinsics k{ 510, 520, 299.5, 210.5, Vector2(640, 480) };
	const Vector3 expected_x[] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(-1, 0, 0), Vector3(0, -1, 0) };
	for (int turns = 0; turns < 4; ++turns) {
		const Basis rotation = arkit_view_rotation(turns);
		check(rotation.get_column(0).is_equal_approx(expected_x[turns]), "documented view X for every orientation");
		check(rotation.get_column(2).is_equal_approx(Vector3(0, 0, 1)), "sensor Z preserved");
		for (const Vector2 &size : { Vector2(1230, 640), Vector2(640, 1230), Vector2(720, 720) }) {
			Projection projection = arkit_intrinsics_projection(k, size, turns, 0.13, 52);
			check(projection[0][0] > 0 && projection[1][1] > 0 && projection[0][1] == 0 && projection[1][0] == 0, "upright projection");
			for (const Vector2 &other : { Vector2(1700, 600), Vector2(600, 1700) }) {
				const Projection adjusted = arkit_projection_aspect(projection, k.image_size, size, turns, other.x / other.y);
				const Projection expected = arkit_intrinsics_projection(k, other, turns, 0.13, 52);
				for (int c = 0; c < 4; ++c) check(adjusted[c].is_equal_approx(expected[c]), "aspect override preserves calibrated crop");
			}
			for (double near : { 0.01, 0.3, 2.0 }) {
				Projection clipped = arkit_projection_depth(projection, near, 79);
				const Vector4 n = clipped.xform(Vector4(0, 0, -near, 1));
				const Vector4 f = clipped.xform(Vector4(0, 0, -79, 1));
				check(std::abs(n.z / n.w + 1) < 1e-5 && std::abs(f.z / f.w - 1) < 1e-5, "GL near/far without stale depth");
				check(clipped[0] == projection[0] && clipped[1] == projection[1], "clipping preserves image calibration");
			}
			for (const Vector3 &point : { Vector3(0.1, -0.2, -2), Vector3(-0.7, 0.3, -4) }) {
				const Vector3 view = rotation.transposed().xform(point);
				const Vector4 clip = projection.xform(Vector4(view.x, view.y, view.z, 1));
				const Vector2 view_uv(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
				const Vector2 image_uv = display_transform_for_view(k.image_size, size, turns).xform(view_uv);
				const Vector2 expected((k.fx * point.x / -point.z + k.cx) / 640, (k.cy - k.fy * point.y / -point.z) / 480);
				check(image_uv.distance_to(expected) < 1e-5, "oriented projection/pose and camera UV agree");
			}
		}
	}
	std::puts("ARKIT MATH PASS orientations, intrinsics, UV and clipping");
}
