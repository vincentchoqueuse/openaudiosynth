# app_cm7.cmake — adds the synth to CubeMX's M7 project (included from CM7/CMakeLists.txt): the DSP engine
# from the repository (same sources as every other build, cmake/synth_sources.cmake), ST's board support
# package for the audio output, and this application.
set(SYNTH_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(H745_APP ${CMAKE_CURRENT_LIST_DIR})
set(H745_DRIVERS ${CMAKE_CURRENT_LIST_DIR}/../Drivers)

# Board memory: one part in the DTCM (~74 KB of state), the effect memory in the AXI SRAM, no wavetables
# (the 12 MB arena of `wt` needs the SDRAM, a later step).
set(SYNTH_BOARD_DEFINES
    SYNTH_PARTS=1
    SYNTH_FX_MEM_FLOATS=106144
    SYNTH_FX_REVERB_SIZE=1.5f
    SYNTH_ENGINE_MEM_FLOATS=1
    VA_STEREO=0
    "DSP_LARGE_DATA=__attribute__((section(\".RAM_D1\")))"
)

include(${SYNTH_ROOT}/cmake/synth_sources.cmake)
list(TRANSFORM SYNTH_DSP_SOURCES PREPEND ${SYNTH_ROOT}/)
add_library(synth_dsp STATIC ${SYNTH_DSP_SOURCES})
target_include_directories(synth_dsp PUBLIC ${SYNTH_ROOT}/dsp)
target_compile_definitions(synth_dsp PUBLIC ${SYNTH_BOARD_DEFINES})
# Debug: each voice's output is checked for NaN / Inf, zeroed and counted (as on the host)
target_compile_definitions(synth_dsp PRIVATE $<$<CONFIG:Debug>:SYNTH_NAN_GUARD>)
# always optimized, also in a Debug build of the board (the DSP at -O0 would not keep up)
# -fno-tree-loop-distribute-patterns: the small block loops (zeroing, copying 32 samples) stay loops instead of
# becoming calls to newlib-nano's memset/memcpy, which are written for size and go byte by byte
target_compile_options(synth_dsp PRIVATE ${SYNTH_DSP_FLAGS} -fsingle-precision-constant -O3
    -fno-tree-loop-distribute-patterns)

# ST board support package: audio output only (plus the PDM microphone filter its audio file links to)
set(BSP ${H745_DRIVERS}/BSP/STM32H745I-DISCO)
set(PDM ${H745_DRIVERS}/Middlewares/ST/STM32_Audio/Addons/PDM)
add_library(h745_bsp STATIC
    ${BSP}/stm32h745i_discovery_audio.c
    ${BSP}/stm32h745i_discovery_bus.c
    ${H745_DRIVERS}/BSP/Components/wm8994/wm8994.c
    ${H745_DRIVERS}/BSP/Components/wm8994/wm8994_reg.c
)
target_include_directories(h745_bsp PUBLIC ${H745_APP} ${BSP} ${PDM}/Inc)
target_link_libraries(h745_bsp PUBLIC stm32cubemx ${PDM}/Lib/libPDMFilter_CM7_GCC_wc32.a)
# HAL modules the audio BSP needs: enabled here when the CubeMX configuration leaves the peripheral to the
# BSP (stm32h7xx_hal_conf.h then has them commented out). Their sources are added in any case (same
# spelling as mx-generated.cmake, so a file listed by both is compiled once).
set(H745_HAL ${CMAKE_CURRENT_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc/stm32h7xx_hal_conf.h H745_M7_HALCONF)
foreach(mod SAI I2C DMA)
  if(NOT H745_M7_HALCONF MATCHES "\n#define HAL_${mod}_MODULE_ENABLED")
    target_compile_definitions(stm32cubemx INTERFACE HAL_${mod}_MODULE_ENABLED)
  endif()
endforeach()
target_sources(h745_bsp PRIVATE ${H745_HAL}/stm32h7xx_hal_sai.c ${H745_HAL}/stm32h7xx_hal_sai_ex.c
    ${H745_HAL}/stm32h7xx_hal_i2c.c ${H745_HAL}/stm32h7xx_hal_i2c_ex.c)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${H745_APP}/app.c ${H745_APP}/audio.c ${H745_APP}/presets_gen.c
    ${H745_APP}/profile.c ${H745_APP}/knobs.c)
# the audio interrupt code is optimized in every build type, like the DSP
set_source_files_properties(${H745_APP}/app.c ${H745_APP}/audio.c PROPERTIES COMPILE_OPTIONS "-O2;-Wextra;-Wdouble-promotion")
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${H745_APP} ${SYNTH_ROOT}/midi) # midi/: the network queue (linkq.h)
target_link_libraries(${CMAKE_PROJECT_NAME} synth_dsp h745_bsp)

