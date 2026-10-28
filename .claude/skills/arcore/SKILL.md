---
name: arcore
description: Implement or validate PocketAR on Android, including ARCore session/JNI lifecycle, camera configuration, CPU uploads, Vulkan hardware buffers, APK export, datasets and device diagnostics. Load for changes under src/platform/android or android and for Android harness runs.
---

# ARCore

Follow the [core requirements](../../../AGENTS.md) and shared tracking-reason table.
For Android builds, exports or phone runs, read [Android diagnostics](references/android.md).
For GPU selection, submission, completion or fallback, read [the GPU camera path](references/gpu-camera.md).
For intent extras, read [application arguments](../scripted-sessions/references/arguments.md).
For plane-finding, tracking, retention or composition checks, read the
[shared harness guidance](../scripted-sessions/references/harness.md).

`ARCoreInterface` implements the same packet/presentation methods as the synthetic
interface. `ARView.backend` defaults to synthetic;
`harness/ar/ar_demo.tscn` chooses ARCore on Android and synthetic on desktop. The
native interface name is `PocketARCore`. The Kotlin singleton is
`PocketARPlugin`; `PocketAR` remains the shared native raycast API.

ARCore uses SDK 1.56.0, requires Google Play Services for AR 1.48 or newer, and logs
its installed APK version. Kotlin checks availability asynchronously without prompts
at startup. ARCoreInterface initialization calls `PocketARPlugin.requestSession()`;
games can also call it explicitly for setup or retry. Installation and CAMERA
permission requests run on Android's UI thread only after that request, including
on subsequent resumes.
The tap-to-place sample creates its ARView from Start AR and offers Retry setup;
repeated camera denials may require app settings. Native code starts only after the JNI
bridge reports installation and permission. CAMERA permission denial is exposed,
including denial after an app-settings change. The harness exports as AR Optional;
set the Android export option `pocketar/ar_required` to add the AR hardware
requirement and change the ARCore manifest metadata to required.

All session/frame/camera/image/plane/query calls belong to Godot's main
thread (VkThread under Vulkan). Kotlin queues lifecycle commands with
`runOnRenderThread`; it never calls the session itself. Application-pause
notifications provide an idempotent backstop. Effective Safe rendering mode is
required and checked through the render-thread API. JNI stores the JavaVM and
application Context global references, never a JNIEnv. Session destroy
is the exception: a JVM-attached worker owns the bare paused session after every
other native reference has been released. Shutdown never waits for destroy.

The config explicitly uses EXPOSE_HARDWARE_BUFFER, LATEST_CAMERA_IMAGE, plane
discovery disabled by default, auto focus and disabled light estimation. Depth, instant
placement, cloud/geospatial anchors, semantics, faces, stabilization and streetscape
geometry keep the SDK's disabled defaults. Camera selection follows
[session controls](../../../docs/api.md#session-controls).
Do not enable Godot CameraServer feeds while an ARSession runs: both open Camera2.

A new timestamp yields one copied packet. On the CPU path, YUV_420_888 is repacked using each
plane's strides and released in the same tick. Missing images drop the whole
packet. CPU R8/RG8 textures keep the existing shader's `.rg` chroma convention and
full-range BT.601 conversion. The ring contains Q+3 pairs, where Q is
RenderingDevice.get_frame_delay(). Only `_end_frame` counts completed RD swaps.
A pair is reused after Q swaps since its last protected use, once no current,
presented or ARView-applied packet uses it. Script-held Refs do not block reuse;
stale packets can therefore show newer camera pixels. The ring is library-owned CPU-upload storage, with no AHardwareBuffer
import or Metal retain workaround.

`reset()` immediately increments the session generation, clears presentation and
removes every old plane tracker. It pauses/releases the old session and hands
only ArSession_destroy to a worker, then creates/configures/resumes a new session.
CAMERA_NOT_AVAILABLE on resume retries with 100ms–2s backoff. Generations and query
IDs use process-wide unique IDs so interface recreation cannot revive a stale handle.

## Plane finding

ARCore changes only plane finding on the existing session config and calls
`ArSession_configure`, without pausing, recreating the session or selecting a
camera config. It reads the config back with `ArSession_getConfig` and
`ArConfig_getPlaneFindingMode`. A rejected change leaves the previous mode active.
The vendored 1.56.0 C header says only that disabled turns off plane detection;
it does not specify whether existing planes stop updating or change tracking state.
PocketAR keeps existing native plane handles and copies their reported state;
native STOPPED planes are still removed. Disabling does not clear the tracker set.

Diagnostics report the selected `plane_finding_mode` and read-back
`effective_plane_finding_mode`; the effective field is absent before a native config exists.

## Session state

- ARCore reports availability, install, permission and tracking states. A reset
  while the bridge is pending reports that pending state;
  a ready but suspended bridge reports PAUSED until resume can create a session.

ARCore poses require native TRACKING; see [session controls](../../../docs/api.md#session-controls).

## Camera upload settings

For ARCore CPU uploads, use
`rendering/rendering_device/staging_buffer/block_size_kb=4096` and
`rendering/rendering_device/staging_buffer/texture_upload_region_size_px=1024`.
A region must fit in one staging block. The harness project sets both; the library reads and
reports the effective values but never changes a game's settings. It warns once
per session start if the region is below 512 pixels.
