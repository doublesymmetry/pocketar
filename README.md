# PocketAR

PocketAR brings handheld AR to Godot through one GDExtension: ARKit on iOS,
ARCore on Android, and simulated cameras for desktop development. `ARView`
combines the camera image, a private 3D scene and a HUD. Games can track the
session, inspect detected planes and place objects with a screen raycast.

## Requirements

Use public Godot 4.7.2 or newer with matching export templates. The supported
editor host is macOS on Apple Silicon. Windows, Linux and Intel Mac editors
are not currently supported.

| Target | Requirements |
| --- | --- |
| iOS | iOS 16+, arm64, ARKit world tracking and hardware supported by Godot's Metal renderer. No simulator build. |
| Android | API 29+, arm64, an ARCore-supported device with Vulkan and Google Play Services for AR 1.48+. |
| Desktop development | macOS Apple Silicon, Mobile/Metal; synthetic camera or scripted replay, without live AR. |

The OS minimum alone does not establish device support. In particular,
iPhone 7 Plus/A10/iOS 15 is outside the supported targets. These are support
requirements, not a certification of every device model.

Set these project settings before creating an ARView:

| Setting | Value |
| --- | --- |
| `rendering/renderer/rendering_method` and `.mobile` | `mobile` |
| `rendering/rendering_device/driver.ios` and `.macos` | `metal` |
| `rendering/rendering_device/driver.android` | `vulkan` |
| `rendering/rendering_device/fallback_to_opengl3` | `false` |
| `rendering/driver/threads/thread_model` | `1` (Safe) |
| `rendering/viewport/hdr_2d` | `false` (SDR outer viewport) |
| `rendering/anti_aliasing/quality/use_debanding` | `true` for in-scene composition |
| `rendering/rendering_device/staging_buffer/block_size_kb` | `4096` for ARCore CPU uploads |
| `rendering/rendering_device/staging_buffer/texture_upload_region_size_px` | `1024` for ARCore CPU uploads |

Separate rendering threads and the Compatibility/OpenGL renderer are unsupported.
Keep the outer viewport SDR even when ARView uses an HDR virtual viewport.
The staging settings avoid small upload regions on Android; PocketAR reports
their values but does not change them.

Stock Godot 4.7.2 uses CPU camera uploads on Android. On engines providing
`ExternalTexture.submit_hardware_buffer`, PocketAR automatically tries the
hardware-buffer path and falls back to CPU if activation fails.

## Install from source

A prebuilt release ZIP installation recipe will be added with the release package.

For now, build on an Apple Silicon Mac with Xcode selected by `xcode-select`,
Nix and devenv installed. The repository environment supplies SCons, Python,
Java 17 and the Android SDK/NDK.

```sh
git clone --recurse-submodules https://github.com/doublesymmetry/pocketar.git
cd pocketar
devenv shell
tools/build.sh macos debug
tools/build.sh android debug  # or: tools/build.sh ios debug
```

Build the same mobile target with `release` before a release export. The macOS
debug library provides editor help and is needed even for a mobile-only game.
Copy the complete generated `demo/addons/pocketar/` directory to your project's
`addons/pocketar/`, preserving binaries, shaders, `.uid` files and the license
notices it contains. Enable **PocketAR** in Project Settings → Plugins.
Open your game with the public Godot editor and install its matching export
templates. When using the repository's shell, `stock godot` selects Godot 4.7.2.

## Add an AR scene

Run [the tap-to-place sample](samples/tap_to_place/README.md) for a small project
with a Start AR button, session messages, placement, reset and plane display.
It also supports placement against a scripted fixture on desktop.

Attach this script to a full-rect `Control` in your main scene. Instantiate the
packed scene; `ARView.new()` alone does not create its required children.

```gdscript
extends Control

var ar: ARView

func _ready() -> void:
	ar = preload("res://addons/pocketar/ar_view.tscn").instantiate() as ARView
	match OS.get_name():
		"iOS": ar.backend = ARView.Backend.ARKIT
		"Android": ar.backend = ARView.Backend.ARCORE
		_: ar.backend = ARView.Backend.SYNTHETIC
	ar.composition = ARView.Composition.IN_SCENE
	ar.handle_presses = true
	ar.screen_pressed.connect(_place)
	ar.session_generation_changed.connect(_clear_placements)
	add_child(ar)
	ar.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var reset_button := Button.new()
	reset_button.text = "Reset AR"
	reset_button.pressed.connect(ar.reset)
	ar.hud.add_child(reset_button)

func _place(point: Vector2, _ray: Dictionary) -> void:
	var hits: Array[ARRaycastHit] = ar.raycast(point)
	if hits.is_empty():
		return
	var marker := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(0.1, 0.1, 0.1)
	marker.mesh = box
	ar.scene_root.add_child(marker)
	marker.global_transform = hits[0].pose
	marker.position += marker.basis.y * 0.05

func _clear_placements(_generation: int) -> void:
	for child: Node in ar.scene_root.get_children():
		child.queue_free()
```

