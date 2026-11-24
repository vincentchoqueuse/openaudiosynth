# justfile — the verbs of the project: `just` lists them. Builds use CMakePresets.json (build-host, build-debug,
# build-asan, build-arm, build-wasm), so a build directory can always be recreated with `cmake --preset <name>`.

set shell := ["bash", "-uc"]
export EMSCRIPTEN_ROOT := `em-config EMSCRIPTEN_ROOT 2>/dev/null || true`

# list the recipes
default:
    @just --list --unsorted

# one-time setup: Python venv, web dependencies, configure every preset whose toolchain is installed
setup:
    [ -d .venv ] || python3 -m venv .venv
    .venv/bin/pip install -q -r tools/requirements.txt
    npm --prefix web/ui install --silent
    cmake --preset host
    cmake --preset debug
    cmake --preset asan
    @if ls ~/opt/arm-gnu-toolchain-*/bin/arm-none-eabi-gcc /Applications/ArmGNUToolchain/*/arm-none-eabi/bin/arm-none-eabi-gcc >/dev/null 2>&1 || command -v arm-none-eabi-gcc >/dev/null; then cmake --preset arm; else echo "arm: no Arm GNU Toolchain (see CLAUDE.md), skipped"; fi
    @if [ -n "$EMSCRIPTEN_ROOT" ]; then cmake --preset wasm; else echo "wasm: emscripten missing (brew install emscripten), skipped"; fi

# build the host tools, tests and plugin
build:
    cmake --build --preset host -j

# unit tests, code rules, API index, audio fingerprints, plugin, sequencer export
test: build
    ctest --preset host

# everything that must pass before handing back (host, Debug, sanitizers, ARM, wasm)
check:
    sh tools/check_all.sh

# drive a board over Ethernet (docs/link.md), e.g. just link --iface en5 discover
link *args: build
    ./build-host/host/synth_link {{args}}

# render a patch's audition to build-host/renders/<patch>.wav (and play it)
render patch: build
    mkdir -p build-host/renders
    ./build-host/host/synth_render --patch patches/{{patch}}.json --out build-host/renders/{{patch}}.wav --tail 3
    afplay build-host/renders/{{patch}}.wav

# render a song to build-host/renders/song_<name>.wav (and play it)
song name: build
    mkdir -p build-host/renders
    ./build-host/host/synth_render --song songs/{{name}}.json --out build-host/renders/song_{{name}}.wav
    afplay build-host/renders/song_{{name}}.wav

# play live with a MIDI controller
play engine="va": build
    ./build-host/host/synth_play --engine {{engine}}

# record an intended change of sound in tests/golden.json
golden: build
    .venv/bin/python tools/golden.py --render build-host/host/synth_render --update

# build the wasm engine (the dev server serves it from build-wasm)
wasm:
    cmake --build --preset wasm -j

# browser prototype with live reload (http://localhost:5173)
web: wasm
    npm --prefix web/ui run dev

# static build of the browser prototype in web/ui/dist
web-dist: wasm
    npm --prefix web/ui run build

# build and install the CLAP plugin in ~/Library/Audio/Plug-Ins/CLAP
plugin-install: build
    mkdir -p ~/Library/Audio/Plug-Ins/CLAP
    rm -rf ~/Library/Audio/Plug-Ins/CLAP/OASynth.clap
    cp -R build-host/plugin/OASynth.clap ~/Library/Audio/Plug-Ins/CLAP/
    ./build-host/plugin/test_clap_host ~/Library/Audio/Plug-Ins/CLAP/OASynth.clap

# remove objects, renders, plots and caches
clean:
    sh tools/clean.sh

# the same, and the build directories too (recreate them with `just setup`)
clean-all:
    sh tools/clean.sh --all

# STM32H745I-DISCO firmware (both cores) in target/h745/build; restores the ST drivers first
h745 type="Debug":
    sh tools/stm32_drivers.sh h745 >/dev/null
    PATH="$(ls -d ~/opt/arm-gnu-toolchain-*/bin /Applications/ArmGNUToolchain/*/arm-none-eabi/bin 2>/dev/null | tail -1):$PATH" sh -c 'cmake -S target/h745 -B target/h745/build -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE={{type}} >/dev/null && cmake --build target/h745/build -j'

# flash both cores of the H745 through the ST-LINK (STM32CubeProgrammer, or STM32CubeCLT which contains it)
flash: h745
    cli=$(ls /Applications/STMicroelectronics/STM32Cube/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/Resources/bin/STM32_Programmer_CLI /opt/ST/STM32CubeCLT_*/STM32CubeProgrammer/bin/STM32_Programmer_CLI 2>/dev/null | tail -1); [ -n "$cli" ] || { echo "STM32_Programmer_CLI not found: install STM32CubeProgrammer"; exit 1; }; "$cli" -c port=SWD mode=UR -w target/h745/CM7/build/h745_CM7.elf -w target/h745/CM4/build/h745_CM4.elf -rst
