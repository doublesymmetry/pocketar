#!/usr/bin/env python3
import os
from pathlib import Path
import re
import selectors
import subprocess
import sys
import time


def main():
    args = sys.argv[1:]
    timeout = float(os.environ.get("HARNESS_TIMEOUT", "300"))
    output = Path("build/harness")
    output.mkdir(parents=True, exist_ok=True)
    if args[:1] == ["--android"]:
        adb, serial = args[1:]
        device = [adb, "-s", serial]
        package = "org.doublesymmetry.handheldar.demo"
        subprocess.run(device + ["shell", "am", "force-stop", package], check=True)
        launch = device + ["shell", "am", "start", "-W", "-n", package + "/com.godot.game.GodotAppLauncher"]
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
                    if b"SCRIPT ERROR:" in line or b"Shader compilation failed" in line:
                        engine_error = True
        timed_out = time.monotonic() >= deadline and not results
        if not launch and process.poll() is None and time.monotonic() < deadline:
            try:
                process.wait(timeout=min(5, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                pass
        exit_code = process.poll()
        if launch and results:
            capture = subprocess.run(device + ["exec-out", "run-as", package, "tar", "-cf", "-", "files"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if capture.returncode == 0:
                (output / "android-userdata.tar").write_bytes(capture.stdout)
            else:
                sys.stderr.write("Could not retrieve debug app captures: " + capture.stderr.decode(errors="replace"))
        bad_exit = exit_code not in (None, 0) if launch else exit_code != 0
        if timed_out or results != [b"PASS"] or engine_error or bad_exit:
            sys.stderr.write(f"Harness verification failed: results={results}, exit={exit_code}, timeout={timed_out}, script_error={engine_error}\n")
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
