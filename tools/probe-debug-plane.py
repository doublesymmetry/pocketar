#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


# Gradle rewrites the non-loaded .comment and section-name tables while packaging the AAR, so
# Android libraries are compared by the image the loader maps.
def loadable_digest(path):
    tools = sorted(Path(os.environ.get("ANDROID_NDK_ROOT", "/nonexistent")).glob("toolchains/llvm/prebuilt/*/bin/llvm-objcopy"))
    if not tools:
        raise RuntimeError("comparing Android libraries needs llvm-objcopy under ANDROID_NDK_ROOT")
    with tempfile.TemporaryDirectory() as directory:
        image = Path(directory) / "image.bin"
        subprocess.run([str(tools[0]), "-O", "binary", str(path), str(image)], check=True)
        return digest(image)


def run(command, log):
    with log.open("w") as output:
        result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"exit {result.returncode}: {command[0]}; see {log}")


def prepare(destination, platform, mode):
    project = destination / "project"
    project.mkdir(parents=True, exist_ok=False)
    addon = project / "addons/pocketar"
    shutil.copytree(ROOT / "demo/addons/pocketar", addon, ignore=shutil.ignore_patterns("bin"))
    for target in {platform, "macos"}:
        source = ROOT / "demo/addons/pocketar/bin" / target
        shutil.copytree(source, addon / "bin" / target)
    shutil.copy2(ROOT / "demo/icon.png", project / "icon.png")
    for name in ("debug_plane_api.gd", "debug_plane_api.gd.uid", "debug_plane_api.tscn"):
        shutil.copy2(ROOT / "tests/probes" / name, project / name)
    expected = "true" if mode == "debug" else "false"
    (project / "project.godot").write_text(f'''config_version=5
[application]
config/name="PocketAR harness"
config/icon="res://icon.png"
run/main_scene="res://debug_plane_api.tscn"
[display]
window/size/viewport_width=640
window/size/viewport_height=480
[rendering]
textures/vram_compression/import_etc2_astc=true
renderer/rendering_method="mobile"
renderer/rendering_method.mobile="mobile"
rendering_device/driver.macos="metal"
rendering_device/driver.ios="metal"
[editor_plugins]
enabled=PackedStringArray("res://addons/pocketar/plugin.cfg")
[probe]
expect_debug={expected}
''')
    presets = (ROOT / "demo/export_presets.cfg").read_text().replace(
        "org.doublesymmetry.pocketar.demo", "org.doublesymmetry.pocketar.apiprobe")
    presets += '''
[preset.2]
name="macOS"
platform="macOS"
runnable=false
export_filter="all_resources"
include_filter=""
exclude_filter=""
[preset.2.options]
binary_format/architecture="arm64"
application/bundle_identifier="org.doublesymmetry.pocketar.apiprobe"
codesign/codesign=0
'''
    (project / "export_presets.cfg").write_text(presets)
    credentials = ROOT / "demo/.godot/export_credentials.cfg"
    if credentials.exists():
        (project / ".godot").mkdir(exist_ok=True)
        shutil.copy2(credentials, project / ".godot/export_credentials.cfg")
        (project / ".godot/export_credentials.cfg").chmod(0o600)
    return project


