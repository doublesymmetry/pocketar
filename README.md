# PocketAR

Mobile augmented reality for Godot

MIT handheld AR for Godot 4.7.2, with ARKit on iOS and ARCore on Android behind
one Godot API. Native backends are implemented; iPhone validation is pending.

Milestone 1 supplies platform build/export skeletons and a synthetic rendering
harness. Its checks cover monoscopic XR with a HUD, off-centre projection and
screen rays, separate camera composition, colour invariance, transparency,
coherent image/pose packets and orientation changes. The camera uses generated
YCbCr calibration frames. The AR demo selects the native backend on each phone.

From the repository's devenv shell:

```sh
tools/build.sh macos debug
tools/test-native.sh
tools/harness.sh desktop -harness-out /tmp/pocketar-captures
```

The harness needs a GPU-backed GUI session. Native compilation and headless
script checks alone do not prove rendering correctness, exports or device loads.
See [AGENTS.md](AGENTS.md) for build/export commands, device requirements, source
evidence and the rendering limitations being tested.

`ARView.world_3d` exposes its private AR world for physics queries and shared-world
cameras. `ARView.environment` supplies the virtual scene's environment settings;
background mode and colour are ignored in favour of a transparent-black clear.
Other settings, including live edits, apply without changing the source resource.

iOS exports are loose Xcode projects under `build/exports/ios/debug/` and
`build/exports/ios/release/`. The certified minimum stays iOS 16.0;
`ios_min_version=15.0` is only a diagnostic SCons override.

The completed build and logic checks are recorded in
[the validation log](docs/milestone-1-validation.md); GPU and device checks remain pending.

`ARFramePacket` keeps pose, projection, intrinsics, display transform, tracking and
plane values valid for later touches and raycasts. Native camera textures are valid
only while the packet is current or presented. Script-held Refs do not pin them:
retired ARKit textures sample the engine default, and reused ARCore slots may show
newer pixels. ARView internally protects its applied textures until replacement.
