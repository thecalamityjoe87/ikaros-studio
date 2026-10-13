# Ikaros Studio — top-level CMake project
#
# Build (any platform):
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --parallel
#
# Tracktion Engine lives in external/tracktion_engine (git submodule) and brings
# its own copy of JUCE in external/tracktion_engine/modules/juce.

cmake_minimum_required(VERSION 3.22)

project(IkarosStudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
        resources/fonts/JetBrainsMono-Medium.ttf
        resources/fonts/JetBrainsMono-Bold.ttf)

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
    src/engine/EffectsController.h  src/engine/EffectsController.cpp
    src/engine/AutomationEditor.h   src/engine/AutomationEditor.cpp
    src/engine/SoundControls.h
    src/engine/SendBuses.h          src/engine/SendBuses.cpp
    src/engine/EqController.h       src/engine/EqController.cpp
    src/engine/SfzPlugin.h          src/engine/SfzPlugin.cpp
    src/engine/SoundShaper.h        src/engine/SoundShaper.cpp
    src/engine/SessionBundle.h      src/engine/SessionBundle.cpp
    src/engine/SessionRecovery.h    src/engine/SessionRecovery.cpp
    src/engine/Autosaver.h          src/engine/Autosaver.cpp
    src/engine/SoundLibrary.h       src/engine/SoundLibrary.cpp
    src/engine/PackStore.h          src/engine/PackStore.cpp
    src/engine/PluginLibrary.h      src/engine/PluginLibrary.cpp
    src/engine/PluginScanner.h      src/engine/PluginScanner.cpp
    src/engine/AudioPreview.h       src/engine/AudioPreview.cpp
    src/engine/SongExporter.h       src/engine/SongExporter.cpp
    src/engine/Mp3Format.h          src/engine/Mp3Format.cpp
    src/engine/RecordingTypes.h
    src/engine/RecordingGuard.h     src/engine/RecordingGuard.cpp)

# The only OS-specific code.
set(IKAROS_PLATFORM_SOURCES
    src/platform/WindowManagerHints.h  src/platform/WindowManagerHints.cpp
    src/platform/ExternalDrops.h       src/platform/ExternalDrops.cpp
    src/platform/AppFolders.h          src/platform/AppFolders.cpp
    src/platform/FileLock.h            src/platform/FileLock.cpp)

