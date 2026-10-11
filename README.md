# PocketAR

MIT handheld AR for Godot 4.7.2, with ARKit on iOS and ARCore on Android behind
one Godot API. The demo selects the native backend on each phone.

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
