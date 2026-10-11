#!/usr/bin/env python3
"""Generate synthetic PocketAR session fixtures without device recordings."""

import argparse
import json
import math
from pathlib import Path


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--output", type=Path, required=True)
    result.add_argument("--view-size", type=int, nargs=2, default=[640, 480], metavar=("W", "H"))
    result.add_argument("--quarter-turns", type=int, choices=range(4))
    result.add_argument("--semantics", choices=("arkit", "arcore"), default="arkit")
    result.add_argument("--duration", type=float, default=2.0)
    result.add_argument("--fps", type=float, default=30.0)
    result.add_argument("--table-height", type=float, default=0.7, help="Metres below the camera origin")
    result.add_argument("--camera", choices=("static", "orbit"), default="static")
    result.add_argument("--tracking-at", type=float, default=0.5)
    result.add_argument("--loss-at", type=float)
    result.add_argument("--loss-duration", type=float, default=0.25)
    result.add_argument("--reset-at", type=float)
    result.add_argument("--pause-at", type=float)
    result.add_argument("--pause-duration", type=float, default=0.1)
    result.add_argument("--tap-at", type=float, action="append", default=[])
    result.add_argument("--tap-world", type=float, nargs=3, metavar=("X", "Y", "Z"))
    result.add_argument("--drag-to", type=float, nargs=3, metavar=("X", "Y", "Z"))
    result.add_argument("--plane-examples", action="store_true", help="Include vertical, paused and subsumed planes")
    return result


def transform(origin, yaw=0.0, roll=0.0):
    cosine, sine = math.cos(yaw), math.sin(yaw)
    result = [cosine, 0, -sine, 0, 0, 1, 0, 0, sine, 0, cosine, 0, *origin, 1]
    if roll:
        roll_cosine, roll_sine = math.cos(roll), math.sin(roll)
        result[:3] = [cosine * roll_cosine, roll_sine, -sine * roll_cosine]
        result[4:7] = [-cosine * roll_sine, roll_cosine, sine * roll_sine]
    return result


def project(point, camera, intrinsics, image_size):
    offset = [point[i] - camera[12 + i] for i in range(3)]
    local = [sum(camera[column * 4 + row] * offset[row] for row in range(3)) for column in range(3)]
    if local[2] >= 0:
        raise ValueError("input world point is behind the camera")
    uv = [(intrinsics["fx"] * local[0] / -local[2] + intrinsics["cx"]) / image_size[0],
          (intrinsics["cy"] - intrinsics["fy"] * local[1] / -local[2]) / image_size[1]]
    if not all(math.isfinite(value) and 0 <= value <= 1 for value in uv):
        raise ValueError("input world point projects outside the camera image")
    return uv


def image_to_view(uv, image_size, view_size, quarter_turns):
    x, y = uv
    oriented = [(x, y), (1 - y, x), (1 - x, 1 - y), (y, 1 - x)][quarter_turns]
    width, height = image_size if quarter_turns % 2 == 0 else image_size[::-1]
    scale = max(view_size[0] / width, view_size[1] / height)
    span = [view_size[0] / (width * scale), view_size[1] / (height * scale)]
    result = [(oriented[i] - (1 - span[i]) / 2) / span[i] for i in range(2)]
    if not all(math.isfinite(value) and 0 <= value <= 1 for value in result):
        raise ValueError("input world point projects outside the target view")
    return result


