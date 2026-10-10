import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location(
    "run_harness", Path(__file__).parents[2] / "tools/run-harness.py"
)
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


def kotlin_unescape(argument):
    result = []
    index = 0
    while index < len(argument):
        character = argument[index]
        index += 1
        if character == "\\" and index < len(argument) and argument[index] in ("\\", ","):
            result.append(argument[index])
            index += 1
        else:
            result.append(character)
    return "".join(result)


class AndroidHarnessTest(unittest.TestCase):
    def test_camera_path_options(self):
        for value in ["cpu", "auto"]:
            RUNNER.validate_harness_options(["-harness-camera-path", value])
        RUNNER.validate_harness_options(["-harness-gpu-validate"])
        for args in [["-harness-camera-path"], ["-harness-camera-path", "gpu"],
                     ["-harness-camera-path", "cpu", "-harness-gpu-validate"]]:
            with self.subTest(args=args), self.assertRaises(ValueError):
                RUNNER.validate_harness_options(args)
        self.assertIn("gpu-vs-cpu", RUNNER.AR_STEPS)

    def test_ar_duration_extends_monitor_deadline(self):
        self.assertEqual(RUNNER.harness_timeout(["-harness-duration", "1800"], True), 1980)

    def test_optional_probes_have_time_to_finish_after_observation(self):
        self.assertEqual(RUNNER.harness_timeout(["-harness-duration", "1800", "-harness-probes"], True), 2005)

    def test_bad_duration_is_rejected_before_launch(self):
        for value in ["0", "-1", "nan", "inf", "abc"]:
            with self.subTest(value=value), self.assertRaises(ValueError):
                RUNNER.harness_timeout(["-harness-duration", value], True)

    def test_dataset_and_camera_options_reject_invalid_requests(self):
        for args in [
            ["-harness-record", "../room.mp4"],
            ["-harness-playback", "room.mov"],
            ["-harness-record", "room.mp4", "-harness-playback", "room.mp4"],
            ["-harness-camera-config", "-1"],
            ["-harness-camera-config", "1.5"],
            ["-harness-record"],
        ]:
            with self.subTest(args=args), self.assertRaises(ValueError):
                RUNNER.validate_harness_options(args)

    def test_dataset_and_camera_options_allow_separate_runs(self):
        RUNNER.validate_harness_options(["-harness-record", "room-1080p.mp4", "-harness-camera-config", "2"])
        RUNNER.validate_harness_options(["-harness-playback", "room-1080p.mp4", "-harness-probes"])

    def test_android_extra_preserves_space_comma_and_shell_metacharacters(self):
        command = RUNNER.android_launch(
            ["/tools/adb", "-s", "pixel"],
            ["-harness-ar", "-harness-steps", "no-gl,camera-image", "-harness-out", "user://camera captures;literal"],
        )
        self.assertEqual(command[:4], ["/tools/adb", "-s", "pixel", "shell"])
        remote_args = shlex.split(command[4])
        self.assertEqual(remote_args[-3:], [
            "--esa", "handheld_ar_harness_args",
            "-harness-ar,-harness-steps,no-gl\\,camera-image,-harness-out,user://camera captures;literal",
        ])

    def test_android_list_arguments_round_trip_through_am_and_plugin(self):
        arguments = ["-harness-ar", "-harness-steps", "a,b", "-harness-out", r"user://a\,b\folder"]
        extra = shlex.split(RUNNER.android_launch(["adb"], arguments)[-1])[-1]
        # am's negative lookbehind splits the array but preserves the escape bytes.
        received = re.split(r"(?<!\\),", extra)
        self.assertEqual([kotlin_unescape(value) for value in received], arguments)
        self.assertEqual(kotlin_unescape(r"\\,"), "\\,")
        self.assertEqual(kotlin_unescape("a\\q\\"), "a\\q\\")

    def test_process_check_distinguishes_exit_restart_and_transport_error(self):
        for code, stdout, stderr, cause in [
            (1, "", "", "exited before a harness result"),
            (0, "456\n", "", "disappeared or restarted"),
            (2, "", "device offline", "device offline"),
        ]:
            with self.subTest(cause=cause), patch.object(RUNNER.subprocess, "run", return_value=subprocess.CompletedProcess([], code, stdout, stderr)):
                self.assertIn(cause, RUNNER.android_process_failure(["adb"], "123"))
        with patch.object(RUNNER.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "123 456\n", "")):
            self.assertIsNone(RUNNER.android_process_failure(["adb"], "123"))


