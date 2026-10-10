#pragma once

#include <godot_cpp/variant/projection.hpp>
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/transform3d.hpp>

namespace godot {

struct ARIntrinsics {
	double fx = 520.0;
	double fy = 510.0;
	double cx = 301.0;
	double cy = 227.0;
	Vector2 image_size = Vector2(640, 480);
};

struct ARRay {
	Vector3 origin;
	Vector3 direction;
};

Transform2D display_transform_for_view(const Vector2 &p_image_size, const Vector2 &p_viewport_size, int p_quarter_turns);
Projection projection_from_intrinsics(const ARIntrinsics &p_intrinsics, const Vector2 &p_viewport_size, int p_quarter_turns, double p_near, double p_far);
ARRay ray_from_screen(const Projection &p_projection, const Transform3D &p_camera_transform, const Vector2 &p_viewport_size, const Vector2 &p_screen_point);

}