def generate(options):
    numeric = [options.duration, options.fps, options.table_height, options.tracking_at,
               options.loss_duration, options.pause_duration, *options.tap_at]
    numeric += [value for value in (options.loss_at, options.reset_at, options.pause_at) if value is not None]
    numeric += options.tap_world or []
    numeric += options.drag_to or []
    if not all(math.isfinite(value) for value in numeric):
        raise ValueError("all numeric options must be finite")
    if not 0 < options.duration <= 3600 or not 0 < options.fps <= 240 or options.table_height <= 0:
        raise ValueError("duration must be in (0,3600], fps in (0,240], and table height positive")
    if not 0 <= options.tracking_at <= options.duration or options.loss_duration <= 0 or options.pause_duration <= 0:
        raise ValueError("tracking time must be within duration and loss/pause durations positive")
    for event in (options.loss_at, options.reset_at, options.pause_at, *options.tap_at):
        if event is not None and not 0 <= event <= options.duration:
            raise ValueError("event times must be within the fixture duration")
    for start, duration in ((options.loss_at, options.loss_duration), (options.pause_at, options.pause_duration)):
        if start is not None and start + duration > options.duration:
            raise ValueError("loss and pause must end within the fixture duration")
    if not all(0 < size <= 16384 for size in options.view_size):
        raise ValueError("view dimensions must be in [1,16384]")
    turns = options.quarter_turns
    if turns is None:
        turns = 1 if options.view_size[1] > options.view_size[0] else 0
    header = {
        "format": "pocketar-session", "version": 1, "semantics": options.semantics,
        "view_size": options.view_size, "quarter_turns": turns,
        "image_size": [640, 480], "intrinsics": {"fx": 520, "fy": 510, "cx": 320, "cy": 240},
        "conventions": {"units": "metres", "axes": "+X right,+Y up,-Z forward",
                        "matrices": "column-major affine world-from-local, rigid, 16 numbers",
                        "camera_transform": "physical image-camera pose before display rotation",
                        "planes": "local XZ polygon, +Y normal, extents are full lengths",
                        "uv": "top-left viewport origin, normalized to view_size at quarter_turns",
                        "time": "microseconds since fixture start; equal timestamps retain file order"},
    }
    center = [0, -options.table_height, -2]
    table = {"id": 1, "alignment": "horizontal_upward_facing", "transform": transform(center),
             "extents": [2, 2], "polygon": [[-1, -1], [1, -1], [1, 1], [-1, 1]],
             "tracking_state": "tracking", "subsumed_by": 0}
    planes = [table]
    if options.plane_examples:
        wall = dict(table, id=2, alignment="vertical", transform=[1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, -3, 1])
        planes += [wall, dict(table, id=3, tracking_state="paused"), dict(table, id=4, subsumed_by=1)]
    records = []
    frames = []
    reset_frame = math.ceil(options.reset_at * options.fps) if options.reset_at is not None else -2
    plane_mode = None
    if options.reset_at is not None:
        records.append({"t_usec": round(options.reset_at * 1_000_000), "reset": True})
    if options.pause_at is not None:
        records += [{"t_usec": round(options.pause_at * 1_000_000), "lifecycle": "pause"},
                    {"t_usec": round((options.pause_at + options.pause_duration) * 1_000_000), "lifecycle": "resume"}]
    for index in range(math.floor(options.duration * options.fps) + 1):
        time = index / options.fps
        lost = options.loss_at is not None and options.loss_at <= time < options.loss_at + options.loss_duration
        paused = options.pause_at is not None and options.pause_at <= time < options.pause_at + options.pause_duration
        tracking = time >= options.tracking_at and not lost and not paused and index != reset_frame
        state = "paused" if paused else "tracking" if tracking else "initializing"
        yaw = 0.35 * math.sin(2 * math.pi * time / options.duration) if options.camera == "orbit" else 0.0
        camera = transform([2 * math.sin(yaw), 0, -2 + 2 * math.cos(yaw)], yaw, roll=-turns * math.pi / 2)
        frame = {"t_usec": round(time * 1_000_000), "frame_id": index + 1,
                 "camera_transform": camera, "tracking_valid": tracking or (lost and options.semantics == "arkit"),
                 "session_state": state, "tracking_reason": "insufficient_features" if lost else "none"}
        if index == reset_frame:
            plane_mode = None
        elif time >= options.tracking_at:
            mode = "paused" if lost or paused else "tracking"
            if mode != plane_mode:
                frame["planes"] = [dict(plane, tracking_state="paused") if mode == "paused" else plane for plane in planes]
                plane_mode = mode
        frames.append(frame)
        records.append(frame)
    for tap_time in options.tap_at:
        first = round(tap_time * options.fps)
        points = [options.tap_world or center]
        kinds = ["down"]
        if options.drag_to:
            points += [options.drag_to, options.drag_to]
            kinds += ["move", "up"]
        else:
            points += points
            kinds += ["up"]
        if first + len(kinds) > len(frames):
            raise ValueError("input gesture extends past the last frame")
        for offset, (kind, point) in enumerate(zip(kinds, points)):
            frame = frames[first + offset]
            uv = project(point, frame["camera_transform"], header["intrinsics"], header["image_size"])
            uv = image_to_view(uv, header["image_size"], options.view_size, turns)
            records.append({"t_usec": frame["t_usec"], "kind": kind, "finger": 0, "uv": uv, "target_frame": frame["frame_id"]})
    records.sort(key=lambda record: (record["t_usec"], 2 if "kind" in record else 1 if "frame_id" in record else 0))
    return [header, *records]


def main():
    argument_parser = parser()
    options = argument_parser.parse_args()
    try:
        records = generate(options)
    except ValueError as error:
        argument_parser.error(str(error))
    options.output.parent.mkdir(parents=True, exist_ok=True)
    options.output.write_text("".join(json.dumps(record, separators=(",", ":"), allow_nan=False) + "\n" for record in records))


if __name__ == "__main__":
    main()
