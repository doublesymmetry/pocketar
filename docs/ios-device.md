# iOS device commands

Run these from ci4's GUI session with Xcode selected and an iOS 17+ device paired,
unlocked, trusted and in Developer Mode. `IOS_DEVICE` or `IOS_UDID` selects the
device. The commands never change its lock, pairing or Developer Mode settings.
Preflight requests DDI services (which may mount the developer image), then checks
pairing, tunnel, Developer Mode, DDI availability and OS version. Every query runs
even if an earlier query fails. Responses, errors and each check's status go into
`build/harness/ios-ar/preflight.json`. Lock state remains `unknown` until the device's
response schema is verified; unlock the phone before running the harness.

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
one. Installation replaces the app without uninstalling it. After the first install,
launch the app once and tap **Allow** on the camera prompt before starting an automated
run. An XCUITest alert handler can perform that step when available. If permission
was denied, enable camera access in Settings before retrying. Launch prints its PID
and saves `launch.json`. App flags follow the devicectl `--` separator and are also
sent as a JSON array of strings in `POCKETAR_ARGS`. Commands have a 30-second
CoreDevice timeout and a 35-second subprocess limit.

The `ios-ar` lane runs the ARKit checks; `ios` runs the synthetic rendering suite
and retains its libimobiledevice transport for older devices. Both launch detached
and pull `Documents/harness-<id>.log` and `Documents/logs/godot.log`. Only a file
containing `HARNESS RUN <id>` can supply results, so a forwarded devicectl separator
or a stale log cannot silently select another run. The host terminates the PID,
pulls the final log and captures, then requires exactly one `HARNESS RESULT PASS`,
all eight ARKit steps for `ios-ar`, no failed steps and no
script or shader compilation errors. `-harness-steps` certifies only the selected
subset plus `arkit-session`. The app must enable iOS file logging; console output
is not used to determine success.

Each run gets a unique capture directory, `user://harness-<id>` by default.
`-harness-out user://name` selects the prefix `user://name-<id>`; nested paths and
paths outside `user://` are rejected. CoreDevice pulls capture files individually.
Host results go into `build/harness/ios-ar/latest.log` and `captures/`, or
`build/harness/ios/` for the synthetic lane. Each lane clears its previous host
captures before launch.

After both the final log and captures have been retrieved, the runner launches a
run-specific cleanup handler and waits for `HARNESS CLEANUP <id> OK` in the default
log. The handler removes only the run's token log and capture directory, then the
host terminates that process. Failed pulls preserve device files for recovery;
cleanup failure fails the run. The default Godot log remains available and uses
Godot's bounded rotation.
Cleanup accepts command-line requests in debug and release synthetic builds;
`POCKETAR_ARGS` remains debug-only.
`HARNESS_TIMEOUT` overrides the default duration plus 180 seconds. Termination
waits up to five seconds for the PID to disappear, then sends SIGKILL if needed.
An already absent PID makes manual termination a no-op. The harness treats an
app that vanished before host termination as a failed run, and still attempts
the final log/capture pulls if termination fails.

The eight default steps are `arkit-session`, `camera-image`, `projection-form`,
`display-transform`, `composite`, `packet-retention`, `tracking` and `lifecycle`. The tooling accepts
`-harness-probes` and `-harness-lifecycle`; it rejects Android dataset/config flags.
Offline tests cover transport construction and log verification. Actual CoreDevice
JSON fields, file-log flushing, copy behavior and device lifecycle ordering still
need a signed-device run.

On iOS, the export plugin links ARKit, Metal, CoreVideo and AVFoundation.
`privacy/camera_usage_description` stays editable and an empty value produces a
warning. `capabilities/additional` also stays editable. When
`pocketar/ar_required` is enabled, the plugin merges `arkit` into the generated
plist's existing capability array. User capabilities and camera descriptions are
preserved, including a manually requested `arkit` capability in optional mode.

Presets below iOS 16.0, or with malformed version strings, fail export validation.
Godot 4.7.2 has no export-plugin abort hook, so unsupported presets receive missing
debug and release template overrides plus the explicit iOS-version warning.
Supported presets keep their chosen templates. This guard does not change the
library's deployment target or certify the diagnostic iOS 15 build override.
