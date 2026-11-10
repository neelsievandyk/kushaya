# =============================================================================
# CMakeLists.txt - build recipe for the Kushaya plugin.
#
# You normally don't run CMake by hand. Use:   ./scripts/build_plugin.sh
#
# What gets built:
#   Kushaya.vst3   - the plugin, for Studio One and other VST3 hosts
#   Kushaya.app    - a standalone version, handy for quick tests without a DAW
#   KushayaSnapshot, KushayaHostTest - small developer test programs
# =============================================================================

cmake_minimum_required(VERSION 3.22)

# Oldest macOS version the plugin will run on. Must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

# Build for Apple Silicon only, which keeps builds fast.
# Before releasing to other people, change this to "arm64;x86_64" so the
# plugin also runs on Intel Macs.
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "CPU types to build for")

project(Kushaya VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is the framework that turns our code into a VST3. The build script
# downloads it into build.nosync/JUCE.
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build.nosync/JUCE" CACHE PATH "Where JUCE lives")
add_subdirectory("${JUCE_DIR}" juce)

# -----------------------------------------------------------------------------
# The plugin
# -----------------------------------------------------------------------------
juce_add_plugin(Kushaya
    PRODUCT_NAME "Kushaya"
    COMPANY_NAME "Kushaya"
    BUNDLE_ID "com.kushaya.kushaya"

    # Four-letter IDs that DAWs use to recognise the plugin. Never change
    # these after releasing, or saved projects will stop finding it.
    PLUGIN_MANUFACTURER_CODE Kshy
    PLUGIN_CODE Kush

    FORMATS VST3 Standalone

    # An audio effect (it sits on your microphone track) that sends MIDI out.
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Tools

    # The standalone app needs permission to use the microphone.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Kushaya listens to your microphone to turn beatboxing into MIDI drum notes."

    # After every build, install the plugin to ~/Library/Audio/Plug-Ins/VST3
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(Kushaya PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_compile_definitions(Kushaya PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Kushaya
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# -----------------------------------------------------------------------------
# Developer tool: draws the plugin window into a .png file, so the design
# can be checked without opening a DAW.
# -----------------------------------------------------------------------------
add_executable(KushayaSnapshot tools/ui_snapshot.cpp)
target_link_libraries(KushayaSnapshot PRIVATE Kushaya)

# Compile this tool with the same JUCE include folders and settings as the
# plugin itself, so it can use the plugin's classes directly.
target_include_directories(KushayaSnapshot PRIVATE $<TARGET_PROPERTY:Kushaya,INCLUDE_DIRECTORIES>)
target_compile_definitions(KushayaSnapshot PRIVATE $<TARGET_PROPERTY:Kushaya,COMPILE_DEFINITIONS>)

# -----------------------------------------------------------------------------
# Developer tool: loads the finished Kushaya.vst3 the way a DAW would, plays
# .wav files through it and prints the MIDI notes that come out.
# -----------------------------------------------------------------------------
juce_add_console_app(KushayaHostTest PRODUCT_NAME "KushayaHostTest")
target_sources(KushayaHostTest PRIVATE tools/host_test.cpp)
target_compile_definitions(KushayaHostTest PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(KushayaHostTest PRIVATE
    juce::juce_audio_processors
    juce::juce_recommended_config_flags)
