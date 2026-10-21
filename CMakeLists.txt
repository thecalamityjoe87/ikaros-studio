# Ikaros Studio — top-level CMake project
#
# Build (any platform):
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --parallel
#
# Tracktion Engine lives in external/tracktion_engine (git submodule) and brings
# its own copy of JUCE in external/tracktion_engine/modules/juce.

cmake_minimum_required(VERSION 3.22)

project(IkarosStudio VERSION 0.0.1 LANGUAGES C CXX)

# The version people see (About, splash, saved sessions): the release number
# plus the milestone being worked on, e.g. 0.0.1-milestone7. The bundle and
# Windows resource versions stay plain PROJECT_VERSION, which must be numeric.
set(IKAROS_MILESTONE 9)
set(IKAROS_VERSION_STRING "${PROJECT_VERSION}-milestone${IKAROS_MILESTONE}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Tracktion/JUCE translation units are huge (~3 GB each with -g), so cap Ninja's
# parallelism by RAM instead of core count. Override with -DIKAROS_COMPILE_JOBS=N.
cmake_host_system_information(RESULT IKAROS_TOTAL_MB QUERY TOTAL_PHYSICAL_MEMORY)
cmake_host_system_information(RESULT IKAROS_CORES QUERY NUMBER_OF_LOGICAL_CORES)
math(EXPR IKAROS_DEFAULT_JOBS "${IKAROS_TOTAL_MB} / 3072")
if(IKAROS_DEFAULT_JOBS GREATER IKAROS_CORES)
    set(IKAROS_DEFAULT_JOBS ${IKAROS_CORES})
endif()
if(IKAROS_DEFAULT_JOBS LESS 1)
    set(IKAROS_DEFAULT_JOBS 1)
endif()
set(IKAROS_COMPILE_JOBS ${IKAROS_DEFAULT_JOBS} CACHE STRING "Max concurrent compile jobs (Ninja)")
set_property(GLOBAL PROPERTY JOB_POOLS ikaros_compile=${IKAROS_COMPILE_JOBS} ikaros_link=1)
set(CMAKE_JOB_POOL_COMPILE ikaros_compile)
set(CMAKE_JOB_POOL_LINK ikaros_link)

# Platform settings that must be decided *before* JUCE is added.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

if(MSVC)
    # Static runtime, same as Tracktion's own examples, so the .exe has no VC++ redist dependency.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------
set(TE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/tracktion_engine")

if(NOT EXISTS "${TE_DIR}/modules/juce/CMakeLists.txt")
    message(FATAL_ERROR
        "Tracktion Engine / JUCE submodules are missing. Run:\n"
        "  git -c url.\"https://github.com/\".insteadOf=\"git@github.com:\" submodule update --init --recursive")
endif()

# Fixes to JUCE that aren't upstream: each patch is applied once (it's skipped
# when it's already in).
find_package(Git REQUIRED)
set(JUCE_DIR "${TE_DIR}/modules/juce")
file(GLOB IKAROS_JUCE_PATCHES "${CMAKE_CURRENT_SOURCE_DIR}/external/patches/juce-*.patch")

foreach(patch IN LISTS IKAROS_JUCE_PATCHES)
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${patch}"
                    WORKING_DIRECTORY "${JUCE_DIR}"
                    RESULT_VARIABLE already_applied OUTPUT_QUIET ERROR_QUIET)

    if(NOT already_applied EQUAL 0)
        execute_process(COMMAND "${GIT_EXECUTABLE}" apply "${patch}"
                        WORKING_DIRECTORY "${JUCE_DIR}"
                        RESULT_VARIABLE applied)

        if(NOT applied EQUAL 0)
            message(FATAL_ERROR "Couldn't apply ${patch} to JUCE")
        endif()
    endif()
endforeach()

# The same for Tracktion Engine's own fixes.
file(GLOB IKAROS_TRACKTION_PATCHES "${CMAKE_CURRENT_SOURCE_DIR}/external/patches/tracktion-*.patch")

foreach(patch IN LISTS IKAROS_TRACKTION_PATCHES)
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${patch}"
                    WORKING_DIRECTORY "${TE_DIR}"
                    RESULT_VARIABLE already_applied OUTPUT_QUIET ERROR_QUIET)

    if(NOT already_applied EQUAL 0)
        execute_process(COMMAND "${GIT_EXECUTABLE}" apply "${patch}"
                        WORKING_DIRECTORY "${TE_DIR}"
                        RESULT_VARIABLE applied)

        if(NOT applied EQUAL 0)
            message(FATAL_ERROR "Couldn't apply ${patch} to Tracktion Engine")
        endif()
    endif()