def verify_native(destination, platform, mode, artifact):
    suffix = {"macos": "dylib", "android": "so", "ios": "a"}[platform]
    built = ROOT / f"demo/addons/pocketar/bin/{platform}/template_{mode}/arm64/libpocketar.{suffix}"
    expected = digest(built)
    if platform == "android":
        with zipfile.ZipFile(artifact) as package:
            names = [name for name in package.namelist() if name.endswith("/libpocketar.so")]
            if len(names) != 1:
                raise RuntimeError(f"expected one packaged library, found {names}")
            packaged_hash = hashlib.sha256(package.read(names[0])).hexdigest()
            packaged = f"{artifact}!{names[0]}"
            with tempfile.TemporaryDirectory() as directory:
                extracted = Path(directory) / "libpocketar.so"
                extracted.write_bytes(package.read(names[0]))
                packaged_image = loadable_digest(extracted)
            built_image = loadable_digest(built)
            if built_image != packaged_image:
                raise RuntimeError(f"native image mismatch: {built} and {packaged}")
            record = {"platform": platform, "mode": mode, "built": str(built), "packaged": packaged,
                      "built_sha256": expected, "packaged_sha256": packaged_hash,
                      "built_image_sha256": built_image, "packaged_image_sha256": packaged_image}
            (destination / "native-hashes.json").write_text(json.dumps(record, indent=2) + "\n")
            print(f"NATIVE HASH PASS {platform} {mode} {built_image} (loadable image)", flush=True)
            return
    else:
        candidates = list(artifact.rglob("libpocketar.dylib")) if platform == "macos" else list((destination / "export").rglob("libpocketar.a"))
        if len(candidates) != 1:
            raise RuntimeError(f"expected one packaged native library, found {candidates}")
        packaged = str(candidates[0])
        packaged_hash = digest(candidates[0])
    if expected != packaged_hash:
        raise RuntimeError(f"native hash mismatch: {built} and {packaged}")
    record = {"platform": platform, "mode": mode, "built": str(built), "packaged": packaged,
              "built_sha256": expected, "packaged_sha256": packaged_hash}
    if platform == "ios":
        project_file = artifact / "project.pbxproj"
        objects = json.loads(subprocess.check_output(["plutil", "-convert", "json", "-o", "-", str(project_file)]))["objects"]
        framework = next(parent for parent in candidates[0].parents if parent.suffix == ".xcframework")
        expected_path = framework.relative_to(destination / "export").as_posix()
        links = []
        for target in objects.values():
            if target.get("isa") != "PBXNativeTarget":
                continue
            for phase_id in target.get("buildPhases", []):
                phase = objects[phase_id]
                if phase.get("isa") != "PBXFrameworksBuildPhase":
                    continue
                for build_id in phase.get("files", []):
                    reference = objects.get(objects[build_id].get("fileRef"), {})
                    path = reference.get("path", "")
                    if path == expected_path and reference.get("sourceTree") in ("<group>", "SOURCE_ROOT"):
                        links.append({"target": target["name"], "framework": path, "build_file": build_id})
        if len(links) != 1:
            raise RuntimeError(f"expected one Xcode target linking {framework}, found {links}")
        record["xcode_links"] = links
        record["device_result"] = "pending: retain the Xcode link map and signed app hash with the device log"
    (destination / "native-hashes.json").write_text(json.dumps(record, indent=2) + "\n")
    print(f"NATIVE HASH PASS {platform} {mode} {expected}", flush=True)


def main():
    parser = argparse.ArgumentParser(description="Export the main-scene debug-plane API probe and verify native artifact hashes.")
    parser.add_argument("platform", choices=["macos", "android", "ios"])
    parser.add_argument("--output", type=Path, default=Path("/tmp/pocketar-api-probe"))
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    os.umask(0o077)
    for mode in ("debug", "release"):
        destination = args.output.resolve() / args.platform / mode
        destination.mkdir(parents=True, exist_ok=True)
        project = prepare(destination, args.platform, mode)
        print(f"PROBE PROJECT {project}", flush=True)
        if args.prepare_only:
            continue
        output = destination / "export"
        output.mkdir()
        extension = {"macos": "app", "android": "apk", "ios": "xcodeproj"}[args.platform]
        artifact = output / f"probe.{extension}"
        command = ["godot", "--headless", "--path", str(project)]
        if args.platform == "android":
            command.append("--install-android-build-template")
        command += [f"--export-{mode}", {"macos": "macOS", "android": "Android", "ios": "iOS"}[args.platform], str(artifact)]
        run(command, destination / "export.log")
        verify_native(destination, args.platform, mode, artifact)
        if args.platform == "macos":
            executables = list((artifact / "Contents/MacOS").iterdir())
            if len(executables) != 1:
                raise RuntimeError(f"unexpected app executables: {executables}")
            log = destination / "run.log"
            run([str(executables[0]), "--headless", "--log-file", str(destination / "godot.log")], log)
            results = [json.loads(line.removeprefix("DEBUG PLANE API ")) for line in log.read_text().splitlines() if line.startswith("DEBUG PLANE API ")]
            if len(results) != 1 or results[0]["result"] != "PASS" or results[0]["debug"] != (mode == "debug"):
                raise RuntimeError(f"missing or failed exported probe result: {log}")
            print(f"EXPORTED API PASS {args.platform} {mode}", flush=True)
        else:
            print(f"DEVICE PROBE REQUIRED {artifact}", flush=True)


if __name__ == "__main__":
    main()
