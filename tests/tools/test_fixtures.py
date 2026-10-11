import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("fixtures", ROOT / "tools/fixtures.py")
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)


class FixtureTests(unittest.TestCase):
    def generate(self, *arguments):
        options = fixtures.parser().parse_args(["--output", "/tmp/unused.jsonl", *arguments])
        return fixtures.generate(options)

    def test_static_tap_is_projected_from_table_world_point(self):
        records = self.generate("--tap-at", "0.6")
        frames = {r["frame_id"]: r for r in records[1:] if "frame_id" in r}
        inputs = [r for r in records[1:] if "kind" in r]
        self.assertEqual([r["kind"] for r in inputs], ["down", "up"])
        for event in inputs:
            self.assertAlmostEqual(event["uv"][0], 0.5)
            self.assertAlmostEqual(event["uv"][1], 0.871875)
            self.assertEqual(frames[event["target_frame"]]["t_usec"], event["t_usec"])
        self.assertEqual(records[1]["session_state"], "initializing")
        self.assertEqual(frames[16]["session_state"], "tracking")

    def test_portrait_tap_hits_table_with_aspect_fill(self):
        records = self.generate("--view-size", "1080", "1920", "--tap-at", "0.6")
        self.assertEqual(records[0]["view_size"], [1080, 1920])
        self.assertEqual(records[0]["quarter_turns"], 1)
        for event in (r for r in records if "kind" in r):
            # Portrait rotates the image clockwise, then crops 180 pixels per side.
            rotated_x = (event["uv"][0] * 1080 + 180) / 3
            rotated_y = event["uv"][1] * 1920 / 3
            image_x, image_y = rotated_y, 480 - rotated_x
            depth = 0.7 * 510 / (image_y - 240)
            hit = ((image_x - 320) * depth / 520, -0.7, -depth)
            self.assertLess(sum((a - b) ** 2 for a, b in zip(hit, (0, -0.7, -2))) ** 0.5, 0.001)

    def test_explicit_rotations_project_into_a_cropped_view(self):
        for turns in range(4):
            records = self.generate("--view-size", "1080", "1920", "--quarter-turns", str(turns),
                                    "--tap-at", "0.6", "--tap-world", "0.1", "-0.2", "-2")
            self.assertEqual(records[0]["quarter_turns"], turns)
            x, y = 346, 291
            width, height = 640, 480
            for _ in range(turns):
                x, y, width, height = height - y, x, height, width
            scale = max(1080 / width, 1920 / height)
            expected = [(x * scale - (width * scale - 1080) / 2) / 1080,
                        (y * scale - (height * scale - 1920) / 2) / 1920]
            for event in (r for r in records if "kind" in r):
                for actual, wanted in zip(event["uv"], expected):
                    self.assertAlmostEqual(actual, wanted)

    def test_view_crop_rejects_an_input_outside_the_view(self):
        with self.assertRaisesRegex(ValueError, "outside the target view"):
            self.generate("--view-size", "1920", "800", "--tap-at", "0.6")
        with self.assertRaisesRegex(ValueError, "view dimensions"):
            self.generate("--view-size", "0", "1920")

    def test_reset_omits_planes_until_following_frame(self):
        records = self.generate("--reset-at", "1.5", "--loss-at", "1", "--loss-duration", "0.25")
        frames = {r["t_usec"]: r for r in records[1:] if "frame_id" in r}
        self.assertNotIn("planes", frames[1_500_000])
        self.assertEqual(frames[1_533_333]["planes"][0]["id"], 1)
        self.assertEqual(frames[1_000_000]["tracking_reason"], "insufficient_features")
        self.assertTrue(frames[1_000_000]["tracking_valid"])
        arcore = self.generate("--semantics", "arcore", "--loss-at", "1")
        self.assertFalse(next(r for r in arcore if r.get("t_usec") == 1_000_000 and "frame_id" in r)["tracking_valid"])

    def test_tracking_loss_and_pause_publish_paused_planes_then_restore(self):
        for semantics in ("arkit", "arcore"):
            records = self.generate("--semantics", semantics, "--loss-at", "1", "--pause-at", "1.75", "--plane-examples")
            frames = {r["t_usec"]: r for r in records[1:] if "frame_id" in r}
            for time in (1_000_000, 1_766_667):
                self.assertIn("planes", frames[time])
                self.assertTrue(all(p["tracking_state"] == "paused" for p in frames[time]["planes"]))
            for time in (1_266_667, 1_866_667):
                states = [p["tracking_state"] for p in frames[time]["planes"]]
                self.assertEqual(states, ["tracking", "tracking", "paused", "tracking"])

    def test_orbit_touch_unprojects_to_requested_world_point(self):
        records = self.generate("--camera", "orbit", "--tap-at", "0.6")
        header = records[0]
        frames = {r["frame_id"]: r for r in records[1:] if "frame_id" in r}
        for event in (r for r in records[1:] if "kind" in r):
            matrix = frames[event["target_frame"]]["camera_transform"]
            fx, fy = header["intrinsics"]["fx"], header["intrinsics"]["fy"]
            ray_local = [(event["uv"][0] * 640 - 320) / fx, -(event["uv"][1] * 480 - 240) / fy, -1]
            direction = [sum(matrix[column * 4 + row] * ray_local[column] for column in range(3)) for row in range(3)]
            distance = (-0.7 - matrix[13]) / direction[1]
            hit = [matrix[12 + row] + distance * direction[row] for row in range(3)]
            for actual, expected in zip(hit, (0, -0.7, -2)):
                self.assertAlmostEqual(actual, expected, places=8)

    def test_generation_is_deterministic_and_events_ordered(self):
        args = ("--loss-at", "1", "--reset-at", "1.5", "--pause-at", "1.75", "--tap-at", "0.6", "--drag-to", "0.1", "-0.7", "-2", "--plane-examples")
        records = self.generate(*args)
        self.assertEqual(records, self.generate(*args))
        times = [r["t_usec"] for r in records[1:]]
        self.assertEqual(times, sorted(times))
        self.assertEqual([r["kind"] for r in records if "kind" in r], ["down", "move", "up"])
        plane_list = next(r["planes"] for r in records if r.get("planes"))
        self.assertEqual([p["id"] for p in plane_list], [1, 2, 3, 4])

    def test_invalid_ranges_and_unprojectable_inputs_fail(self):
        for args in (("--fps", "0"), ("--duration", "-1"), ("--fps", "nan"), ("--table-height", "0"), ("--tap-at", "3"), ("--tap-at", "0.6", "--tap-world", "0", "0", "1")):
            with self.subTest(args=args), self.assertRaises(ValueError):
                self.generate(*args)


if __name__ == "__main__":
    unittest.main()
