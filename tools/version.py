#!/usr/bin/env python3
import argparse
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description="Synchronize release metadata from VERSION.")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    version = (root / "VERSION").read_text().strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?", version):
        raise SystemExit("VERSION must contain a semantic version without a v prefix")
    fields = {
        "demo/addons/pocketar/plugin.cfg": [(r'(?m)^version="[^"]+"', f'version="{version}"')],
        "demo/addons/pocketar/pocketar.gdextension": [(r'(?m)^version = "[^"]+"', f'version = "{version}"')],
        "src/core/pocketar.h": [(r'get_version\(\) const \{ return "[^"]+";', f'get_version() const {{ return "{version}";')],
    }
    for name in ("demo", "samples/tap_to_place"):
        fields[f"{name}/export_presets.cfg"] = [
            (r'(?m)^application/short_version="[^"]+"', f'application/short_version="{version.split("-")[0]}"'),
            (r'(?m)^version/name="[^"]+"', f'version/name="{version}"'),
        ]
    stale = []
    for name, replacements in fields.items():
        path = root / name
        original = updated = path.read_text()
        for pattern, replacement in replacements:
            updated, count = re.subn(pattern, replacement, updated)
            if count != 1:
                raise SystemExit(f"Expected one version field in {name}: {pattern}")
        if original != updated:
            if args.check:
                stale.append(name)
            else:
                path.write_text(updated)
    if stale:
        raise SystemExit("Run python3 tools/version.py to update: " + ", ".join(stale))


if __name__ == "__main__":
    main()