class HarnessMonitorTest(unittest.TestCase):
    def test_dead_android_app_stops_even_when_logcat_remains_running(self):
        for noisy, late_result in [(False, False), (True, False), (False, True)]:
            with self.subTest(noisy=noisy, late_result=late_result), tempfile.TemporaryDirectory() as directory:
                fake_adb = Path(directory) / "device-transport"
                fake_adb.write_text(
                    "#!" + sys.executable + "\n"
                    "import pathlib, sys, time\n"
                    "args = sys.argv[1:]\n"
                    "if 'pidof' in args:\n"
                    "    marker = pathlib.Path('launched')\n"
                    "    if marker.exists(): sys.exit(1)\n"
                    "    marker.touch(); print('123')\n"
                    "elif 'logcat' in args:\n"
                    + ("    time.sleep(1.2)\n"
                       "    print('HARNESS STEP arcore-session OK initialized')\n"
                       "    print('HARNESS STEP camera-image OK images=30')\n"
                       "    print('HARNESS RESULT PASS', flush=True)\n" if late_result else "")
                    + "    while True:\n"
                    + ("        print('ARCORE waiting', flush=True)\n" if noisy else "")
                    + "        time.sleep(0.05)\n"
                )
                fake_adb.chmod(0o755)
                result = subprocess.run(
                    [sys.executable, str(Path(RUNNER.__file__).resolve()), "--android-ar", str(fake_adb), "test-device", "-harness-steps", "camera-image"],
                    cwd=directory, env={**os.environ, "HARNESS_TIMEOUT": "60"},
                    text=True, capture_output=True, timeout=8,
                )
                self.assertEqual(result.returncode, 0 if late_result else 1, result.stderr)
                if not late_result:
                    self.assertIn("Android app exited before a harness result", result.stderr)
                    self.assertIn(RUNNER.PACKAGE, result.stderr)
                    self.assertIn("timeout=False", result.stderr)

    def run_monitor(self, script):
        with tempfile.TemporaryDirectory() as directory:
            return subprocess.run(
                [sys.executable, str(Path(RUNNER.__file__).resolve()), "--", sys.executable, "-c", script],
                cwd=directory,
                env={**os.environ, "HARNESS_TIMEOUT": "2"},
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=5,
            )

    def test_one_pass_and_clean_exit_succeeds(self):
        result = self.run_monitor("print('HARNESS STEP camera-image OK images=30'); print('HARNESS RESULT PASS')")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_failed_step_cannot_be_hidden_by_final_pass(self):
        result = self.run_monitor("print('HARNESS STEP camera-image FAIL images=0'); print('HARNESS RESULT PASS')")
        self.assertEqual(result.returncode, 1)
        self.assertIn("failed_steps=['camera-image']", result.stderr)

    def test_invalid_run_cannot_pass(self):
        scripts = [
            "print('no result')",
            "print('HARNESS RESULT PASS'); print('HARNESS RESULT PASS')",
            "print('SCRIPT ERROR: invalid call'); print('HARNESS RESULT PASS')",
            "print('HARNESS RESULT PASS'); raise SystemExit(3)",
        ]
        for script in scripts:
            with self.subTest(script=script):
                self.assertEqual(self.run_monitor(script).returncode, 1)

    def test_ar_launch_cannot_pass_with_synthetic_results(self):
        with tempfile.TemporaryDirectory() as directory:
            fake_adb = Path(directory) / "device-transport"
            fake_adb.write_text(
                "#!" + sys.executable + "\n"
                "import json, sys\n"
                "args = sys.argv[1:]\n"
                "with open('commands.jsonl', 'a') as log: log.write(json.dumps(args) + '\\n')\n"
                "if 'pidof' in args: print('123')\n"
                "elif 'logcat' in args:\n"
                "    print('HARNESS STEP loaded OK synthetic')\n"
                "    print('HARNESS RESULT PASS')\n"
            )
            fake_adb.chmod(0o755)
            result = subprocess.run(
                [sys.executable, str(Path(RUNNER.__file__).resolve()), "--android-ar", str(fake_adb), "test-device"],
                cwd=directory,
                env={**os.environ, "HARNESS_TIMEOUT": "2"},
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=5,
            )
            self.assertEqual(result.returncode, 1)
            self.assertIn("arcore-session", result.stderr)
            self.assertIn("camera-image", result.stderr)
            commands = [json.loads(line) for line in (Path(directory) / "commands.jsonl").read_text().splitlines()]
            self.assertEqual(commands[1][2:], ["shell", "run-as", RUNNER.PACKAGE, "rm", "-rf", "files/harness"])
            self.assertIn("am start", commands[2][-1])
            self.assertTrue((Path(directory) / "build/harness/android-ar/latest.log").is_file())
            self.assertFalse((Path(directory) / "build/harness/latest.log").exists())


