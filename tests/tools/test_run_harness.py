import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location(
    "run_harness", Path(__file__).parents[2] / "tools/run-harness.py"
)
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class AndroidHarnessTest(unittest.TestCase):
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


class HarnessMonitorTest(unittest.TestCase):
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
                "import sys\n"
                "args = sys.argv[1:]\n"
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


if __name__ == "__main__":
    unittest.main()
