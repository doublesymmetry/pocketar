# ARCore C API

`include/arcore_c_api.h` is the unchanged 1.44-era header from the local
`godot_arcore` checkout. The M3 backend uses declarations present in that snapshot.
It has not yet been replaced or verified against the pinned ARCore SDK 1.56.0.
The header retains Google's ARCore Additional Terms of Service notice; it is not
covered by this repository's MIT license.

Run `tools/update-arcore-header.sh` to fetch the 1.56.0 AAR and replace the header.
The updater checks the AAR for a header first, then uses the matching SDK tag's
`libraries/include/arcore_c_api.h` when the AAR contains only binaries. It writes
the source URL and SHA-256 to `header-source.txt`.

Gradle compiles the plugin against `com.google.ar:core:1.56.0` and extracts the
arm64 C API shim to `android/plugin/build/arcore-native/1.56.0/` for inspection.
The export plugin adds that same Maven dependency to the application, which
packages the ARCore classes, manifest and shim. The plugin AAR does not contain a
second copy of `libarcore_sdk_c.so`. Native code loads it with `dlopen`.
