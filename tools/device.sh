#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
[[ ${1:-} == ios ]] || { echo "Usage: $0 ios [--device UDID] preflight|install|launch|terminate|pull" >&2; exit 2; }
shift
exec python3 tools/ios-device.py "$@"
