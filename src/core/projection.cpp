#include "projection.h"

#include <algorithm>

namespace godot {

Transform2D display_transform_for_view(const Vector2 &p_image_size, const Vector2 &p_viewport_size, int p_quarter_turns) {
	const int turns = ((p_quarter_turns % 4) + 4) % 4;
	const Vector2 oriented_size = turns % 2 ? Vector2(p_image_size.y, p_image_size.x) : p_image_size;
	const double scale = std::max(p_viewport_size.x / oriented_size.x, p_viewport_size.y / oriented_size.y);
	const Vector2 span = p_viewport_size / (oriented_size * scale);
	const Transform2D crop(Vector2(span.x, 0), Vector2(0, span.y), (Vector2(1, 1) - span) * 0.5);
	Transform2D rotation;
	switch (turns) {
		case 1: rotation = Transform2D(Vector2(0, -1), Vector2(1, 0), Vector2(0, 1)); break;
		case 2: rotation = Transform2D(Vector2(-1, 0), Vector2(0, -1), Vector2(1, 1)); break;
		case 3: rotation = Transform2D(Vector2(0, 1), Vector2(-1, 0), Vector2(1, 0)); break;
	}
	return rotation * crop;
}

Projection projection_from_intrinsics(const ARIntrinsics &p_intrinsics, const Vector2 &p_viewport_size, int p_quarter_turns, double p_near, double p_far) {
	const int turns = ((p_quarter_turns % 4) + 4) % 4;
	const Transform2D image_to_view = display_transform_for_view(p_intrinsics.image_size, p_viewport_size, p_quarter_turns).affine_inverse();
	const Vector2 centre = image_to_view.xform(Vector2(p_intrinsics.cx, p_intrinsics.cy) / p_intrinsics.image_size);
	const Vector2 extent = turns % 2 ? Vector2(p_intrinsics.image_size.y, p_intrinsics.image_size.x) : p_intrinsics.image_size;
	const Vector2 focal = turns % 2 ? Vector2(p_intrinsics.fy, p_intrinsics.fx) : Vector2(p_intrinsics.fx, p_intrinsics.fy);
	const double scale = std::max(p_viewport_size.x / extent.x, p_viewport_size.y / extent.y);
	Projection result;
	result[0] = Vector4(2 * focal.x * scale / p_viewport_size.x, 0, 0, 0);
	result[1] = Vector4(0, 2 * focal.y * scale / p_viewport_size.y, 0, 0);
	result[2] = Vector4(1 - 2 * centre.x, 2 * centre.y - 1, -(p_far + p_near) / (p_far - p_near), -1);
	result[3] = Vector4(0, 0, -2 * p_far * p_near / (p_far - p_near), 0);
	return result;
}

Transform3D display_oriented_pose(const Transform3D &p_pose, int p_quarter_turns) {
	const int turns = ((p_quarter_turns % 4) + 4) % 4;
	return p_pose * Transform3D(Basis(Vector3(0, 0, 1), turns * Math::PI * 0.5), Vector3());
}

ARRay ray_from_screen(const Projection &p_projection, const Transform3D &p_camera_transform, const Vector2 &p_viewport_size, const Vector2 &p_screen_point) {
	const Vector2 ndc = p_screen_point / p_viewport_size * 2 - Vector2(1, 1);
	const Vector4 near_h = p_projection.inverse().xform(Vector4(ndc.x, -ndc.y, -1, 1));
	const Vector3 local(near_h.x / near_h.w, near_h.y / near_h.w, near_h.z / near_h.w);
	return { p_camera_transform.origin, p_camera_transform.basis.xform(local).normalized() };
}

}
