cmake_minimum_required(VERSION 3.22)

# Universal binary på macOS (Apple Silicon + Intel). Måste sättas före project().
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minsta macOS-version")

project(Slagg VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/juce)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

set(SLAGG_FORMATS VST3)
if(APPLE)
    list(APPEND SLAGG_FORMATS AU)
endif()

juce_add_plugin(Slagg
    # Tillverkar- och pluginkoden (Falk/Slgg) får ALDRIG ändras efter första release:
    # DAW:ar känner igen pluginet på dem, så sparade projekt fortsätter fungera.
    COMPANY_NAME "falk elektro"
    # BUNDLE_ID är osynligt och behålls från tiden som Falk Instruments (byte kan ge dubbla AU-plugins).
    BUNDLE_ID "com.falkinstruments.slagg"
    PLUGIN_MANUFACTURER_CODE Falk
    PLUGIN_CODE Slgg
    FORMATS ${SLAGG_FORMATS}
    PRODUCT_NAME "Slagg"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Drum
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(SLAGG_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/Pattern.cpp
    Source/Kits.cpp
    Source/Skin.cpp
    Source/SampleLayer.cpp)

target_sources(Slagg PRIVATE ${SLAGG_SOURCES})

# Inbäddat typsnitt (PT Sans Narrow, SIL Open Font License – se fonts/OFL.txt).
juce_add_binary_data(SlaggAssets
    NAMESPACE SlaggAssets
    SOURCES
        fonts/PT_Sans-Narrow-Web-Bold.ttf
        fonts/PT_Sans-Narrow-Web-Regular.ttf)
set_target_properties(SlaggAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(SLAGG_DEFINES
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    SLAGG_VERSION="${PROJECT_VERSION}")

target_compile_definitions(Slagg PUBLIC ${SLAGG_DEFINES})
target_link_libraries(Slagg
    PRIVATE
        SlaggAssets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(SLAGG_BUILD_TESTS "Bygg testprogrammet" ON)
if(SLAGG_BUILD_TESTS)
    juce_add_console_app(SlaggTests PRODUCT_NAME "SlaggTests")
    target_sources(SlaggTests PRIVATE tests/SlaggTests.cpp ${SLAGG_SOURCES})
    target_compile_definitions(SlaggTests PRIVATE ${SLAGG_DEFINES})
    target_link_libraries(SlaggTests
        PRIVATE
            SlaggAssets
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
