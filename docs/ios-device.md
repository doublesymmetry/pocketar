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
python3 tools/ios-device.py preflight
python3 tools/ios-device.py install /path/to/signed.app
python3 tools/ios-device.py launch -- -harness-ar -harness-duration 60
python3 tools/ios-device.py terminate <pid>
python3 tools/ios-device.py pull Documents/logs/godot.log /tmp/ios.log
tools/harness.sh ios-ar -harness-duration 60
```

Installation needs an already signed `.app`; these commands do not build or sign
one. Installation replaces the app without uninstalling it. After the first install,
launch the app once and tap **Allow** on the camera prompt before starting an automated
run. An XCUITest alert handler can perform that step when available. If permission
was denied, enable camera access in Settings before retrying. Launch prints its PID
and saves `launch.json`. App flags follow the devicectl `--` separator and are also
sent as a JSON array of strings in `POCKETAR_ARGS`.

The `ios-ar` lane runs the ARKit checks; `ios` runs the synthetic rendering suite
and uses afcclient/idevicedebug 1.4.0 for older devices, with a UDID in
`IOS_DEVICE`; `IOS_VERSION` overrides OS discovery. Both launch detached
and pull `Documents/harness-<id>.log` and `Documents/logs/godot.log`. Only a file
containing `HARNESS RUN <id>` can supply results, so a forwarded devicectl separator
or a stale log cannot silently select another run. The host terminates the PID,
pulls the final log and captures, then requires exactly one `HARNESS RESULT PASS`,
all required ARKit steps for `ios-ar`, no failed steps and no
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
`HARNESS_TIMEOUT` overrides the default duration plus 180 seconds. The harness treats
an app that vanished before host termination as a failed run, and still attempts
the final log/capture pulls if termination fails.

Offline tests cover transport construction and log verification. Actual CoreDevice
JSON fields, file-log flushing, copy behavior and device lifecycle ordering still
need a signed-device run.

Enable `pocketar/ar_required` to require ARKit hardware in the exported app.
