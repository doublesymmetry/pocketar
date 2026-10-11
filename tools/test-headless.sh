#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests/headless
run() {
    local name=$1
    shift
    local log="$PWD/build/tests/headless/$name.log"
    godot --headless --path demo --log-file "$log" "$@"
    if rg -q 'SCRIPT ERROR:|FAIL' "$log"; then
        exit 1
    fi
    rg -q 'PASS' "$log"
}
for name in packet_smoke ios_harness_args pocketar_args scripted_clock scripted_suite session_facade session_reentrancy; do
    run "$name" --script "$PWD/tests/headless/$name.gd"
done
run pocketar_args_values --script "$PWD/tests/headless/pocketar_args.gd" -- -game-level 'snow field' -game-seed 42
run ios_export_plugin --editor --script "$PWD/tests/headless/ios_export_plugin.gd"
run thread_guard --script "$PWD/tests/headless/thread_guard.gd"
run thread_guard_separate --render-thread separate --script "$PWD/tests/headless/thread_guard.gd" -- reject
