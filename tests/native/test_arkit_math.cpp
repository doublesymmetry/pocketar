#include "platform/ios/arkit_math.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace godot;

static void check(bool p_value, const char *p_name) {
	if (!p_value) { std::fprintf(stderr, "ARKIT MATH FAIL %s\n", p_name); std::exit(1); }
}

int main() {
	ARKitOrientationTransition transition;
	check(!transition.wait_for_resize(1, Vector2(390, 844), 1000), "initial portrait window is accepted");
	check(transition.wait_for_resize(0, Vector2(390, 844), 2000), "90 degree change waits for window resize");
	check(!transition.wait_for_resize(0, Vector2(844, 390), 3000), "window resize releases orientation transition");
	check(!transition.wait_for_resize(2, Vector2(844, 390), 4000), "180 degree change commits immediately");
	check(transition.wait_for_resize(3, Vector2(844, 390), 5000), "reverse portrait waits for window resize");
	check(transition.wait_for_resize(3, Vector2(844, 390), 504999), "orientation transition remains bounded below half second");
	check(!transition.wait_for_resize(3, Vector2(844, 390), 505000), "orientation transition expires after half second");
	check(!transition.wait_for_resize(3, Vector2(844, 390), 600000), "expired mismatch cannot freeze embedded ARView");
	check(!transition.wait_for_resize(1, Vector2(844, 390), 601000), "180 degree change ignores aspect mismatch");
	check(transition.wait_for_resize(0, Vector2(844, 390), 602000), "90 degree change waits even with matching window aspect");
	check(!transition.wait_for_resize(1, Vector2(390, 844), 603000), "simultaneous orientation and resize commit immediately");
	check(!transition.wait_for_resize(1, Vector2(391, 844), 604000), "independent window resize stays committed");
	check(transition.wait_for_resize(0, Vector2(391, 844), 605000), "earlier independent resize does not skip a new rotation wait");
	check(!transition.wait_for_resize(0, Vector2(391, 843), 606000), "subsequent window size change releases wait");

	const Transform3D local_plane = arkit_plane_local_frame(Vector3(2, 0, -3), std::acos(-1.0) / 2);
	check(local_plane.origin.is_equal_approx(Vector3(2, 0, -3)), "plane frame follows off-centre extent");
	check(local_plane.basis.get_column(0).is_equal_approx(Vector3(0, 0, -1)), "plane extent uses positive Y rotation");
	const Transform3D anchor(Basis(Vector3(0, 0, 1), std::acos(-1.0) / 2), Vector3(-4, 5, 6));
	const Transform3D plane = anchor * local_plane;
	const Transform3D anchor_to_plane = local_plane.affine_inverse();
	const Vector3 expected_polygon[] = { Vector3(-2, 0, -1), Vector3(2, 0, -1), Vector3(2, 0, 1), Vector3(-2, 0, 1) };
	const Vector3 anchor_polygon[] = { Vector3(1, 0, -1), Vector3(1, 0, -5), Vector3(3, 0, -5), Vector3(3, 0, -1) };
	for (int i = 0; i < 4; ++i) {
		const Vector3 vertex = anchor_to_plane.xform(anchor_polygon[i]);
		check(vertex.is_equal_approx(expected_polygon[i]), "rotated boundary belongs to centered extent frame");
		check(plane.xform(vertex).is_equal_approx(anchor.xform(anchor_polygon[i])), "plane remapping preserves world polygon");
	}
	check(plane.basis.get_column(1).is_equal_approx(anchor.basis.get_column(1)), "plane remapping preserves normal");

	const ARIntrinsics k{ 510, 520, 299.5, 210.5, Vector2(640, 480) };
	ARKitOrientationTransition embedded;
	const Vector2 portrait_window(390, 844), landscape_arview(390, 300);
	check(!embedded.wait_for_resize(1, portrait_window, 1000000), "landscape ARView starts in portrait window");
	check(embedded.wait_for_resize(2, portrait_window, 1100000), "embedded ARView waits at 90 degree change");
	check(embedded.wait_for_resize(2, portrait_window, 1599999), "unchanged portrait window waits below half second");
	check(!embedded.wait_for_resize(2, portrait_window, 1600000), "unchanged portrait window commits after half second");
	const Projection embedded_projection = arkit_intrinsics_projection(k, landscape_arview, 2, 0.1, 100);
	const Vector3 physical_point(0.2, -0.1, -2);
	const Vector3 oriented_point = arkit_view_rotation(2).transposed().xform(physical_point);
	const Vector4 embedded_clip = embedded_projection.xform(Vector4(oriented_point.x, oriented_point.y, oriented_point.z, 1));
	const Vector2 embedded_uv(embedded_clip.x / embedded_clip.w * 0.5 + 0.5, 0.5 - embedded_clip.y / embedded_clip.w * 0.5);
	const Vector2 embedded_image = display_transform_for_view(k.image_size, landscape_arview, 2).xform(embedded_uv);
	const Vector2 expected_image((k.fx * 0.1 + k.cx) / 640, (k.cy + k.fy * 0.05) / 480);
	check(embedded_image.distance_to(expected_image) < 1e-5, "landscape ARView keeps its crop after portrait-window rotation timeout");
	const Vector3 expected_x[] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(-1, 0, 0), Vector3(0, -1, 0) };
	for (int turns = 0; turns < 4; ++turns) {
		const Basis rotation = arkit_view_rotation(turns);
		check(rotation.get_column(0).is_equal_approx(expected_x[turns]), "documented view X for every orientation");
		check(rotation.get_column(2).is_equal_approx(Vector3(0, 0, 1)), "sensor Z preserved");
		for (const Vector2 &size : { Vector2(1230, 640), Vector2(640, 1230), Vector2(720, 720) }) {
			Projection projection = arkit_intrinsics_projection(k, size, turns, 0.13, 52);
			check(arkit_projection_residual_px(projection, projection, size) < 1e-4, "identical projection has zero pixel residual");
			Projection shifted = projection;
			shifted[2][0] += 2.0 / size.x;
			shifted[2][1] += 4.0 / size.y;
			check(std::abs(arkit_projection_residual_px(projection, shifted, size) - std::sqrt(5.0)) < 1e-3, "projection residual measures viewport pixel distance");
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
	std::puts("ARKIT MATH PASS orientations, bounded transitions, planes, pixel residuals, intrinsics, UV and clipping");
}
