#pragma once

#include "core/projection.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace godot {

class ARKitOrientationTransition {
	int turns = -1;
	Vector2 window_size;
	uint64_t changed_at = 0;
	bool waiting = false;

public:
	bool wait_for_resize(int p_turns, const Vector2 &p_window_size, uint64_t p_now) {
		if (p_turns != turns) {
			waiting = turns >= 0 && ((p_turns ^ turns) & 1) && p_window_size == window_size;
			turns = p_turns;
			changed_at = p_now;
		} else if (p_window_size != window_size || p_now - changed_at >= 500000) {
			waiting = false;
		}
		window_size = p_window_size;
		return waiting;
	}
};

inline Transform3D arkit_plane_local_frame(const Vector3 &p_center, double p_rotation) {
	return Transform3D(Basis(Vector3(0, 1, 0), p_rotation), p_center);
}

inline double arkit_projection_residual_px(const Projection &p_reference, const Projection &p_candidate, const Vector2 &p_view) {
	const Projection inverse = p_reference.inverse();
	double maximum = 0;
	for (int y = -1; y <= 1; ++y) {
		for (int x = -1; x <= 1; ++x) {
			const Vector4 point = inverse.xform(Vector4(x, y, 0, 1));
			const Vector4 candidate = p_candidate.xform(point);
			const double dx = (candidate.x / candidate.w - x) * p_view.x * 0.5;
			const double dy = (candidate.y / candidate.w - y) * p_view.y * 0.5;
			const double error = std::hypot(dx, dy);
			if (!std::isfinite(error)) return std::numeric_limits<double>::infinity();
			maximum = std::max(maximum, error);
		}
	}
	return maximum;
}

inline Basis arkit_view_rotation(int p_turns) {
	const Vector3 x[] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(-1, 0, 0), Vector3(0, -1, 0) };
	const Vector3 y[] = { Vector3(0, 1, 0), Vector3(-1, 0, 0), Vector3(0, -1, 0), Vector3(1, 0, 0) };
	return Basis(x[p_turns & 3], y[p_turns & 3], Vector3(0, 0, 1));
}

inline Projection arkit_projection_depth(Projection p_projection, double p_near, double p_far) {
	p_projection[0][2] = p_projection[1][2] = 0;
	p_projection[2][2] = -(p_far + p_near) / (p_far - p_near);
	p_projection[3][2] = -2 * p_far * p_near / (p_far - p_near);
	return p_projection;
}

inline Projection arkit_projection_aspect(Projection p_projection, const Vector2 &p_image, const Vector2 &p_view, int p_turns, double p_aspect) {
	const double image_aspect = p_turns % 2 ? p_image.y / p_image.x : p_image.x / p_image.y;
	const double old_aspect = p_view.x / p_view.y;
	const double x = std::min(1.0, old_aspect / image_aspect) / std::min(1.0, p_aspect / image_aspect);
	const double y = std::min(1.0, image_aspect / old_aspect) / std::min(1.0, image_aspect / p_aspect);
	for (int c = 0; c < 4; ++c) { p_projection[c][0] *= x; p_projection[c][1] *= y; }
	return p_projection;
}

inline Projection arkit_intrinsics_projection(const ARIntrinsics &p_k, const Vector2 &p_view, int p_turns, double p_near, double p_far) {
	double fx = p_k.fx, fy = p_k.fy, cx = p_k.cx, cy = p_k.cy, w = p_k.image_size.x, h = p_k.image_size.y;
	switch (p_turns & 3) {
		case 1: std::swap(fx, fy); cx = h - p_k.cy; cy = p_k.cx; std::swap(w, h); break;
		case 2: cx = w - cx; cy = h - cy; break;
		case 3: std::swap(fx, fy); cx = p_k.cy; cy = w - p_k.cx; std::swap(w, h); break;
	}
	const double kx = std::min(1.0, (p_view.x / p_view.y) / (w / h));
	const double ky = std::min(1.0, (w / h) / (p_view.x / p_view.y));
	Projection result;
	result[0] = Vector4(2 * fx / (kx * w), 0, 0, 0);
	result[1] = Vector4(0, 2 * fy / (ky * h), 0, 0);
	result[2] = Vector4((1 - 2 * cx / w) / kx, (2 * cy / h - 1) / ky, 0, -1);
	result[3] = Vector4();
	return arkit_projection_depth(result, p_near, p_far);
}

}