Only one active ARView may own the primary XR interface. Set `backend` and
`composition` before `add_child`; there is no automatic platform selection.
Access `scene_root`, `hud`, `camera` and `world_3d` after the view is ready.
World units are metres. Leave the XR origin and scene root at identity and XR
world scale at one. Set `ar.camera.near` and `far` for clipping distances.

HUD Controls receive input first. With `handle_presses=true`, unconsumed presses
inside the view emit `screen_pressed`. When handling input yourself, convert
`event.position` with `ar.view_point()` before calling `raycast()` or
`screen_to_ray()`. `world_to_screen()` projects into the same local pixels.

The synthetic desktop backend has no real planes, so this placement example
needs a phone or a scripted fixture. A hit is a copied pose, not an anchor that
keeps placed content attached as tracking changes. Use `session_state`,
`tracking_reason` and their change signals to show tracking, permission,
unsupported-device and interruption states. A new `session_generation`
invalidates old plane identities; clear or rebuild placements then.

## Export

Keep the PocketAR editor plugin enabled. Build and copy the matching debug or
release mobile binaries before exporting. Use your game's identifiers and
signing credentials, and the export templates matching your Godot editor.

For Android:

- Install the Android build template from Godot's Project menu. PocketAR forces
  Gradle export and supplies its AAR plus `com.google.ar:core:1.56.0`.
- Set `gradle_build/min_sdk="29"`, `gradle_build/target_sdk="36"` and enable only
  `architectures/arm64-v8a`. Configure the Android SDK and Java 17 in the editor.
- Enable the camera permission (`permissions/camera`). The AAR declares CAMERA
  and the plugin requests runtime permission and ARCore installation.
- Leave `pocketar/ar_required=false` for AR Optional, or enable it to require
  ARCore hardware and set required manifest metadata. Availability checking at
  app startup is prompt-free. Installation and camera permission are requested
  when an ARCore ARView starts, so create the view when the player enters AR.
- To request setup before creating a view, or retry after denial, call
  `Engine.get_singleton("PocketARPlugin").requestSession()` on Android.
  Later resumes continue setup only after AR was requested; resumes do not
  retry denied permission prompts. Show `PERMISSION_DENIED` or `NEEDS_INSTALL`
  through the session state and offer a retry button. Android may require the
  player to enable Camera in app settings after repeated denials.

For iOS:

- Enable arm64 and set `application/min_ios_version="16.0"` or higher.
- Set `application/bundle_identifier`, your signing team and a meaningful
  `privacy/camera_usage_description` (`NSCameraUsageDescription`). Camera access
  needs user consent; blank usage text is not a valid setup.
- Set `pocketar/ar_required=true` if ARKit is essential. It adds the `arkit`
  required device capability; false leaves unsupported-device handling to the game.
- Export an Xcode project, then sign and build it in Xcode. PocketAR adds ARKit,
  Metal, CoreVideo and AVFoundation linkage. Its XCFramework contains device
  arm64 only, not an iOS simulator slice.

## Composition

`IN_SCENE` is the default. The camera and virtual objects share a scene buffer,
so transparency and screen-reading materials can blend with the camera at
lower bandwidth. Exposure, tonemapping, grading and glow also affect the camera.
Mobile's normalized buffer clips bright radiance at approximately two. Enable
material debanding to reduce visible camera gradients; it introduces small
per-pixel noise. Reserve material priority 127 for the camera, and keep decal
boxes away from the camera's near-plane proxy: an overlapping decal can paint
the camera image.

`ISOLATED_CAMERA` draws an SDR camera beneath an HDR virtual scene and composites
them. The camera bypasses the game's Environment effects, and virtual objects
keep the float colour range, at the cost of the HDR viewport/composite. Screen
reads see only virtual objects; glow at zero coverage alpha is discarded.

Both modes ignore the supplied Environment's background mode and colour,
preserve the source resource and apply its other live settings before drawing.
The transparent-viewport depth-of-field restriction applies in both modes.
Custom blend modes need their own rendering checks.

## API and limitations

Start with [the API contract](docs/api.md). Native class help is embedded in
editor/debug libraries, and ARView has editor tooltips and script help.
Packets copy calibration, poses and plane values, but native texture references
are not image archives. Retaining a packet does not preserve camera pixels.

There is no game-facing anchors API, depth occlusion, image/face tracking, light
estimation, cloud/geospatial anchors or persistent world map API. Native live
queries, Android datasets and diagnostic controls are advanced interfaces.
Multiple simultaneous ARViews and mixing an AR session with Godot CameraServer
feeds are unsupported.

Contributor build commands, harnesses, device procedures and platform traps
live in [AGENTS.md](AGENTS.md).

## License

PocketAR is [MIT licensed](LICENSE); godot-cpp is also MIT licensed. Apple ARKit
and Google ARCore remain subject to their own SDK/runtime terms. See
[the ARCore dependency notice](thirdparty/arcore/README.md) for provenance.
