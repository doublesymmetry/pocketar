#!/usr/bin/env python3
import os
import math
from pathlib import Path
import re
import selectors
import shlex
import subprocess
import sys
import time


PACKAGE = "org.doublesymmetry.handheldar.demo"
AR_STEPS = {"arcore-session", "no-gl", "camera-image", "projection-form", "display-transform", "composite", "lifecycle", "tracking"}


def harness_timeout(args, ar):
    duration = 60.0
    if "-harness-duration" in args:
        index = args.index("-harness-duration")
        if index + 1 == len(args):
            raise ValueError("-harness-duration needs seconds")
        duration = float(args[index + 1])
        if not math.isfinite(duration) or duration <= 0:
            raise ValueError("-harness-duration must be a finite positive number")
    probe_grace = 25 if "-harness-probes" in args else 0
    return duration + 180 + probe_grace if ar else 300.0


def validate_harness_options(args):
    dataset_selected = False
    for index, option in enumerate(args):
        if option not in {"-harness-record", "-harness-playback", "-harness-camera-config"}:
            continue
        if index + 1 == len(args):
            raise ValueError(option + " needs a value")
        value = args[index + 1]
        if option == "-harness-camera-config":
            if not re.fullmatch(r"[0-9]+", value):
                raise ValueError("camera config must be a nonnegative integer")
        else:
            if dataset_selected or not re.fullmatch(r"[A-Za-z0-9_-][A-Za-z0-9_.-]*\.mp4", value):
                raise ValueError("choose one recording or playback .mp4 basename")
            dataset_selected = True


def android_launch(device, args):
    command = ["am", "start", "-W", "-n", PACKAGE + "/com.godot.game.GodotAppLauncher"]
    if args:
        extra = ",".join(arg.replace("\\", "\\\\").replace(",", "\\,") for arg in args)
        command += ["--esa", "handheld_ar_harness_args", extra]
    return device + ["shell", shlex.join(command)]


def main():
    args = sys.argv[1:]
    android = args[:1] in (["--android"], ["--android-ar"])
    ar = args[:1] == ["--android-ar"]
    harness_args = args[3:] if android else args
    if harness_args[:1] == ["--"]:
        harness_args = harness_args[1:]
    validate_harness_options(harness_args)
    timeout = float(os.environ.get("HARNESS_TIMEOUT", harness_timeout(harness_args, ar)))
    required_steps = AR_STEPS.copy() if ar else set()
    if ar and "-harness-steps" in harness_args:
        index = harness_args.index("-harness-steps")
        if index + 1 == len(harness_args):
            raise SystemExit("-harness-steps needs a comma-separated list")
        required_steps = set(harness_args[index + 1].split(","))
        if not required_steps or not required_steps <= AR_STEPS:
            raise SystemExit("Unknown or empty ARCore harness steps")
        required_steps.add("arcore-session")
    output = Path("build/harness")
    output.mkdir(parents=True, exist_ok=True)
    if android:
        adb, serial = args[1:3]
        device = [adb, "-s", serial]
        package = PACKAGE
        subprocess.run(device + ["shell", "am", "force-stop", package], check=True)
        launch = android_launch(device, (["-harness-ar"] if ar else ["-harness"]) + harness_args)
        subprocess.run(launch, check=True)
        pid = subprocess.check_output(device + ["shell", "pidof", "-s", package], text=True).strip()
        if not pid.isdigit():
            raise SystemExit("Android demo has no running process")
        command = device + ["logcat", "--pid=" + pid, "-v", "brief", "godot:V", "*:S"]
    else:
        command = args[1:] if args[:1] == ["--"] else args
        launch = None
    if not command:
        raise SystemExit("A harness command is required")
    results = []
    steps = []
    engine_error = False
    pending = b""
    deadline = time.monotonic() + timeout
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
    try:
        selector = selectors.DefaultSelector()
        selector.register(process.stdout, selectors.EVENT_READ)
        with (output / "latest.log").open("wb") as log:
            while time.monotonic() < deadline:
                events = selector.select(timeout=min(0.5, max(0, deadline - time.monotonic())))
                if not events:
                    if process.poll() is not None:
                        break
                    continue
                chunk = os.read(process.stdout.fileno(), 65536)
                if not chunk:
                    break
                log.write(chunk)
                log.flush()
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
                pending += chunk
                while b"\n" in pending:
                    line, pending = pending.split(b"\n", 1)
                    result = re.search(rb"HARNESS RESULT (PASS|FAIL)\s*$", line)
                    if result:
                        results.append(result.group(1))
                        deadline = min(deadline, time.monotonic() + (1 if launch else 5))
                    step = re.search(rb"HARNESS STEP ([a-z-]+) (OK|FAIL|INFO)\b", line)
                    if step:
                        steps.append((step.group(1).decode(), step.group(2).decode()))
                    if b"SCRIPT ERROR:" in line or b"Shader compilation failed" in line:
                        engine_error = True
        timed_out = time.monotonic() >= deadline and not results
        if not launch and process.poll() is None and time.monotonic() < deadline:
            try:
                process.wait(timeout=min(5, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                pass
        exit_code = process.poll()
        if launch:
            capture = subprocess.run(device + ["exec-out", "run-as", package, "tar", "-cf", "-", "files"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if capture.returncode == 0:
                (output / "android-userdata.tar").write_bytes(capture.stdout)
            else:
                sys.stderr.write("Could not retrieve debug app captures: " + capture.stderr.decode(errors="replace"))
        bad_exit = exit_code not in (None, 0) if launch else exit_code != 0
        missing_steps = required_steps - {name for name, _ in steps}
        failed_steps = [name for name, status in steps if status == "FAIL"]
        if timed_out or results != [b"PASS"] or engine_error or bad_exit or missing_steps or failed_steps:
            sys.stderr.write(f"Harness verification failed: results={results}, exit={exit_code}, timeout={timed_out}, script_error={engine_error}, missing_steps={sorted(missing_steps)}, failed_steps={failed_steps}\n")
            return 1
        return 0
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == "__main__":
    raise SystemExit(main())
