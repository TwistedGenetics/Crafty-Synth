# SPDX-License-Identifier: GPL-3.0-only
# Crafty Synth as an AU / VST3 plugin and a standalone app (macOS; the audio side also builds on Linux for the
# tests). The engine is the FM-1's own C (src/core, src/dsp) through src/host; the window is the web editor.
#   cmake -S ports/plugin -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/plugin
#   -DCRAFTY_JUCE_DIR=<a JUCE 8 checkout> uses that JUCE instead of downloading it.
#   -DCRAFTY_TESTS_ONLY=ON builds only the console test (no window, no WebView): tests/plugin_test.cpp.
# Copyright (C) 2026 Jamie (TwistedGenetics).
cmake_minimum_required(VERSION 3.22)

get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
file(READ "${REPO}/VERSION" CRAFTY_VERSION)
string(STRIP "${CRAFTY_VERSION}" CRAFTY_VERSION)

project(CraftySynth VERSION ${CRAFTY_VERSION} LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 99)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
option(CRAFTY_TESTS_ONLY "only the console test (no plugin, no window)" OFF)

set(CRAFTY_JUCE_DIR "" CACHE PATH "a JUCE checkout (else JUCE 8.0.4 is downloaded)")
if(CRAFTY_JUCE_DIR)
    add_subdirectory("${CRAFTY_JUCE_DIR}" juce)
else()
    include(FetchContent)
    FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

# the generated tables, as the FM-1 and the editor make them
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GEN "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${GEN}")
foreach(step "gen_wavetables.py;cr_wavetables.h" "gen_presets.py;cr_presets.h" "gen_presets.py;--keys;cr_keys.h")
    list(POP_BACK step out)
    list(POP_FRONT step script)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${REPO}/tools/${script}" ${step} "${GEN}/${out}"
                    WORKING_DIRECTORY "${REPO}" RESULT_VARIABLE r)
    if(NOT r EQUAL 0)
        message(FATAL_ERROR "tools/${script} failed")
    endif()
endforeach()

set(ENGINE
    ${REPO}/src/core/cr_patch.c ${REPO}/src/core/cr_arp.c ${REPO}/src/core/cr_text.c
    ${REPO}/src/dsp/cr_synth.c ${REPO}/src/dsp/cr_wt.c ${REPO}/src/dsp/cr_filter.c ${REPO}/src/dsp/cr_fx.c
    ${REPO}/src/host/cr_inst.c ${REPO}/src/host/cr_proto.c
    ${REPO}/src/host/cr_wav.c ${REPO}/src/host/cr_tables.c)
add_library(crafty_engine STATIC ${ENGINE})
target_include_directories(crafty_engine PUBLIC ${REPO}/src/core ${REPO}/src/dsp ${REPO}/src/host ${GEN})
set_target_properties(crafty_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
    target_compile_options(crafty_engine PRIVATE -O2 -Wall -Wno-unused-function)
endif()

set(JUCE_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    "CRAFTY_VERSION=\"${CRAFTY_VERSION}\"")

if(CRAFTY_TESTS_ONLY)
    juce_add_console_app(CraftyPluginTest PRODUCT_NAME "CraftyPluginTest")
    target_sources(CraftyPluginTest PRIVATE src/CraftyProcessor.cpp tests/plugin_test.cpp)
    target_compile_definitions(CraftyPluginTest PRIVATE ${JUCE_DEFS} CRAFTY_NO_EDITOR=1 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Build_Standalone=0)
    target_link_libraries(CraftyPluginTest PRIVATE crafty_engine juce::juce_audio_processors
        juce::juce_recommended_config_flags)
    return()
endif()

juce_add_plugin(CraftySynth
    PRODUCT_NAME "Crafty Synth"
    COMPANY_NAME "Twisted Genetics"
    BUNDLE_ID com.twistedgenetics.craftysynth
    PLUGIN_MANUFACTURER_CODE TwGn
    PLUGIN_CODE CrSy
    FORMATS AU VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_WEB_BROWSER TRUE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)

# the web editor, built in (the same files the site serves)
file(GLOB WEB_FILES ${REPO}/web/editor/*.html ${REPO}/web/editor/*.js ${REPO}/web/editor/*.css ${REPO}/web/editor/*.wasm)
juce_add_binary_data(CraftyWeb SOURCES ${WEB_FILES})

target_sources(CraftySynth PRIVATE src/CraftyProcessor.cpp src/CraftyEditor.cpp)
list(REMOVE_ITEM JUCE_DEFS JUCE_WEB_BROWSER=0)
target_compile_definitions(CraftySynth PUBLIC ${JUCE_DEFS} JUCE_WEB_BROWSER=1)
target_link_libraries(CraftySynth PRIVATE crafty_engine CraftyWeb juce::juce_audio_utils juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
