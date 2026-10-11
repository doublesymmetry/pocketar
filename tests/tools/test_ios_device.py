import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
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
        self.assertEqual(json.loads(environment["POCKETAR_ARGS"]), arguments)

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

    def test_preflight_records_every_check_after_ddi_failure(self):
        details = {"connectionProperties": {"pairingState": "unpaired", "tunnelState": "disconnected"},
                   "deviceProperties": {"developerModeStatus": "disabled", "ddiServicesAvailable": False, "osVersionNumber": "16.6"}}
        with patch.object(self.device, "call", side_effect=[RuntimeError("DDI mount failed"), details, {"unexpected": True}]):
            with self.assertRaisesRegex(RuntimeError, "pairing.*tunnel.*Developer Mode.*DDI.*iOS 17"):
                self.device.preflight()
        report = json.loads((self.device.output / "preflight.json").read_text())
        self.assertEqual(report["checks"], {"pairing": "fail", "tunnel": "fail", "Developer Mode": "fail", "DDI": "fail", "iOS 17+": "fail", "unlocked": "unknown"})
        self.assertIn("DDI mount failed", report["errors"]["ddiServices"])

    def test_preflight_does_not_invent_lock_state_fields(self):
        details = {"connectionProperties": {"pairingState": "paired", "tunnelState": "connected"},
                   "deviceProperties": {"developerModeStatus": "enabled", "ddiServicesAvailable": True, "osVersionNumber": "26.0"}}
        with patch.object(self.device, "call", side_effect=[{}, details, {"isLocked": True}]):
            self.device.preflight()
        report = json.loads((self.device.output / "preflight.json").read_text())
        self.assertEqual(report["checks"]["unlocked"], "unknown")
        self.assertEqual(report["lockState"], {"isLocked": True})

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
        args, _, capture, _, _ = ios.harness_options(["-harness-out", "user://custom"], "unique")
        self.assertEqual(capture, "Documents/custom-unique")
        self.assertEqual(args[args.index("-harness-run-id") + 1], "unique")

    def test_fallback_log_requires_current_marker(self):
        for valid_source in ["Documents/harness-unique.log", "Documents/logs/godot.log", None]:
            def pull(source, destination):
                Path(destination).write_text("HARNESS RUN " + ("unique" if source == valid_source else "old") + "\nHARNESS RESULT PASS\n")
            with self.subTest(source=valid_source), patch.object(self.device, "pull", side_effect=pull):
                data = self.device.run_log("Documents/harness-unique.log", "HARNESS RUN unique")
            self.assertEqual(data is not None, valid_source is not None)

    def test_authoritative_result_validation(self):
        valid = "".join("HARNESS STEP " + step + " OK detail\n" for step in ios.STEPS) + "HARNESS RESULT PASS\n"
        ios.verify_log(valid, ios.STEPS)
        for invalid in [valid + "HARNESS RESULT PASS\n", valid + "SCRIPT ERROR: failed\n", valid.replace("tracking OK", "tracking FAIL"), valid.replace("arkit-session", "arcore-session"), valid.replace("RESULT PASS", "RESULT FAIL")]:
            with self.subTest(log=invalid), self.assertRaises(RuntimeError):
                ios.verify_log(invalid, ios.STEPS)

    def test_harness_terminates_and_checks_final_pull(self):
        valid = "HARNESS RUN unique\n" + "".join("HARNESS STEP " + step + " OK detail\n" for step in ios.STEPS) + "HARNESS RESULT PASS\n"
        pulls = []
        def pull(source, destination):
            pulls.append(source)
            Path(destination).write_text(valid if len(pulls) <= 2 else valid + "HARNESS RESULT PASS\n")
        with patch.object(ios.uuid, "uuid4", return_value=SimpleNamespace(hex="unique")), patch.object(self.device, "preflight"), patch.object(self.device, "launch", return_value=42), patch.object(self.device, "pull", side_effect=pull), patch.object(self.device, "terminate") as terminate, patch.object(self.device, "captures"), patch.object(self.device, "cleanup"), self.assertRaises(RuntimeError):
            ios.harness(self.device, [])
        terminate.assert_called_once_with(42)
        self.assertEqual(len(pulls), 4)

    def test_harness_preserves_evidence_after_termination_failure(self):
        valid = "HARNESS RUN unique\n" + "".join("HARNESS STEP " + step + " OK detail\n" for step in ios.STEPS) + "HARNESS RESULT PASS\n"
        def pull(source, destination):
            Path(destination).write_text(valid)
        with patch.object(ios.uuid, "uuid4", return_value=SimpleNamespace(hex="unique")), patch.object(self.device, "preflight"), patch.object(self.device, "launch", return_value=42), patch.object(self.device, "pull", side_effect=pull) as pulls, patch.object(self.device, "terminate", side_effect=RuntimeError("tunnel lost")), patch.object(self.device, "captures") as captures, patch.object(self.device, "cleanup"), self.assertRaisesRegex(RuntimeError, "tunnel lost"):
            ios.harness(self.device, [])
        self.assertEqual(pulls.call_count, 4)
        captures.assert_called_once()

    def test_completed_run_clears_host_captures_and_cleans_device_after_pull(self):
        captures = self.device.output / "captures"
        captures.mkdir()
        (captures / "stale.png").touch()
        operations = []
        def pull(source, destination):
            operations.append(source)
            Path(destination).write_text("HARNESS RUN unique\nHARNESS STEP arkit-session OK ready\nHARNESS RESULT PASS\n")
        def pull_captures(source, destination):
            self.assertFalse((destination / "stale.png").exists())
            operations.append("captures")
        def cleanup(token, capture):
            self.assertEqual((token, capture), ("unique", "user://custom-unique"))
            self.assertEqual(operations[-1], "captures")
        with patch.object(ios.uuid, "uuid4", return_value=SimpleNamespace(hex="unique")), patch.object(self.device, "preflight"), patch.object(self.device, "launch", return_value=42), patch.object(self.device, "pull", side_effect=pull), patch.object(self.device, "terminate"), patch.object(self.device, "captures", side_effect=pull_captures), patch.object(self.device, "cleanup", side_effect=cleanup):
            ios.harness(self.device, ["-harness-steps", "arkit-session", "-harness-out", "user://custom"])
        self.assertIn("HARNESS RUN unique", (self.device.output / "latest.log").read_text())

    def test_cleanup_launch_waits_for_its_receipt_and_terminates(self):
        def pull(source, destination):
            self.assertEqual(source, "Documents/logs/godot.log")
            Path(destination).write_text("HARNESS CLEANUP unique OK\n")
        with patch.object(self.device, "launch", return_value=43) as launch, patch.object(self.device, "pull", side_effect=pull), patch.object(self.device, "terminate") as terminate:
            self.device.cleanup("unique", "user://harness-unique")
        launch.assert_called_once_with(["-harness-cleanup", "unique", "-harness-out", "user://harness-unique"])
        terminate.assert_called_once_with(43)


if __name__ == "__main__":
    unittest.main()
