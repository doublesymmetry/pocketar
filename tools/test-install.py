#!/usr/bin/env python3
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
ERROR = re.compile(r"(?:SCRIPT ERROR|SHADER ERROR|USER ERROR|ERROR):|Shader compilation failed")


def run(project, log, *arguments, timeout=120, marker=None):
    with log.open("w") as output:
        try:
            result = subprocess.run(["godot", "--path", str(project), *arguments],
                                    stdout=output, stderr=subprocess.STDOUT, timeout=timeout)
        except subprocess.TimeoutExpired:
            raise SystemExit(f"Godot timed out: {log}")
    text = log.read_text(errors="replace")
    if result.returncode or ERROR.search(text) or (marker and text.count(marker) != 1):
        print(text)
        raise SystemExit(f"Install check failed: {log} (exit {result.returncode})")
    print(f"PASS {log.name}", flush=True)


def main():
    parser = argparse.ArgumentParser(description="Test release ZIPs in fresh projects; requires a GUI session.")
    parser.add_argument("--package", type=Path, default=ROOT / "build/package")
    parser.add_argument("--output", type=Path, default=ROOT / "build/install")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    version = (ROOT / "VERSION").read_text().strip()
    for line in (args.package / "SHA256SUMS").read_text().splitlines():
        digest, name = line.split()
        if hashlib.sha256((args.package / name).read_bytes()).hexdigest() != digest:
            raise SystemExit(f"Checksum mismatch: {name}")
    work = Path(tempfile.mkdtemp(prefix="projects-", dir=output))
    empty = work / "empty"
    sample = work / "tap_to_place"
    with zipfile.ZipFile(args.package / f"pocketar-{version}.zip") as archive:
        archive.extractall(empty)
    # Android libraries ship inside the AAR; every other listed library must be in the ZIP.
    extension = (empty / "addons/pocketar/pocketar.gdextension").read_text()
    for library in re.findall(r'^(?!android\.)\S+ = "res://(.+)"$', extension, re.M):
        if not (empty / library).exists():
            raise SystemExit(f"Missing library in the ZIP: {library}")
    with zipfile.ZipFile(args.package / f"pocketar-tap-to-place-{version}.zip") as archive:
        archive.extractall(work)
    presets = sample / "export_presets.cfg"
    presets.write_text(presets.read_text().replace("package/signed=true", "package/signed=false"))
    (empty / "project.godot").write_text('''config_version=5
[application]
config/name="PocketAR install check"
run/main_scene="res://addons/pocketar/ar_view.tscn"
[display]
window/size/viewport_width=960
window/size/viewport_height=720
[editor_plugins]
enabled=PackedStringArray("res://addons/pocketar/plugin.cfg")
[rendering]
renderer/rendering_method="mobile"
rendering_device/driver.macos="metal"
rendering_device/fallback_to_opengl3=false
driver/threads/thread_model=1
viewport/hdr_2d=false
anti_aliasing/quality/use_debanding=true
''')
    for name, project in (("empty", empty), ("sample", sample)):
        # Stock Godot can crash when quitting immediately after discovering a GDExtension.
        run(project, output / f"{name}-import.log", "--headless", "--editor", "--import", "--quit-after", "180")
        run(project, output / f"{name}-gui.log", "--script", str(ROOT / "tests/install/smoke.gd"),
            "--", str(output / f"{name}.png"), timeout=300, marker="INSTALL RESULT PASS")
    if list(sample.glob("addons/pocketar/bin/android/**/*.so")):
        raise SystemExit("The addon contains loose Android libraries")
    for mode in ("debug", "release"):
        run(sample, output / f"android-{mode}.log", "--headless", "--install-android-build-template",
            f"--export-{mode}", "Android", str(output / f"sample-{mode}.apk"), timeout=600)
        with zipfile.ZipFile(output / f"sample-{mode}.apk") as apk:
            for library in ("lib/arm64-v8a/libpocketar.so", "lib/arm64-v8a/libarcore_sdk_c.so",
                            "assets/addons/pocketar/pocketar.gdextension"):
                if apk.namelist().count(library) != 1:
                    raise SystemExit(f"Expected exactly one {library} in the {mode} APK")
    print(f"INSTALL PASS: {output}")


if __name__ == "__main__":
    main()
