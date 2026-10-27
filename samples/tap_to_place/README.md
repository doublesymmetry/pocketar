# Tap to place

A scene and script implementing the [README recipe](https://github.com/doublesymmetry/pocketar#add-an-ar-scene).
Press **Start AR**, then tap a detected plane to place a coral cube. The HUD shows
backend, session state and tracking reason. **Reset AR** starts a new generation
and clears placements; **Show planes** toggles cyan planes. HUD buttons consume
input without placing cubes. Placements are copied poses, not persistent anchors.

Start AR creates the ARView and begins Android installation and permission setup;
the menu does not prompt. Denial shows **Retry setup**; repeated camera denials
may require enabling Camera in Android app settings. Interrupted and unsupported
sessions have their own messages. On iOS, grant camera permission when prompted;
after denial, enable it in app settings and return.

## Run

From a release ZIP, import `tap_to_place/project.godot` into public Godot 4.7.2+
and press Run. The addon and desktop fixture are included.

From the repository root, inside `devenv shell`:

```sh
tools/build.sh macos debug
stock godot --editor --path samples/tap_to_place
```

Press F6 or run `stock godot --path samples/tap_to_place` for public Godot 4.7.2;
omit `stock` to check the PocketAR fork.

Desktop uses a static table fixture and synthetic camera. Click the centre cyan
wall or table below; reset reloads that frame. Live camera and plane detection
require a phone, which selects ARKit or ARCore.

In source, `addons/pocketar` links to `demo/addons/pocketar`, so builds update
the sample. Packaging replaces the link with the release addon. Mobile exports
exclude the sample's `fixtures/table.jsonl`.

## Export

Replace `com.example.pocketar.tapto.place` with your game's identifier and set
your iOS signing team. Both presets are AR Optional and `runnable=false`;
launch exports yourself to avoid editor device polling on the shared host.

Build both variants for the chosen platform from the repository root:

```sh
tools/build.sh android debug
tools/build.sh android release
mkdir -p build/exports/tap-to-place/android
stock godot --headless --path samples/tap_to_place --editor --import
stock godot --headless --path samples/tap_to_place --install-android-build-template --export-debug Android
```

First install matching public 4.7.2 export templates and configure Java 17 and the
Android SDK in Godot; release exports need signing credentials. When switching engines,
delete the sample's generated `android/` directory before reinstalling the build
template to avoid leftover Java files.

For iOS, build `ios debug` and `ios release`, create
`build/exports/tap-to-place/ios`, export an Xcode project, then sign and build in Xcode.
See [platform requirements](https://github.com/doublesymmetry/pocketar#export).
The sample has no harness or device runner.
