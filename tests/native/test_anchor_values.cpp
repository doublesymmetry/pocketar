#include "core/ar_anchor_registry.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <type_traits>

using namespace godot;

static_assert(std::is_trivially_copyable<ARAnchorSnapshot>::value, "Packets must not own native anchors or tracker Refs");

int main() {
	Transform3D pose(Basis(Vector3(0, 1, 0), 0.3), Vector3(0, -0.7, -2));
	assert(ARAnchorRegistry::valid_pose(pose));
	ARAnchorSnapshot sample{1, pose, ARAnchorTracker::TRACKING};
	ARAnchorSnapshot copy = sample;
	copy.transform.origin.x += 1;
	assert(sample.transform == pose);
	assert(!ARAnchorRegistry::valid_pose(Transform3D(Basis::from_scale(Vector3(2, 1, 1)), Vector3())));
	assert(!ARAnchorRegistry::valid_pose(Transform3D(Basis::from_scale(Vector3(-1, 1, 1)), Vector3())));
	pose.origin.x = std::numeric_limits<real_t>::quiet_NaN();
	assert(!ARAnchorRegistry::valid_pose(pose));
	pose.origin.x = std::numeric_limits<real_t>::infinity();
	assert(!ARAnchorRegistry::valid_pose(pose));
	std::cout << "Anchor value tests passed; snapshot bytes=" << sizeof(ARAnchorSnapshot) << '\n';
}
