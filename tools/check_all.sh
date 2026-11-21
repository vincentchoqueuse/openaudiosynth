#!/bin/sh
# check_all.sh — everything that must pass before handing back, on every configured preset (CMakePresets.json):
# host build + ctest (unit tests, rules, API index, sound hashes, plugin, sequencer export), Debug build with the
# NaN guard + ctest, sanitizers (asan), ARM cross build, wasm build + Node smoke test, H745 firmware. A preset whose build
# directory does not exist is skipped (create them with `just setup`).
# Usage: sh tools/check_all.sh  (or `just check`; prints one summary line per step)
set -e
cd "$(dirname "$0")/.."
step() { printf '%-28s' "$1"; shift; if out=$("$@" 2>&1); then echo ok; else echo FAILED; echo "$out" | tail -30; exit 1; fi; }
# a build step fails if the build fails, or if the compiler printed a warning or an error
warn() { out=$("$@" 2>&1) || { echo "$out"; return 1; }; if echo "$out" | grep -E "(warning|error):"; then return 1; fi; }
step "host build"            warn cmake --build --preset host -j
step "host ctest"            ctest --preset host
if [ -d build-debug ]; then
  step "debug build"         warn cmake --build --preset debug -j
  step "debug ctest"         ctest --preset debug
fi
if [ -d build-asan ]; then   # AddressSanitizer + UndefinedBehaviorSanitizer on every unit test
  step "asan build"          warn cmake --build --preset asan -j
  step "asan tests"          ./build-asan/tests/synth_tests
fi
if [ -d build-arm ]; then
  step "arm build"           warn cmake --build --preset arm -j
fi
if [ -d build-wasm ]; then
  step "wasm build"          warn cmake --build --preset wasm -j
  step "wasm smoke test"     node web/test_wasm.mjs build-wasm/web/wasm/synth.wasm
  step "wasm song player"    node web/test_song.mjs build-wasm/web/wasm/synth.wasm
fi
if [ -d target/h745/Drivers ]; then  # STM32H745I-DISCO firmware, both cores (drivers copied by tools/stm32_drivers.sh)
  step "h745 build"          warn just h745
fi
