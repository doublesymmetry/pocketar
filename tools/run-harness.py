#!/usr/bin/env python3
import os
import importlib.util
import json
import math
from pathlib import Path
import re
import selectors
import shlex
import shutil
import subprocess
import sys
import time
import uuid


PACKAGE = "org.doublesymmetry.handheldar.demo"
AR_STEPS = {"arcore-session", "no-gl", "camera-image", "projection-form", "display-transform", "composite", "packet-retention", "lifecycle", "tracking"}
IOS_SPEC = importlib.util.spec_from_file_location("ios_device", Path(__file__).with_name("ios-device.py"))
IOS_DEVICE = importlib.util.module_from_spec(IOS_SPEC)
IOS_SPEC.loader.exec_module(IOS_DEVICE)


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


def android_process_failure(device, pid):
    try:
        status = subprocess.run(device + ["shell", "pidof", PACKAGE],
                                capture_output=True, text=True, timeout=5)
    except subprocess.TimeoutExpired:
        return "Android process check failed: pidof timed out"
    if status.returncode == 1 and not status.stdout.strip():
        return f"Android app exited before a harness result: {PACKAGE} (pid {pid}) is no longer running"
    if status.returncode != 0:
        return f"Android process check failed: pidof exited {status.returncode}: {status.stderr.strip()}"
    if pid not in status.stdout.split():
        return f"Android app process disappeared or restarted: {PACKAGE} (pid {pid})"
    return None


def parse_log(data):
    results = re.findall(rb"HARNESS RESULT (PASS|FAIL)\s*$", data, re.MULTILINE)
    steps = [(name.decode(), status.decode()) for name, status in re.findall(
        rb"HARNESS STEP ([a-z-]+) (OK|FAIL|INFO)\b", data)]
    engine_error = b"SCRIPT ERROR:" in data or b"Shader compilation failed" in data
    return results, steps, engine_error


def verify_log(results, steps, engine_error, timed_out, bad_exit, required_steps=()):
    missing_steps = set(required_steps) - {name for name, _ in steps}
    failed_steps = [name for name, status in steps if status == "FAIL"]
    if timed_out or results != [b"PASS"] or engine_error or bad_exit or missing_steps or failed_steps:
        sys.stderr.write(f"Harness verification failed: results={results}, bad_exit={bad_exit}, timeout={timed_out}, script_error={engine_error}, missing_steps={sorted(missing_steps)}, failed_steps={failed_steps}\n")
        return 1
    return 0


def ios_major_version(device, output):
    version = os.environ.get("IOS_VERSION")
    if not version:
        if shutil.which("ideviceinfo"):
            version = subprocess.check_output(["ideviceinfo", "-u", device, "-k", "ProductVersion"], text=True, timeout=30).strip()
        else:
            details = output / "device.json"
            subprocess.run(["xcrun", "devicectl", "device", "info", "details", "--device", device,
                            "--json-output", str(details)], check=True, timeout=30)
            version = json.loads(details.read_text())["result"]["deviceProperties"]["osVersionNumber"]
    return int(version.split(".")[0])


def ios_launch(device, args, output, legacy, log_path="user://logs/harness.log"):
    user_args = ([] if "-harness-cleanup" in args else ["--harness"]) + args
    engine_args = ["--log-file", log_path, "--"] + user_args
    if legacy:
        return ["idevicedebug", "-u", device, "--detach", "--", "run", PACKAGE] + engine_args
    return ["xcrun", "devicectl", "device", "process", "launch", "--device", device,
            "--terminate-existing", "--json-output", str(output / "launch.json"),
            "--environment-variables", json.dumps({"HANDHELD_AR_ARGS": json.dumps(user_args)}), PACKAGE, "--"] + engine_args


