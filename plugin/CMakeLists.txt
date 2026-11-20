# plugin/ — CLAP plugin of the engine (synth_clap.c) and a test host that loads it (test_clap_host.c).
# The plugin is built as OASynth.clap (a bundle on macOS); install: copy it to ~/Library/Audio/Plug-Ins/CLAP/.

add_library(synth_clap MODULE synth_clap.c)
target_include_directories(synth_clap PRIVATE ${PROJECT_SOURCE_DIR}/third_party/clap/include)
target_link_libraries(synth_clap PRIVATE synth_host_util) # patches, sample kits (host/kitfile.c)
target_compile_options(synth_clap PRIVATE -Wpedantic -Wshadow)
set_target_properties(synth_clap PROPERTIES OUTPUT_NAME OASynth PREFIX "" C_VISIBILITY_PRESET hidden)
if(APPLE)
  set_target_properties(synth_clap PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION clap
    MACOSX_BUNDLE_GUI_IDENTIFIER synth.engine MACOSX_BUNDLE_BUNDLE_NAME OASynth
    MACOSX_BUNDLE_SHORT_VERSION_STRING 0.1.0 MACOSX_BUNDLE_BUNDLE_VERSION 0.1.0)
  target_link_options(synth_clap PRIVATE -Wl,-exported_symbol,_clap_entry)
  # the demo sample kits inside the bundle (Contents/Resources/kits/NAME.oidk)
  add_dependencies(synth_clap kits_demo)
  add_custom_command(TARGET synth_clap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_BUNDLE_CONTENT_DIR:synth_clap>/Resources/kits
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/kits/synth_analog.oidk ${CMAKE_BINARY_DIR}/kits/synth_hybrid.oidk
            ${CMAKE_BINARY_DIR}/kits/synth_glitch.oidk $<TARGET_BUNDLE_CONTENT_DIR:synth_clap>/Resources/kits/)
  set(SYNTH_CLAP_BINARY $<TARGET_FILE:synth_clap>)
else()
  set_target_properties(synth_clap PROPERTIES SUFFIX ".clap")
endif()

# test host: loads the built plugin and checks it against the engine called directly (bit for bit)
add_executable(test_clap_host test_clap_host.c)
target_include_directories(test_clap_host PRIVATE ${PROJECT_SOURCE_DIR}/third_party/clap/include)
target_link_libraries(test_clap_host PRIVATE synth_dsp ${CMAKE_DL_LIBS})
add_dependencies(test_clap_host synth_clap)
add_test(NAME clap COMMAND test_clap_host $<TARGET_FILE:synth_clap>)