endforeach()

# JUCE first (provides juce_add_gui_app, juce_add_binary_data, ...),
# then Tracktion's modules (registered as tracktion::tracktion_engine etc.).
add_subdirectory("${TE_DIR}/modules/juce" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)
add_subdirectory("${TE_DIR}/modules"      "${CMAKE_BINARY_DIR}/tracktion" EXCLUDE_FROM_ALL)

# sfizz plays SFZ instruments (velocity layers, round-robins, samples
# streamed from disk) for the library's sampled sounds; see
# src/engine/SfzPlugin.h. Only its library is built: no stand-alone apps,
# JACK client or MIDI renderer. Pinned to a release tag (1.2.3).
set(SFIZZ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/sfizz")

if(NOT EXISTS "${SFIZZ_DIR}/external/abseil-cpp/CMakeLists.txt")
    message(FATAL_ERROR
        "The sfizz submodule (or its own submodules) is missing. Run:\n"
        "  git submodule update --init --recursive external/sfizz")
endif()

set(SFIZZ_JACK OFF CACHE BOOL "" FORCE)
set(SFIZZ_RENDER OFF CACHE BOOL "" FORCE)
set(SFIZZ_SHARED OFF CACHE BOOL "" FORCE)
set(SFIZZ_TESTS OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEMOS OFF CACHE BOOL "" FORCE)
set(SFIZZ_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEVTOOLS OFF CACHE BOOL "" FORCE)
set(ENABLE_LTO OFF CACHE BOOL "" FORCE)   # sfizz's own option; keeps its build quick

# Two settings just for sfizz's code. The values a subdirectory sees are
# fixed when it's added, so restoring them afterwards leaves our own targets
# as they were:
#  - C++17: sfizz doesn't build as C++20 (which the app uses).
#  - <cstdint>: sfizz's copy of abseil uses uintptr_t in its debug-build
#    asserts without including it, which newer GCC versions reject.
set(IKAROS_SAVED_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_STANDARD 17)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    string(APPEND CMAKE_CXX_FLAGS " -include cstdint")
endif()

add_subdirectory("${SFIZZ_DIR}" "${CMAKE_BINARY_DIR}/sfizz" EXCLUDE_FROM_ALL)
set(CMAKE_CXX_FLAGS "${IKAROS_SAVED_CXX_FLAGS}")
set(CMAKE_CXX_STANDARD 20)

# miniz writes and reads .iksession files (ZIP archives, ZIP64 when they're
# over 4 GB); see src/engine/SessionBundle.h. Pinned to a release tag (3.1.2).
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/miniz/miniz_zip.c")
    message(FATAL_ERROR
        "The miniz submodule is missing. Run:\n"
        "  git submodule update --init external/miniz")
endif()

add_subdirectory(external/miniz "${CMAKE_BINARY_DIR}/miniz" EXCLUDE_FROM_ALL)

# LAME's MP3 encoder (libmp3lame 3.100, LGPL), kept in the repo since LAME
# has no official git repository; see external/lame/README.ikaros.
add_subdirectory(external/lame/ikaros "${CMAKE_BINARY_DIR}/lame" EXCLUDE_FROM_ALL)

# The DSP of the GxPlugins pedals (GPL 3) and two Guitarix effects (GPL 2+)
# behind most of the pedalboard's pedals, kept in the repo (one GitHub
# repository per pedal, and only their DSP is used); see
# external/gxplugins/README.ikaros.
add_subdirectory(external/gxplugins/ikaros "${CMAKE_BINARY_DIR}/gxplugins" EXCLUDE_FROM_ALL)

# Guitarix's amp models, tone stacks and cabinet IRs (GPL 2+), the sound of
# the Amp after the pedals, kept in the repo (only the Faust-generated DSP is
# used); see external/guitarix/README.ikaros.
add_subdirectory(external/guitarix/ikaros "${CMAKE_BINARY_DIR}/guitarix" EXCLUDE_FROM_ALL)

# Rubber Band stretches audio regions in time and pitch (loops following the
# song's tempo and key, Flex Time). Tracktion builds it into its own
# time-stretch code from Rubber Band's single-file version
# (single/RubberBandSingle.cpp), so there's no separate library to build or
# ship: it ends up inside the app on every platform, AppImage and Flatpak
# included. Rubber Band is GPL (version 2 or later). Pinned to a release tag
# (4.0.0).
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/rubberband/single/RubberBandSingle.cpp")
    message(FATAL_ERROR
        "The Rubber Band submodule is missing. Run:\n"
        "  git submodule update --init external/rubberband")
endif()

# An "interface" library has no code of its own: linking to it just passes on
# these settings, so the app and the smoke test build Tracktion the same way.
#  - Tracktion includes <rubberband/single/RubberBandSingle.cpp>, so the
#    folder above the submodule goes on the include path.
#  - SoundTouch stays compiled in so sessions saved with it still open (they're
#    switched to Rubber Band on load; see engine/TimeStretch.h).
add_library(ikaros_timestretch INTERFACE)
target_include_directories(ikaros_timestretch INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/external")
target_compile_definitions(ikaros_timestretch INTERFACE
    TRACKTION_ENABLE_TIMESTRETCH_RUBBERBAND=1
    TRACKTION_BUILD_RUBBERBAND=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

if(APPLE)
    # Rubber Band uses Apple's vDSP for its FFT on macOS.
    target_link_libraries(ikaros_timestretch INTERFACE "-framework Accelerate")
endif()

# Third-party plug-in formats: VST3 everywhere, LV2 on Linux and Windows, Audio
# Units on macOS. JUCE ships everything needed to host them.
if(APPLE)
    set(IKAROS_PLUGIN_HOST_DEFINITIONS JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_LV2=0)
else()
    set(IKAROS_PLUGIN_HOST_DEFINITIONS JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_PLUGINHOST_LV2=1)
endif()

# ------------------------------------------------------------------------------
# App target
# ------------------------------------------------------------------------------
juce_add_gui_app(IkarosStudio
    PRODUCT_NAME "Ikaros Studio"
    COMPANY_NAME "ikarosdev"
    BUNDLE_ID "io.github.thecalamityjoe87.IkarosStudio"
    VERSION ${PROJECT_VERSION}
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/io.github.thecalamityjoe87.IkarosStudio.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Ikaros Studio uses audio inputs for recording."
    DOCUMENT_EXTENSIONS iksession   # macOS: Finder opens .iksession files with the app
    NEEDS_CURL TRUE                 # downloading sound packs over https (see PackStore)
    NEEDS_WEB_BROWSER FALSE)

# Fonts are compiled into the binary so the app looks identical everywhere.
# This generates a static library with a `BinaryData` namespace
# (e.g. BinaryData::FigtreeRegular_ttf / BinaryData::FigtreeRegular_ttfSize).
juce_add_binary_data(IkarosStudioBinaryData
    HEADER_NAME "BinaryData.h"
    NAMESPACE BinaryData
    SOURCES
        resources/fonts/Figtree-Regular.ttf
        resources/fonts/Figtree-SemiBold.ttf
        resources/fonts/Figtree-Bold.ttf
        resources/fonts/Figtree-ExtraBold.ttf
        resources/fonts/JetBrainsMono-Medium.ttf
        resources/fonts/JetBrainsMono-Bold.ttf
        resources/icons/io.github.thecalamityjoe87.IkarosStudio.png)   # the About window

# ------------------------------------------------------------------------------
# Sources, grouped like the folders under src/. Headers are listed too so they
# show up in IDE project trees (Visual Studio, Xcode).
# ------------------------------------------------------------------------------
set(IKAROS_APP_SOURCES
    src/app/Main.cpp
    src/app/MainWindow.h            src/app/MainWindow.cpp
    src/app/MainComponent.h         src/app/MainComponent.cpp
    src/app/AppMenuModel.h          src/app/AppMenuModel.cpp
    src/app/CommandIDs.h)

# The only code that talks to Tracktion. Also built into the smoke test.
set(IKAROS_ENGINE_SOURCES
    src/engine/EngineTypes.h
    src/engine/AudioEngine.h        src/engine/AudioEngine.cpp
    src/engine/TrackController.h    src/engine/TrackController.cpp
    src/engine/TrackHelpers.h
    src/engine/InputRouter.h        src/engine/InputRouter.cpp
    src/engine/WaveformCache.h      src/engine/WaveformCache.cpp
    src/engine/RegionEditor.h       src/engine/RegionEditor.cpp
    src/engine/NoteEditor.h         src/engine/NoteEditor.cpp
    src/engine/TakeEditor.h         src/engine/TakeEditor.cpp
    src/engine/FlexEditor.h         src/engine/FlexEditor.cpp
    src/engine/FlexHelpers.h
    src/engine/FlexMap.h
    src/engine/Quantize.h
    src/engine/LoopKey.h
    src/engine/TimeStretch.h
    src/engine/TakeHelpers.h
    src/engine/EffectsController.h  src/engine/EffectsController.cpp
    src/engine/AutomationEditor.h   src/engine/AutomationEditor.cpp
    src/engine/SectionEditor.h      src/engine/SectionEditor.cpp
    src/engine/SoundControls.h
    src/engine/SendBuses.h          src/engine/SendBuses.cpp
    src/engine/EqController.h       src/engine/EqController.cpp
    src/engine/SfzPlugin.h          src/engine/SfzPlugin.cpp
    src/engine/SoundShaper.h        src/engine/SoundShaper.cpp
    src/engine/BuiltInEffects.h     src/engine/BuiltInEffects.cpp
    src/engine/PortedPedals.h           src/engine/PortedPedals.cpp
    src/engine/KlonCentaurDsp.h         src/engine/KlonCentaurDsp.cpp
    src/engine/PedalStyles.h        src/engine/PedalStyles.cpp
    src/engine/AmpPlugin.h          src/engine/AmpPlugin.cpp
    src/engine/AmpStyles.h          src/engine/AmpStyles.cpp
    src/engine/SessionBundle.h      src/engine/SessionBundle.cpp
    src/engine/SessionRecovery.h    src/engine/SessionRecovery.cpp
    src/engine/Autosaver.h          src/engine/Autosaver.cpp
    src/engine/SoundLibrary.h       src/engine/SoundLibrary.cpp
    src/engine/PackStore.h          src/engine/PackStore.cpp
    src/engine/FreesoundStore.h     src/engine/FreesoundStore.cpp
    src/engine/PluginLibrary.h      src/engine/PluginLibrary.cpp
    src/engine/PluginScanner.h      src/engine/PluginScanner.cpp
    src/engine/AudioPreview.h       src/engine/AudioPreview.cpp
    src/engine/SongExporter.h       src/engine/SongExporter.cpp
    src/engine/Mp3Format.h          src/engine/Mp3Format.cpp
    src/engine/RecordingTypes.h
    src/engine/Preferences.h        src/engine/Preferences.cpp
    src/engine/RecordingGuard.h     src/engine/RecordingGuard.cpp
    src/engine/PitchDetector.h      src/engine/PitchDetector.cpp
    src/engine/Tuner.h              src/engine/Tuner.cpp
    src/engine/LiveWaveform.h       src/engine/LiveWaveform.cpp
    src/engine/AudioDeviceWatcher.h src/engine/AudioDeviceWatcher.cpp
    src/engine/RealtimeAudioThread.h src/engine/RealtimeAudioThread.cpp)

# The Golden Boost's circuit model (KlonCentaur, BSD 3-clause, with parts of
# chowdsp_utils, GPL 3) is compiled in KlonCentaurDsp.cpp. Its files include
# <pch.h>, our stand-in for ChowCentaur's own; see external/kloncentaur/README.ikaros.
set_source_files_properties(src/engine/KlonCentaurDsp.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/external/kloncentaur/ikaros"
    COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")   # third-party code's warnings

# The only OS-specific code.
set(IKAROS_PLATFORM_SOURCES
    src/platform/WindowManagerHints.h  src/platform/WindowManagerHints.cpp
    src/platform/ExternalDrops.h       src/platform/ExternalDrops.cpp
    src/platform/AppFolders.h          src/platform/AppFolders.cpp
    src/platform/FileLock.h            src/platform/FileLock.cpp
    src/platform/AudioHardware.h       src/platform/AudioHardware.cpp
    src/platform/SystemAppearance.h    src/platform/SystemAppearance.cpp)

set(IKAROS_UI_SOURCES
    # theme: colours/sizes, fonts, and the LookAndFeel that styles JUCE widgets
    src/ui/theme/Tokens.h                   src/ui/theme/Family.h
    src/ui/theme/Theme.h                    src/ui/theme/Theme.cpp
    src/ui/theme/ThemeManager.h             src/ui/theme/ThemeManager.cpp
    src/ui/theme/Fonts.h                    src/ui/theme/Fonts.cpp
    src/ui/theme/IkarosLookAndFeel.h        src/ui/theme/IkarosLookAndFeel.cpp

    # widgets: small reusable controls
    src/ui/widgets/Icons.h                  src/ui/widgets/Icons.cpp
    src/ui/widgets/IconButton.h             src/ui/widgets/IconButton.cpp
    src/ui/widgets/Decibels.h
    src/ui/widgets/ResizeHandle.h           src/ui/widgets/ResizeHandle.cpp
    src/ui/widgets/Knob.h                   src/ui/widgets/Knob.cpp
    src/ui/widgets/AutomationHint.h         src/ui/widgets/AutomationHint.cpp
    src/ui/widgets/LevelMeter.h             src/ui/widgets/LevelMeter.cpp
    src/ui/widgets/ClipLight.h              src/ui/widgets/ClipLight.cpp
    src/ui/widgets/CachedShadow.h           src/ui/widgets/CachedShadow.cpp
    src/ui/widgets/MeterSlider.h            src/ui/widgets/MeterSlider.cpp
    src/ui/widgets/CollapsibleSection.h     src/ui/widgets/CollapsibleSection.cpp
    src/ui/widgets/FolderField.h            src/ui/widgets/FolderField.cpp

    # state: UI state shared between areas
    src/ui/state/Selection.h

    # dialogs shown over the window
    src/ui/dialogs/NewTrackDialog.h         src/ui/dialogs/NewTrackDialog.cpp
    src/ui/dialogs/AddPedalDialog.h         src/ui/dialogs/AddPedalDialog.cpp
    src/ui/dialogs/AboutDialog.h            src/ui/dialogs/AboutDialog.cpp
    src/ui/dialogs/StartupSplash.h          src/ui/dialogs/StartupSplash.cpp
    src/ui/dialogs/SessionChooser.h         src/ui/dialogs/SessionChooser.cpp
    src/ui/dialogs/SavingOverlay.h          src/ui/dialogs/SavingOverlay.cpp
    src/ui/dialogs/ExportDialog.h           src/ui/dialogs/ExportDialog.cpp
    src/ui/dialogs/RecoveryPrompt.h         src/ui/dialogs/RecoveryPrompt.cpp
    src/ui/dialogs/MonitoringWarning.h      src/ui/dialogs/MonitoringWarning.cpp
    src/ui/dialogs/DeviceChangePrompt.h     src/ui/dialogs/DeviceChangePrompt.cpp
    src/ui/dialogs/AlertDialog.h            src/ui/dialogs/AlertDialog.cpp

    # the Preferences window and its pages
    src/ui/preferences/PreferencesDialog.h  src/ui/preferences/PreferencesDialog.cpp
    src/ui/preferences/PreferencesPage.h    src/ui/preferences/PreferencesPage.cpp
    src/ui/preferences/PreferencesPages.h   src/ui/preferences/PreferencesPages.cpp
    src/ui/preferences/ThemePicker.h        src/ui/preferences/ThemePicker.cpp
    src/ui/preferences/SoundsPluginsPage.cpp
    src/ui/preferences/AudioSettingsPanel.h src/ui/preferences/AudioSettingsPanel.cpp

    # windows of their own
    src/ui/pluginwindow/PluginWindow.h      src/ui/pluginwindow/PluginWindow.cpp

    # one folder per area of the window
    src/ui/titlebar/MenuBar.h               src/ui/titlebar/MenuBar.cpp
    src/ui/titlebar/WindowControls.h        src/ui/titlebar/WindowControls.cpp
    src/ui/toolbar/Toolbar.h                src/ui/toolbar/Toolbar.cpp
    src/ui/toolbar/TransportBar.h           src/ui/toolbar/TransportBar.cpp
    src/ui/toolbar/LcdDisplay.h             src/ui/toolbar/LcdDisplay.cpp
    src/ui/library/LibrarySidebar.h         src/ui/library/LibrarySidebar.cpp
    src/ui/library/PreviewButton.h          src/ui/library/PreviewButton.cpp
    src/ui/library/ManageSoundsDialog.h       src/ui/library/ManageSoundsDialog.cpp
    src/ui/library/FreesoundSetupDialog.h   src/ui/library/FreesoundSetupDialog.cpp
    src/ui/library/LoopBrowser.h            src/ui/library/LoopBrowser.cpp
    src/ui/arrangement/ArrangementView.h    src/ui/arrangement/ArrangementView.cpp
    src/ui/arrangement/ArrangementLayout.h
    src/ui/arrangement/ArrangementRuler.h   src/ui/arrangement/ArrangementRuler.cpp
    src/ui/arrangement/ArrangementTempoLane.h   src/ui/arrangement/ArrangementTempoLane.cpp
    src/ui/arrangement/ArrangementSections.h    src/ui/arrangement/ArrangementSections.cpp
    src/ui/arrangement/ArrangementLane.h    src/ui/arrangement/ArrangementLane.cpp
    src/ui/arrangement/ArrangementAutomation.h src/ui/arrangement/ArrangementAutomation.cpp
    src/ui/arrangement/ArrangementContent.h src/ui/arrangement/ArrangementContent.cpp
    src/ui/arrangement/ArrangementOverlays.h    src/ui/arrangement/ArrangementOverlays.cpp
    src/ui/arrangement/ArrangementRegionDrag.h  src/ui/arrangement/ArrangementRegionDrag.cpp
    src/ui/arrangement/TrackHeader.h        src/ui/arrangement/TrackHeader.cpp
    src/ui/arrangement/RegionComponent.h    src/ui/arrangement/RegionComponent.cpp
    src/ui/bottompanel/BottomPanel.h        src/ui/bottompanel/BottomPanel.cpp
    src/ui/bottompanel/PianoKeyboard.h      src/ui/bottompanel/PianoKeyboard.cpp
    src/ui/bottompanel/AudioEditor.h        src/ui/bottompanel/AudioEditor.cpp
    src/ui/bottompanel/EditorGrid.h
    src/ui/bottompanel/RegionInspector.h    src/ui/bottompanel/RegionInspector.cpp
    src/ui/bottompanel/PianoRoll.h          src/ui/bottompanel/PianoRoll.cpp
    src/ui/bottompanel/PianoRollLayout.h
    src/ui/bottompanel/PianoRollGestures.cpp
    src/ui/bottompanel/PianoRollPainting.cpp
    src/ui/bottompanel/EqView.h             src/ui/bottompanel/EqView.cpp
    src/ui/bottompanel/PedalView.h          src/ui/bottompanel/PedalView.cpp
    src/ui/bottompanel/PedalboardView.h     src/ui/bottompanel/PedalboardView.cpp
    src/ui/bottompanel/AmpView.h            src/ui/bottompanel/AmpView.cpp
    src/ui/bottompanel/MixerView.h          src/ui/bottompanel/MixerView.cpp
    src/ui/bottompanel/TrackInspector.h     src/ui/bottompanel/TrackInspector.cpp
    src/ui/bottompanel/PluginChainView.h    src/ui/bottompanel/PluginChainView.cpp
    src/ui/bottompanel/PluginEditor.h       src/ui/bottompanel/PluginEditor.cpp
    src/ui/bottompanel/NoiseGateView.h      src/ui/bottompanel/NoiseGateView.cpp
    src/ui/bottompanel/RecordingView.h      src/ui/bottompanel/RecordingView.cpp)

target_sources(IkarosStudio PRIVATE
    ${IKAROS_APP_SOURCES}
    ${IKAROS_ENGINE_SOURCES}
    ${IKAROS_PLATFORM_SOURCES}
    ${IKAROS_UI_SOURCES})

# Mirror the src/ folders in IDE project trees.
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "src"
             FILES ${IKAROS_APP_SOURCES} ${IKAROS_ENGINE_SOURCES} ${IKAROS_PLATFORM_SOURCES} ${IKAROS_UI_SOURCES})

target_include_directories(IkarosStudio PRIVATE src)

target_compile_definitions(IkarosStudio PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:IkarosStudio,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${IKAROS_VERSION_STRING}"
    JUCE_USE_CURL=1                 # for PackStore's downloads
    JUCE_WEB_BROWSER=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0
    ${IKAROS_PLUGIN_HOST_DEFINITIONS})

# The version, as the engine records it in saved sessions.
target_compile_definitions(IkarosStudio PRIVATE IKAROS_VERSION="${IKAROS_VERSION_STRING}")

# Where the "More Sounds" window gets its list of downloadable sound packs
# (see src/engine/PackStore.h): a web address or a file path, in the app's own
# index format or a Nota sample registry's. The default is the registry fork,
# which publishes its index.json with GitHub Pages; the smoke test uses its
# own index in tests/store.
set(IKAROS_PACK_INDEX_URL "https://thecalamityjoe87.github.io/nota-samples-registry/index.json"
    CACHE STRING "The sound pack index the app downloads packs from")
target_compile_definitions(IkarosStudio PRIVATE IKAROS_PACK_INDEX_URL="${IKAROS_PACK_INDEX_URL}")

target_link_libraries(IkarosStudio
    PRIVATE
        IkarosStudioBinaryData
        ikaros_gxplugins
        ikaros_guitarix
        ikaros_timestretch
        miniz
        mp3lame
        sfizz::sfizz
        tracktion::tracktion_core
        tracktion::tracktion_engine
        tracktion::tracktion_graph
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # Tracktion uses some MSVC/Clang-only pragmas; GCC warns about each one.
    target_compile_options(IkarosStudio PRIVATE -Wno-unknown-pragmas)
endif()

if(APPLE)
    # Matches Tracktion's own example settings.
    target_compile_options(IkarosStudio PRIVATE -fno-aligned-allocation)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(IkarosStudio PRIVATE atomic)

    # Xlib for src/platform (window-manager hints and drag-and-drop fixes JUCE
    # doesn't do itself). JUCE itself loads X11 at runtime, so it's already required.
    find_package(X11 REQUIRED)
    target_link_libraries(IkarosStudio PRIVATE X11::X11)

    # sd-bus (part of libsystemd) for src/platform/AudioHardware, which asks
    # RealtimeKit for a real-time audio thread, and SystemAppearance, which
    # asks the desktop portal whether dark mode is on.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(IKAROS_SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
    target_link_libraries(IkarosStudio PRIVATE PkgConfig::IKAROS_SYSTEMD)
endif()

# The sound packs that come with the app (made by tools/generate_sounds.py).
# The sampler plays them from disk, so they're copied next to the program
# (into Contents/Resources on macOS); SoundLibrary looks for them there.
set(IKAROS_SOUNDS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/sounds")

function(ikaros_copy_sounds target)
    if(APPLE AND target STREQUAL "IkarosStudio")
        set(dest "$<TARGET_FILE_DIR:${target}>/../Resources/Sounds")
    else()
        set(dest "$<TARGET_FILE_DIR:${target}>/Sounds")
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${IKAROS_SOUNDS_DIR}" "${dest}"
        VERBATIM)
endfunction()

ikaros_copy_sounds(IkarosStudio)

# ------------------------------------------------------------------------------
# Linux install layout (used by the Flatpak manifest and `cmake --install`)
# ------------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(GNUInstallDirs)
    set(APP_ID "io.github.thecalamityjoe87.IkarosStudio")

    # juce_add_gui_app puts the binary at <build>/IkarosStudio_artefacts/<Config>/Ikaros Studio
    install(PROGRAMS "$<TARGET_FILE:IkarosStudio>"
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            RENAME ikaros-studio)
    install(FILES packaging/linux/${APP_ID}.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/linux/${APP_ID}.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    install(FILES packaging/linux/${APP_ID}.xml   # the .iksession file type
            DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    install(FILES resources/icons/${APP_ID}.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES resources/icons/${APP_ID}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
    install(DIRECTORY resources/sounds/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/ikaros-studio/Sounds)
    install(FILES CREDITS
            DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

# ------------------------------------------------------------------------------
# Engine smoke test (off by default: it compiles Tracktion a second time)
#   cmake -B build -DIKAROS_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build
# ------------------------------------------------------------------------------
option(IKAROS_BUILD_TESTS "Build the engine smoke test" OFF)

if(IKAROS_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(IkarosEngineSmokeTest PRODUCT_NAME "IkarosEngineSmokeTest")

    target_sources(IkarosEngineSmokeTest PRIVATE
        tests/EngineSmokeTest.cpp
        src/platform/AppFolders.cpp      # the engine keeps open sessions there
        src/platform/FileLock.cpp        # and marks them as open with a lock
        src/platform/AudioHardware.cpp   # and the audio device watcher notices new sound cards
        src/engine/TestAudioDevice.cpp   # a pretend audio interface the test drives
        ${IKAROS_ENGINE_SOURCES})

    target_include_directories(IkarosEngineSmokeTest PRIVATE src)

    # The test sound pack (made by tests/fixtures/generate_test_pack.py).
    target_compile_definitions(IkarosEngineSmokeTest PRIVATE
        IKAROS_VERSION="${IKAROS_VERSION_STRING}"
        IKAROS_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
        IKAROS_TEST_STORE="${CMAKE_CURRENT_SOURCE_DIR}/tests/store"
        IKAROS_PACK_INDEX_URL="${CMAKE_CURRENT_SOURCE_DIR}/tests/store/index.json")

    target_compile_definitions(IkarosEngineSmokeTest PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1        # for runDispatchLoopUntil() in the test
        JUCE_USE_MP3AUDIOFORMAT=1           # to read exported MP3s back
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        ${IKAROS_PLUGIN_HOST_DEFINITIONS})

    target_link_libraries(IkarosEngineSmokeTest PRIVATE
        ikaros_gxplugins
        ikaros_guitarix
        ikaros_timestretch
        miniz
        mp3lame
        sfizz::sfizz
        tracktion::tracktion_core
        tracktion::tracktion_engine
        tracktion::tracktion_graph
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_recommended_config_flags)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(IkarosEngineSmokeTest PRIVATE -Wno-unknown-pragmas)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(IkarosEngineSmokeTest PRIVATE atomic PkgConfig::IKAROS_SYSTEMD)
    endif()

    ikaros_copy_sounds(IkarosEngineSmokeTest)

    # The test scans the test plug-ins when they've been built.
    # One test per section (see main() in the test), so `ctest -L export`
    # runs just that section.
    add_test(NAME EngineSmokeTest COMMAND IkarosEngineSmokeTest "${CMAKE_BINARY_DIR}/TestPlugins" --skip=export,undo,recovery,recording,takes,longrecording,preferences,effects,tempo,sections,stretch,flex,quantize,followtempo,tuner,pedals,amp)
    add_test(NAME EngineSmokeTest.Export COMMAND IkarosEngineSmokeTest --only=export)
    add_test(NAME EngineSmokeTest.Undo COMMAND IkarosEngineSmokeTest --only=undo)
    add_test(NAME EngineSmokeTest.Recovery COMMAND IkarosEngineSmokeTest --only=recovery)
    set_tests_properties(EngineSmokeTest PROPERTIES LABELS "engine")
    set_tests_properties(EngineSmokeTest.Export PROPERTIES LABELS "export")
    set_tests_properties(EngineSmokeTest.Undo PROPERTIES LABELS "undo")
    set_tests_properties(EngineSmokeTest.Recovery PROPERTIES LABELS "recovery")

    # App-wide settings (see src/engine/Preferences.h), and what they change.
    add_test(NAME EngineSmokeTest.Preferences COMMAND IkarosEngineSmokeTest --only=preferences)
    set_tests_properties(EngineSmokeTest.Preferences PROPERTIES LABELS "preferences")

    # Recording through a pretend audio interface (see src/engine/TestAudioDevice.h):
    # latency, device trouble, full disks. The long take is "slow" (minutes).
    add_test(NAME EngineSmokeTest.Recording COMMAND IkarosEngineSmokeTest --only=recording)
    add_test(NAME EngineSmokeTest.Takes COMMAND IkarosEngineSmokeTest --only=takes)
    add_test(NAME EngineSmokeTest.LongRecording COMMAND IkarosEngineSmokeTest --only=longrecording)
    set_tests_properties(EngineSmokeTest.Recording PROPERTIES LABELS "recording")
    set_tests_properties(EngineSmokeTest.Takes PROPERTIES LABELS "takes")
    set_tests_properties(EngineSmokeTest.LongRecording PROPERTIES LABELS "slow" TIMEOUT 1800)

    # The built-in effects (see src/engine/BuiltInEffects.h), heard through
    # the pretend audio interface.
    add_test(NAME EngineSmokeTest.Effects COMMAND IkarosEngineSmokeTest --only=effects)
    set_tests_properties(EngineSmokeTest.Effects PROPERTIES LABELS "effects")
    add_test(NAME EngineSmokeTest.Tempo COMMAND IkarosEngineSmokeTest --only=tempo)
    set_tests_properties(EngineSmokeTest.Tempo PROPERTIES LABELS "tempo")
    add_test(NAME EngineSmokeTest.Sections COMMAND IkarosEngineSmokeTest --only=sections)
    set_tests_properties(EngineSmokeTest.Sections PROPERTIES LABELS "sections")
    add_test(NAME EngineSmokeTest.Stretch COMMAND IkarosEngineSmokeTest --only=stretch)
    set_tests_properties(EngineSmokeTest.Stretch PROPERTIES LABELS "stretch")
    add_test(NAME EngineSmokeTest.Flex COMMAND IkarosEngineSmokeTest --only=flex)
    set_tests_properties(EngineSmokeTest.Flex PROPERTIES LABELS "flex")
    add_test(NAME EngineSmokeTest.Quantize COMMAND IkarosEngineSmokeTest --only=quantize)
    set_tests_properties(EngineSmokeTest.Quantize PROPERTIES LABELS "quantize")
    add_test(NAME EngineSmokeTest.FollowTempo COMMAND IkarosEngineSmokeTest --only=followtempo)
    set_tests_properties(EngineSmokeTest.FollowTempo PROPERTIES LABELS "followtempo")
    add_test(NAME EngineSmokeTest.Tuner COMMAND IkarosEngineSmokeTest --only=tuner)
    set_tests_properties(EngineSmokeTest.Tuner PROPERTIES LABELS "tuner")
    add_test(NAME EngineSmokeTest.Pedals COMMAND IkarosEngineSmokeTest --only=pedals)
    set_tests_properties(EngineSmokeTest.Pedals PROPERTIES LABELS "pedals")
    add_test(NAME EngineSmokeTest.Amp COMMAND IkarosEngineSmokeTest --only=amp)
    set_tests_properties(EngineSmokeTest.Amp PROPERTIES LABELS "amp")
endif()

# Small VST3/LV2 plug-ins (and two deliberately broken ones) for testing
# plug-in scanning and hosting. See tests/plugins/CMakeLists.txt.
option(IKAROS_BUILD_TEST_PLUGINS "Build plug-ins for testing plug-in hosting" OFF)

if(IKAROS_BUILD_TEST_PLUGINS)
    add_subdirectory(tests/plugins)
endif()