def ios_pull_log(device, destination, legacy, source="Documents/logs/harness.log"):
    if legacy:
        return ["afcclient", "-u", device, "--container", PACKAGE, "get", "/" + source, str(destination)]
    return ["xcrun", "devicectl", "device", "copy", "from", "--device", device,
            "--domain-type", "appDataContainer", "--domain-identifier", PACKAGE,
            "--source", source, "--destination", str(destination)]


class IOSHarnessDevice(IOS_DEVICE.Device):
    def __init__(self, device, output, legacy):
        super().__init__(device, output)
        self.legacy = legacy

    def launch(self, arguments):
        log_path = "user://logs/godot.log"
        if "-harness-run-id" in arguments:
            log_path = "user://harness-" + arguments[arguments.index("-harness-run-id") + 1] + ".log"
        command = ios_launch(self.udid, arguments, self.output, self.legacy, log_path)
        with (self.output / "launch.log").open("ab") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
        if self.legacy:
            return None
        return json.loads((self.output / "launch.json").read_text())["result"]["process"]["processIdentifier"]

    def terminate(self, pid):
        command = ["idevicedebug", "-u", self.udid, "kill", PACKAGE] if self.legacy else [
            "xcrun", "devicectl", "device", "process", "terminate", "--device", self.udid, "--pid", str(pid)]
        with (self.output / "launch.log").open("ab") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=30)
        return True

    def pull(self, source, destination):
        with (self.output / "launch.log").open("ab") as log:
            subprocess.run(ios_pull_log(self.udid, destination, self.legacy, source),
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=30)

    def captures(self, source, destination):
        if not self.legacy:
            return super().captures(source, destination)
        with (self.output / "launch.log").open("ab") as log:
            subprocess.run(["afcclient", "-u", self.udid, "--container", PACKAGE, "get", "-r", "/" + source, str(destination)],
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)


def run_ios(device, args, output, timeout):
    if "-harness-ar" in args:
        raise ValueError("Use tools/harness.sh ios-ar for the ARKit harness")
    if any(option in args for option in ["--log-file", "-harness-run-id", "-harness-cleanup"]):
        raise ValueError("The iOS runner owns its log, run ID and cleanup arguments")
    args = list(args)
    run_id = uuid.uuid4().hex
    prefix = "user://harness"
    if "-harness-out" in args:
        index = args.index("-harness-out")
        if index + 1 == len(args):
            raise ValueError("-harness-out needs a user:// directory prefix")
        prefix = args[index + 1]
    if not re.fullmatch(r"user://[A-Za-z0-9_-]+", prefix):
        raise ValueError("iOS capture output must be one user:// directory prefix")
    capture = prefix + "-" + run_id
    if "-harness-out" in args:
        args[args.index("-harness-out") + 1] = capture
    else:
        args += ["-harness-out", capture]
    args += ["-harness-run-id", run_id]
    log_source = "Documents/harness-" + run_id + ".log"
    marker = "HARNESS RUN " + run_id
    transport = IOSHarnessDevice(device, output, ios_major_version(device, output) < 17)
    log = output / "latest.log"
    log.write_text("")
    captures = output / "captures"
    if captures.exists():
        shutil.rmtree(captures)
    pid = transport.launch(args)
    results, steps, engine_error = [], [], False
    errors = []
    printed = b""
    deadline = time.monotonic() + timeout
    try:
        while time.monotonic() < deadline:
            text = transport.run_log(log_source, marker)
            if text is not None:
                data = text.encode()
                log.write_bytes(data)
                sys.stdout.buffer.write(data[len(printed):] if data.startswith(printed) else data)
                sys.stdout.buffer.flush()
                printed = data
                results, steps, engine_error = parse_log(data)
                if results:
                    break
            time.sleep(min(1, max(0, deadline - time.monotonic())))
    finally:
        try:
            transport.terminate(pid)
        except (RuntimeError, OSError, subprocess.SubprocessError) as error:
            errors.append("Termination: " + str(error))
    timed_out = not results
    text = transport.run_log(log_source, marker)
    if text is None:
        errors.append("Could not retrieve an iOS log containing this run's marker")
    else:
        data = text.encode()
        log.write_bytes(data)
        sys.stdout.buffer.write(data[len(printed):] if data.startswith(printed) else data)
        sys.stdout.buffer.flush()
        results, steps, engine_error = parse_log(data)
    captures_pulled = False
    try:
        transport.captures("Documents/" + capture[7:], captures)
        captures_pulled = True
    except (RuntimeError, OSError, ValueError, subprocess.SubprocessError) as error:
        errors.append("Captures: " + str(error))
    if text is not None and captures_pulled:
        try:
            transport.cleanup(run_id, capture)
        except (RuntimeError, OSError, subprocess.SubprocessError) as error:
            errors.append("Cleanup: " + str(error))
    if errors:
        sys.stderr.write("; ".join(errors) + "\n")
    return verify_log(results, steps, engine_error, timed_out, bool(errors))


