#!/usr/bin/env python3
import argparse
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tempfile
import time
import uuid


BUNDLE = "org.doublesymmetry.pocketar.demo"
STEPS = {"arkit-session", "camera-image", "projection-form", "display-transform", "composite", "packet-retention", "lifecycle", "tracking"}


class Device:
    def __init__(self, udid, output, bundle=BUNDLE):
        self.udid, self.output, self.bundle = udid, Path(output), bundle
        self.output.mkdir(parents=True, exist_ok=True)

    def call(self, arguments):
        with tempfile.TemporaryDirectory(dir=self.output) as directory:
            report = Path(directory) / "result.json"
            command = ["xcrun", "devicectl", "--timeout", "30", "--json-output", str(report), *arguments]
            subprocess.run(command, check=True, timeout=35, stdout=subprocess.DEVNULL)
            payload = json.loads(report.read_text())
            if payload.get("info", {}).get("outcome") != "success":
                raise RuntimeError(f"devicectl failed: {payload}")
            return payload.get("result", {})

    def command(self, *arguments):
        return ["device", *arguments, "--device", self.udid]

    def preflight(self):
        report = {"errors": {}}
        for query in ["ddiServices", "details", "lockState"]:
            try:
                report[query] = self.call(self.command("info", query))
            except (RuntimeError, OSError, ValueError, subprocess.SubprocessError) as error:
                report[query] = {}
                report["errors"][query] = str(error)
        details = report["details"]
        connection = details.get("connectionProperties", {})
        device = details.get("deviceProperties", {})
        def status(value, expected):
            return "unknown" if value is None else "pass" if value == expected else "fail"
        version = str(device.get("osVersionNumber", "")).split(".")[0]
        checks = {"pairing": status(connection.get("pairingState"), "paired"),
                  "tunnel": status(connection.get("tunnelState"), "connected"),
                  "Developer Mode": status(device.get("developerModeStatus"), "enabled"),
                  "DDI": "fail" if "ddiServices" in report["errors"] else status(device.get("ddiServicesAvailable"), True),
                  "iOS 17+": status(int(version) >= 17 if version.isdigit() else None, True),
                  "unlocked": "unknown"}
        report["checks"] = checks
        (self.output / "preflight.json").write_text(json.dumps(report, indent=2))
        print("iOS preflight: " + ", ".join(name + "=" + state for name, state in checks.items()))
        failures = [name for name, state in checks.items() if name != "unlocked" and state != "pass"]
        if failures:
            raise RuntimeError("Preflight failed: " + ", ".join(failures))

    def install(self, app):
        return self.call(self.command("install", "app") + [str(app)])

    def launch(self, arguments):
        if len(arguments) > 55 or any("\0" in arg for arg in arguments):
            raise ValueError("App arguments exceed Godot's safe argument budget or contain NUL")
        environment = json.dumps({"POCKETAR_ARGS": json.dumps(arguments), "OS_ACTIVITY_DT_MODE": "enable"})
        result = self.call(self.command("process", "launch") + ["--terminate-existing", "--environment-variables", environment, self.bundle, "--", *arguments])
        (self.output / "launch.json").write_text(json.dumps(result, indent=2))
        pid = result.get("process", {}).get("processIdentifier")
        if not isinstance(pid, int) or pid <= 0:
            raise RuntimeError("Launch returned no process identifier")
        return pid

    def terminate(self, pid):
        def running():
            processes = self.call(self.command("info", "processes")).get("runningProcesses", [])
            return any(process.get("processIdentifier") == pid for process in processes)
        if not running():
            return False
        try:
            self.call(self.command("process", "terminate") + ["--pid", str(pid)])
        except (RuntimeError, OSError, subprocess.SubprocessError):
            if running():
                raise
            return True
        deadline = time.monotonic() + 5
        while True:
            if not running():
                return True
            if time.monotonic() >= deadline:
                self.call(self.command("process", "terminate") + ["--pid", str(pid), "--kill"])
                return True
            time.sleep(0.5)

    def pull(self, source, destination):
        self.call(self.command("copy", "from") + ["--domain-type", "appDataContainer", "--domain-identifier", self.bundle, "--user", "mobile", "--source", source, "--destination", str(destination)])

    def run_log(self, source, marker):
        selected = None
        for candidate, name in [(source, "requested.log"), ("Documents/logs/godot.log", "default.log")]:
            destination = self.output / name
            destination.unlink(missing_ok=True)
            try:
                self.pull(candidate, destination)
            except (RuntimeError, OSError, subprocess.SubprocessError):
                continue
            if destination.is_file():
                data = destination.read_text(errors="replace")
                if marker in data.splitlines() and selected is None:
                    selected = data
        return selected

    def cleanup(self, token, capture):
        pid = self.launch(["-harness-cleanup", token, "-harness-out", capture])
        deadline = time.monotonic() + 30
        destination = self.output / "cleanup.log"
        try:
            while time.monotonic() < deadline:
                destination.unlink(missing_ok=True)
                try:
                    self.pull("Documents/logs/godot.log", destination)
                except (RuntimeError, OSError, subprocess.SubprocessError):
                    time.sleep(0.5)
                    continue
                data = destination.read_text(errors="replace")
                if "HARNESS CLEANUP " + token + " OK" in data.splitlines():
                    return
                if "HARNESS CLEANUP " + token + " FAIL" in data:
                    raise RuntimeError("App could not remove this run's files; see cleanup.log")
                time.sleep(0.5)
            raise RuntimeError("Timed out waiting for iOS cleanup confirmation")
        finally:
            self.terminate(pid)

    def captures(self, source, destination):
        listing = self.call(self.command("info", "files") + ["--domain-type", "appDataContainer", "--domain-identifier", self.bundle, "--username", "mobile", "--subdirectory", source, "--no-recurse"])
        destination.mkdir(parents=True, exist_ok=True)
        for entry in listing.get("files", []):
            name = entry.get("name", "")
            if not name or PurePosixPath(name).name != name or name in {".", ".."}:
                raise ValueError("Unsafe capture filename")
            self.pull(source + "/" + name, destination / name)