set(IKAROS_UI_SOURCES
    # theme: colours/sizes, fonts, and the LookAndFeel that styles JUCE widgets
    src/ui/theme/Tokens.h
    src/ui/theme/Fonts.h                    src/ui/theme/Fonts.cpp
    src/ui/theme/IkarosLookAndFeel.h        src/ui/theme/IkarosLookAndFeel.cpp

    # widgets: small reusable controls
    src/ui/widgets/Icons.h                  src/ui/widgets/Icons.cpp
    src/ui/widgets/IconButton.h             src/ui/widgets/IconButton.cpp
    src/ui/widgets/ResizeHandle.h           src/ui/widgets/ResizeHandle.cpp
    src/ui/widgets/Knob.h                   src/ui/widgets/Knob.cpp
    src/ui/widgets/AutomationHint.h         src/ui/widgets/AutomationHint.cpp
    src/ui/widgets/LevelMeter.h             src/ui/widgets/LevelMeter.cpp
    src/ui/widgets/ClipLight.h              src/ui/widgets/ClipLight.cpp
    src/ui/widgets/MeterSlider.h            src/ui/widgets/MeterSlider.cpp
    src/ui/widgets/CollapsibleSection.h     src/ui/widgets/CollapsibleSection.cpp

    # state: UI state shared between areas
    src/ui/state/Selection.h

    # dialogs shown over the window
    src/ui/dialogs/NewTrackDialog.h         src/ui/dialogs/NewTrackDialog.cpp
    src/ui/dialogs/AboutDialog.h            src/ui/dialogs/AboutDialog.cpp
    src/ui/dialogs/SessionChooser.h         src/ui/dialogs/SessionChooser.cpp
    src/ui/dialogs/SavingOverlay.h          src/ui/dialogs/SavingOverlay.cpp
    src/ui/dialogs/ExportDialog.h           src/ui/dialogs/ExportDialog.cpp
    src/ui/dialogs/RecoveryPrompt.h         src/ui/dialogs/RecoveryPrompt.cpp
    src/ui/dialogs/MonitoringWarning.h      src/ui/dialogs/MonitoringWarning.cpp
    src/ui/dialogs/AudioSettingsPanel.h     src/ui/dialogs/AudioSettingsPanel.cpp

    # windows of their own
    src/ui/pluginwindow/PluginWindow.h      src/ui/pluginwindow/PluginWindow.cpp

    # one folder per area of the window
    src/ui/titlebar/MenuBar.h               src/ui/titlebar/MenuBar.cpp
    src/ui/titlebar/WindowControls.h        src/ui/titlebar/WindowControls.cpp
    src/ui/toolbar/Toolbar.h                src/ui/toolbar/Toolbar.cpp
    src/ui/toolbar/TransportBar.h           src/ui/toolbar/TransportBar.cpp
    src/ui/toolbar/LcdDisplay.h             src/ui/toolbar/LcdDisplay.cpp
    src/ui/library/LibrarySidebar.h         src/ui/library/LibrarySidebar.cpp
    src/ui/library/MoreSoundsDialog.h       src/ui/library/MoreSoundsDialog.cpp
    src/ui/arrangement/ArrangementView.h    src/ui/arrangement/ArrangementView.cpp
    src/ui/arrangement/ArrangementLayout.h
    src/ui/arrangement/ArrangementRuler.h   src/ui/arrangement/ArrangementRuler.cpp
    src/ui/arrangement/ArrangementLane.h    src/ui/arrangement/ArrangementLane.cpp
    src/ui/arrangement/ArrangementAutomation.h src/ui/arrangement/ArrangementAutomation.cpp
    src/ui/arrangement/ArrangementContent.h src/ui/arrangement/ArrangementContent.cpp
    src/ui/arrangement/ArrangementOverlays.h    src/ui/arrangement/ArrangementOverlays.cpp
    src/ui/arrangement/ArrangementRegionDrag.h  src/ui/arrangement/ArrangementRegionDrag.cpp
    src/ui/arrangement/TrackHeader.h        src/ui/arrangement/TrackHeader.cpp
    src/ui/arrangement/RegionComponent.h    src/ui/arrangement/RegionComponent.cpp
    src/ui/bottompanel/BottomPanel.h        src/ui/bottompanel/BottomPanel.cpp
    src/ui/bottompanel/PianoKeyboard.h      src/ui/bottompanel/PianoKeyboard.cpp
    src/ui/bottompanel/PianoRoll.h          src/ui/bottompanel/PianoRoll.cpp
    src/ui/bottompanel/PianoRollLayout.h
    src/ui/bottompanel/PianoRollGestures.cpp
    src/ui/bottompanel/PianoRollPainting.cpp
    src/ui/bottompanel/EqView.h             src/ui/bottompanel/EqView.cpp
    src/ui/bottompanel/MixerView.h          src/ui/bottompanel/MixerView.cpp
    src/ui/bottompanel/TrackInspector.h     src/ui/bottompanel/TrackInspector.cpp
    src/ui/bottompanel/PluginChainView.h    src/ui/bottompanel/PluginChainView.cpp
    src/ui/bottompanel/PluginEditor.h       src/ui/bottompanel/PluginEditor.cpp
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
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:IkarosStudio,JUCE_VERSION>"
    JUCE_USE_CURL=1                 # for PackStore's downloads
    JUCE_WEB_BROWSER=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0
    ${IKAROS_PLUGIN_HOST_DEFINITIONS}
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

# The version, as the engine records it in saved sessions.
target_compile_definitions(IkarosStudio PRIVATE IKAROS_VERSION="${PROJECT_VERSION}")

# Where the "More Sounds" window gets its list of downloadable sound packs
# (see src/engine/PackStore.h). Until the packs are hosted, a local test
# index made by tools/make_pack.py. A web address or a file path.
set(IKAROS_PACK_INDEX_URL "${CMAKE_CURRENT_SOURCE_DIR}/tests/store/index.json"
    CACHE STRING "The sound pack index the app downloads packs from")
target_compile_definitions(IkarosStudio PRIVATE IKAROS_PACK_INDEX_URL="${IKAROS_PACK_INDEX_URL}")

target_link_libraries(IkarosStudio
    PRIVATE
        IkarosStudioBinaryData
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
        src/engine/TestAudioDevice.cpp   # a pretend audio interface the test drives
        ${IKAROS_ENGINE_SOURCES})

    target_include_directories(IkarosEngineSmokeTest PRIVATE src)

    # The test sound pack (made by tests/fixtures/generate_test_pack.py).
    target_compile_definitions(IkarosEngineSmokeTest PRIVATE
        IKAROS_VERSION="${PROJECT_VERSION}"
        IKAROS_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
        IKAROS_TEST_STORE="${CMAKE_CURRENT_SOURCE_DIR}/tests/store"
        IKAROS_PACK_INDEX_URL="${CMAKE_CURRENT_SOURCE_DIR}/tests/store/index.json")

    target_compile_definitions(IkarosEngineSmokeTest PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1        # for runDispatchLoopUntil() in the test
        JUCE_USE_MP3AUDIOFORMAT=1           # to read exported MP3s back
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        ${IKAROS_PLUGIN_HOST_DEFINITIONS}
        TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

    target_link_libraries(IkarosEngineSmokeTest PRIVATE
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
        target_link_libraries(IkarosEngineSmokeTest PRIVATE atomic)
    endif()

    ikaros_copy_sounds(IkarosEngineSmokeTest)

    # The test scans the test plug-ins when they've been built.
    # One test per section (see main() in the test), so `ctest -L export`
    # runs just that section.
    add_test(NAME EngineSmokeTest COMMAND IkarosEngineSmokeTest "${CMAKE_BINARY_DIR}/TestPlugins" --skip=export,undo,recovery,recording,longrecording)
    add_test(NAME EngineSmokeTest.Export COMMAND IkarosEngineSmokeTest --only=export)
    add_test(NAME EngineSmokeTest.Undo COMMAND IkarosEngineSmokeTest --only=undo)
    add_test(NAME EngineSmokeTest.Recovery COMMAND IkarosEngineSmokeTest --only=recovery)
    set_tests_properties(EngineSmokeTest PROPERTIES LABELS "engine")
    set_tests_properties(EngineSmokeTest.Export PROPERTIES LABELS "export")
    set_tests_properties(EngineSmokeTest.Undo PROPERTIES LABELS "undo")
    set_tests_properties(EngineSmokeTest.Recovery PROPERTIES LABELS "recovery")

    # Recording through a pretend audio interface (see src/engine/TestAudioDevice.h):
    # latency, device trouble, full disks. The long take is "slow" (minutes).
    add_test(NAME EngineSmokeTest.Recording COMMAND IkarosEngineSmokeTest --only=recording)
    add_test(NAME EngineSmokeTest.LongRecording COMMAND IkarosEngineSmokeTest --only=longrecording)
    set_tests_properties(EngineSmokeTest.Recording PROPERTIES LABELS "recording")
    set_tests_properties(EngineSmokeTest.LongRecording PROPERTIES LABELS "slow" TIMEOUT 1800)
endif()

# Small VST3/LV2 plug-ins (and two deliberately broken ones) for testing
# plug-in scanning and hosting. See tests/plugins/CMakeLists.txt.
option(IKAROS_BUILD_TEST_PLUGINS "Build plug-ins for testing plug-in hosting" OFF)

if(IKAROS_BUILD_TEST_PLUGINS)
    add_subdirectory(tests/plugins)
endif()
