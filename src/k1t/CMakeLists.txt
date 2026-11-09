cmake_minimum_required(VERSION 3.22)
project(K1T VERSION 3.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
endif()
add_subdirectory(${JUCE_DIR} JUCE)

if(APPLE)
    set(K1T_FORMATS VST3 AU Standalone)
else()
    set(K1T_FORMATS VST3 Standalone)
endif()

juce_add_plugin(K1T
    COMPANY_NAME "clideurbeats"
    PLUGIN_MANUFACTURER_CODE Cldr
    PLUGIN_CODE K1tz
    FORMATS ${K1T_FORMATS}
    PRODUCT_NAME "K1T"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Drum Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    BUNDLE_ID com.clideurbeats.k1t)

target_sources(K1T PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Engine.cpp
    Source/Flp.cpp
    Source/Widgets.cpp
    Source/Pages.cpp
    Source/Updater.cpp)

target_compile_definitions(K1T PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(UNIX AND NOT APPLE) # build de test Linux uniquement
    target_compile_definitions(K1T PUBLIC JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0
        JUCE_USE_XRENDER=0 JUCE_USE_XCURSOR=0 JUCE_ALSA=0 JUCE_JACK=0)
endif()

target_link_libraries(K1T PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

option(K1T_TESTS "Build the headless test harness" OFF)
if(K1T_TESTS)
    juce_add_console_app(K1TTest PRODUCT_NAME "K1TTest")
    target_sources(K1TTest PRIVATE Test/TestMain.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp
        Source/Engine.cpp Source/Flp.cpp Source/Widgets.cpp Source/Pages.cpp Source/Updater.cpp)
    target_compile_definitions(K1TTest PRIVATE JucePlugin_Build_Standalone=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_MP3AUDIOFORMAT=1 JucePlugin_Name="K1T")
    target_link_libraries(K1TTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
