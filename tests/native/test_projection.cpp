#include "core/projection.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace godot;

static double worst_error = 0;
static int checks = 0;

static void check(double p_error, const char *p_name) {
	checks++;
	worst_error = std::max(worst_error, p_error);
	if (!std::isfinite(p_error) || p_error >= 1e-4) {
		std::fprintf(stderr, "NATIVE FAIL %s error=%.9g\n", p_name, p_error);
		std::exit(1);
	}
}

static Vector2 expected_pixel(const ARIntrinsics &p_k, const Vector2 &p_view, int p_turns, const Vector3 &p_point) {
	Vector2 image(p_k.fx * p_point.x / -p_point.z + p_k.cx, p_k.cy - p_k.fy * p_point.y / -p_point.z);
	Vector2 extent = p_k.image_size;
	switch (p_turns) {
		case 1: image = Vector2(extent.y - image.y, image.x); extent = Vector2(extent.y, extent.x); break;
		case 2: image = extent - image; break;
		case 3: image = Vector2(image.y, extent.x - image.x); extent = Vector2(extent.y, extent.x); break;
	}
	const double scale = std::max(p_view.x / extent.x, p_view.y / extent.y);
	return (image - extent * 0.5) * scale + p_view * 0.5;
}

int main() {
	const ARIntrinsics calibrations[] = {
		{ 520, 510, 301, 227, Vector2(640, 480) },
		{ 733, 811, 451, 301, Vector2(1024, 768) },
		{ 510, 510, 320, 240, Vector2(640, 480) }
	};
	const Vector2 viewports[] = { Vector2(960, 720), Vector2(720, 960), Vector2(1920, 1080), Vector2(393, 852), Vector2(800, 800) };
	const Vector2 clips[] = { Vector2(0.05, 100), Vector2(0.3, 17), Vector2(1.2, 2000) };
	const Transform3D camera(Basis(Vector3(0, 1, 0), 0.37), Vector3(1.25, -0.8, 2.2));
	for (const ARIntrinsics &k : calibrations) {
		for (const Vector2 &view : viewports) {
			for (int turns = 0; turns < 4; turns++) {
				for (const Vector2 &clip : clips) {
					const Projection projection = projection_from_intrinsics(k, view, turns, clip.x, clip.y);
					for (int row = 0; row <= 8; row++) {
						for (int col = 0; col <= 8; col++) {
							const Vector2 pixel(view.x * col / 8.0, view.y * row / 8.0);
							const ARRay ray = ray_from_screen(projection, camera, view, pixel);
							check(ray.origin.distance_to(camera.origin), "world origin");
							check(std::abs(ray.direction.length() - 1), "normalized direction");
							const Vector3 local = camera.affine_inverse().xform(ray.origin + ray.direction * 5);
							const Vector2 predicted = expected_pixel(k, view, turns, local);
							check(((predicted - pixel) / view * 2).length(), "independent pinhole oracle");
							const Vector4 ndc = projection.xform(Vector4(local.x, local.y, local.z, 1));
							check(Vector2(ndc.x / ndc.w - (pixel.x / view.x * 2 - 1), ndc.y / ndc.w - (1 - pixel.y / view.y * 2)).length(), "projection ray round trip");
							const Transform2D display = display_transform_for_view(k.image_size, view, turns);
							const Vector2 image_uv = display.xform(pixel / view);
							check((image_uv - Vector2(k.fx * local.x / -local.z + k.cx, k.cy - k.fy * local.y / -local.z) / k.image_size).length(), "display transform agrees with projection");
						}
					}
					const Vector4 near_h = projection.xform(Vector4(0, 0, -clip.x, 1));
					const Vector4 far_h = projection.xform(Vector4(0, 0, -clip.y, 1));
					check(std::abs(near_h.z / near_h.w + 1), "near depth is minus one");
					check(std::abs(far_h.z / far_h.w - 1), "far depth is plus one");
				}
			}
		}
	}
	std::printf("NATIVE PASS %d checks, worst NDC/UV error %.9g\n", checks, worst_error);
	return 0;
}
