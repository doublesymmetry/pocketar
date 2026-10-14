#include "core/debug_plane.h"
#include <cstdio>
#include <limits>

using namespace godot;

static int checks = 0;
static int failures = 0;

static void check(bool p_condition, const char *p_name) {
	++checks;
	if (!p_condition) {
		std::fprintf(stderr, "DEBUG GEOMETRY FAIL %s\n", p_name);
		++failures;
	}
}

int main() {
	const Vector2 extents(2, 4);
	check(DebugPlane::valid_geometry(Transform3D(), extents), "identity rectangle");
	check(DebugPlane::valid_geometry(Transform3D(Basis(Vector3(0, 1, 0), 0.7), Vector3(2, -1, -4)), extents), "yaw and translation");
	check(DebugPlane::valid_geometry(Transform3D(Basis(Vector3(1, 0, 0), 0.000005), Vector3()), extents), "tilt inside tolerance");
	check(!DebugPlane::valid_geometry(Transform3D(Basis(Vector3(1, 0, 0), 0.00002), Vector3()), extents), "tilt outside tolerance");
	check(!DebugPlane::valid_geometry(Transform3D(Basis(Vector3(1, 0, 0), 3.141592653589793), Vector3()), extents), "downward rectangle");
	Basis scale;
	scale.set_column(0, Vector3(1.001, 0, 0));
	check(!DebugPlane::valid_geometry(Transform3D(scale, Vector3()), extents), "scale");
	for (float stretch : { 2.0f, 1.001f }) {
		Basis area_preserving;
		area_preserving.set_column(0, Vector3(stretch, 0, 0));
		area_preserving.set_column(2, Vector3(0, 0, 1 / stretch));
		check(!DebugPlane::valid_geometry(Transform3D(area_preserving, Vector3()), extents), "area-preserving scale");
	}
	Basis shear;
	shear.set_column(0, Vector3(1, 0, 0.001));
	check(!DebugPlane::valid_geometry(Transform3D(shear, Vector3()), extents), "shear");
	Basis reflection;
	reflection.set_column(0, Vector3(-1, 0, 0));
	check(!DebugPlane::valid_geometry(Transform3D(reflection, Vector3()), extents), "reflection");
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	for (float value : { nan, inf, -inf }) {
		check(!DebugPlane::valid_geometry(Transform3D(Basis(), Vector3(value, 0, 0)), extents), "nonfinite origin");
		Basis basis;
		basis.set_column(0, Vector3(value, 0, 0));
		check(!DebugPlane::valid_geometry(Transform3D(basis, Vector3()), extents), "nonfinite basis");
		check(!DebugPlane::valid_geometry(Transform3D(), Vector2(value, 1)), "nonfinite extent");
	}
	for (const Vector2 &bad : { Vector2(0, 1), Vector2(1, 0), Vector2(-1, 1), Vector2(1, -1) }) {
		check(!DebugPlane::valid_geometry(Transform3D(), bad), "nonpositive extent");
	}
	std::printf("DEBUG GEOMETRY %s %d checks\n", failures ? "FAIL" : "PASS", checks);
	return failures ? 1 : 0;
}
