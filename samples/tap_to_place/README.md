# Tap to place

A single scene and script showing the [README recipe](../../README.md#add-an-ar-scene).
Press **Start AR** to create the ARView, then tap a detected plane to place a coral
cube. The HUD shows the backend, session state and tracking reason. **Reset AR**
starts a new generation and clears placements; **Show planes** toggles the cyan
plane overlay. HUD buttons consume input, so pressing them does not place a cube.
Placements are copied hit poses, not persistent anchors.

On Android, the menu does not request installation or camera permission. Start AR
begins setup. A denial shows **Retry setup**; repeated camera denials may require
enabling Camera in Android app settings. Interrupted and unsupported sessions
have their own messages. On iOS, grant camera permission in the system prompt;
after a denial, enable it in app settings and return to the app.

## Run

From a release sample ZIP, import `tap_to_place/project.godot` into public Godot
4.7.2 or newer and press Run. The addon and desktop fixture are included.

From the repository root, inside `devenv shell`:

```sh
tools/build.sh macos debug
stock godot --editor --path samples/tap_to_place
```

Press F6 or run `stock godot --path samples/tap_to_place`. Public Godot 4.7.2 is
the primary editor; omit `stock` to check the PocketAR engine fork.

Desktop uses a static table fixture with a synthetic
camera image. Click the cyan wall in the centre or the table below it. Reset
reloads that same frame. There is no live camera or plane detection on desktop.
Phones select ARKit or ARCore and use live planes.

In the source checkout, `addons/pocketar` links to `demo/addons/pocketar`, so
builds update the sample without a second addon copy. Packaging replaces this
link with the release addon. The sample owns `fixtures/table.jsonl`; mobile
exports exclude this desktop fixture.

## Export

The presets use `com.example.pocketar.tapto.place`. Replace it with your game's
identifier and set your iOS signing team. Both presets are AR Optional and
`runnable=false`; launch exports yourself. This avoids editor device polling
on the repository's shared host.

Build both variants for the chosen platform from the repository root:

```sh
tools/build.sh android debug
tools/build.sh android release
mkdir -p build/exports/tap-to-place/android
stock godot --headless --path samples/tap_to_place --editor --import
stock godot --headless --path samples/tap_to_place --install-android-build-template --export-debug Android
```

Install matching public 4.7.2 export templates first and configure Java 17 and
the Android SDK in Godot. A release export also needs your signing credentials.
When switching engine versions, remove the generated sample `android/` directory
before installing the new build template; overwriting it can leave old Java files.
For iOS, build `ios debug` and `ios release`, create
`build/exports/tap-to-place/ios`, then export the iOS preset as an Xcode project
and sign/build it in Xcode. The [main export notes](https://github.com/doublesymmetry/pocketar#export)
cover platform requirements. The sample contains no harness or device runner.