def main():
    args = sys.argv[1:]
    android = args[:1] in (["--android"], ["--android-ar"])
    ar = args[:1] == ["--android-ar"]
    ios = args[:1] == ["--ios"]
    harness_args = args[3:] if android else args[2:] if ios else args
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
    platform = "android-ar" if ar else "android" if android else "ios" if ios else "desktop"
    output = Path("build/harness") / platform
    output.mkdir(parents=True, exist_ok=True)
    (output / "latest.log").write_bytes(b"")
    if ios:
        return run_ios(args[1], harness_args, output, timeout)
    if android:
        adb, serial = args[1:3]
        device = [adb, "-s", serial]
        package = PACKAGE
        subprocess.run(device + ["shell", "am", "force-stop", package], check=True)
        subprocess.run(device + ["shell", "run-as", package, "rm", "-rf", "files/harness"], check=True)
        launch = android_launch(device, (["-harness-ar"] if ar else ["-harness"]) + harness_args)
        subprocess.run(launch, check=True)
        status = subprocess.run(device + ["shell", "pidof", "-s", package], capture_output=True, text=True, timeout=5)
        pid = status.stdout.strip()
        if status.returncode != 0 or not pid.isdigit():
            raise SystemExit("Android app exited or failed to start: no running process for " + package)
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
    next_pid_check = time.monotonic() + 1
    app_failure = None
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
    try:
        selector = selectors.DefaultSelector()
        selector.register(process.stdout, selectors.EVENT_READ)
        with (output / "latest.log").open("wb") as log:
            while time.monotonic() < deadline:
                if launch and not results and not app_failure and time.monotonic() >= next_pid_check:
                    app_failure = android_process_failure(device, pid)
                    next_pid_check = time.monotonic() + 1
                    if app_failure:
                        # Drain logcat so a normal quit immediately after the result can still pass.
                        deadline = min(deadline, time.monotonic() + 1)
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
        timed_out = time.monotonic() >= deadline and not results and not app_failure
        if not launch and process.poll() is None and time.monotonic() < deadline:
            try:
                process.wait(timeout=min(5, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                pass
        exit_code = process.poll()
        bad_exit = exit_code not in (None, 0) if launch else exit_code != 0
        if app_failure and not results:
            sys.stderr.write(app_failure + "\n")
            bad_exit = True
        if launch:
            try:
                capture = subprocess.run(device + ["exec-out", "run-as", package, "tar", "-cf", "-", "files"],
                                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
                if capture.returncode == 0:
                    (output / "android-userdata.tar").write_bytes(capture.stdout)
                else:
                    sys.stderr.write("Could not retrieve debug app captures: " + capture.stderr.decode(errors="replace"))
            except subprocess.TimeoutExpired:
                sys.stderr.write("Could not retrieve debug app captures: adb timed out\n")
        return verify_log(results, steps, engine_error, timed_out, bad_exit, required_steps)
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
