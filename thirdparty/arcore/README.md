# ARCore C API

`include/arcore_c_api.h` is the unchanged header from the ARCore SDK `v1.56.0`
GitHub tag at `libraries/include/arcore_c_api.h`. The 1.56.0 AAR has no header.
The header retains Google's ARCore Additional Terms of Service notice; it is not
covered by this repository's MIT license.

Run `tools/update-arcore-header.sh` to refresh the pinned header.
The updater fetches the matching SDK tag's `libraries/include/arcore_c_api.h` and writes
the source URL and SHA-256 to `header-source.txt`.

Gradle compiles the plugin against `com.google.ar:core:1.56.0`.
The export plugin adds that same Maven dependency to the application, which
packages the ARCore classes, manifest and shim. The plugin AAR does not contain a
second copy of `libarcore_sdk_c.so`. Native code loads it with `dlopen`.