def harness_options(arguments, token):
    values = {"-harness-duration", "-harness-out", "-harness-steps"}
    options = {}
    index = 0
    while index < len(arguments):
        key = arguments[index]
        if key in options or key not in values:
            raise ValueError("Unknown or duplicate iOS harness option: " + key)
        index += 1
        if index == len(arguments):
            raise ValueError(key + " needs a value")
        options[key] = arguments[index]
        index += 1
    duration = float(options.get("-harness-duration", "60"))
    if not math.isfinite(duration) or duration <= 0:
        raise ValueError("Duration must be finite and positive")
    required = set(str(options.get("-harness-steps", ",".join(sorted(STEPS)))).split(","))
    if not required or not required <= STEPS:
        raise ValueError("Unknown or empty ARKit harness steps")
    required.add("arkit-session")
    prefix = str(options.get("-harness-out", "user://harness"))
    if not re.fullmatch(r"user://[A-Za-z0-9_-]+", prefix):
        raise ValueError("iOS capture output must be one user:// directory")
    capture = prefix + "-" + token
    log = "harness-" + token + ".log"
    args = ["--log-file", "user://" + log, "-harness-ar", *arguments]
    if "-harness-out" in options:
        args[args.index("-harness-out") + 1] = capture
    else:
        args += ["-harness-out", capture]
    args += ["-harness-run-id", token]
    return args, "Documents/" + log, "Documents/" + capture[7:], required, duration + 180


def verify_log(text, required):
    results = re.findall(r"^HARNESS RESULT (PASS|FAIL)\s*$", text, re.MULTILINE)
    steps = re.findall(r"^HARNESS STEP ([a-z-]+) (OK|FAIL|INFO)\b", text, re.MULTILINE)
    missing = required - {name for name, _ in steps}
    failed = [name for name, status in steps if status == "FAIL"]
    if results != ["PASS"] or missing or failed or "SCRIPT ERROR:" in text or "Shader compilation failed" in text:
        raise RuntimeError(f"Harness verification failed: results={results}, missing={sorted(missing)}, failed={failed}")


def harness(device, arguments):
    token = uuid.uuid4().hex
    args, log_source, capture_source, required, default_timeout = harness_options(arguments, token)
    marker = "HARNESS RUN " + token
    timeout = float(os.environ.get("HARNESS_TIMEOUT", default_timeout))
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("HARNESS_TIMEOUT must be finite and positive")
    log = device.output / "latest.log"
    log.write_text("")
    captures = device.output / "captures"
    if captures.exists():
        shutil.rmtree(captures)
    device.preflight()
    pid = device.launch(args)
    deadline = time.monotonic() + timeout
    finished = False
    errors = []
    try:
        while time.monotonic() < deadline:
            data = device.run_log(log_source, marker)
            if data is not None:
                log.write_text(data)
                if re.search(r"^HARNESS RESULT (PASS|FAIL)\s*$", data, re.MULTILINE):
                    finished = True
                    break
            time.sleep(1)
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        errors.append(str(error))
    finally:
        try:
            if device.terminate(pid) is False:
                errors.append("App exited before host termination")
        except (RuntimeError, OSError, subprocess.SubprocessError) as error:
            errors.append("Termination: " + str(error))
    data = device.run_log(log_source, marker)
    if data is None:
        errors.append("Could not retrieve an iOS log containing this run's marker")
    else:
        log.write_text(data)
    captures_pulled = False
    try:
        device.captures(capture_source, captures)
        captures_pulled = True
    except (RuntimeError, OSError, ValueError, subprocess.SubprocessError) as error:
        errors.append("Captures: " + str(error))
    if data is not None and captures_pulled:
        try:
            device.cleanup(token, "user://" + capture_source.removeprefix("Documents/"))
        except (RuntimeError, OSError, subprocess.SubprocessError) as error:
            errors.append("Cleanup: " + str(error))
    text = log.read_text(errors="replace") if log.exists() else ""
    print(text, end="")
    if errors:
        raise RuntimeError("; ".join(errors))
    if not finished:
        raise RuntimeError("iOS harness timed out")
    verify_log(text, required)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", default=os.environ.get("IOS_DEVICE") or os.environ.get("IOS_UDID"))
    parser.add_argument("--output", default="build/harness/ios-ar")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("preflight")
    sub.add_parser("install").add_argument("app")
    sub.add_parser("launch").add_argument("arguments", nargs=argparse.REMAINDER)
    sub.add_parser("terminate").add_argument("pid", type=int)
    pull = sub.add_parser("pull")
    pull.add_argument("source")
    pull.add_argument("destination")
    sub.add_parser("harness").add_argument("arguments", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not args.device:
        parser.error("Set IOS_DEVICE (or IOS_UDID), or pass --device")
    device = Device(args.device, args.output)
    if args.command in {"launch", "harness"}:
        arguments = args.arguments[1:] if args.arguments[:1] == ["--"] else args.arguments
        if args.command == "harness":
            harness(device, arguments)
        else:
            device.preflight()
            print(device.launch(arguments))
    elif args.command == "install":
        device.preflight()
        device.install(args.app)
    elif args.command == "pull":
        device.pull(args.source, args.destination)
    elif args.command == "terminate":
        device.terminate(args.pid)
    else:
        device.preflight()


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, OSError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
