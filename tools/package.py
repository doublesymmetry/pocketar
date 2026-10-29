#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
import zipfile


ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True).strip()


def copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.is_dir():
        shutil.copytree(source, destination)
    else:
        shutil.copyfile(source, destination)


def archive(source, destination):
    timestamp = time.gmtime(int(git("show", "-s", "--format=%ct", "HEAD")))[:6]
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for path in sorted(source.rglob("*")):
            if path.is_file():
                entry = zipfile.ZipInfo(path.relative_to(source).as_posix(), timestamp)
                entry.external_attr = 0o100644 << 16
                entry.compress_type = zipfile.ZIP_DEFLATED
                output.writestr(entry, path.read_bytes())


def main():
    parser = argparse.ArgumentParser(description="Build and package the addon and tap-to-place sample.")
    parser.add_argument("--output", type=Path, default=ROOT / "build/package")
    args = parser.parse_args()
    output = args.output.resolve()
    subprocess.run(["python3", "tools/version.py", "--check"], cwd=ROOT, check=True)
    version = (ROOT / "VERSION").read_text().strip()
    for mode in ("debug", "release"):
        subprocess.run(["tools/build.sh", "all", mode], cwd=ROOT, env={**os.environ, "ARCH": "arm64"}, check=True)
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pocketar-package-") as temporary:
        staging = Path(temporary)
        addon = staging / "addon/addons/pocketar"
        source = ROOT / "harness/addons/pocketar"
        for name in git("ls-files", "harness/addons/pocketar").splitlines():
            path = ROOT / name
            if not path.name.startswith("pocketar_args."):
                copy(path, addon / path.relative_to(source))
        # Linux and Windows libraries come from CI's desktop jobs; Android ships in the AARs.
        listed = re.findall(r'^(?!android\.)\S+ = "res://addons/pocketar/(.+)"$', (source / "pocketar.gdextension").read_text(), re.M)
        for name in sorted(set(listed)) + [f"bin/android/pocketar-{mode}.aar" for mode in ("debug", "release")]:
            if not (source / name).exists():
                raise SystemExit(f"Missing {name}; build it or download CI's desktop artifacts")
            copy(source / name, addon / name)
        copy(ROOT / "docs/api.md", addon / "docs/api.md")
        provenance = {
            "version": version,
            "revision": git("rev-parse", "HEAD"),
            "dirty": bool(git("status", "--porcelain", "--untracked-files=normal")),
            "godot_cpp": git("-C", str(ROOT / "godot-cpp"), "rev-parse", "HEAD"),
            "godot_minimum": "4.7.2",
            "arcore": re.search(r'val arcoreVersion = "([^"]+)"', (ROOT / "android/plugin/build.gradle.kts").read_text())[1],
            "nix_inputs": json.loads((ROOT / "devenv.lock").read_text()),
            "build_input_sha256": {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in (
                "devenv.lock", "devenv.nix", "nix/godot-bin.nix", "nix/godot-pocketar.nix", "nix/godot.sh",
                "android/build.gradle.kts", "android/plugin/build.gradle.kts", "SConstruct",
            )},
        }
        (addon / "release.json").write_text(json.dumps(provenance, indent=2) + "\n")
        sample = staging / "sample/tap_to_place"
        for name in git("ls-files", "samples/tap_to_place").splitlines():
            path = ROOT / name
            relative = path.relative_to(ROOT / "samples/tap_to_place")
            if relative.parts[0] != "addons":
                copy(path, sample / relative)
        copy(addon, sample / "addons/pocketar")
        archives = [output / f"pocketar-{version}.zip", output / f"pocketar-tap-to-place-{version}.zip"]
        for directory, path in zip((staging / "addon", staging / "sample"), archives):
            archive(directory, path)
            print(f"{path}: {path.stat().st_size} bytes", flush=True)
        (output / "SHA256SUMS").write_text("".join(
            f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n" for path in archives
        ))


if __name__ == "__main__":
    main()
