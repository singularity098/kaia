cmake_minimum_required(VERSION 3.22)

project(Kaia VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(KAIA_BUILD_CLAP  "Build the CLAP plugin (needs clap-juce-extensions)" ON)
option(KAIA_BUILD_TESTS "Build the offline test runner" ON)
option(KAIA_BUILD_PLUGIN "Build the plugin" ON)
option(KAIA_PORTABLE_LINUX "Linux: static libstdc++ and an older glibc baseline for portable binaries" OFF)

set(KAIA_JUCE_DIR "" CACHE PATH "Path to a JUCE checkout (downloaded automatically when empty)")
set(KAIA_CLAP_EXT_DIR "" CACHE PATH "Path to clap-juce-extensions (downloaded automatically when empty)")

include(FetchContent)

# ---------------------------------------------------------------------------
# JUCE
# ---------------------------------------------------------------------------
if(KAIA_JUCE_DIR)
    add_subdirectory(${KAIA_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.10
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(KAIA_BUILD_PLUGIN AND KAIA_BUILD_CLAP)
    if(KAIA_CLAP_EXT_DIR)
        add_subdirectory(${KAIA_CLAP_EXT_DIR} ${CMAKE_BINARY_DIR}/clap-juce-extensions EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(clap-juce-extensions
            GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
            GIT_TAG        7adee3a1bd4684d4caa5601100e364abccff4b4d
            GIT_SUBMODULES_RECURSE TRUE)
        FetchContent_MakeAvailable(clap-juce-extensions)
    endif()
endif()

# ---------------------------------------------------------------------------
# Sources shared by the plugin and the test runner
# ---------------------------------------------------------------------------
set(KAIA_CORE_SOURCES
    Source/Model/Kit.cpp
    Source/Model/TriggerMap.cpp
    Source/Model/UserFiles.cpp
    Source/Formats/ZipReader.cpp
    Source/Formats/SampleSource.cpp
    Source/Formats/SndDecoder.cpp
    Source/Formats/XpmParser.cpp
    Source/Formats/PgmParser.cpp
    Source/Formats/KitLoader.cpp
    Source/Formats/Library.cpp
    Source/Engine/SincTable.cpp
    Source/Engine/KitRuntime.cpp
    Source/Engine/SamplerEngine.cpp)

# Let the compiler vectorise the interpolation dot products (reassociating float sums is fine here)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(Source/Engine/SamplerEngine.cpp PROPERTIES
        COMPILE_OPTIONS "-O3;-fassociative-math;-fno-signed-zeros;-fno-trapping-math")
endif()

set(KAIA_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/UI/LookAndFeel.cpp
    Source/UI/PadGrid.cpp
    Source/UI/PadEditor.cpp
    Source/UI/ProgramBrowser.cpp
    Source/UI/KineticScroller.cpp
    Source/UI/LibraryBrowser.cpp
    Source/UI/SettingsPanel.cpp
    Source/UI/PluginEditor.cpp)

# Jost, the UI font (static instances made by tools/make_fonts.py)
if(KAIA_BUILD_PLUGIN OR KAIA_BUILD_HOST_TEST)
    juce_add_binary_data(KaiaFonts
        HEADER_NAME KaiaFonts.h
        NAMESPACE KaiaFontData
        SOURCES
            Resources/Fonts/Jost-Light.ttf
            Resources/Fonts/Jost-Regular.ttf
            Resources/Fonts/Jost-Bold.ttf)

    # The wordmark of the header logo (tools/make_logo.py; the pad K is drawn in code)
    juce_add_binary_data(KaiaLogo
        HEADER_NAME KaiaLogo.h
        NAMESPACE KaiaLogoData
        SOURCES
            Resources/Logo/kaia-wordmark.svg)
endif()

set(KAIA_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_VST3_CAN_REPLACE_VST2=0)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(KAIA_BUILD_PLUGIN)
if(APPLE)
    set(KAIA_FORMATS VST3 AU Standalone)
elseif(WIN32)
    set(KAIA_FORMATS VST3 Standalone)
else()
    set(KAIA_FORMATS VST3 LV2 Standalone)
endif()

juce_add_plugin(Kaia
    COMPANY_NAME                "Kaia"
    COMPANY_WEBSITE             "https://github.com/singularity098/kaia"
    BUNDLE_ID                   "io.github.singularity098.kaia"
    PLUGIN_MANUFACTURER_CODE    Kaia
    PLUGIN_CODE                 Kai1
    FORMATS                     ${KAIA_FORMATS}
    PRODUCT_NAME                "Kaia"
    DESCRIPTION                 "Drum sampler for MPC programs (XPM, XPN, PGM)"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Sampler Drum
    LV2URI                      "https://github.com/singularity098/kaia"
    LV2_SHARED_LIBRARY_NAME     Kaia
    ICON_BIG                    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Logo/kaia-app-icon-512.png"
    ICON_SMALL                  "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Logo/kaia-app-icon-64.png")

target_sources(Kaia PRIVATE ${KAIA_CORE_SOURCES} ${KAIA_PLUGIN_SOURCES})
target_include_directories(Kaia PRIVATE Source)
target_compile_definitions(Kaia PUBLIC ${KAIA_COMMON_DEFS} JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(Kaia
    PRIVATE
        KaiaFonts
        KaiaLogo
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(KAIA_PORTABLE_LINUX AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(Kaia PUBLIC -include ${CMAKE_CURRENT_SOURCE_DIR}/cmake/glibc_compat.h)
    target_sources(Kaia PRIVATE cmake/glibc_compat.c)
    set_source_files_properties(cmake/glibc_compat.c PROPERTIES COMPILE_OPTIONS "-std=gnu11;-fno-lto;-fno-builtin;-U_GNU_SOURCE")
    target_link_options(Kaia PUBLIC -static-libstdc++ -static-libgcc -Wl,-u,kaia_glibc_compat_anchor)
endif()

if(KAIA_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET Kaia
        CLAP_ID "io.github.singularity098.kaia"
        CLAP_FEATURES instrument sampler drum-machine stereo)
endif()
endif()

# ---------------------------------------------------------------------------
# Offline test runner (parsers + engine, no GUI)
# ---------------------------------------------------------------------------
if(KAIA_BUILD_TESTS)
    juce_add_console_app(KaiaTests PRODUCT_NAME "KaiaTests")
    target_sources(KaiaTests PRIVATE ${KAIA_CORE_SOURCES} Tests/TestMain.cpp)
    target_include_directories(KaiaTests PRIVATE Source)
    target_compile_definitions(KaiaTests PRIVATE ${KAIA_COMMON_DEFS} JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(KaiaTests
        PRIVATE
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_data_structures
            juce::juce_events
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
# Host test: loads the built VST3/LV2 binaries like a DAW (development only)
# ---------------------------------------------------------------------------
option(KAIA_BUILD_HOST_TEST "Build the plugin host test harness" OFF)
if(KAIA_BUILD_HOST_TEST)
    juce_add_gui_app(KaiaHostTest PRODUCT_NAME "KaiaHostTest")
    target_sources(KaiaHostTest PRIVATE ${KAIA_CORE_SOURCES} Tests/HostTest.cpp)
    target_include_directories(KaiaHostTest PRIVATE Source)
    target_compile_definitions(KaiaHostTest PRIVATE ${KAIA_COMMON_DEFS}
        JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_LV2=1 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(KaiaHostTest
        PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags)
endif()

if(KAIA_BUILD_HOST_TEST)
    juce_add_gui_app(KaiaUiSnapshot PRODUCT_NAME "KaiaUiSnapshot")
    target_sources(KaiaUiSnapshot PRIVATE ${KAIA_CORE_SOURCES} ${KAIA_PLUGIN_SOURCES} Tests/UiSnapshot.cpp)
    target_include_directories(KaiaUiSnapshot PRIVATE Source)
    target_compile_definitions(KaiaUiSnapshot PRIVATE ${KAIA_COMMON_DEFS} JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(KaiaUiSnapshot
        PRIVATE KaiaFonts KaiaLogo juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags)
endif()