class IOSHarnessTest(unittest.TestCase):
    def test_launch_separates_devicectl_engine_and_user_arguments(self):
        command = RUNNER.ios_launch("phone", ["-harness-steps", "projection"], Path("out"), False)
        self.assertEqual(command[command.index(RUNNER.PACKAGE):], [
            RUNNER.PACKAGE, "--", "--log-file", "user://logs/harness.log",
            "--", "--harness", "-harness-steps", "projection",
        ])
        environment = json.loads(command[command.index("--environment-variables") + 1])
        self.assertEqual(json.loads(environment["HANDHELD_AR_ARGS"]), ["--harness", "-harness-steps", "projection"])
        legacy = RUNNER.ios_launch("phone", ["-harness-steps", "projection"], Path("out"), True)
        self.assertEqual(legacy[:7], ["idevicedebug", "-u", "phone", "--detach", "--", "run", RUNNER.PACKAGE])
        self.assertEqual(legacy[7:], command[command.index(RUNNER.PACKAGE) + 2:])

    def test_copy_uses_app_container_on_both_transports(self):
        modern = RUNNER.ios_pull_log("phone", Path("out/pulled.log"), False)
        self.assertIn("appDataContainer", modern)
        self.assertEqual(modern[-4:], ["--source", "Documents/logs/harness.log", "--destination", "out/pulled.log"])
        self.assertEqual(RUNNER.ios_pull_log("phone", Path("out/pulled.log"), True), [
            "afcclient", "-u", "phone", "--container", RUNNER.PACKAGE,
            "get", "/Documents/logs/harness.log", "out/pulled.log",
        ])

    def run_device(self, output, content, legacy=False, fallback=False, stale=False, late_duplicate=False, termination_failure=False):
        calls = []
        token = ""
        cleanup = False
        stopped = False
        def transport(command, **kwargs):
            nonlocal token, cleanup, stopped
            calls.append(command)
            if "-harness-run-id" in command or "-harness-cleanup" in command:
                cleanup = "-harness-cleanup" in command
                key = "-harness-cleanup" if cleanup else "-harness-run-id"
                token = command[command.index(key) + 1]
                (output / "launch.json").write_text('{"result":{"process":{"processIdentifier":123}}}')
            elif "terminate" in command or "kill" in command:
                stopped = True
                if termination_failure:
                    raise subprocess.CalledProcessError(1, command)
            elif "files" in command:
                report = Path(command[command.index("--json-output") + 1])
                report.write_text(json.dumps({"info": {"outcome": "success"}, "result": {"files": [{"name": "screen.png"}]}}))
            elif "copy" in command or "get" in command:
                source = command[command.index("--source") + 1] if "--source" in command else command[-2]
                destination = Path(command[command.index("--destination") + 1] if "--destination" in command else command[-1])
                if source.endswith(".png") or "-r" in command:
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    if "-r" in command:
                        destination.mkdir(exist_ok=True)
                        destination = destination / "screen.png"
                    destination.write_bytes(b"new capture")
                elif cleanup:
                    destination.write_text("HARNESS CLEANUP " + token + " OK\n")
                else:
                    current = not stale and (not fallback or source.endswith("logs/godot.log"))
                    data = "HARNESS RUN " + (token if current else "old") + "\n" + content
                    if stopped and late_duplicate:
                        data += "HARNESS RESULT PASS\n"
                    destination.write_text(data)
                if "--json-output" in command:
                    Path(command[command.index("--json-output") + 1]).write_text('{"info":{"outcome":"success"},"result":{}}')
            return subprocess.CompletedProcess(command, 0)
        with patch.object(RUNNER, "ios_major_version", return_value=16 if legacy else 17), patch.object(RUNNER.subprocess, "run", side_effect=transport):
            result = RUNNER.run_ios("phone", [], output, 0.01 if stale else 2)
        return result, calls

    def test_both_transports_accept_current_requested_or_default_log_and_cleanup_after_captures(self):
        for legacy in [False, True]:
            for fallback in [False, True]:
                with self.subTest(legacy=legacy, fallback=fallback), tempfile.TemporaryDirectory() as directory:
                    output = Path(directory)
                    (output / "captures").mkdir()
                    (output / "captures/stale.png").touch()
                    result, calls = self.run_device(output, "HARNESS RESULT PASS\n", legacy=legacy, fallback=fallback)
                    self.assertEqual(result, 0)
                    self.assertFalse((output / "captures/stale.png").exists())
                    self.assertEqual((output / "captures/screen.png").read_bytes(), b"new capture")
                    self.assertIn("HARNESS RESULT PASS", (output / "latest.log").read_text())
                    cleanup_index = next(i for i, command in enumerate(calls) if "-harness-cleanup" in command)
                    capture_index = next(i for i, command in enumerate(calls) if "-r" in command or any(arg.endswith("screen.png") for arg in command))
                    self.assertLess(capture_index, cleanup_index)
                    self.assertTrue("kill" in calls[-1] if legacy else "terminate" in calls[-1])
                    sources = [command[command.index("--source") + 1] if "--source" in command else command[-2] for command in calls if "copy" in command or "get" in command]
                    self.assertTrue(any(source.endswith("logs/godot.log") for source in sources))
                    self.assertTrue(any("/harness-" in source and source.endswith(".log") for source in sources))

    def test_invalid_results_are_rejected_after_final_pull(self):
        for legacy in [False, True]:
            for content in ["HARNESS RESULT FAIL\n", "HARNESS RESULT PASS\nHARNESS RESULT PASS\n", "SCRIPT ERROR: broken\nHARNESS RESULT PASS\n", "HARNESS STEP projection FAIL error\nHARNESS RESULT PASS\n"]:
                with self.subTest(legacy=legacy, content=content), tempfile.TemporaryDirectory() as directory:
                    result, _ = self.run_device(Path(directory), content, legacy=legacy)
                    self.assertEqual(result, 1)

    def test_stale_pass_times_out_and_does_not_delete_device_files(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            result, calls = self.run_device(output, "HARNESS RESULT PASS\n", stale=True)
            self.assertEqual(result, 1)
            self.assertEqual((output / "latest.log").read_text(), "")
            self.assertFalse(any("-harness-cleanup" in command for command in calls))
            self.assertTrue(any("terminate" in command for command in calls))

    def test_final_pull_detects_a_late_duplicate_result(self):
        with tempfile.TemporaryDirectory() as directory:
            result, _ = self.run_device(Path(directory), "HARNESS RESULT PASS\n", late_duplicate=True)
            self.assertEqual(result, 1)

    def test_termination_failure_still_pulls_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            result, _ = self.run_device(output, "HARNESS RESULT PASS\n", termination_failure=True)
            self.assertEqual(result, 1)
            self.assertIn("HARNESS RESULT PASS", (output / "latest.log").read_text())
            self.assertTrue((output / "captures/screen.png").exists())

    def test_ar_option_is_rejected_before_device_access(self):
        with tempfile.TemporaryDirectory() as directory, self.assertRaisesRegex(ValueError, "ios-ar"):
            RUNNER.run_ios("phone", ["-harness-ar"], Path(directory), 2)


if __name__ == "__main__":
    unittest.main()
