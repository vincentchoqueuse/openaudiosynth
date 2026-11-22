# synth — host build (macOS) and cross build (arm-none-eabi, DSP/MIDI libraries only).
cmake_minimum_required(VERSION 3.20)
project(synth C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CheckCCompilerFlag)
add_compile_options(-Wall -Wextra -Wdouble-promotion)
# Unsuffixed constants treated as float (gcc; clang ignores this flag): DSP and MIDI only,
# tests and host compute their references in double.
check_c_compiler_flag("-fsingle-precision-constant -Werror" HAVE_SINGLE_PRECISION_CONSTANT)
# -ffp-contract=off: never fuse a*b+c into one FMA behind our back. Fusing depends on the compiler and the
# optimization level (clang -O0 vs -O2, arm-none-eabi-gcc fuses by default), so the same code would round
# differently on the host, in Debug and on the board; off = bit-identical results everywhere.
set(DSP_STRICT_FLAGS -Wpedantic -Wshadow -ffp-contract=off)
if(HAVE_SINGLE_PRECISION_CONSTANT)
  list(APPEND DSP_STRICT_FLAGS -fsingle-precision-constant)
endif()

# ---- dsp: no dependencies (except libm at init) ----
include(cmake/synth_sources.cmake)
list(TRANSFORM SYNTH_DSP_SOURCES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)
add_library(synth_dsp STATIC ${SYNTH_DSP_SOURCES})
target_include_directories(synth_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dsp)
# NaN/Inf guard in Debug builds only
target_compile_definitions(synth_dsp PUBLIC $<$<CONFIG:Debug>:SYNTH_NAN_GUARD>)
target_compile_options(synth_dsp PRIVATE ${DSP_STRICT_FLAGS})

add_library(synth_midi STATIC ${SYNTH_MIDI_SOURCES})
target_include_directories(synth_midi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/midi)
target_compile_options(synth_midi PRIVATE ${DSP_STRICT_FLAGS})

# Portable host layer (no OS calls but stdio/malloc): JSON, catalog lookups, patches, sequences.
# Shared by the macOS tools and the web build.
set(SYNTH_HOST_CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/host/demos.c ${CMAKE_CURRENT_SOURCE_DIR}/host/json.c
  ${CMAKE_CURRENT_SOURCE_DIR}/host/catalog.c ${CMAKE_CURRENT_SOURCE_DIR}/host/patch.c
  ${CMAKE_CURRENT_SOURCE_DIR}/host/song.c)

if(EMSCRIPTEN)
  add_subdirectory(web/wasm) # browser build: synth.wasm for the AudioWorklet (web/README.md)
  return()
endif()

if(CMAKE_CROSSCOMPILING)
  return() # on target: only the libraries are built (CubeMX projects in target/)
endif()

target_compile_definitions(synth_dsp PUBLIC DSP_HOST)

# ---- host ----
add_library(synth_host_core STATIC ${SYNTH_HOST_CORE_SOURCES})
target_include_directories(synth_host_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/host)
target_link_libraries(synth_host_core PUBLIC synth_dsp)
add_library(synth_host_util STATIC host/wav.c host/fm6_sysex.c host/kitfile.c)
target_link_libraries(synth_host_util PUBLIC synth_host_core)

add_executable(synth_link host/synth_link.c) # drives the boards over Ethernet (midi/link.h)
target_link_libraries(synth_link PRIVATE synth_host_util synth_midi)
set_target_properties(synth_link PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/host)

# demo sample kits recorded from the drums engine (build-host/kits/synth_*), played by the sampler's patches
add_executable(kit_demo host/kit_demo.c)
target_link_libraries(kit_demo PRIVATE synth_host_util)
set_target_properties(kit_demo PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/host)
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/kits/synth_analog.oidk
  COMMAND kit_demo ${CMAKE_BINARY_DIR}/kits DEPENDS kit_demo COMMENT "Recording the demo sample kits")
add_custom_target(kits_demo ALL DEPENDS ${CMAKE_BINARY_DIR}/kits/synth_analog.oidk)

add_executable(synth_render host/synth_render.c)
target_link_libraries(synth_render PRIVATE synth_host_util)
add_executable(fm6_import host/fm6_import.c)
target_link_libraries(fm6_import PRIVATE synth_host_util)
set_target_properties(fm6_import PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/host)
add_executable(synth_catalog host/synth_catalog.c)
target_link_libraries(synth_catalog PRIVATE synth_host_util)
set_target_properties(synth_catalog PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/host)

