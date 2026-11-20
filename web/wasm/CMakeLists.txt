# web/wasm — browser build of the engine (emscripten): one standalone synth.wasm with no JS glue,
# instantiated by hand inside the AudioWorklet (web/ui/public/synth-worklet.js).
#   just wasm   (or: cmake --preset wasm && cmake --build --preset wasm); the dev server serves it from build-wasm

add_library(synth_host_core STATIC ${SYNTH_HOST_CORE_SOURCES})
target_include_directories(synth_host_core PUBLIC ${PROJECT_SOURCE_DIR}/host)
target_link_libraries(synth_host_core PUBLIC synth_dsp)

add_executable(synth_wasm synth_wasm.c)
target_link_libraries(synth_wasm PRIVATE synth_host_core)
target_compile_options(synth_wasm PRIVATE -Wpedantic -Wshadow)
set_target_properties(synth_wasm PROPERTIES OUTPUT_NAME synth SUFFIX ".wasm")
# Standalone module: no emscripten JS runtime (it does not load inside an AudioWorkletGlobalScope);
# the few WASI imports (stdio) are stubbed by the worklet. Static state is ~14 MB (wavetables, delay).
target_link_options(synth_wasm PRIVATE
  -sSTANDALONE_WASM=1 --no-entry
  -sINITIAL_MEMORY=33554432 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=262144
  -sFILESYSTEM=0 -sERROR_ON_UNDEFINED_SYMBOLS=1)
