PocketAR is distributed under the MIT license in `LICENSE`. Its binaries include
godot-cpp, covered by `LICENSE.godot-cpp.md`.

The Android backend uses Google's ARCore SDK 1.56.0. The C API header in the source
repository is covered by Google's ARCore terms, not PocketAR's MIT license. The
export plugin supplies the runtime through Maven (`com.google.ar:core:1.56.0`).

Apps using ARCore must disclose Google's ARCore functionality in their terms and
state that its use is subject to Google's Terms of Service and Privacy Policy.
See the [ARCore Additional Terms of Service](https://developers.google.com/ar/develop/terms)
for these requirements and the applicable user privacy requirements.
