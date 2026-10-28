Configure `ADB` and `ANDROID_SERIAL` using [Android diagnostics](android.md) before the runs below.

## ARCore GPU camera path

The zero-copy Vulkan path needs the patched `doublesymmetry/godot` 4.8-dev build
and matching templates until `ExternalTexture.submit_hardware_buffer` ships upstream.
The extension still builds against godot-cpp 4.7: ClassDB detects ExternalTexture
and resolves `SUPPORTS_ANDROID_HARDWARE_BUFFER` by name before selecting a GPU
config. The dynamic call supplies buffer, color model, range, crop and completion.
`OK` means queued; each session generation waits for its first
`COMPLETION_COMPLETED` before enabling GPU presentation.
Stock 4.7.2 and devices without the feature use CPU immediately. A rejected
submission or a failed completion before activation restores CPU config selection
through a session reset. Hardware-buffer errors before activation also fall back;
`NOT_YET_AVAILABLE` gets 30 consecutive attempts. After activation, missing buffers
drop frames and failed completions are counted. `camera_path` and
`camera_path_reason` report the choice. Callbacks from old generations are ignored.
The engine keeps each completion until its GPU frame finishes, which at shutdown is
after the interface has been freed, and godot-cpp's `callable_mp` dereferences its
object to check validity. Completions are therefore static callables bound to the
interface's instance ID; never hand the engine a `callable_mp` it may keep.

GPU selection prefers the largest GPU texture under the existing camera filter,
then the smallest CPU image: a CPU image larger than ARCore's own 640x480 tracking
stream adds a third camera stream nothing reads. An explicit config index still wins. Each new frame submits its borrowed hardware
buffer before the next ARCore update; the engine retains it through GPU completion.
One stable `rgb_texture` replaces Y/CbCr textures, with the display transform
mapping into `TEXTURE_NORMALIZED`. Buffers larger than ARCore's texture dimensions
are cropped to that size at the origin. RGB
hardware buffers use `COLOR_MODEL_RGB`. Ordinary GPU operation never acquires a CPU
image; GPU validation acquires its CPU comparison image explicitly.

The explicit YUV color model stays BT.601 full, matching Camera2's YUV_420_888
(JFIF) definition and the CPU camera shader; `-harness-gpu-validate` runs the
`gpu-vs-cpu` check in `harness/ar/ar_harness.gd` and saves details in `diagnostics.json`.

Both camera paths report RenderingServer frame setup CPU p50/p95; keep validation
runs separate from throughput measurements because synchronous readback adds stalls.

After building the AAR/APK with patched templates and checking/unlocking the
test phone, use the host `ADB` and `ANDROID_SERIAL` described above:

```bash
tools/harness.sh android-ar -harness-duration 60 -harness-gpu-validate
tools/harness.sh android-ar -harness-duration 60
tools/harness.sh android-ar -harness-duration 60 -harness-camera-path cpu
```

Requested validation fails on a CPU
fallback; ordinary CPU runs report `gpu-vs-cpu INFO`.
