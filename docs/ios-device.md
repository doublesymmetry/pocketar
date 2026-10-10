# iOS device commands

Run these from ci4's GUI session with Xcode selected and an iOS 17+ device paired,
unlocked, trusted and in Developer Mode. `IOS_DEVICE` or `IOS_UDID` selects the
device. The commands never change its lock, pairing or Developer Mode settings.
Preflight requests DDI services (which may mount the developer image), then checks
pairing, tunnel, Developer Mode, DDI availability and OS version. It saves details
and the lock-state response in `build/harness/ios/preflight.json`.

```sh
export IOS_DEVICE=<udid>
tools/device.sh ios preflight
tools/device.sh ios install /path/to/signed.app
tools/device.sh ios launch -- -harness-ar -harness-duration 60
tools/device.sh ios terminate <pid>
tools/device.sh ios pull Documents/logs/godot.log /tmp/ios.log
tools/harness.sh ios-ar -harness-duration 60
```

Installation needs an already signed `.app`; these commands do not build or sign
one. Installation replaces the app without uninstalling it. Launch prints its PID
and saves `launch.json`. App flags follow the devicectl `--` separator and are also
sent as a JSON array of strings in `HANDHELD_AR_ARGS`. Commands have a 30-second
CoreDevice timeout and a 35-second subprocess limit.

The `ios-ar` lane launches detached and polls a unique `user://harness-<id>.log`.
The host terminates the PID, pulls the final log and captures, then requires
exactly one `HARNESS RESULT PASS`, all seven ARKit steps, no failed steps and no
script or shader compilation errors. `-harness-steps` certifies only the selected
subset plus `arkit-session`. The app must enable iOS file logging; console output
is not used to determine success.

Each run also gets a unique capture directory. `-harness-out user://name` overrides
it; nested paths and paths outside `user://` are rejected. Capture files are pulled
individually into `build/harness/ios/captures`. Keep the device directory flat.
`HARNESS_TIMEOUT` overrides the default duration plus 180 seconds. Termination
waits up to five seconds for the PID to disappear, then sends SIGKILL if needed.
An already absent PID makes manual termination a no-op. The harness treats an
app that vanished before host termination as a failed run, and still attempts
the final log/capture pulls if termination fails.

The seven default steps are `arkit-session`, `camera-image`, `projection-form`,
`display-transform`, `composite`, `lifecycle` and `tracking`. The tooling accepts
`-harness-probes` and `-harness-lifecycle`; it rejects Android dataset/config flags.
Offline tests cover transport construction and log verification. Actual CoreDevice
JSON fields, file-log flushing, copy behavior and device lifecycle ordering still
need a signed-device run. Preflight records unknown lock-state fields for diagnosis;
it rejects the known `isLocked` and `deviceIsLocked` boolean forms when true.

On iOS, the export plugin links ARKit, Metal, CoreVideo and AVFoundation. It fills
an empty `privacy/camera_usage_description` preset value and preserves a supplied
one. `handheld_ar/ar_required` adds or removes `arkit` in `capabilities/additional`
while preserving other capabilities. Both values flow through Godot's existing
plist keys; the plugin does not append duplicate keys.
