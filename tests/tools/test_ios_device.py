import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("ios_device", Path(__file__).resolve().parents[2] / "tools/ios-device.py")
ios = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ios)


class DeviceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.device = ios.Device("test-udid", self.temp.name)

    def test_launch_separator_and_json_environment(self):
        arguments = ["--log-file", "user://test.log", "-harness-ar", "-harness-out", "user://a b"]
        with patch.object(self.device, "call", return_value={"process": {"processIdentifier": 42}}) as call:
            self.assertEqual(self.device.launch(arguments), 42)
        command = call.call_args.args[0]
        separator = command.index("--")
        self.assertEqual(command[separator - 1], ios.BUNDLE)
        self.assertEqual(command[separator + 1:], arguments)
        environment = json.loads(command[command.index("--environment-variables") + 1])
        self.assertEqual(json.loads(environment["HANDHELD_AR_ARGS"]), arguments)

    def test_json_file_is_only_command_result(self):
        def run(command, **kwargs):
            output = Path(command[command.index("--json-output") + 1])
            output.write_text(json.dumps({"info": {"outcome": "success"}, "result": {"answer": 42}}))
        with patch.object(ios.subprocess, "run", side_effect=run):
            self.assertEqual(self.device.call(["list", "devices"]), {"answer": 42})

    def test_preflight_conditions(self):
        details = {"connectionProperties": {"pairingState": "paired", "tunnelState": "connected"}, "deviceProperties": {"developerModeStatus": "enabled", "ddiServicesAvailable": True, "osVersionNumber": "26.0"}, "hardwareProperties": {"productType": "iPhone18,1"}}
        with patch.object(self.device, "call", side_effect=[{}, details, {}]):
            self.device.preflight()
        for group, key, value in [("connectionProperties", "pairingState", "unpaired"), ("connectionProperties", "tunnelState", "disconnected"), ("deviceProperties", "developerModeStatus", "disabled"), ("deviceProperties", "ddiServicesAvailable", False), ("deviceProperties", "osVersionNumber", "16.6")]:
            invalid = json.loads(json.dumps(details))
            invalid[group][key] = value
            with self.subTest(key=key), patch.object(self.device, "call", side_effect=[{}, invalid, {}]), self.assertRaises(RuntimeError):
                self.device.preflight()

    def test_terminate_kills_process_after_grace(self):
        running = {"runningProcesses": [{"processIdentifier": 42}]}
        with patch.object(self.device, "call", side_effect=[running, {}, running, {}]) as call, patch.object(ios.time, "monotonic", side_effect=[0, 6]):
            self.device.terminate(42)
        self.assertEqual(call.call_args.args[0][-3:], ["--pid", "42", "--kill"])

    def test_terminate_absent_process_is_idempotent(self):
        with patch.object(self.device, "call", return_value={"runningProcesses": []}) as call:
            self.assertFalse(self.device.terminate(42))
        self.assertEqual(call.call_count, 1)
        self.assertEqual(call.call_args.args[0][1:3], ["info", "processes"])

    def test_capture_copy_is_per_file(self):
        with patch.object(self.device, "call", return_value={"files": [{"name": "camera.png"}, {"name": "diagnostics.json"}]}) as call, patch.object(self.device, "pull") as pull:
            self.device.captures("Documents/harness", Path(self.temp.name) / "captures")
        self.assertIn("--no-recurse", call.call_args.args[0])
        self.assertEqual([c.args[0] for c in pull.call_args_list], ["Documents/harness/camera.png", "Documents/harness/diagnostics.json"])

    def test_harness_rejects_invalid_options(self):
        for arguments in [["-harness-duration", "nan"], ["-harness-duration", "0"], ["-harness-steps", "arcore-session"], ["-harness-out", "user://../escape"], ["--log-file", "old.log"], ["-harness-duration"]]:
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                ios.harness_options(arguments, "unique")
        args, log, capture, required, timeout = ios.harness_options([], "unique")
        self.assertEqual(log, "Documents/harness-unique.log")
        self.assertEqual(capture, "Documents/harness-unique")
        self.assertEqual(required, ios.STEPS)
        self.assertEqual(timeout, 240)

    def test_authoritative_result_validation(self):
        valid = "".join("HARNESS STEP " + step + " OK detail\n" for step in ios.STEPS) + "HARNESS RESULT PASS\n"
        ios.verify_log(valid, ios.STEPS)
        for invalid in [valid + "HARNESS RESULT PASS\n", valid + "SCRIPT ERROR: failed\n", valid.replace("tracking OK", "tracking FAIL"), valid.replace("arkit-session", "arcore-session"), valid.replace("RESULT PASS", "RESULT FAIL")]:
            with self.subTest(log=invalid), self.assertRaises(RuntimeError):
                ios.verify_log(invalid, ios.STEPS)

    def test_harness_terminates_and_checks_final_pull(self):
        valid = "".join("HARNESS STEP " + step + " OK detail\n" for step in ios.STEPS) + "HARNESS RESULT PASS\n"
        pulls = []
        def pull(source, destination):
            pulls.append(source)
            Path(destination).write_text(valid if len(pulls) == 1 else valid + "HARNESS RESULT PASS\n")
        with patch.object(self.device, "preflight"), patch.object(self.device, "launch", return_value=42), patch.object(self.device, "pull", side_effect=pull), patch.object(self.device, "terminate") as terminate, patch.object(self.device, "captures"), self.assertRaises(RuntimeError):
            ios.harness(self.device, [])
        terminate.assert_called_once_with(42)
        self.assertEqual(len(pulls), 2)

    def test_harness_preserves_evidence_after_termination_failure(self):
        valid = "".join("HARNESS STEP " + step + " OK detail\n" for step in ios.STEPS) + "HARNESS RESULT PASS\n"
        def pull(source, destination):
            Path(destination).write_text(valid)
        with patch.object(self.device, "preflight"), patch.object(self.device, "launch", return_value=42), patch.object(self.device, "pull", side_effect=pull) as pulls, patch.object(self.device, "terminate", side_effect=RuntimeError("tunnel lost")), patch.object(self.device, "captures") as captures, self.assertRaisesRegex(RuntimeError, "tunnel lost"):
            ios.harness(self.device, [])
        self.assertEqual(pulls.call_count, 2)
        captures.assert_called_once()


if __name__ == "__main__":
    unittest.main()
