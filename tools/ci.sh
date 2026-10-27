#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/ci
actionlint
python3 tools/package.py
TMPDIR=/tmp tools/test-native.sh
TMPDIR=/tmp stock tools/test-arcore.sh
stock godot --headless --path harness --editor --import --quit-after 180 > build/ci/import.log 2>&1
if rg 'SCRIPT ERROR:|ERROR:' build/ci/import.log; then exit 1; fi
stock tools/test-headless.sh
gradle -p android :plugin:testDebugUnitTest
python3 -m unittest discover -s tests/tools
stock python3 tools/test-install.py --output build/install/stock
if [[ $(sysctl -n hw.model) == VirtualMac* ]]; then
    GODOT_MTL_SYNC_MODE=none python3 tools/test-install.py --output build/install/fork
else
    python3 tools/test-install.py --output build/install/fork
fi
stock tools/harness.sh desktop -harness-out "$PWD/build/harness/desktop/captures"