if(APPLE)
  add_executable(synth_play host/synth_play.c)
  target_link_libraries(synth_play PRIVATE synth_host_util synth_midi
    "-framework CoreAudio" "-framework AudioToolbox" "-framework CoreMIDI" "-framework CoreFoundation")
  set_target_properties(synth_play PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/host)
endif()
set_target_properties(synth_render PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/host)

# ---- generated documentation, refreshed by every host build: docs/catalog.md and docs/catalog.json (from the
# descriptor tables, through synth_catalog) and docs/api.md (from the headers, tools/gen_api.py) ----
file(GLOB_RECURSE SYNTH_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dsp/*.h ${CMAKE_CURRENT_SOURCE_DIR}/midi/*.h)
# Prefer the project's virtual environment (tools/requirements.txt) over the system Python
if(NOT Python3_EXECUTABLE AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.venv/bin/python3)
  set(Python3_EXECUTABLE ${CMAKE_CURRENT_SOURCE_DIR}/.venv/bin/python3)
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/docs.stamp
  COMMAND $<TARGET_FILE:synth_catalog> > ${CMAKE_CURRENT_SOURCE_DIR}/docs/catalog.md
  COMMAND $<TARGET_FILE:synth_catalog> --json > ${CMAKE_CURRENT_SOURCE_DIR}/docs/catalog.json
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_api.py
  COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/docs.stamp
  DEPENDS synth_catalog ${SYNTH_HEADERS} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_api.py
  COMMENT "Refreshing docs/catalog.md, docs/catalog.json and docs/api.md"
  VERBATIM)
add_custom_target(docs ALL DEPENDS ${CMAKE_BINARY_DIR}/docs.stamp)

# ---- board presets: patches/*.json as a constant table for the STM32 boards (target/h745/app/presets_gen.c) ----
add_executable(board_presets host/board_presets.c)
target_link_libraries(board_presets PRIVATE synth_host_util)
set_target_properties(board_presets PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/host)
file(GLOB SYNTH_PATCHES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/patches/*.json)
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/board_presets.stamp
  COMMAND $<TARGET_FILE:board_presets> ${CMAKE_CURRENT_SOURCE_DIR}/patches
          ${CMAKE_CURRENT_SOURCE_DIR}/target/h745/app/presets_gen.c
  COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/board_presets.stamp
  DEPENDS board_presets ${SYNTH_PATCHES}
  COMMENT "Refreshing target/h745/app/presets_gen.c"
  VERBATIM)
add_custom_target(board_presets_gen ALL DEPENDS ${CMAKE_BINARY_DIR}/board_presets.stamp)

# ---- examples ----
foreach(ex minimal automation)
  add_executable(example_${ex} examples/${ex}.c)
  target_link_libraries(example_${ex} PRIVATE synth_host_util)
  set_target_properties(example_${ex} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples)
endforeach()

# ---- tests ----
enable_testing()

# ---- CLAP plugin (plugin/, SDK headers in third_party/clap) and its test host ----
add_subdirectory(plugin)
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c)
add_executable(synth_tests ${TEST_SOURCES})
target_link_libraries(synth_tests PRIVATE synth_host_util synth_midi m)
set_target_properties(synth_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/tests/suites.h SUITE_LINES REGEX "^TEST_SUITE\\(")
string(REGEX REPLACE "TEST_SUITE\\(([a-z0-9_]+)\\)" "\\1" SUITES "${SUITE_LINES}")
foreach(suite ${SUITES})
  add_test(NAME ${suite} COMMAND synth_tests ${suite})
endforeach()

# Open format: every patch and song validates against spec/schemas and the instrument description
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME spec_validate
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate.py
                   --catalog-cmd $<TARGET_FILE:synth_catalog>
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  # Code rules of CLAUDE.md (portable dsp/, no libm/allocation in the audio code, headers, engines registered)
  add_test(NAME rules
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_rules.py)
  # docs/api.md (index of the portable API, generated from the headers) is up to date
  add_test(NAME api
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_api.py --check)
  # Sound regression guard: every patch and song renders without warning and keeps its audio hash
  # (tests/golden.json; record an intended change with tools/golden.py --update)
  # Web sequencer: an exported song (inline patch, one track per row) validates and renders (needs Node)
  find_program(NODE_EXECUTABLE node)
  if(NODE_EXECUTABLE)
    add_test(NAME seq_export
             COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/web/test_seq_export.mjs $<TARGET_FILE:synth_render>
                     $<TARGET_FILE:synth_catalog> ${Python3_EXECUTABLE}
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    # the piano roll's note operations (pure functions)
    add_test(NAME note_ops COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/web/test_note_ops.mjs
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endif()
  add_test(NAME golden
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/golden.py
                   --render $<TARGET_FILE:synth_render>)
endif()
