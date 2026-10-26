cmake_minimum_required(VERSION 3.22)

project(NeuroDrop VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Statische MSVC-Runtime: kein VC-Redistributable auf Windows noetig
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(NEURODROP_BUILD_TESTS "Konsolen-Testprogramm (Analyse + Render) bauen" ON)
option(NEURODROP_BUILD_PLUGIN "VST3-Plugin bauen" ON)

include(FetchContent)
# Lokaler JUCE-Ordner moeglich: -DFETCHCONTENT_SOURCE_DIR_JUCE=/pfad/zu/JUCE
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

set(NEURODROP_ENGINE_SOURCES
    Source/Engine/Analysis.cpp
    Source/Engine/DropGenerator.cpp
    Source/Engine/Patterns.cpp
    Source/Engine/Voices.cpp
    Source/Engine/StemExporter.cpp)

set(NEURODROP_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# ---------------------------------------------------------------- Plugin
if(NEURODROP_BUILD_PLUGIN)
juce_add_plugin(NeuroDrop
    COMPANY_NAME            "JustCreateit"
    COMPANY_WEBSITE         "https://justcreateit.ch"
    PLUGIN_MANUFACTURER_CODE Jcit
    PLUGIN_CODE             NrDp
    FORMATS                 VST3
    PRODUCT_NAME            "NeuroDrop"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES         Fx Analyzer Generator
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(NeuroDrop PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/NeuroLookAndFeel.cpp
    Source/UI/Panels.cpp
    ${NEURODROP_ENGINE_SOURCES})

target_include_directories(NeuroDrop PRIVATE Source)
target_compile_definitions(NeuroDrop PUBLIC ${NEURODROP_COMMON_DEFS})

target_link_libraries(NeuroDrop
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------- Tests
if(NEURODROP_BUILD_TESTS)
    juce_add_console_app(NeuroDropTest PRODUCT_NAME "NeuroDropTest")
    target_sources(NeuroDropTest PRIVATE Tests/TestMain.cpp ${NEURODROP_ENGINE_SOURCES})
    target_include_directories(NeuroDropTest PRIVATE Source)
    target_compile_definitions(NeuroDropTest PRIVATE ${NEURODROP_COMMON_DEFS})
    target_link_libraries(NeuroDropTest
        PRIVATE
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------- Host-Integrationstest
option(NEURODROP_BUILD_HOSTCHECK "Host-Integrationstest (Prozessor, UI-Screenshot, VST3 laden)" OFF)
if(NEURODROP_BUILD_HOSTCHECK)
    juce_add_console_app(NeuroDropHostCheck PRODUCT_NAME "NeuroDropHostCheck")
    target_sources(NeuroDropHostCheck PRIVATE
        Tests/HostCheck.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/UI/NeuroLookAndFeel.cpp
        Source/UI/Panels.cpp
        ${NEURODROP_ENGINE_SOURCES})
    target_include_directories(NeuroDropHostCheck PRIVATE Source)
    target_compile_definitions(NeuroDropHostCheck PRIVATE ${NEURODROP_COMMON_DEFS}
        JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(NeuroDropHostCheck
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_audio_formats
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
