# Plug-ins for testing plug-in hosting. Not part of the app.
#
#   cmake -B build -DIKAROS_BUILD_TESTS=ON -DIKAROS_BUILD_TEST_PLUGINS=ON
#
# Everything lands in <build>/TestPlugins:
#   Good/VST3, Good/LV2   a sine synth and a gain effect (JUCE plug-ins)
#   Broken/VST3           one bundle that crashes when loaded, one that hangs
#
# Point a scan at those folders instead of a real plug-in collection: real
# plug-ins can be slow to scan, and some crash.

set(IKAROS_TEST_PLUGIN_DIR "${CMAKE_BINARY_DIR}/TestPlugins")

function(ikaros_add_test_plugin target name code is_synth)
    juce_add_plugin(${target}
        PRODUCT_NAME "${name}"
        COMPANY_NAME "Ikaros Test"
        BUNDLE_ID "io.github.thecalamityjoe87.IkarosTest.${code}"
        PLUGIN_MANUFACTURER_CODE Iktt
        PLUGIN_CODE ${code}
        IS_SYNTH ${is_synth}
        NEEDS_MIDI_INPUT ${is_synth}
        FORMATS VST3 LV2
        LV2URI "urn:ikaros-studio:test:${code}"
        # Never copy test plug-ins into the user's real plug-in folders.
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(${target} PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endfunction()

ikaros_add_test_plugin(IkarosTestSynth "Ikaros Test Synth" Iksy TRUE)
target_sources(IkarosTestSynth PRIVATE TestSynth.cpp)

ikaros_add_test_plugin(IkarosTestGain "Ikaros Test Gain" Ikgn FALSE)
target_sources(IkarosTestGain PRIVATE TestGain.cpp)

# The broken ones aren't real VST3s, just a shared library in a VST3 bundle's
# folder layout, which is all a host needs to try loading it.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    foreach(kind IN ITEMS Crash Hang)
        set(target IkarosTest${kind}Plugin)
        add_library(${target} MODULE BrokenPlugin.cpp)
        target_compile_definitions(${target} PRIVATE IKAROS_HANG=$<STREQUAL:${kind},Hang>)
        set_target_properties(${target} PROPERTIES
            PREFIX ""
            OUTPUT_NAME "Ikaros ${kind} Test"
            LIBRARY_OUTPUT_DIRECTORY
                "${IKAROS_TEST_PLUGIN_DIR}/Broken/VST3/Ikaros ${kind} Test.vst3/Contents/${CMAKE_SYSTEM_PROCESSOR}-linux")
    endforeach()

endif()

# Gather the built plug-ins into one folder per kind.
set(copy_commands "")

foreach(plugin IN ITEMS IkarosTestSynth IkarosTestGain)
    foreach(format IN ITEMS VST3 LV2)
        list(APPEND copy_commands
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_BINARY_DIR}/${plugin}_artefacts/$<CONFIG>/${format}"
                "${IKAROS_TEST_PLUGIN_DIR}/Good/${format}")
    endforeach()
endforeach()

add_custom_target(IkarosTestPlugins ALL ${copy_commands}
    COMMENT "Collecting test plug-ins in ${IKAROS_TEST_PLUGIN_DIR}")
add_dependencies(IkarosTestPlugins IkarosTestSynth_All IkarosTestGain_All)

if(TARGET IkarosTestCrashPlugin)
    add_dependencies(IkarosTestPlugins IkarosTestCrashPlugin IkarosTestHangPlugin)
endif()
